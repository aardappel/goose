// Goose compiler — the typechecker's expressions (definitions of TypeCheck
// members, typecheck.h): lvalue paths, values with reference transparency
// (§3.8) and the implicit conversions (§6.3, §3.10), the store rule (§9.2),
// constant folding and operand unification (§6.1), and struct and variant
// literals (§4.2).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Lvalue paths: names, fields, elements, optionally through references.

// The base of a path: itself a path, or any other expression (a call
// result, a string literal, ...) whose value is then addressed. A null
// root means static data; temporaries carry the temproot sentinel.
inline TypeCheck::LVal TypeCheck::LValueBase(Node *n) {
    if (Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n)) return CheckLValue(n);
    auto v = CheckV(n, nullptr);
    n->exprtype = v.type;
    LVal lv;
    lv.type = v.type;
    lv.SetProv(v);
    return lv;
}

// Crossing a reference in a path (auto-deref, §3.8): the storage owner
// becomes the reference's root, writability its provenance.
inline void TypeCheck::DerefLValue(LVal &lv, Node *at) {
    if (lv.type->kind != TY_REF) return;
    if (lv.type->ref->optional)
        Error(at, "optional value must be narrowed (if/guard/assert) before use");
    // Reading a reference variable requires it to have a value.
    if (lv.var) {
        RequireAssigned(lv.var, at);
        lv.SetProv(RefProvOf(lv.var));
    } else if (lv.fromstorage) {
        // Crossing a reference the path read out of a container: it is a
        // read-back, so where it points is re-derived (§9.5). A relative
        // one loads as an ordinary reference into the same pool, so it
        // keeps the container's root.
        ReadBackLVal(lv);
    }
    lv.type = lv.type->ref->sub;
    lv.var = nullptr;
    if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT) lv.isvarint = true;
}

inline void TypeCheck::RequireAssigned(VarDef *vd, Node *at) {
    if (!vd->assigned)
        Error(at, cat("variable ", vd->name, " may be used before it is assigned"));
}

// Accessing through a slice variable: writes and roots follow the slice
// value's provenance, not the variable's own var-ness.
inline void TypeCheck::SliceProvenance(LVal &lv, Node *at) {
    if (lv.type->kind != TY_SLICE) return;
    if (!lv.var) {
        if (lv.fromstorage) ReadBackLVal(lv);
        return;
    }
    RequireAssigned(lv.var, at);
    lv.SetProv(RefProvOf(lv.var));
    lv.var = nullptr;
}

// A location whose own type is a reference or slice: the value loaded out
// of it is a read-back, so its root is re-derived (§9.5).
inline void TypeCheck::ReadBackLVal(LVal &lv) {
    if (lv.type->kind != TY_REF && lv.type->kind != TY_SLICE) return;
    auto rb = ReadBackRoot(lv.type, lv.root, lv.rootexact);
    lv.root = rb.root;
    lv.rootexact = rb.exact;
    lv.rootfrom = rb.from;
}

// The value a field or element location yields: the load, re-rooted by
// the read-back rule, and writable by design where it is a reference or
// slice (§9.5's laundering; see the header note).
inline Val TypeCheck::ContainerRead(LVal lv) {
    ReadBackLVal(lv);
    Val v;
    v.type = LoadType(lv.type);
    v.SetProv(lv);
    if (v.type->kind == TY_REF || v.type->kind == TY_SLICE) v.writable = true;
    v.lvalue = v.type->kind != TY_REF;
    return v;
}

// Field / builtin-property resolution on an lvalue path.
inline void TypeCheck::ResolveMemberLValue(LVal &lv, Dot *d) {
    auto t = lv.type;
    // Steps the path into the named field of a field run; a frame
    // object's resizable tail has a header of its own to address (C.2).
    auto step = [&](const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                    bool frameobj) {
        for (auto i = 0; i < (int)fields.size(); i++) {
            auto &f = fields[i];
            if (f.ispad || f.name != d->name) continue;
            d->fieldidx = i;
            if (f.isconst) lv.writable = false;
            lv.type = ftypes[i];
            lv.var = nullptr;
            lv.fromstorage = true;
            lv.fotail = frameobj && ClassOf(lv.type) == SC_RESIZABLE;
            lv.isvarint = lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT;
            return true;
        }
        return false;
    };
    if (t->kind == TY_STRUCT) {
        auto inst = GetStructInst(t);
        if (step(inst->st->fields, inst->ftypes, inst->frameobj)) return;
        Error(d, cat("struct ", inst->st->name, " has no field ", d->name));
    }
    if (t->kind == TY_VARIANT) {
        auto inst = GetEnumInst(t->var->adt);
        auto vi = VariantIndex(inst->en, t->var->variant);
        if (step(t->var->variant->fields, inst->vftypes[vi], false)) return;
        Error(d, cat("variant ", inst->en->name, ".", t->var->variant->name,
                     " has no field ", d->name));
    }
    Error(d, cat("no field access on a value of type ", TypeStr(t)));
}

// ------------------------------------------------------------------
// Values: the per-node dispatch plus the implicit-conversion rules.

// References are transparent: load the pointee unless the destination
// wants the reference itself. Optionals never decay (narrow first).
inline Val TypeCheck::DecayRef(Val v) {
    if (!IsPlainRef(v.type)) return v;
    Val r;
    r.type = LoadType(v.type->ref->sub);
    r.root = v.root;  // Compound pointee values: container info, harmless.
    r.rootexact = v.rootexact;
    r.rootfrom = v.rootfrom;
    return r;
}

// Does dt consume a reference value as-is (so no decay before fitting)?
inline bool TypeCheck::KeepsRef(Val &v, TypeExpr *dt) {
    if (!IsPlainRef(v.type)) return true;  // Nothing to decay.
    if (dt->kind == TY_REF) return true;   // Binding (plain/optional/relative).
    // Whole-(pointee-)array argument to a slice parameter (§3.10).
    if (dt->kind == TY_SLICE && v.type->ref->sub->kind == TY_ARRAY) return true;
    return false;
}

// An lvalue of a reference destination's pointee type binds by reference
// (§4.1): the node becomes `&node`, as if written, so every later pass
// sees an ordinary reference argument.
inline Node *TypeCheck::AutoRef(Node *n, Val &v) {
    if (!Referenceable(n, v))
        Error(n, "cannot reference a resizable value nested in a variable-size prefix "
                 "or an ADT payload; reference the owning variable instead");
    auto u = ast.New<Unary>(n->line, T_BITAND, n);
    u->synth = true;
    v.type = RefTo(v.type, n->line);
    v.lvalue = false;
    u->exprtype = v.type;
    return u;
}

inline bool TypeCheck::BindsRef(const Val &v, TypeExpr *dt) {
    return dt->kind == TY_REF && v.lvalue && v.type->kind != TY_REF && !v.isnull &&
           TypeEq(v.type, dt->ref->sub);
}

inline bool TypeCheck::IsNonFixedLValue(const Val &v) {
    return v.lvalue && v.type->kind != TY_REF && v.type->kind != TY_SLICE &&
           ClassOf(v.type) != SC_FIXED;
}

// Whether a resizable-valued path has a header of its own to reference
// (C.2): a variable, or the tail of a frame object.
inline bool TypeCheck::Referenceable(Node *n, const Val &v) {
    if (ClassOf(v.type) != SC_RESIZABLE) return true;
    if (Is<Ident>(n)) return true;
    auto d = Is<Dot>(n);
    if (!d || !d->obj->exprtype) return false;
    auto ot = d->obj->exprtype;
    if (ot->kind == TY_REF) ot = ot->ref->sub;
    return ot->kind == TY_STRUCT && GetStructInst(ot)->frameobj;
}

inline bool TypeCheck::UserRefOf(Node *n) {
    auto u = Is<Unary>(n);
    return u && u->op == T_BITAND && !u->synth;
}

// A non-fixed value reaches a value destination only as an rvalue or an
// explicit copy (§4.1): an lvalue, or a reference to one, is never copied
// implicitly. A function's own local is moved by `return`.
inline void TypeCheck::RequireCopyable(const Val &v, Node *n, TypeExpr *dt) {
    if (!reachable) return;
    if (dt->kind == TY_REF || dt->kind == TY_SLICE || dt->kind == TY_VOID) return;
    if (ClassOf(dt) == SC_FIXED) return;
    auto src = IsPlainRef(v.type) ? v.type->ref->sub : v.type;
    if (ClassOf(src) == SC_FIXED) return;
    if (!v.lvalue && !IsPlainRef(v.type)) return;
    if (inreturn && v.lvalue)
        if (auto id = Is<Ident>(n); id && id->vdef && !id->vdef->isglobal &&
                                    id->vdef->ownerspec == frames.back().spec)
            return;
    auto u = Is<Unary>(n);
    auto what = ExprStr(u && u->op == T_BITAND ? u->child : n);
    Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1): pass copy(",
                 what, ") for a copy, or bind it by reference"));
}

// Argument nodes the checker rebound (by reference, or a copy() unwrapped)
// replace the originals: the receiver, then the call's own arguments.
inline void TypeCheck::WriteBackArgs(Call *c, Dot *d, vector<Node *> &argnodes) {
    d->obj = argnodes[0];
    for (size_t i = 0; i < c->args.size(); i++) c->args[i] = argnodes[i + 1];
}

// copy(x) checked: the node becomes x itself, the stored value codegen
// copies at the destination like any lvalue source.
inline void TypeCheck::UnwrapCopy(Node *&n) {
    if (auto c = Is<Call>(n); c && c->builtin == B_COPY) n = c->args[0];
}

// A value meeting a destination of type `expected` (null or void: none).
// Argument position (`callsite`) additionally allows the array→slice
// coercion (§3.10), and leaves the redundant-& warning to the call's own
// resolution, where an explicit & may have picked the overload.
inline Val TypeCheck::CheckValue(Node *&n, TypeExpr *expected, bool callsite) {
    auto v = CheckV(n, expected);
    UnwrapCopy(n);
    if (!expected || expected->kind == TY_VOID) {
        v = DecayRef(v);
    } else {
        if (!callsite && expected->kind == TY_REF && UserRefOf(n))
            Warn(n, cat("redundant &: ", ExprStr(Is<Unary>(n)->child),
                        " binds by reference here without it (§4.1)"));
        if (BindsRef(v, expected)) n = AutoRef(n, v);
        RequireCopyable(v, n, expected);
        if (!KeepsRef(v, expected)) v = DecayRef(v);
        MustFit(v, n, expected, callsite);
        NoRelRefCopy(n, expected);
    }
    n->exprtype = v.type;
    return v;
}

// The same, with `d` as the destination the value constructs into.
inline Val TypeCheck::CheckValueAt(Node *&n, TypeExpr *expected, Dest d, bool callsite) {
    DestScope ds(*this, d);
    return CheckValue(n, expected, callsite);
}

// An operand of an operator: always the pointee.
inline Val TypeCheck::Operand(Node *n) {
    auto v = DecayRef(CheckV(n, nullptr));
    n->exprtype = v.type;
    return v;
}

// A specific reason from the last failing FitsAt, if any.

inline void TypeCheck::MustFit(Val &v, Node *n, TypeExpr *dt, bool callsite) {
    if (!reachable) return;  // A diverging operand fits anything.
    fitfail.clear();
    if (!FitsAt(v, dt, callsite)) {
        if (!fitfail.empty()) Error(n, fitfail);
        Error(n, cat("expected a value of type ", TypeStr(dt), ", got ", TypeStr(v.type),
                     v.type->kind == TY_INT && dt->kind == TY_INT
                         ? " (narrowing and sign changes require an explicit `as`)"
                         : ""));
    }
}

// The implicit adaptations legal at construction/assignment sites (§6.3,
// §3.1, §3.7, §3.10). On success v.type becomes dt. Also the enforcement
// point of the store rule (§9.2): a reference/slice stored into storage
// owned by curdst must be rooted at least as shallow (call-site argument
// slots pass curdst null: parameters always die before their arguments'
// roots).
inline bool TypeCheck::FitsAt(Val &v, TypeExpr *dt, bool callsite) {
    auto t = v.type;
    // An lvalue at a reference destination is the reference to it (§4.1).
    if (BindsRef(v, dt)) {
        t = v.type = RefTo(t, dt->line);
        v.lvalue = false;
    }
    // The null literal fits any optional (plain or relative).
    if (v.isnull) {
        if (dt->kind == TY_REF && dt->ref->optional) { v.type = dt; return true; }
        fitfail = cat("null is only a value of optional types, not ", TypeStr(dt));
        return false;
    }
    if ((dt->kind == TY_REF || dt->kind == TY_SLICE) &&
        (t->kind == TY_REF || t->kind == TY_SLICE) && !callsite && curdst.root) {
        auto root = CanonRoot(v.root);
        if (root && root == cycleroot) {
            fitfail = "storing the result of a recursive call whose returned "
                      "reference's root the cycle's returns do not determine "
                      "(§7.8); it may only be passed down";
            return false;
        }
        if (Depth(root) > Depth(CanonRoot(curdst.root))) {
            fitfail = cat("storing a reference rooted at ",
                          root ? root->name : string_view("static data"),
                          ", which does not outlive the destination (§9.2)");
            return false;
        }
        if (!curdst.varbind && IsGrowShrinkRoot(root)) {
            fitfail = cat("storing a reference into ", root->name,
                          ", which holds a grow-shrink array: such a reference lives in a "
                          "variable, is passed down or returned, and is never stored (§5.2)");
            return false;
        }
        auto spec = CurRealFrame().spec;
        if (root && !root->isglobal && !root->poolclass && spec &&
            (spec->incycle || spec->sf->isrec)) {
            fitfail = "references may only be passed down, not stored, inside a "
                      "recursive cycle (§7.8)";
            return false;
        }
    }
    if (TypeEq(t, dt)) { v.type = dt; return true; }
    switch (dt->kind) {
        case TY_INT:
            if (t->kind != TY_INT) return false;
            // A constant adapts to any integer type its value fits.
            if (v.ck == CK_INT) {
                if (FitsIntStorage(v.ival, v.uns, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                fitfail = cat("constant ", ConstStr(v), " does not fit ", TypeStr(dt));
                return false;
            }
            if (dt->intstorage == IS_VARINT) {
                // varint stores hold the full i64 value range: every
                // integer type embeds except u64 (§3.6).
                if (t->intstorage != IS_U64 && t->intstorage != IS_VARINT) {
                    v.type = dt;
                    return true;
                }
                return false;
            }
            if (ImplicitInt(t->intstorage, dt->intstorage)) {
                v.type = dt;
                return true;
            }
            return false;
        case TY_FLT:
            if (t->kind != TY_FLT) return false;
            // Literals adapt to f32; f32 widens to f64.
            if (IsF32(dt)) { if (v.ck != CK_FLT) return false; }
            else if (!IsF32(t)) return false;
            v.type = dt;
            return true;
        case TY_ARRAY: {
            // Construction of an array from another array/slice of the
            // same element type (copies, §3.7/§4.2). Fixed destinations
            // need a statically known length, so only [] adapts.
            TypeExpr *selem = nullptr;
            if (t->kind == TY_ARRAY) selem = t->arr->sub;
            else if (t->kind == TY_SLICE) selem = t->sub;
            else return false;
            if (v.emptyarr) {
                if (dt->arr->akind == A_FIXED && ArraySize(dt->arr) != 0) return false;
                v.type = dt;
                return true;
            }
            if (dt->arr->akind == A_FIXED) return false;
            if (!TypeEq(selem, dt->arr->sub)) return false;
            v.type = dt;
            return true;
        }
        case TY_SLICE: {
            // Whole-array argument to a slice parameter, call sites only;
            // through a reference the pointee array is sliced in place.
            auto at = t;
            if (IsPlainRef(t) && t->ref->sub->kind == TY_ARRAY) at = t->ref->sub;
            if (!callsite || at->kind != TY_ARRAY) return false;
            if (!TypeEq(at->arr->sub, dt->sub)) return false;
            v.type = dt;
            return true;
        }
        case TY_REF: {
            if (t->kind != TY_REF) return false;
            if (!TypeEq(t->ref->sub, dt->ref->sub)) return false;
            if (t->ref->lenstorage >= 0) return false;  // Values are never relative.
            if (dt->ref->lenstorage >= 0) {
                // Storing an ordinary reference into a relative reference
                // location (§3.9). A self-relative one needs both ends in
                // the same root array; an `in pool` one needs the value in
                // the pool, and takes the destination wherever it is.
                if (t->ref->optional && !dt->ref->optional) return false;
                // Null is the only reference value rooted at static data
                // (§9.5's read-back rule says so too, and a null argument
                // is what puts a reference parameter in the static root
                // class), and it stores as the sentinel offset, which
                // means the same in every location. So an optional
                // relative slot takes it wherever the slot is: a linked
                // structure's sentinel end does not force plain links.
                if (t->ref->optional && dt->ref->optional && v.rootexact &&
                    !CanonRoot(v.root)) {
                    v.type = dt;
                    return true;
                }
                // The target must be the *same* array, so a root that only
                // bounds the pointee's lifetime will not do (§9.5).
                auto want = dt->ref->pool ? dt->ref->pool : CanonRoot(curdst.root);
                auto have = dt->ref->pool ? PoolOf(v.root) : CanonRoot(v.root);
                if (!want || (!dt->ref->pool && !curdst.exact) || !v.rootexact ||
                    have != want) {
                    auto why = v.rootexact ? string() : ReadBackWhy(v.type, v.rootfrom);
                    fitfail = cat(dt->ref->pool
                                      ? cat("a relative reference in ", want->name,
                                            " must point into ", want->name, " (§3.9); ")
                                      : string("a relative reference must point within the "
                                               "same root as its location (§3.9); "),
                                  !why.empty() ? why
                                  : !v.rootexact
                                      ? cat("this reference's root is not known exactly, "
                                            "only that it outlives ",
                                            v.root ? CanonRoot(v.root)->name
                                                   : string_view("static data"))
                                  : cat("this reference is rooted at ",
                                        v.root ? CanonRoot(v.root)->name
                                               : string_view("static data")));
                    return false;
                }
                v.type = dt;
                return true;
            }
            // T& widens to T?.
            if (dt->ref->optional && !t->ref->optional) { v.type = dt; return true; }
            return false;
        }
        case TY_ENUM: {
            // Mode adaptation copies at construction (fixed <-> variable);
            // a variant value constructs its enum (the tag is static).
            if (t->kind == TY_VARIANT) {
                auto adt = t->var->adt;
                if (adt->enu->en != dt->enu->en) return false;
                if (!TypeArgsEq(adt->enu->args, dt->enu->args)) return false;
            } else if (t->kind == TY_ENUM) {
                if (t->enu->en != dt->enu->en) return false;
                if (!TypeArgsEq(t->enu->args, dt->enu->args)) return false;
            } else {
                return false;
            }
            if (!dt->enu->varmode && !GetEnumInst(dt)->allfixed) return false;
            v.type = dt;
            return true;
        }
        default: return false;
    }
}

// Whether the constant (value bits v, u64-flavored when uns) fits the
// given integer type. varint holds the full i64 value range.
inline bool TypeCheck::FitsIntStorage(int64_t v, bool uns, IntStorage s) {
    if (uns) return s == IS_U64;   // Above i64.max: only u64 holds it.
    switch (s) {
        case IS_I8:  return v >= -128 && v <= 127;
        case IS_I16: return v >= -32768 && v <= 32767;
        case IS_I32: return v >= INT32_MIN && v <= INT32_MAX;
        case IS_U8:  return v >= 0 && v <= 255;
        case IS_U16: return v >= 0 && v <= 65535;
        case IS_U32: return v >= 0 && v <= UINT32_MAX;
        case IS_U64: return v >= 0;
        case IS_I64: case IS_VARINT: return true;
    }
    return false;
}

inline string TypeCheck::ConstStr(const Val &v) {
    return v.uns ? cat((uint64_t)v.ival) : cat(v.ival);
}

// Conditions: bool, or an optional (§3.8 truthiness + narrowing). Plain
// references decay (a bool& condition reads its pointee); an already
// narrowed optional stays a valid (trivially true) test.
inline Val TypeCheck::CheckCond(Node *n) {
    auto v = CheckV(n, nullptr);
    auto id = Is<Ident>(n);
    auto narrowedopt = id && id->vdef && IsOptional(id->vdef->type) && id->vdef->narrowed;
    if (!narrowedopt) v = DecayRef(v);
    n->exprtype = v.type;
    if (!narrowedopt && v.type->kind != TY_BOOL && !IsOptional(v.type))
        Error(n, cat("condition must be bool or an optional, got ", TypeStr(v.type)));
    return v;
}

// Unifies two branch values (literal/f32/root adaptations); null = branch
// diverged (bottom). Errors when both produce values of unrelated types
// and a value is wanted.
inline TypeExpr *TypeCheck::UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue) {
    if (!a) return b;
    if (!b) return a;
    if (TypeEq(a, b)) return a;
    if (a->kind == TY_INT && b->kind == TY_INT) {
        if (ImplicitInt(a->intstorage, b->intstorage)) return b;
        if (ImplicitInt(b->intstorage, a->intstorage)) return a;
    }
    if (a->kind == TY_FLT && b->kind == TY_FLT) return ast.flttypes[FS_F64];
    if (!wantvalue) return ast.voidtype;
    Error(at, cat("branches have mismatched types: ", TypeStr(a), " vs ", TypeStr(b)));
}

inline Val TypeCheck::VoidVal() {
    Val v;
    v.type = ast.voidtype;
    return v;
}

inline TypeExpr *TypeCheck::FixedArrayOf(TypeExpr *elem, int64_t count, Line l) {
    auto t = ast.NewType(TY_ARRAY, l);
    t->arr = ast.NewDetail<TypeArray>();
    t->arr->sub = elem;
    t->arr->akind = A_FIXED;
    t->arr->size = count;
    return t;
}

// &lvalue: reference creation (§3.8) with its restrictions. On a location
// that itself holds a reference (a reference variable or field), yields
// the stored reference — there are no references to references.
inline Val TypeCheck::CheckRefOf(Unary *x) {
    auto lv = CheckLValue(x->child);
    if (lv.var) RequireAssigned(lv.var, x);
    // A reference to a resizable value points at its header (C.2): a whole
    // variable's, or a frame object's tail's; a resizable nested in any
    // other shape (a variable-size prefix, an ADT payload) has none.
    if (!lv.var && !lv.fotail && lv.type->kind != TY_REF &&
        ClassOf(lv.type) == SC_RESIZABLE)
        Error(x, "cannot reference a resizable value nested in a variable-size prefix "
                 "or an ADT payload; reference the owning variable instead");
    if (lv.type->kind == TY_REF) {
        // Out of a container, the stored reference is a read-back (§9.5).
        if (!lv.var) return ContainerRead(lv);
        Val v;
        v.type = LoadType(lv.type);  // Relative refs load as plain (§3.9).
        v.SetProv(RefProvOf(lv.var));
        return v;
    }
    Val v;
    v.type = RefTo(lv.type, x->line);
    v.SetProv(lv);
    v.writable = lv.writable && !lv.isvarint;
    return v;
}

// Wrap-free signed 64-bit arithmetic, reporting overflow.
inline bool TypeCheck::AddOv(int64_t a, int64_t b, int64_t &r) {
    r = (int64_t)((uint64_t)a + (uint64_t)b);
    return ((a ^ r) & (b ^ r)) < 0;
}

inline bool TypeCheck::SubOv(int64_t a, int64_t b, int64_t &r) {
    r = (int64_t)((uint64_t)a - (uint64_t)b);
    return ((a ^ b) & (a ^ r)) < 0;
}

inline bool TypeCheck::MulOv(int64_t a, int64_t b, int64_t &r) {
    r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a == 0 || b == 0) return false;
    if (a == -1) return b == INT64_MIN;
    if (b == -1) return a == INT64_MIN;
    return r / b != a;
}

// Signed `%` is Euclidean (§6.2): the result is in [0, |b|), never
// negative. Callers check b != 0 first. The adjustment is computed
// unsigned so that b == i64.min (whose negation is unrepresentable) and
// the i64.min % -1 case both work out; the latter's exact remainder is 0,
// which is why it needs no hardware division.
inline int64_t TypeCheck::EuclidMod(int64_t a, int64_t b) {
    if (b == -1) return 0;
    auto r = a % b;
    if (r < 0) r = (int64_t)((uint64_t)r + (b < 0 ? 0u - (uint64_t)b : (uint64_t)b));
    return r;
}

// Folds a constant binary op at the width and signedness of out.type. An
// operation whose result does not fit is left for the runtime (overflow
// aborts in debug builds, §6.2); division by a constant zero is a
// compile error. Operand values fit out.type (the unify rules ensured it).
inline void TypeCheck::FoldInt(TType op, Val &l, Val &r, Val &out, Node *at) {
    if (l.ck != CK_INT || r.ck != CK_INT) return;
    auto s = out.type->intstorage;
    if (s == IS_VARINT) return;
    auto bits = IntBits(s);
    if (IsUnsigned(s)) {
        // Unsigned arithmetic wraps modulo 2^width by definition (§6.2),
        // so every operation folds exactly.
        auto a = (uint64_t)l.ival, b = (uint64_t)r.ival;
        auto max = bits == 64 ? UINT64_MAX : (1ull << bits) - 1;
        uint64_t res = 0;
        switch (op) {
            case T_PLUS:   res = (a + b) & max; break;
            case T_MINUS:  res = (a - b) & max; break;
            case T_MUL:    res = (a * b) & max; break;
            case T_DIV:    if (!b) Error(at, "constant division by zero"); res = a / b; break;
            case T_MOD:    if (!b) Error(at, "constant division by zero"); res = a % b; break;
            case T_BITAND: res = a & b; break;
            case T_BITOR:  res = a | b; break;
            case T_XOR:    res = a ^ b; break;
            case T_SHL:    res = (a << (b & (bits - 1))) & max; break;
            case T_SHR:    res = (a & max) >> (b & (bits - 1)); break;
            default:       return;
        }
        out.ck = CK_INT;
        out.ival = (int64_t)res;
        out.uns = s == IS_U64 && out.ival < 0;
        return;
    }
    auto a = l.ival, b = r.ival;
    int64_t res = 0;
    switch (op) {
        case T_PLUS:   if (AddOv(a, b, res)) return; break;
        case T_MINUS:  if (SubOv(a, b, res)) return; break;
        case T_MUL:    if (MulOv(a, b, res)) return; break;
        case T_DIV:
            if (!b) Error(at, "constant division by zero");
            if (a == INT64_MIN && b == -1) return;
            res = a / b;
            break;
        case T_MOD:
            if (!b) Error(at, "constant division by zero");
            res = EuclidMod(a, b);
            break;
        case T_BITAND: res = a & b; break;
        case T_BITOR:  res = a | b; break;
        case T_XOR:    res = a ^ b; break;
        case T_SHL:    res = (int64_t)((uint64_t)a << (b & (bits - 1))); break;
        case T_SHR:    res = a >> (b & (bits - 1)); break;
        default:       return;
    }
    if (!FitsIntStorage(res, false, s)) return;
    out.ck = CK_INT;
    out.ival = res;
}

// The operand/result type of a binary numeric operator: equal types
// stand; a constant adapts to the other operand's type; otherwise the
// operand that implicitly widens into the other picks the wider type
// (§6.1). Returns null for non-numeric or int/float-mixed pairs.
inline TypeExpr *TypeCheck::UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt,
                                         TypeExpr *rt, bool cmp) {
    if (IsIntT(lt) && IsIntT(rt)) {
        if (TypeEq(lt, rt)) return lt;
        if (lv.ck == CK_INT && rv.ck == CK_INT) {
            if (lv.uns || rv.uns) {
                if ((!lv.uns && lv.ival < 0) || (!rv.uns && rv.ival < 0))
                    Error(at, "constant operands have no common type (one is above "
                              "i64.max, the other negative)");
                return ast.inttypes[IS_U64];
            }
            return ast.inttypes[IS_I64];
        }
        if (lv.ck == CK_INT) {
            if (!FitsIntStorage(lv.ival, lv.uns, rt->intstorage))
                Error(at, cat("constant ", ConstStr(lv), " does not fit ", TypeStr(rt)));
            return rt;
        }
        if (rv.ck == CK_INT) {
            if (!FitsIntStorage(rv.ival, rv.uns, lt->intstorage))
                Error(at, cat("constant ", ConstStr(rv), " does not fit ", TypeStr(lt)));
            return lt;
        }
        if (ImplicitInt(lt->intstorage, rt->intstorage)) return rt;
        if (ImplicitInt(rt->intstorage, lt->intstorage)) return lt;
        // §6.1: a comparison produces bool, so it has no result type to
        // pick and the mathematical answer across signedness is never in
        // doubt. u64 is the one unsigned type with no signed supertype;
        // it may meet a signed operand the compiler knows is
        // non-negative, and the compare is then a single unsigned one.
        // Without that knowledge the conversion could change the value,
        // so the cast has to be written (and thought about).
        if (cmp) {
            auto isu64 = [](TypeExpr *t) { return t->intstorage == IS_U64; };
            if (isu64(lt) != isu64(rt)) {
                auto &sv = isu64(lt) ? rv : lv;
                auto st = isu64(lt) ? rt : lt;
                if (!IsUnsigned(st->intstorage)) {
                    if (sv.nonneg) return ast.inttypes[IS_U64];
                    Error(at, cat("comparing ", TypeStr(lt), " with ", TypeStr(rt),
                                  " needs the signed side to be known non-negative "
                                  "(a literal, .len/.cap, or a `let` bound to one); "
                                  "convert it with `as` otherwise"));
                }
            }
        }
        Error(at, cat("operands of ", TName(op), " have no common type: ", TypeStr(lt),
                      " and ", TypeStr(rt), " (convert one with `as`)"));
    }
    if (lt->kind == TY_FLT && rt->kind == TY_FLT) {
        if (TypeEq(lt, rt)) return lt;
        // One side is f32, the other f64: a literal adapts to the typed
        // side, otherwise f32 widens (§6.3).
        if (lv.ck == CK_FLT) return rt;
        if (rv.ck == CK_FLT) return lt;
        return ast.flttypes[FS_F64];
    }
    return nullptr;
}

// Re-types both operands to the unified type ct: adapted constants and
// implicitly widened operands emit at ct downstream.
inline void TypeCheck::RetypeOperands(Node *left, Node *right, Val &lv, Val &rv, TypeExpr *ct) {
    lv.type = ct;
    rv.type = ct;
    left->exprtype = ct;
    right->exprtype = ct;
}

// All scalar leaves integers, or all floats; only structs and fixed
// arrays compose; the value must be fixed-size (a constructed result).
inline bool TypeCheck::ElementwiseOK(TypeExpr *t) {
    int isint = -1;
    function<bool(TypeExpr *)> rec = [&](TypeExpr *t2) -> bool {
        switch (t2->kind) {
            case TY_INT:
                if (t2->intstorage == IS_VARINT) return false;
                if (isint == 0) return false;
                isint = 1;
                return true;
            case TY_FLT:
                if (isint == 1) return false;
                isint = 0;
                return true;
            case TY_STRUCT: {
                auto inst = GetStructInst(t2);
                for (size_t i = 0; i < inst->ftypes.size(); i++)
                    if (inst->ftypes[i] && !rec(inst->ftypes[i])) return false;
                return true;
            }
            case TY_ARRAY:
                return t2->arr->akind == A_FIXED && rec(t2->arr->sub);
            default:
                return false;
        }
    };
    return (t->kind == TY_STRUCT || t->kind == TY_ARRAY) && rec(t);
}

inline Val TypeCheck::CheckVariantConst(Dot *d, SEnum *en) {
    if (!en->generics.empty())
        Error(d, cat("generic enum ", en->name, " needs type arguments to name a variant"));
    auto t = ast.NewType(TY_ENUM, d->line);
    t->enu = ast.NewDetail<TypeEnum>();
    t->enu->en = en;
    auto inst = GetEnumInst(t);
    SVariant *found = nullptr;
    for (auto &var : en->variants) if (var.name == d->name) { found = &var; break; }
    if (!found) Error(d, cat("enum ", en->name, " has no variant named ", d->name));
    if (found->has_payload)
        Error(d, cat("variant ", en->name, ".", d->name,
                     " has a payload; construct it with ", en->name, ".", d->name, " { ... }"));
    d->variantconst = found;
    d->einst = inst;
    if (!inst->allfixed) t->enu->varmode = true;
    Val v;
    v.type = t;
    return v;
}

inline TypeExpr *TypeCheck::VariantTypeOf(TypeExpr *enumtype, SVariant *v, Line l) {
    auto t = ast.NewType(TY_VARIANT, l);
    t->var = ast.NewDetail<TypeVariant>();
    // Variant types are mode-neutral; drop varmode from the adt type.
    if (enumtype->enu->varmode) {
        auto base = ast.NewType(TY_ENUM, l);
        base->enu = ast.NewDetail<TypeEnum>();
        base->enu->en = enumtype->enu->en;
        base->enu->args = enumtype->enu->args;
        t->var->adt = base;
    } else {
        t->var->adt = enumtype;
    }
    t->var->variant = v;
    return t;
}

// ------------------------------------------------------------------
// Struct and variant literals (§4.2). The per-node entry is
// StructLit::Check at the end of this file.

// `self` in a field initializer: the field must hold a non-optional
// relative reference to the very value being constructed (§3.9), which is
// the one reference to it that exists before the value does. Optional
// relative references are excluded because offset 0 is their null.
inline void TypeCheck::CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft) {
    if (ft->kind != TY_REF || ft->ref->lenstorage < 0)
        Error(n, cat("self initializes relative-reference fields (T&<u32> and friends), "
                     "not ", TypeStr(ft)));
    if (ft->ref->optional)
        Error(n, cat("self cannot initialize the optional relative reference ",
                     TypeStr(ft), ": offset 0 is its null (§3.9)"));
    if (!TypeEq(ft->ref->sub, selft))
        Error(n, cat("self here is a value of type ", TypeStr(selft), ", which does not "
                     "fit a field of type ", TypeStr(ft)));
    // An `in pool` self is the value's own offset in the pool, so unlike a
    // self-relative one it only means anything where the literal is being
    // built: inside that pool.
    if (ft->ref->pool && (!curdst.exact || PoolOf(curdst.root) != ft->ref->pool))
        Error(n, cat("self initializes ", TypeStr(ft), " only in a literal being built "
                     "inside ", ft->ref->pool->name, " (a push, an alloc, or an element "
                     "store), since it stores the value's own offset in it (§3.9)"));
    // A resizable pointee needs a header the offset cannot carry; the root
    // rule keeps every other relative reference away from one, but a
    // self-reference satisfies that rule by construction.
    if (ClassOf(selft) == SC_RESIZABLE)
        Error(n, cat("self cannot be stored relative: ", TypeStr(selft),
                     " is resizable, and a relative reference is an offset alone (§3.9)"));
    n->exprtype = ft;
}

// ------------------------------------------------------------------
// Statements.

inline void TypeCheck::CheckStmt(Node *n) {
    if (auto vd = Is<VarDecl>(n)) { CheckVarDecl(vd, false); return; }
    if (auto a = Is<Assign>(n)) { CheckAssign(a); return; }
    if (auto x = Is<IncDec>(n)) { CheckIncDec(x); return; }
    if (auto fd = Is<FnDecl>(n)) {
        // Nested function: visible from here to the end of the scope;
        // checked when called, specialized per caller (§7.5).
        localfns.push_back({ (int)scopes.size() - 1, fd->sf });
        return;
    }
    CheckStmtExpr(n);
}

}  // namespace goose
