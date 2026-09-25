// Goose compiler — the typechecker's expressions (definitions of TypeCheck
// members, typecheck.h): lvalue paths, values with reference transparency
// (§3.8) and the implicit conversions (§6.3, §3.10), the store rule (§9.2),
// constant folding and operand unification (§6.1), and struct and variant
// literals (§4.2).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Lvalue paths: names, fields, elements, optionally through references.

inline TypeCheck::LVal TypeCheck::CheckLValue(Node *n) {
    NodeScope ns(*this, n);
    // The value the path denotes, recorded as its Check would (Ident::Check,
    // ContainerRead): what the operands evaluated before a later point of
    // the statement hold (HeldOperands).
    auto record = [&](const LVal &lv) {
        Val v;
        v.type = LoadType(lv.type);
        v.SetProv(lv);
        RecordVal(n, v);
    };
    if (auto id = Is<Ident>(n)) {
        auto vd = LookupVar(id->name, id->ns, n);
        if (!vd) Error(n, cat("unknown variable: ", id->name));
        id->vdef = vd;
        LVal lv;
        lv.type = vd->narrowed ? vd->narrowed : vd->type;
        lv.var = vd;
        lv.Set(vd, true);
        lv.byteview = vd->contentbyteview;
        // Contents are writable unless the type says const or the binding
        // is a copy (§9.5). Whether the variable itself is assigned is up
        // to its binding alone (`let`, a copy: WholeWritable, §4.4).
        lv.writable = !vd->copybind && !(vd->type && vd->type->cq);
        lv.letbound = !vd->isvar;
        lv.letname = vd->name;
        lv.copyof = vd->copybind ? vd : nullptr;
        lv.reusable = vd->reusable;
        n->exprtype = vd->type;
        if (IsRefOrSlice(lv.type)) {
            Val v;
            v.type = LoadType(lv.type);
            v.SetProv(RefProvOf(vd));
            RecordVal(n, v);
        } else {
            record(lv);
        }
        return lv;
    }
    if (auto d = Is<Dot>(n)) {
        auto lv = LValueBase(d->obj);
        DerefLValue(lv, d->obj);
        ResolveMemberLValue(lv, d);
        n->exprtype = lv.type;
        RecordVal(n, ContainerRead(lv));
        return lv;
    }
    if (auto ix = Is<Index>(n)) {
        auto lv = LValueBase(ix->obj);
        DerefLValue(lv, ix->obj);
        SliceProvenance(lv, ix->obj);
        TypeExpr *elem;
        if (lv.type->kind == TY_SLICE) {
            elem = lv.type->sub;
        } else {
            if (lv.type->kind != TY_ARRAY)
                Error(n, cat("cannot index a value of type ", TypeStr(lv.type)));
            RequireComplete(lv.type, n->line);
            elem = lv.type->arr->sub;
        }
        if (ClassOf(elem) != SC_FIXED)
            Error(n, cat(lv.type->kind == TY_SLICE ? "slices" : "arrays",
                         " of variable-size elements cannot be indexed, only iterated"));
        CheckIntAny(ix->idx);
        if (lv.type->cq) lv.writable = false;   // An element of a const value.
        lv.letbound = false;
        lv.type = elem;
        lv.var = nullptr;
        lv.fromstorage = true;
        lv.isslot = true;
        lv.isvarint = elem->kind == TY_INT && elem->intstorage == IS_VARINT;
        n->exprtype = lv.type;
        RecordVal(n, ContainerRead(lv));
        return lv;
    }
    Error(n, "not an assignable location");
}

// The base of a path: itself a path, or any other expression (a call
// result, a string literal, ...) whose value is then addressed. A null
// root means static data; a temporary has a root of its own (TempRoot).
// `cmpview`: the path is the slice `==` compares an operand as (§4.5),
// which ends with the comparison, so it may view any temporary.
inline TypeCheck::LVal TypeCheck::LValueBase(Node *n, bool cmpview) {
    if (Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n)) return CheckLValue(n);
    auto v = CheckV(n, nullptr);
    if (!cmpview) NoTemporaryLiteral(n, v.type);
    n->exprtype = v.type;
    LVal lv;
    lv.type = v.type;
    lv.SetProv(v);
    // A value result is materialized in its own temporary storage. References
    // and slices instead retain the (possibly inexact) owner they borrow.
    lv.intemp = TempContents(v, lv.contents);
    if (lv.intemp) lv.Set(v.Root(), true);
    return lv;
}

// An array literal of variable-size elements is a T[] (§3.3), and such a
// value comes into existence only where a construction context builds it
// (§4.2). A statement temporary would have to hold it for a slice or
// reference to view, where a fixed one is a plain C temporary.
inline void TypeCheck::NoTemporaryLiteral(Node *n, TypeExpr *t) {
    if (!Is<ArrayLit>(n) || ClassOf(t) == SC_FIXED) return;
    Error(n, cat("an array literal of variable-size elements is a ", TypeStr(t),
                 ", built only in a construction context (§4.2), never as a temporary "
                 "for a slice or reference to view; bind it to a variable first"));
}

// Crossing a reference in a path (auto-deref, §3.8): the storage owner
// becomes the reference's root, writability its provenance, and the rest of
// the path lies in its pointee (Prov::reached).
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
    lv.reached = LoadType(lv.type);
    lv.var = nullptr;
    lv.letbound = false;
    lv.throughref = true;
    // The pointee may be a whole grow-shrink array, or a variable holding a
    // view into one (RootAlt::slotread).
    lv.isslot = false;
    lv.ClearSlotRead();
    if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT) lv.isvarint = true;
}

inline void TypeCheck::RequireAssigned(VarDef *vd, Node *at) {
    if (!vd->assigned)
        Error(at, cat("variable ", vd->name, " may be used before it is assigned"));
}

// Accessing through a slice variable: writes and roots follow the slice
// value's provenance, not the variable's own var-ness. What is indexed or
// sliced lies in the elements it views (Prov::reached).
inline void TypeCheck::SliceProvenance(LVal &lv, Node *at) {
    if (lv.type->kind != TY_SLICE) return;
    if (!lv.var) {
        if (lv.fromstorage) ReadBackLVal(lv);
        else if (lv.throughref) lv.SetProv(SlotView(lv, lv.type));
        lv.reached = lv.type->sub;
        return;
    }
    RequireAssigned(lv.var, at);
    lv.SetProv(RefProvOf(lv.var));
    lv.reached = lv.type->sub;
    lv.var = nullptr;
}

// A location whose own type is a reference or slice: the value loaded out
// of it is a read-back, so its root is re-derived (§9.5).
inline void TypeCheck::ReadBackLVal(LVal &lv) {
    if (!IsRefOrSlice(lv.type)) return;
    // A byte view can point at any typed storage. Its owner cannot be
    // recovered by enumerating u8 containers. Global slots may have been
    // filled by functions whose stores have not yet been checked. That
    // matters to a grow-only array's shrink (§5.1), whose byte views are
    // stored like any other view; a grow-shrink array's shrink passes over
    // a slot read, byte view or not (§5.2).
    lv.byteview = lv.byteview || lv.Any([&](const RootAlt &a) {
        return a.root && (a.root->contentbyteview ||
                          (a.root->isglobal && lv.type->cq && IsU8(PointeeOf(lv.type))));
    });
    auto rb = ReadBackRoot(lv.type, lv, lv.byteview, lv.intemp ? &lv.contents : nullptr);
    // What was stored into a slot passed the store rule (§5.2) with its own
    // provenance; the container's says nothing about it.
    auto slotread = lv.isslot && SlotReadable(lv.type);
    lv.alts = rb.alts;
    for (auto &a : lv.alts) a.slotread = slotread;
    lv.reached = nullptr;
    lv.intemp = false;
    if (lv.type->cq) lv.writable = false;   // A `const` slot's contents (§9.5).
}

// The value a field or element location yields: the load, re-rooted by
// the read-back rule, and, where it is a reference or slice, as writable
// as the slot's type says: only a writable value can have been stored in
// a slot that is not `const` (§9.5).
inline Val TypeCheck::ContainerRead(LVal lv) {
    ReadBackLVal(lv);
    Val v;
    v.type = LoadType(lv.type);
    v.SetProv(lv);
    if (IsRefOrSlice(v.type)) v.writable = !lv.type->cq;
    else if (HoldsPlainRef(v.type)) {
        // What a holder read out of a container points at is bounded by
        // the container: everything stored into it had to outlive it. Out
        // of a temporary, it points where the temporary's contents do.
        if (lv.intemp) {
            v.contents = lv.contents.roots;
        } else {
            v.contents = lv;
            v.contents.Weaken();
        }
        v.holderset = true;
        v.holderfrom = lv.intemp ? lv.contents.from : lv.Root();
    }
    v.lvalue = v.type->kind != TY_REF;
    return v;
}

// The operands of n, in evaluation order (§2), each with how n consumes it
// once its later operands have run. A call consumes its receiver and
// arguments at the call; print, str and format render each argument just
// before the next runs (EmitFormatInto, EmitStr), so an earlier argument is
// nothing afterwards, and only the one being rendered is held (HeldOperands).
// A condition, a scrutinee and an iterated sequence are read before the
// construct's parts run, so they hold nothing over them: a `for` binding
// and a match binder are variables in scope, which the scans see.
template<typename F> void TypeCheck::ForOperands(Node *n, F f) {
    if (auto c = Is<Call>(n)) {
        auto builtin = c->builtin >= 0;
        auto member = builtin && (BuiltinByKind(c->builtin).flags & BF_MEMBER);
        auto render = builtin && (c->builtin == B_PRINT || c->builtin == B_STR ||
                                  c->builtin == B_FORMAT);
        auto first = true;
        auto kind = [&]() {
            auto k = member && first ? HK_RECEIVER : render ? HK_NONE : HK_VALUE;
            first = false;
            return k;
        };
        if (auto d = Is<Dot>(c->callee)) f(d->obj, kind());
        for (auto a : c->args) f(a, kind());
        return;
    }
    if (auto ix = Is<Index>(n)) {
        f(ix->obj, HK_ELEMS);
        f(ix->idx, HK_VALUE);
        return;
    }
    if (auto se = Is<SliceExpr>(n)) {
        f(se->obj, HK_ELEMS);
        if (se->lo) f(se->lo, HK_VALUE);
        if (se->hi) f(se->hi, HK_VALUE);
        return;
    }
    if (auto b = Is<Binary>(n)) {
        f(b->left, HK_VIEW);
        f(b->right, HK_VALUE);
        return;
    }
    if (auto a = Is<Assign>(n)) {
        f(a->lval, HK_LOCATION);
        f(a->rhs, HK_VALUE);
        return;
    }
    if (auto al = Is<ArrayLit>(n)) {
        for (auto e : al->elems) f(e, HK_VALUE);
        if (al->fillval) f(al->fillval, HK_VALUE);
        if (al->fillcount) f(al->fillcount, HK_VALUE);
        if (al->capexpr) f(al->capexpr, HK_VALUE);
        return;
    }
    if (auto sl = Is<StructLit>(n)) {
        for (auto &fi : sl->inits) f(fi.val, HK_VALUE);
        return;
    }
    if (auto r = Is<Return>(n)) {
        for (auto v : r->vals) f(v, HK_VALUE);
        return;
    }
    if (auto vd = Is<VarDecl>(n)) {
        for (auto i : vd->inits) f(i, HK_VALUE);
        return;
    }
    if (Is<Unary>(n) || Is<AsCast>(n) || Is<Dot>(n) || Is<IncDec>(n) || Is<Break>(n)) {
        n->Children([&](Node *ch) { f(ch, HK_VALUE); });
        return;
    }
    // Conditions, scrutinees, sequences, blocks and bodies: nothing held.
}

inline int TypeCheck::OperandIndex(Node *parent, Node *child) {
    auto idx = -1, i = 0;
    ForOperands(parent, [&](Node *ch, HoldKind) {
        if (ch == child) idx = i;
        i++;
    });
    return idx;
}

// Enters n, the operand its parent is at, or a node checked on its own (a
// statement, a path the checker walks itself); the same node entered again
// on the way, as a path is by its node's Check, stays one entry.
inline bool TypeCheck::Descend(Node *n) {
    if (!nodepath.empty() && nodepath.back().node == n) return false;
    auto idx = nodepath.empty() ? -1 : OperandIndex(nodepath.back().node, n);
    if (idx >= 0) nodepath.back().pos = idx;
    nodepath.push_back({ n, idx, 0 });
    return true;
}

// Leaves the innermost node: as an operand it has been evaluated, so its
// parent stands past it. An error's unwinding leaves a path a body's check
// replaced (CheckSpecBody) as it is.
inline void TypeCheck::Ascend(Node *n) {
    if (nodepath.empty() || nodepath.back().node != n) return;
    auto e = nodepath.back();
    nodepath.pop_back();
    if (e.idx >= 0 && !nodepath.empty()) nodepath.back().pos = e.idx + 1;
}

// What the operand n of `parent`, whose value is v, holds live: its value
// where it is a reference or slice; an array's elements where the parent
// views them (HK_VIEW), or indexes or slices them (HK_ELEMS, the elements the
// path leads to, a temporary's exactly); the references a holder holds,
// each as a reference to its pointee; a builtin's receiver as a reference
// to the array it is, or its elements where the builtin reads them
// (to_bytes, a slice); an assignment's location as recorded (CheckAssign).
template<typename F>
void TypeCheck::HoldAs(Node *n, const Val &v, HoldKind kind, Node *parent, const char *render,
                       F f) {
    if (!v.type || kind == HK_NONE) return;
    auto t = v.type;
    auto hold = [&](Val hv, bool location = false) {
        f(Held { n, std::move(hv), location, render });
    };
    switch (kind) {
        case HK_LOCATION:
            hold(v, true);
            return;
        case HK_ELEMS: {
            auto d = IsPlainRef(t) ? DecayRef(v) : v;
            TypeExpr *elem = d.type->kind == TY_ARRAY ? d.type->arr->sub
                             : d.type->kind == TY_SLICE ? d.type->sub : nullptr;
            if (!elem) return;
            if (IsTemp(d.Root())) d.Set(d.Root(), true);
            d.type = SliceOf(elem, n->line);
            hold(d);
            return;
        }
        case HK_RECEIVER: {
            auto c = (Call *)parent;
            auto rt = IsPlainRef(t) ? t->ref->sub : t;
            TypeExpr *elem = rt->kind == TY_ARRAY ? rt->arr->sub
                             : rt->kind == TY_SLICE ? rt->sub : nullptr;
            auto held = v;
            if (c->builtin == B_TO_BYTES || rt->kind == TY_SLICE) {
                if (!elem) return;
                held.type = SliceOf(elem, n->line);
            } else if (held.type->kind != TY_REF) {
                held.type = RefTo(rt, n->line);
            }
            if (IsTemp(held.Root()) && t->kind != TY_REF && t->kind != TY_SLICE)
                held.Set(held.Root(), true);
            hold(held);
            return;
        }
        default: break;
    }
    // HK_VALUE and HK_VIEW: values constructed in argument, literal and
    // result slots own their copies; references, slices and reference fields
    // still retain borrows, and an array operand viewed as a sequence its
    // elements until the operation ends.
    if (IsRefOrSlice(t)) {
        hold(v);
        return;
    }
    if (kind == HK_VIEW && t->kind == TY_ARRAY && ClassOf(t) != SC_FIXED) {
        auto view = v;
        view.type = SliceOf(t->arr->sub, n->line);
        hold(view);
    }
    if (HoldsPlainRef(t)) {
        vector<TypeExpr *> pointees;
        RefPointees(t, pointees);
        auto held = v;
        held.alts = ContentsOf(v).alts;
        for (auto pt : pointees) {
            held.type = RefTo(pt, n->line);
            hold(held);
        }
    }
}

// Every value evaluated before the point being checked that is still live:
// the operands each node on the path has evaluated so far (its parent
// consumes them after this point), as their parents hold them. A call at
// the point itself, applying its callee's effects, has consumed its own
// operands: what the callee does to them its parameters' pairs judge
// (§5.1, NoteLiveViews). The exception is the argument print, str or format
// is rendering (`renderarg`): its views, and where a struct, enum or array
// argument lies (`renderwhere`), which an overload of a part runs in the
// middle of rendering.
template<typename F> void TypeCheck::HeldOperands(F f) {
    for (auto &e : nodepath) {
        auto own = e.discovering || (&e == &nodepath.back() && Is<Call>(e.node));
        auto i = 0;
        ForOperands(e.node, [&](Node *ch, HoldKind kind) {
            auto at = i++;
            if (at >= e.pos) return;
            auto it = nodevals.find(ch);
            if (it == nodevals.end()) return;
            if (ch == renderarg && rendering) {
                HoldAs(ch, it->second, HK_VIEW, e.node, rendering, f);
                if (ch == renderwhere) {
                    auto where = it->second;
                    where.type = RefTo(where.type, ch->line);
                    f(Held { ch, std::move(where), false, rendering });
                }
                return;
            }
            if (own) return;
            HoldAs(ch, it->second, kind, e.node, nullptr, f);
        });
    }
}

// Every part of the statement that runs after the point being checked: the
// operands each node on the path has not evaluated yet, the one being
// checked excluded.
template<typename F> void TypeCheck::LaterOperands(F f) {
    for (size_t k = 0; k < nodepath.size(); k++) {
        auto &e = nodepath[k];
        auto last = k + 1 == nodepath.size();
        auto i = 0;
        ForOperands(e.node, [&](Node *ch, HoldKind) {
            auto at = i++;
            if (at > e.pos || (last && at == e.pos)) f(ch);
        });
    }
}

// The root bounding the references inside a holder value: what was
// derived for it, else the value's own root (a temporary's outlives nothing).
inline VarDef *TypeCheck::HolderRootOf(const Val &v) { return ContentsOf(v).Root(); }

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
            if (lv.type->cq) lv.writable = false;   // A field of a const value.
            lv.letbound = f.isconst;
            lv.letname = f.name;
            lv.type = ftypes[i];
            lv.var = nullptr;
            lv.fromstorage = true;
            lv.isslot = true;
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
        auto vi = inst->en->VariantIndex(t->var->variant);
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
    // A slice is the one its slot holds; a compound pointee value keeps the
    // container info, harmless, though crossing the reference drops what a
    // slot read says (RootAlt::slotread).
    if (r.type->kind == TY_SLICE) {
        r.alts = SlotView(v, r.type).alts;
    } else {
        r.alts = v.alts;
        r.ClearSlotRead();
    }
    r.byteview = v.byteview && HoldsPlainRef(r.type);
    return r;
}

// Does dt consume a reference value as-is (so no decay before fitting)?
inline bool TypeCheck::KeepsRef(const Val &v, TypeExpr *dt) {
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
    if (!Referenceable(n, v)) NoResizableRef(n);
    auto u = ast.New<Unary>(n->line, T_BITAND, n);
    u->synth = true;
    v.type = RefTo(ast.PlainOf(v.type), n->line);
    v.type->cq = !v.writable;   // The reference carries a const value's qualifier.
    v.lvalue = false;
    u->exprtype = v.type;
    return u;
}

inline bool TypeCheck::BindsRef(const Val &v, TypeExpr *dt) {
    return dt->kind == TY_REF && v.lvalue && v.type->kind != TY_REF && !v.isnull &&
           TypeEq(v.type, dt->ref->sub);
}

inline bool TypeCheck::IsNonFixedLValue(const Val &v) {
    return v.lvalue && !IsRefOrSlice(v.type) && ClassOf(v.type) != SC_FIXED;
}

// A reference to a non-fixed value denotes a non-fixed lvalue (§3.8). A
// varint pointee loads as the i64 it decodes to, a fixed-size value.
inline bool TypeCheck::IsNonFixedRef(const Val &v) {
    return IsPlainRef(v.type) && ClassOf(LoadType(v.type->ref->sub)) != SC_FIXED;
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

// A reference to a resizable value is a reference to its header (C.2), and
// only a variable and a frame object's tail have one of their own.
[[noreturn]] inline void TypeCheck::NoResizableRef(Node *at) {
    Error(at, "cannot reference a resizable value nested in a variable-size prefix "
              "or an ADT payload; reference the owning variable instead");
}

inline bool TypeCheck::UserRefOf(Node *n) {
    auto u = Is<Unary>(n);
    return u && u->op == T_BITAND && !u->synth;
}

// A non-fixed value reaches a value destination only as an rvalue or an
// explicit copy (§4.1): an lvalue, or a reference to one, is never copied
// implicitly. A function's own local is moved by `return`, or as its body's
// final expression.
inline bool TypeCheck::ImplicitCopy(const Val &v, Node *n, TypeExpr *dt) {
    if (!reachable) return false;
    if (IsRefOrSlice(dt) || dt->kind == TY_VOID) return false;
    if (ClassOf(dt) == SC_FIXED) return false;
    auto src = IsPlainRef(v.type) ? v.type->ref->sub : v.type;
    // A varint is read as the i64 it decodes to (§3.6), like any scalar.
    if (ClassOf(src) == SC_FIXED || IsVarintT(src)) return false;
    if (!v.lvalue && !IsPlainRef(v.type)) return false;
    if (inreturn && v.lvalue && IsOwnLocal(n)) return false;
    return true;
}

inline void TypeCheck::ImplicitCopyError(Node *n) {
    auto u = Is<Unary>(n);
    auto what = ExprStr(u && u->op == T_BITAND ? u->child : n);
    Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1): pass copy(",
                 what, ") for a copy, or bind it by reference"));
}

// A branch's value v whose construct has no destination type: the
// construct's value is a copy of it, of type dt, which non-fixed storage, or
// a reference to it, reaches only through copy(x), as at a value destination
// (§4.1). On an argument's path (argpath) a reference parameter may yet bind
// the branch by reference instead; `out`, the value the branch gives its
// construct, notes both for that argument.
inline void TypeCheck::CheckBranchCopy(const Val &v, Node *n, TypeExpr *dt, Val &out) {
    out.storagebranches = v.storagebranches || (IsNonFixedLValue(v) && Referenceable(n, v)) ||
                          IsNonFixedRef(v);
    out.implicitcopy = v.implicitcopy;
    if (!ImplicitCopy(v, n, dt)) return;
    if (argpath != n) ImplicitCopyError(n);
    if (!out.implicitcopy) out.implicitcopy = n;
}

// A local of the function being checked, which its return moves (§4.1).
inline bool TypeCheck::IsOwnLocal(Node *n) {
    auto id = Is<Ident>(n);
    return id && id->vdef && !id->vdef->isglobal && id->vdef->ownerspec == frames.back().spec;
}

// A value that tspec's result type is inferred from (§7.1) is taken as an
// un-annotated `let` takes its initializer (§3.8, §4.1): an explicit `&x`
// keeps its reference, a reference to a non-fixed value stays that
// reference, and a non-fixed lvalue is returned by reference, except the
// function's own local, which the return moves. Storage that does not
// outlive the function cannot be returned by reference, and a non-fixed
// value in it is not copied implicitly either.
inline Val TypeCheck::CheckInferredResult(Node *&n, FnSpec *tspec) {
    if (auto u = Is<Unary>(n); UserRefOf(n)) {
        auto v = CheckV(n, nullptr);
        n->exprtype = v.type;
        if (IsNonFixedRef(v) && !IsOwnLocal(u->child))
            Warn(n, cat("redundant &: ", ExprStr(u->child),
                        " is returned by reference without it (§4.1)"));
        return v;
    }
    auto v = CheckValue(n, nullptr, false, false, true);
    if (IsNonFixedLValue(v) && !IsOwnLocal(n)) n = AutoRef(n, v);
    // All of a multi-value call's results are forwarded, as they are.
    if (auto c = Is<Call>(n); c && c->rettypes.size() > 1) return v;
    if (reachable && IsNonFixedRef(v)) {
        auto dies = v.Any([&](const RootAlt &a) {
            return (a.root && a.root->ownerspec == tspec) || IsTemp(a.root);
        });
        if (dies) {
            auto what = ExprStr(n);
            Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1), and "
                         "its storage does not outlive this function: return copy(", what,
                         ")"));
        }
    }
    return v;
}

// Argument nodes the checker rebound by reference
// replace the originals: the receiver, then the call's own arguments.
inline void TypeCheck::WriteBackArgs(Call *c, Dot *d, vector<Node *> &argnodes) {
    d->obj = argnodes[0];
    for (size_t i = 0; i < c->args.size(); i++) c->args[i] = argnodes[i + 1];
}

// The whole of array-valued n as a slice: `n[..]`, synthesized for an
// equality between array kinds (§4.5).
inline Node *TypeCheck::WholeSlice(Node *n) {
    auto se = ast.New<SliceExpr>(n->line, n);
    se->cmpview = true;
    return se;
}

// A value meeting a destination of type `expected` (null or void: none).
// Argument position (`callsite`) additionally allows the array→slice
// coercion (§3.10), and leaves the redundant-& warning to the call's own
// resolution, where an explicit & may have picked the overload. A variable
// whose type is `inferred` from the value is no destination type either, but
// binds a reference to a non-fixed value rather than copying the pointee
// (§3.8, §4.1). `branchcopy`: n is a branch's value whose construct has no
// destination type, which copies it (CheckBranchCopy), and `expected` at
// most the type an earlier break gave the construct.
inline Val TypeCheck::CheckValue(Node *&n, TypeExpr *expected, bool callsite, bool branchcopy,
                                 bool inferred) {
    auto v = CheckV(n, expected);
    // A nominal default is an ordinary construction at this destination,
    // including relative fields; do not turn it into a copied call result.
    if (auto c = Is<Call>(n); c && c->builtin == B_DEFAULT &&
        v.type->kind != TY_ARRAY && Is<StructLit>(c->defaultinit))
        n = c->defaultinit;
    auto dt = expected && expected->kind != TY_VOID ? expected : DecayRef(v).type;
    if (branchcopy && UserRefOf(n) && IsPlainRef(v.type) && !KeepsRef(v, dt) &&
        ClassOf(dt) == SC_FIXED) {
        auto what = ExprStr(Is<Unary>(n)->child);
        Warn(n, cat("redundant &: the construct's value is a copy of ", what, " either way "
                    "(§4.1); a reference-typed binding binds ", what, " without it"));
    }
    Val branch;
    if (!expected || expected->kind == TY_VOID) {
        if (branchcopy) CheckBranchCopy(v, n, dt, branch);
        if (!inferred || !IsNonFixedRef(v)) v = DecayRef(v);
    } else {
        if (!callsite && expected->kind == TY_REF && UserRefOf(n))
            Warn(n, cat("redundant &: ", ExprStr(Is<Unary>(n)->child),
                        " binds by reference here without it (§4.1)"));
        if (BindsRef(v, expected)) n = AutoRef(n, v);
        if (branchcopy) CheckBranchCopy(v, n, expected, branch);
        else RequireCopyable(v, n, expected);
        if (!KeepsRef(v, expected)) v = DecayRef(v);
        MustFit(v, n, expected, callsite);
        NoRelRefCopy(n, expected);
    }
    v.storagebranches = branch.storagebranches;
    v.implicitcopy = branch.implicitcopy;
    n->exprtype = v.type;
    RecordVal(n, v);
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

inline void TypeCheck::MustFit(Val &v, Node *n, TypeExpr *dt, bool callsite) {
    auto vt = DecayRef(v).type;
    if (vt->kind == TY_ARRAY && vt->arr->akind == A_FIXED)
        CheckArrayCount(n, dt, ArraySize(vt->arr));
    if (!reachable) return;  // A diverging operand fits anything.
    fitfail.clear();
    fitnode = n;
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
    // An lvalue at a reference destination is the reference to it (§4.1),
    // a `const T&` where the lvalue is read-only (§9.5).
    if (BindsRef(v, dt)) {
        t = v.type = RefTo(ast.PlainOf(t), dt->line);
        v.type->cq = !v.writable;
        v.lvalue = false;
    }
    // The null literal fits any optional (plain or relative).
    if (v.isnull) {
        if (dt->kind == TY_REF && dt->ref->optional) { v.type = dt; return true; }
        fitfail = cat("null is only a value of optional types, not ", TypeStr(dt));
        return false;
    }
    // The store rule (§9.2) applies to a reference or slice, and to a value
    // holding references or slices by value (a struct with a slice field),
    // whose contents are bounded by its holder root.
    // Constness (§9.5): a read-only reference or slice lands in a slot only
    // if the slot's type says `const`, which is what a later read of the
    // slot then sees; a parameter or result takes either and is read-only
    // in that instantiation.
    if (IsRefOrSlice(dt) && IsRefOrSlice(t) && !v.writable && !dt->cq && constslot) {
        fitfail = cat("storing a read-only ", dt->kind == TY_SLICE ? "slice" : "reference",
                      " of type ", TypeStr(t),
                      t->cq ? "" : " (read-only in this instantiation)",
                      " in a slot of type ", TypeStr(dt), " (§9.5); declare the slot const");
        return false;
    }
    auto holder = !IsRefOrSlice(dt) && !IsRefOrSlice(t) && HoldsPlainRef(dt);
    // Argument slots pass no destination (parameters die before their
    // arguments' roots); an element or field being constructed does.
    if (((IsRefOrSlice(dt) && IsRefOrSlice(t)) || holder) && !curdst.roots.None()) {
        const Roots &roots = holder ? ContentsOf(v) : v.AsRoots();
        if (roots.Has(cycleroot)) {
            fitfail = NeverStoredError(cycleroot);
            return false;
        }
        // An inexact destination root only bounds the storage the slot is
        // in: that may be any storage there or further out that can hold
        // what the destination's path reached (Dest::reached), which holds
        // the slot by value, so its owner holds one too (ShrinkTargets). What
        // can hold that can hold the slot, so of the storage the slot's type
        // admits, this leaves out only what cannot be its owner. Each place
        // the value may point must outlive each (§9.2).
        auto reached = curdst.reached ? curdst.reached : dt;
        auto dsts = ShrinkTargets(curdst.roots, reached);
        for (auto &a : roots.alts) {
            for (auto &d : dsts) {
                if (Depth(a.root) <= Depth(d.root)) continue;
                fitfail = cat("storing a reference rooted at ",
                              a.root ? a.root->name : string_view("static data"),
                              ", which does not outlive the destination");
                if (!curdst.roots.Exact())
                    Append(fitfail, ": it is reached through a reference that may point into ",
                           TargetStr(d));
                Append(fitfail, " (§9.2)");
                return false;
            }
        }
        // Binding a global reference or slice variable stores into a global
        // (§5.2).
        if (!curdst.varbind || curdst.roots.None() || curdst.roots.Root()->isglobal) {
            if (auto gs = StoredIntoGrowShrink(v, roots, t, holder)) {
                fitfail = NeverStoredError(gs, MayPointWording(roots, gs));
                return false;
            }
        }
        // Rebinding one of this activation's own variables is not a store
        // that could outlive it: what the variable is bound to came from
        // an activation that outlives this one, as the first binding did.
        auto spec = CurRealFrame().spec;
        auto ownvar = curdst.varbind && curdst.roots.Exact() &&
                      curdst.roots.Root()->ownerspec == spec;
        if (!CycleStorable(roots) && spec && !ownvar) {
            // A threaded parameter class may be stored while it stays
            // threaded, and only where every activation's store lands in
            // the same storage: a parameter's class the slot may be in must
            // stay threaded too. From here on the store relies on both.
            CycleStore s { spec, fitnode ? fitnode->line : Line {} };
            for (auto &a : roots.alts) {
                if (!s.refused && !CycleStorable(a.root) && !ThreadStorable(a.root)) {
                    s.refused = true;
                    s.refusedby = a.root;
                }
                s.relies.push_back(a.root);
            }
            for (auto &d : dsts) {
                if (!s.refused && !ThreadedChain(d.root)) {
                    s.refused = true;
                    s.refusedby = d.root;
                }
                s.relies.push_back(d.root);
            }
            if (spec->incycle || spec->sf->isrec) {
                if (s.refused) {
                    fitfail = CycleStoreError(s.refusedby);
                    return false;
                }
                for (auto r : s.relies) RelyOnThread(r, s.at);
            } else {
                // A call back into a cycle may yet show this function to be
                // in one, and the store with it (JoinCycle).
                cyclestores.push_back(std::move(s));
            }
        }
        // Each storage the slot may be in holds the value from here on; a
        // reference or slice variable itself holds it as its binding.
        if (!curdst.varbind) {
            for (auto &d : dsts) {
                if (holder) {
                    // A literal's fields were each recorded as they were
                    // stored; a whole-value event for it would only be a
                    // looser copy.
                    if (!Is<StructLit>(fitnode) && !Is<ArrayLit>(fitnode))
                        RecordStore(d.root, roots, v.byteview, nullptr, v.holderfrom, reached,
                                    d.bound);
                } else {
                    RecordStore(d.root, roots, v.byteview, PointeeOf(t), nullptr, reached,
                                d.bound);
                }
            }
        }
    }
    if (TypeEq(t, dt)) { v.type = dt; return true; }
    switch (dt->kind) {
        case TY_INT:
            if (t->kind != TY_INT) return false;
            // A literal parameter adapts to any integer type; whether the
            // literal fits is each call site's question (§7.7).
            if (v.unsized) {
                RecordLitAdapt(v, dt, fitnode ? fitnode->line : Line {});
                v.type = dt;
                return true;
            }
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
            if (t->kind != TY_FLT) {
                if (v.ck == CK_INT)
                    fitfail = cat("an integer literal where ", TypeStr(dt),
                                  " is expected: write ", ConstStr(v), ".0");
                else if (v.unsized)
                    fitfail = cat("an integer literal argument where ", TypeStr(dt),
                                  " is expected: pass a float literal");
                return false;
            }
            // Literals adapt to f32; f32 widens to f64.
            if (IsF32(dt)) { if (v.ck != CK_FLT && !v.unsized) return false; }
            else if (!IsF32(t)) return false;
            if (v.unsized) RecordLitAdapt(v, dt, fitnode ? fitnode->line : Line {});
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
            // The same slice type but for constness: adding `const` is
            // implicit, and dropping it was rejected above for a slot and
            // is the read-only instantiation everywhere else (§9.5).
            if (t->kind == TY_SLICE && TypeEq(t->sub, dt->sub)) {
                v.type = dt;
                return true;
            }
            // Whole-array argument to a slice parameter, call sites only;
            // through a reference the pointee array is sliced in place.
            auto at = t;
            if (IsPlainRef(t) && t->ref->sub->kind == TY_ARRAY) at = t->ref->sub;
            if (!callsite || at->kind != TY_ARRAY) return false;
            if (!TypeEq(at->arr->sub, dt->sub)) return false;
            if (at != t) v.ClearSlotRead();   // As DerefLValue.
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
                if (t->ref->optional && dt->ref->optional && v.Exact() && !v.Root()) {
                    v.type = dt;
                    return true;
                }
                // A reference that points nowhere yet (RefProvOf), or a
                // location reached through one, is read again once it does.
                if (v.None() || (!dt->ref->pool && curdst.unknown)) {
                    v.type = dt;
                    return true;
                }
                // The target must be the *same* array, so a root that only
                // bounds the pointee's lifetime will not do (§9.5).
                auto want = dt->ref->pool ? dt->ref->pool : curdst.roots.Root();
                auto have = dt->ref->pool ? PoolOf(v.Root()) : v.Root();
                if (!want || (!dt->ref->pool && !curdst.roots.Exact()) || !v.Exact() ||
                    have != want) {
                    auto why = v.Exact() ? string() : ReadBackWhy(v);
                    auto vroot = v.Root() ? v.Root()->name : string_view("static data");
                    fitfail = cat(dt->ref->pool
                                      ? cat("a relative reference in ", want->name,
                                            " must point into ", want->name, " (§3.9); ")
                                      : string("a relative reference must point within the "
                                               "same root as its location (§3.9); "),
                                  !why.empty() ? why
                                  : !v.Exact()
                                      ? cat("this reference's root is not known exactly, "
                                            "only that it outlives ", vroot)
                                  : cat("this reference is rooted at ", vroot));
                    return false;
                }
                v.type = dt;
                return true;
            }
            // T& widens to T?.
            if (dt->ref->optional && !t->ref->optional) { v.type = dt; return true; }
            // The same reference type but for constness (as for slices).
            if (dt->ref->optional == t->ref->optional) { v.type = dt; return true; }
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

inline string TypeCheck::ConstStr(const Val &v) {
    return v.uns ? cat((uint64_t)v.ival) : cat(v.ival);
}

// Conditions: bool, or an optional (§3.8 truthiness + narrowing). Plain
// references decay (a bool& condition reads its pointee); an already
// narrowed optional stays a valid (trivially true) test.
inline Val TypeCheck::CheckCond(Node *n) {
    FlagScope rs(inreturn, false);
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
    if (TopConstEq(a, b)) return a->cq ? a : b;   // Read-only in one branch: in both.
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
    // A resizable value has a header of its own only as a whole variable or
    // as a frame object's tail (C.2).
    if (!lv.var && !lv.fotail && lv.type->kind != TY_REF &&
        ClassOf(lv.type) == SC_RESIZABLE)
        NoResizableRef(x);
    if (lv.type->kind == TY_REF) {
        // Out of a container, the stored reference is a read-back (§9.5).
        if (!lv.var) return ContainerRead(lv);
        Val v;
        v.type = LoadType(lv.type);  // Relative refs load as plain (§3.9).
        v.SetProv(RefProvOf(lv.var));
        return v;
    }
    Val v;
    v.type = RefTo(ast.PlainOf(lv.type), x->line);
    v.SetProv(lv);
    v.writable = lv.writable && !lv.isvarint;
    v.type->cq = !v.writable;   // `&x` of a const value is a `const T&` (§9.5).
    return v;
}

// Folds a constant binary op at the width and signedness of out.type
// (FoldIntOp, ast.h, which the optimizer folds with too). Operand values fit
// out.type (the unify rules ensured it). A zero divisor aborts at run time,
// which a constant expression need not wait for.
inline void TypeCheck::FoldInt(TType op, Val &l, Val &r, Val &out, Node *at) {
    if (l.ck != CK_INT || r.ck != CK_INT) return;
    auto s = out.type->intstorage;
    if (s == IS_VARINT) return;
    if ((op == T_DIV || op == T_MOD) && !r.ival) Error(at, "constant division by zero");
    int64_t res;
    if (!FoldIntOp(op, l.ival, r.ival, s, res)) return;
    out.ck = CK_INT;
    out.ival = res;
    out.uns = s == IS_U64 && res < 0;
}

// The operand/result type of a binary numeric operator: equal types
// stand; a constant adapts to the other operand's type; otherwise the
// operand that implicitly widens into the other picks the wider type
// (§6.1). Returns null for non-numeric or int/float-mixed pairs.
inline TypeExpr *TypeCheck::UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt,
                                         TypeExpr *rt, bool cmp) {
    if (IsIntT(lt) && IsIntT(rt)) {
        if (TypeEq(lt, rt)) return lt;
        // A literal parameter adapts to a typed operand, as a constant
        // does; meeting a constant, it stays at its own type (§7.7).
        if (lv.unsized && !rv.unsized && rv.ck == CK_NONE) {
            RecordLitAdapt(lv, rt, at->line);
            return rt;
        }
        if (rv.unsized && !lv.unsized && lv.ck == CK_NONE) {
            RecordLitAdapt(rv, lt, at->line);
            return lt;
        }
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
        if (lv.unsized && !rv.unsized && rv.ck == CK_NONE) {
            RecordLitAdapt(lv, rt, at->line);
            return rt;
        }
        if (rv.unsized && !lv.unsized && lv.ck == CK_NONE) {
            RecordLitAdapt(rv, lt, at->line);
            return lt;
        }
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

// Array extents and fill counts obey the same integer types as expressions
// (§3.3, §6.1–6.2). Keep known values separate from literal adaptability:
// a named u8 constant is known here, but N + 1 still computes at u8 width.
// This runs even while type declarations are validated, before globals have
// been checked, so resolve their initializers without evaluating runtime code
// or attaching caller-local bindings to the shared expression nodes.
inline bool TypeCheck::ConstIntValue(Node *n, Val &v, bool &literal,
                                     set<VarDecl *> &visiting) {
    if (auto i = Is<IntLit>(n)) {
        v.type = ast.inttypes[i->uns ? IS_U64 : IS_I64];
        v.ck = CK_INT;
        v.ival = i->val;
        v.uns = i->uns;
        literal = true;
        return true;
    }
    if (auto id = Is<Ident>(n)) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (!g || g->isvar || g->inits.size() != 1) return false;
        if (!visiting.insert(g).second)
            Error(n, cat("cycle in constant initializer: ", id->name));
        auto ok = ConstIntValue(g->inits[0], v, literal, visiting);
        visiting.erase(g);
        if (!ok) return false;
        if (g->type) {
            auto t = g->type;
            if (!IsIntT(t)) return false;
            if (!FitsIntStorage(v.ival, v.uns, t->intstorage))
                Error(n, cat("constant ", ConstStr(v), " does not fit ", TypeStr(t)));
            v.type = ast.PlainOf(t);
            v.uns = t->intstorage == IS_U64 && v.ival < 0;
        }
        literal = false;
        return true;
    }
    if (auto u = Is<Unary>(n)) {
        if (u->op != T_MINUS && u->op != T_BITNOT) return false;
        if (!ConstIntValue(u->child, v, literal, visiting)) return false;
        auto s = v.type->intstorage;
        if (u->op == T_MINUS) {
            if (v.uns && (!literal || v.ival != INT64_MIN))
                Error(n, "negated constant too large for i64");
            if (!literal && IsUnsigned(s))
                Error(n, cat("cannot negate a value of unsigned type ", TypeStr(v.type)));
            if (!v.uns && v.ival == INT64_MIN)
                Error(n, "signed overflow in constant expression");
            v.ival = (int64_t)(0u - (uint64_t)v.ival);
            if (literal) v.type = ast.inttypes[IS_I64];
            v.uns = false;
        } else {
            v.ival = ~v.ival;
            auto bits = IntBits(s);
            if (IsUnsigned(s) && bits < 64)
                v.ival = (int64_t)((uint64_t)v.ival & ((1ull << bits) - 1));
            v.uns = s == IS_U64 && v.ival < 0;
        }
        if (!FitsIntStorage(v.ival, v.uns, v.type->intstorage))
            Error(n, "signed overflow in constant expression");
        return true;
    }
    if (auto b = Is<Binary>(n)) {
        switch (b->op) {
            case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD:
            case T_BITAND: case T_BITOR: case T_XOR: case T_SHL: case T_SHR: break;
            default: return false;
        }
        Val l, r;
        bool llit, rlit;
        if (!ConstIntValue(b->left, l, llit, visiting) ||
            !ConstIntValue(b->right, r, rlit, visiting)) return false;
        if (b->op == T_SHL || b->op == T_SHR) {
            v.type = l.type;
        } else {
            auto lf = l, rf = r;
            if (!llit) lf.ck = CK_NONE;
            if (!rlit) rf.ck = CK_NONE;
            v.type = UnifyNumeric(n, b->op, lf, rf, l.type, r.type);
        }
        if (b->op == T_DIV && !IsUnsigned(v.type->intstorage) &&
            l.ival == INT64_MIN && r.ival == -1)
            Error(n, "constant division overflow");
        FoldInt(b->op, l, r, v, n);
        if (v.ck != CK_INT) Error(n, "signed overflow in constant expression");
        literal = llit && rlit;
        return true;
    }
    return false;
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
    auto found = en->FindVariant(d->name);
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
    t->var->name = v->name;
    return t;
}

// ------------------------------------------------------------------
// Struct and variant literals (§4.2). The per-node entry is
// StructLit::Check in typecheck_nodes.h.

// `selft` is the type of the value this literal constructs (the enum type
// for a variant literal in fixed enum mode), which is what `self` names.
inline TypeCheck::LitDeep TypeCheck::CheckInits(StructLit *sl, vector<Field> &fields,
                                               vector<TypeExpr *> &ftypes,
                                               string_view what, TypeExpr *selft) {
    LitDeep deep;
    auto named = !sl->inits.empty() && !sl->inits[0].name.empty();
    vector<bool> got(fields.size(), false);
    auto pos = 0;
    for (auto &fi : sl->inits) {
        auto idx = -1;
        if (named) {
            for (auto i = 0; i < (int)fields.size(); i++)
                if (!fields[i].ispad && fields[i].name == fi.name) { idx = i; break; }
            if (idx < 0) Error(fi.val, cat(what, " has no field ", fi.name));
            if (got[idx]) Error(fi.val, cat("duplicate initializer for field ", fi.name));
            // Declaration order is required (§4.2): values construct
            // front-to-back, so out-of-order names would obfuscate either
            // evaluation order or cost.
            for (auto i = idx + 1; i < (int)fields.size(); i++)
                if (got[i])
                    Error(fi.val, cat("field initializers must follow declaration "
                                      "order: ", fi.name, " comes before ",
                                      fields[i].name));
        } else {
            while (pos < (int)fields.size() && fields[pos].ispad) pos++;
            if (pos >= (int)fields.size())
                Error(fi.val, cat("too many initializers for ", what));
            idx = pos++;
        }
        got[idx] = true;
        sl->fieldindices.push_back(idx);
    }
    vector<FieldInit> ordered(fields.size());
    for (size_t i = 0; i < sl->inits.size(); i++) ordered[sl->fieldindices[i]] = sl->inits[i];
    sl->inits.clear();
    sl->fieldindices.clear();
    for (auto i = 0; i < (int)fields.size(); i++) {
        if (fields[i].ispad) continue;
        auto fi = ordered[i];
        fi.name = fields[i].name;
        if (!fi.val) {
            if (fields[i].defaultval) {
                fi.val = fields[i].defaultval->Clone(ast);
                fi.fromdefault = true;
            } else if (IsOptional(ftypes[i])) continue;
            else if (sl->defaultall) {
                fi.val = DefaultCall(ftypes[i], sl->line);
            }
            else Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                               " (it has no default)"));
        }
        if (Is<SelfRef>(fi.val)) CheckSelfInit(fi.val, ftypes[i], selft);
        else {
            SlotScope ss(*this, true);
            auto fv = fi.fromdefault ? CheckDefaultInit(fi.val, ftypes[i], selft)
                                     : CheckValue(fi.val, ftypes[i]);
            NoteLitElem(deep, fi.val, fv, ftypes[i]);
        }
        sl->inits.push_back(fi);
        sl->fieldindices.push_back(i);
    }
    return deep;
}

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
    if (ft->ref->pool && (!curdst.roots.Exact() || PoolOf(curdst.roots.Root()) != ft->ref->pool))
        Error(n, cat("self initializes ", TypeStr(ft), " only in a literal being built "
                     "inside ", ft->ref->pool->name, " (a push, an append, an alloc, or an "
                     "element store), since it stores the value's own offset in it (§3.9)"));
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

inline void TypeCheck::CheckStmts(Block *b) {
    for (size_t i = 0; i < b->stmts.size(); i++) {
        blockpos.back().idx = i;
        CheckStmt(b->stmts[i]);
    }
    blockpos.back().idx = b->stmts.size();
}

// Whether the code under n names `name`: a variable use, or a call of a
// nested function whose body does. Syntactic, so a shadowing declaration
// counts too, which only errs on the safe side.
inline bool TypeCheck::MentionsName(Node *n, string_view name, set<SFunction *> &seen) {
    if (!n) return false;
    if (auto id = Is<Ident>(n); id && id->name == name) return true;
    if (auto fd = Is<FnDecl>(n)) {
        if (fd->sf->body && seen.insert(fd->sf).second &&
            MentionsName(fd->sf->body, name, seen))
            return true;
    }
    if (auto c = Is<Call>(n)) {
        string_view callee;
        if (auto id = Is<Ident>(c->callee)) callee = id->name;
        else if (auto d = Is<Dot>(c->callee)) callee = d->name;
        if (!callee.empty()) {
            for (auto &[si, sf] : localfns)
                if (sf->name == callee && sf->body && seen.insert(sf).second &&
                    MentionsName(sf->body, name, seen))
                    return true;
        }
    }
    auto hit = false;
    n->Children([&](Node *ch) { hit = hit || MentionsName(ch, name, seen); });
    return hit;
}

// Whether variable v can be read again after the shrink being checked
// (§5.1): in the rest of its statement, later in an open block at or
// inside v's scope, or anywhere in a loop that contains this point and
// that v was declared outside of, whose next iteration runs the earlier
// part of the body again.
inline bool TypeCheck::UsedAfter(VarDef *v) {
    set<SFunction *> seen;
    auto later = false;
    LaterOperands([&](Node *n) { later = later || MentionsName(n, v->name, seen); });
    if (later) return true;
    for (auto i = 0; i < (int)scopes.size(); i++) {
        if (scopes[i].kind != SK_LOOP) continue;
        // A `for` binding is rebound by the loop itself at every iteration.
        if (auto fl = Is<ForLoop>(scopes[i].node); fl && (fl->vdef == v || fl->idxdef == v))
            return true;
        if (Depth(v) <= i && scopes[i].node && MentionsName(scopes[i].node, v->name, seen))
            return true;
    }
    for (auto &bp : blockpos) {
        if (bp.scopeidx < Depth(v) - 1) continue;
        for (auto i = bp.idx + 1; i < bp.block->stmts.size(); i++)
            if (MentionsName(bp.block->stmts[i], v->name, seen)) return true;
        if (bp.idx < bp.block->stmts.size() && bp.block->tail &&
            MentionsName(bp.block->tail, v->name, seen))
            return true;
    }
    return false;
}

inline void TypeCheck::CheckStmt(Node *n) {
    // A statement inside a returned value's block is no part of that value:
    // the function's locals outlive it.
    FlagScope rs(inreturn, false);
    NodeScope ns(*this, n);
    if (auto vd = Is<VarDecl>(n)) { CheckVarDecl(vd, false); return; }
    if (auto a = Is<Assign>(n)) { CheckAssign(a); return; }
    if (auto x = Is<IncDec>(n)) { CheckIncDec(x); return; }
    if (auto fd = Is<FnDecl>(n)) { DeclareLocalFn(fd); return; }
    CheckStmtExpr(n);
}

}  // namespace goose
