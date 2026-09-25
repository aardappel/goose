// Goose compiler — the typechecker's types (definitions of TypeCheck members,
// typecheck.h): struct and enum instantiation with the size classes and
// placement rules (§1.1, §3.4), type validation, the pending `var x = []`
// array (§4.2), the small type views and conversions, pool-relative
// references (§3.9), and the read-back roots of §9.5.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Struct/enum instantiation, size classes (§1.1), and placement rules
// (§3.4). Field types are substituted with the instance's own bindings
// only (bindonly), so a stray name in a declaration errors cleanly.

inline TypeExpr *TypeCheck::LookupBindingOuter(string_view name) {
    if (extrabindings)
        for (auto &[n, t] : *extrabindings) if (n == name) return t;
    if (ownexclude)
        for (auto &g : *ownexclude) if (g.name == name) return nullptr;
    if (bindonly || frames.empty()) return nullptr;
    return LookupBinding(name);
}

inline void TypeCheck::BindGenerics(vector<GenericParam> &generics, vector<TypeExpr *> &args,
                                    string_view what, string_view name, Line l,
                                    vector<pair<string_view, TypeExpr *>> &out) {
    if (args.size() != generics.size())
        Error(l, cat(what, " ", name, " takes ", (int64_t)generics.size(),
                     " type argument(s), ", (int64_t)args.size(), " given"));
    for (size_t i = 0; i < generics.size(); i++)
        out.push_back({ generics[i].name, args[i] });
}

inline StructInst *TypeCheck::GetStructInst(TypeExpr *t) {
    auto st = t->struc->st;
    if (t->struc->inst) return t->struc->inst;
    for (auto inst : st->insts)
        if (TypeArgsEq(inst->args, t->struc->args)) return t->struc->inst = inst;
    auto inst = ast.NewStructInst();
    inst->st = st;
    inst->args = t->struc->args;
    st->insts.push_back(inst);
    t->struc->inst = inst;
    vector<pair<string_view, TypeExpr *>> bindings;
    BindGenerics(st->generics, inst->args, "struct", st->name, t->line, bindings);
    WithBindings(bindings, [&]() {
        for (auto &f : st->fields)
            inst->ftypes.push_back(f.ispad ? nullptr : Subst(f.type));
    });
    // Placement (§3.4): a resizable field only as the tail, making the
    // struct itself resizable; any variable part makes it variable.
    auto lastreal = LastRealField(st->fields);
    for (auto i = 0; i < (int)st->fields.size(); i++) {
        if (st->fields[i].ispad) continue;
        auto ft = inst->ftypes[i];
        ValidateType(ft, st->line, VT_FIELD);
        auto c = ClassOf(ft);
        if (c == SC_RESIZABLE) {
            if (i != lastreal)
                Error(st->line, cat("resizable field ", st->fields[i].name, " of struct ",
                                    st->name, " must be the final field"));
            inst->sclass = SC_RESIZABLE;
        } else if (c == SC_VARIABLE && inst->sclass == SC_FIXED) {
            inst->sclass = SC_VARIABLE;
        }
        inst->flat = inst->flat && IsFlat(ft);
    }
    if (inst->sclass == SC_RESIZABLE) {
        auto fo = true;
        for (auto i = 0; i < (int)st->fields.size(); i++) {
            if (st->fields[i].ispad) continue;
            auto ft = inst->ftypes[i];
            if (i == lastreal)
                fo &= ft->kind == TY_ARRAY ||
                      (ft->kind == TY_STRUCT && GetStructInst(ft)->frameobj);
            else
                fo &= ClassOf(ft) == SC_FIXED && !HasRelRefT(ft);
        }
        inst->frameobj = fo;
    }
    inst->validated = true;
    return inst;
}

inline EnumInst *TypeCheck::GetEnumInst(TypeExpr *t) {
    auto en = t->enu->en;
    if (t->enu->inst) return t->enu->inst;
    for (auto inst : en->insts)
        if (TypeArgsEq(inst->args, t->enu->args)) return t->enu->inst = inst;
    auto inst = ast.NewEnumInst();
    inst->en = en;
    inst->args = t->enu->args;
    en->insts.push_back(inst);
    t->enu->inst = inst;
    vector<pair<string_view, TypeExpr *>> bindings;
    BindGenerics(en->generics, inst->args, "enum", en->name, t->line, bindings);
    WithBindings(bindings, [&]() {
        for (auto &v : en->variants) {
            inst->vftypes.emplace_back();
            for (auto &f : v.fields)
                inst->vftypes.back().push_back(f.ispad ? nullptr : Subst(f.type));
        }
    });
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
        auto &v = en->variants[vi];
        auto lastreal = LastRealField(v.fields);
        for (auto i = 0; i < (int)v.fields.size(); i++) {
            if (v.fields[i].ispad) continue;
            auto ft = inst->vftypes[vi][i];
            ValidateType(ft, en->line, VT_FIELD);
            auto c = ClassOf(ft);
            if (c == SC_RESIZABLE) {
                if (i != lastreal)
                    Error(en->line, cat("resizable field ", v.fields[i].name, " of variant ",
                                        en->name, ".", v.name, " must be the final field"));
                inst->varclass = SC_RESIZABLE;
            }
            if (c != SC_FIXED) inst->allfixed = false;
            inst->flat = inst->flat && IsFlat(ft);
        }
    }
    inst->validated = true;
    return inst;
}

// Defaults are checked at their execution sites, after the surrounding
// globals have initialized, with the actual destination and live values.
inline Val TypeCheck::CheckDefaultInit(Node *&n, TypeExpr *ft, TypeExpr *owner) {
    auto t = owner->kind == TY_VARIANT ? owner->var->adt : owner;
    auto env = ast.NewFunValEnv();
    if (t->kind == TY_STRUCT)
        BindGenerics(t->struc->st->generics, t->struc->args, "struct", t->struc->st->name,
                     n->line, env->bindings);
    else
        BindGenerics(t->enu->en->generics, t->enu->args, "enum", t->enu->en->name,
                     n->line, env->bindings);
    Frame f;
    f.spec = CurRealFrame().spec;
    f.lexspec = env;
    f.isfunval = true;  // Effects belong to the caller, lexical lookup does not.
    f.isdefault = true;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    frames.push_back(f);
    auto v = CheckValue(n, ft);
    frames.pop_back();
    return v;
}

inline Call *TypeCheck::DefaultCall(TypeExpr *t, Line line) {
    auto c = ast.New<Call>(line, ast.New<Ident>(line, "::default"));
    c->tyargs.push_back(t);
    return c;
}

inline vector<FieldRun> TypeCheck::FieldRuns(TypeExpr *t) {
    vector<FieldRun> runs;
    switch (t->kind) {
        case TY_STRUCT: runs.push_back(RunOf(GetStructInst(t))); break;
        case TY_ENUM: AllRunsOf(GetEnumInst(t), runs); break;
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            runs.push_back(RunOf(inst, inst->en->VariantIndex(t->var->variant)));
            break;
        }
        default: break;
    }
    return runs;
}

inline SizeClass TypeCheck::ClassOf(TypeExpr *t) {
    switch (t->kind) {
        case TY_INT:  return t->intstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
        case TY_REF:
            // A varint-width relative reference is varint-encoded storage,
            // so it makes its container variable-class like any varint (§3.6).
            return t->ref->lenstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
        case TY_FLT: case TY_BOOL: case TY_SLICE: return SC_FIXED;
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            // Still being validated = the struct (transitively) contains
            // itself by value; references to self are fine (fixed class).
            if (!inst->validated)
                Error(t->line, cat("struct ", inst->st->name, " contains itself by value"));
            return inst->sclass;
        }
        case TY_ENUM: {
            if (!t->enu->varmode) return SC_FIXED;
            auto inst = GetEnumInst(t);
            if (!inst->validated)
                Error(t->line, cat("enum ", inst->en->name, " contains itself by value"));
            return inst->varclass;
        }
        case TY_ARRAY:
            switch (t->arr->akind) {
                case A_FIXED:   return SC_FIXED;
                case A_VAR:     return SC_VARIABLE;
                case A_LIMITED: return ArraySize(t->arr) >= 0 ? SC_FIXED : SC_VARIABLE;
                default:        return SC_RESIZABLE;
            }
        case TY_VARIANT: {
            auto c = SC_FIXED;
            EachField(t, [&](TypeExpr *ft) { c = std::max(c, ClassOf(ft)); });
            return c;
        }
        default: return SC_FIXED;
    }
}

// Flat (§1.1): no references, slices, or relative references at any depth.
inline bool TypeCheck::IsFlat(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: case TY_SLICE: return false;
        case TY_STRUCT: return GetStructInst(t)->flat;
        case TY_ENUM:   return GetEnumInst(t)->flat;
        case TY_ARRAY:  return IsFlat(t->arr->sub);
        case TY_VARIANT: return !AnyField(t, [&](TypeExpr *ft) { return !IsFlat(ft); });
        default: return true;
    }
}

// Whether a value of type t can hold a plain reference or a slice at any
// depth -- the only kinds that can point into an arbitrary array. A
// self-relative reference points within its own root array and an
// `in pool` one into its named global pool, so a structure linked only by
// those (a node pool) can never hold a reference into some other local.
inline bool TypeCheck::HoldsPlainRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage < 0;
        case TY_SLICE: return true;
        case TY_ARRAY: return HoldsPlainRef(t->arr->sub);
        default: return AnyField(t, [&](TypeExpr *ft) { return HoldsPlainRef(ft); });
    }
}

// Whether a value of type t is meaningful as bytes on their own
// (docs/design/serialization.md §4): plain references and slices are
// addresses, and an `in pool` offset is measured from a named global's base,
// so none of the three survives leaving the program. Self-relative
// references do, which is the whole point.
inline bool TypeCheck::ImageSafe(TypeExpr *t, string &why) {
    switch (t->kind) {
        case TY_SLICE:
            why = cat(TypeStr(t), " is a slice, which is an address");
            return false;
        case TY_REF:
            if (t->ref->lenstorage < 0) {
                why = cat(TypeStr(t), " is a plain reference, which is an address");
                return false;
            }
            if (t->ref->pool) {
                why = cat(TypeStr(t), " is measured from ", t->ref->pool->name,
                          ", not from the image");
                return false;
            }
            return true;
        case TY_ARRAY: return ImageSafe(t->arr->sub, why);
        case TY_FN:
            why = "function values are addresses";
            return false;
        default: return !AnyField(t, [&](TypeExpr *ft) { return !ImageSafe(ft, why); });
    }
}

// The extra thing a *verifier* needs on top of ImageSafe (§3 step 3): every
// self-relative reference in the image must point at an element start, so
// its pointee has to be the element type itself, or one variant of it. A
// reference into a field of an element would need every valid address of
// that type enumerated, which v1 does not do.
inline bool TypeCheck::VerifiableElem(TypeExpr *t, TypeExpr *elem, string &why) {
    switch (t->kind) {
        case TY_REF: {
            if (!ImageSafe(t, why)) return false;
            auto p = t->ref->sub;
            if (TypeEq(p, elem)) return true;
            if (p->kind == TY_VARIANT && elem->kind == TY_ENUM &&
                p->var->adt->kind == TY_ENUM && p->var->adt->enu->en == elem->enu->en &&
                TypeArgsEq(p->var->adt->enu->args, elem->enu->args))
                return true;
            why = cat(TypeStr(t), " points at ", TypeStr(p), ", not at an element of the "
                      "array or one of its variants");
            return false;
        }
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            return !AnyField(t, [&](TypeExpr *ft) { return !VerifiableElem(ft, elem, why); });
        case TY_ARRAY: return VerifiableElem(t->arr->sub, elem, why);
        default: return ImageSafe(t, why);
    }
}

// Does a fixed-size type have a default value (§4.2)? Everything does
// except a non-optional reference, which has nothing to point at, and so
// anything containing one without a declared field default.
inline bool TypeCheck::HasDefault(TypeExpr *t, string &why) {
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: case TY_SLICE: return true;
        case TY_REF:
            if (t->ref->optional) return true;
            why = cat(TypeStr(t), " is a non-optional reference");
            return false;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT: {
            auto runs = FieldRuns(t);
            if (t->kind == TY_ENUM) runs.resize(1);   // Variant 0 is the default variant.
            for (auto &run : runs) {
                for (size_t i = 0; i < run.fields->size(); i++) {
                    auto &f = (*run.fields)[i];
                    if (f.ispad || f.defaultval) continue;
                    if (!HasDefault((*run.ftypes)[i], why)) {
                        why = cat("field ", f.name, " has no declared default and ", why);
                        return false;
                    }
                }
            }
            return true;
        }
        case TY_ARRAY:
            if (t->arr->akind == A_LIMITED) return true;   // Empty.
            return HasDefault(t->arr->sub, why);
        default:
            why = cat(TypeStr(t), " has no default value");
            return false;
    }
}

// ------------------------------------------------------------------
// `var out = [];` (§4.2): a grow-only array whose element type is still
// to be learned. The placeholder element is a private void type, so the
// pending array is recognizable by kind alone; the first push, append or
// assignment into the variable overwrites it in place, which completes the
// type everywhere it was already recorded (the VarDef, every Ident
// checked so far, the literal itself), since all of them share the one
// TypeExpr object.

inline TypeExpr *TypeCheck::PendingArray(Line l) {
    auto t = ast.NewType(TY_ARRAY, l);
    t->arr = ast.NewDetail<TypeArray>();
    t->arr->sub = ast.NewType(TY_VOID, l);
    t->arr->akind = A_GROW;
    return t;
}

// Distinct from an empty array literal's `void[0]`, which is fixed-size.
inline bool TypeCheck::IsPendingArray(TypeExpr *t) {
    return t && t->kind == TY_ARRAY && t->arr->akind == A_GROW && t->arr->sub->kind == TY_VOID;
}

// The element type an argument value supplies to a pending array: a
// string literal makes it an array of owned strings (u8[]), the natural
// element to be pushing literals into; [] and null say nothing.
inline TypeExpr *TypeCheck::PendingElemFrom(const Val &av, Node *at) {
    if (av.emptyarr || av.isnull || av.type->kind == TY_VOID || av.type == fntype)
        Error(at, "cannot infer the element type of this array from this value");
    if (av.strlit) {
        auto t = ast.NewType(TY_ARRAY, at->line);
        t->arr = ast.NewDetail<TypeArray>();
        t->arr->sub = ast.inttypes[IS_U8];
        t->arr->akind = A_VAR;
        return t;
    }
    return av.type;
}

// The element type a sequence value (an array, slice or string literal
// being appended or assigned whole) supplies to a pending array.
inline TypeExpr *TypeCheck::PendingElemFromSeq(const Val &av, Node *at) {
    auto t = av.type;
    TypeExpr *elem = nullptr;
    if (t->kind == TY_ARRAY) elem = t->arr->sub;
    else if (t->kind == TY_SLICE) elem = t->sub;
    if (av.strlit) elem = ast.inttypes[IS_U8];
    if (!elem || av.emptyarr)
        Error(at, "cannot infer the element type of this array from this value");
    return elem;
}

inline void TypeCheck::CompletePending(TypeExpr *arrt, TypeExpr *elem, Line l) {
    arrt->arr->sub = elem;
    ValidateType(arrt, l, VT_LOCAL);
}

inline void TypeCheck::RequireComplete(TypeExpr *t, Line l) {
    if (IsPendingArray(t))
        Error(l, "the element type of this array is not known yet (it was declared "
                 "with `= []`): push or append into it first, or annotate the "
                 "declaration");
}

inline void TypeCheck::ValidateType(TypeExpr *t, Line l, int pos) {
    switch (t->kind) {
        case TY_GENERIC:
            Error(l, cat("unknown type: ", t->named->name));
        case TY_UNRESOLVED:
            assert(false);
            return;
        case TY_INT: {
            // varint is encoded storage: it exists only inside compound
            // types (fields, elements) and behind references (§3.6).
            auto compound = pos == VT_FIELD || pos == VT_ELEM || pos == VT_POINTEE;
            if (t->intstorage == IS_VARINT && !compound)
                Error(l, "varint is a storage type: only fields and array elements");
            return;
        }
        case TY_FLT: return;
        case TY_BOOL: return;
        case TY_VOID:
            Error(l, "expression has no value here");
        case TY_FN:
            Error(l, "function value types are compile-time only and cannot be stored");
        case TY_STRUCT: GetStructInst(t); return;
        case TY_ENUM: {
            auto inst = GetEnumInst(t);
            if (!inst->validated && !t->enu->varmode)
                Error(l, cat("enum ", t->enu->en->name, " contains itself by value"));
            if (inst->validated && !t->enu->varmode && !inst->allfixed)
                Error(l, cat("enum ", t->enu->en->name, " has non-fixed-size payloads and "
                             "can only be used in variable mode (",
                             t->enu->en->name, "..)"));
            return;
        }
        case TY_VARIANT:
            if (t->var->adt->kind != TY_ENUM)
                Error(l, cat("variant type of non-ADT type ", TypeStr(t->var->adt)));
            GetEnumInst(t->var->adt);
            return;
        case TY_ARRAY: {
            RequireComplete(t, l);
            ValidateType(t->arr->sub, l, VT_ELEM);
            auto ec = ClassOf(t->arr->sub);
            switch (t->arr->akind) {
                case A_FIXED:
                    ArraySize(t->arr);
                    if (ec != SC_FIXED)
                        Error(l, cat("fixed array elements must be fixed-size: ",
                                     TypeStr(t->arr->sub)));
                    break;
                case A_LIMITED:
                    if (t->arr->sizeexpr) ArraySize(t->arr);
                    if (ec != SC_FIXED)
                        Error(l, cat("limited array elements must be fixed-size: ",
                                     TypeStr(t->arr->sub)));
                    break;
                case A_GROWSHRINK:
                    if (ec != SC_FIXED)
                        Error(l, cat("grow-shrink array elements must be fixed-size: ",
                                     TypeStr(t->arr->sub)));
                    break;
                case A_VAR: case A_GROW:
                    if (ec == SC_RESIZABLE)
                        Error(l, cat("array elements may not be resizable: ",
                                     TypeStr(t->arr->sub)));
                    break;
            }
            return;
        }
        case TY_SLICE: ValidateType(t->sub, l, VT_ELEM); return;
        case TY_REF:
            // A pool's own type is only known once the globals are
            // checked; the driver revisits every concrete in-pool type
            // then, and this catches the ones substitution makes later.
            if (t->ref->pool && t->ref->pool->type && !HasGenerics(t->ref->sub))
                ValidatePool(t);
            ValidateType(t->ref->sub, l, VT_POINTEE);
            return;
    }
}

// ------------------------------------------------------------------
// Small type constructors and views.

inline TypeExpr *TypeCheck::SliceOf(TypeExpr *t, Line l) {
    auto s = ast.NewType(TY_SLICE, l);
    s->sub = t;
    return s;
}

// A fresh `u8[>..]`: the text str() builds and the image to_bytes() writes
// (§3.7, docs/design/serialization.md). Fresh per call, since a result type
// is the call's own.
inline TypeExpr *TypeCheck::GrowU8Array(Line l) {
    auto t = ast.NewType(TY_ARRAY, l);
    t->arr = ast.NewDetail<TypeArray>();
    t->arr->sub = ast.inttypes[IS_U8];
    t->arr->akind = A_GROW;
    return t;
}

// The value type a load from storage yields: numeric types load as
// themselves, varint decodes to i64 (§3.6), and relative references load
// as ordinary references (§3.9).
inline TypeExpr *TypeCheck::LoadType(TypeExpr *t) {
    if (t->kind == TY_INT && t->intstorage == IS_VARINT) return ast.inttypes[IS_I64];
    // A const value loads as a copy, which is plain; a const reference or
    // slice loads as itself, its qualifier being about the pointee.
    if (t->cq && !IsRefOrSlice(t)) return ast.PlainOf(t);
    if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
        auto r = ast.NewType(TY_REF, t->line);
        r->ref = ast.NewDetail<TypeRef>();
        r->ref->sub = t->ref->sub;
        r->ref->optional = t->ref->optional;
        r->cq = t->cq;
        return r;
    }
    return t;
}

// The implicit numeric widenings (§6.3): conversions that can never
// change a value — to a wider type of the same signedness, or from an
// unsigned type to any strictly wider signed type; f32 to f64.
inline bool TypeCheck::ImplicitInt(IntStorage from, IntStorage to) {
    if (from == IS_VARINT || to == IS_VARINT) return false;
    if (from == to) return true;
    if (IntBits(from) >= IntBits(to)) return false;
    return IsUnsigned(to) ? IsUnsigned(from) : true;
}

// The pointee type when v is a (non-optional) reference, else null.
inline TypeExpr *TypeCheck::DerefType(TypeExpr *t) {
    if (t->kind != TY_REF) return nullptr;
    if (t->ref->optional) return nullptr;
    return t->ref->sub;
}

// Positions that accept any integer type (indices, slice bounds, sizes,
// counts): the value is used at 64 bits internally; a u64 above i64.max
// is out of range for every such use and the bounds check catches it.
inline Val TypeCheck::CheckIntAny(Node *n) {
    auto v = Operand(n);
    if (!IsIntT(v.type))
        Error(n, cat("an integer is expected here, got ", TypeStr(v.type)));
    return v;
}

// Does this type embed self-relative references at the value level (not
// behind plain references/slices)? Those are the offsets that depend on
// where the value sits, so a copy would carry the wrong ones; an
// `in pool` offset is measured from the pool and copies fine (§3.9), and
// counts only with `inpool`.
inline bool TypeCheck::HasRelRefT(TypeExpr *t, bool inpool) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0 && (inpool || !t->ref->pool);
        case TY_ARRAY: return HasRelRefT(t->arr->sub, inpool);
        default: return AnyField(t, [&](TypeExpr *ft) { return HasRelRefT(ft, inpool); });
    }
}

// A copy of a value containing self-relative references would carry
// offsets measured from the source location; only in-place construction
// (a literal) is allowed for now. TODO: track the region a relative
// reference ranges over so whole-region copies can be permitted.
inline void TypeCheck::NoRelRefCopy(Node *n, TypeExpr *t) {
    if (!reachable || !t) return;
    if (IsRefOrSlice(t) || !HasRelRefT(t)) return;
    if (Is<StructLit>(n) || Is<ArrayLit>(n)) return;   // Constructed in place.
    if (auto d = Is<Dot>(n); d && d->variantconst) return;   // A payload-less variant: a tag.
    Error(n, cat("copying a value of type ", TypeStr(t), ", which contains self-relative "
                 "references, is not supported; construct it in place"));
}

// ------------------------------------------------------------------
// Pool-relative references (§3.9). `T&<u32 in pool>` names a global pool
// at the declaration, so nothing has to be discovered per call site: the
// base is that global's, everywhere.

// Names resolve once, before any type is instantiated, so the pool is
// part of the type's identity from the first comparison on.
inline void TypeCheck::ResolvePools() {
    for (auto t : ast.alltypes) {
        if (t->kind != TY_REF || t->ref->poolname.empty()) continue;
        auto g = ast.LookupGlobal(t->ref->poolname, t->ref->poolns);
        if (!g || g->defs.empty())
            Error(t->line, cat("in ", t->ref->poolname,
                               ": a relative reference's pool must be a global variable; a "
                               "local or parameter pool has no name at this declaration, so "
                               "use the self-relative form ", TypeStr(t->ref->sub), "&<",
                               IntStorageName(t->ref->lenstorage), "> instead (§3.9)"));
        t->ref->pool = g->defs[0];
        poolglobals.insert(t->ref->pool);
    }
}

// The pool a reference rooted at `r` points into, or null. A global names
// itself; a synthetic parameter class names what every call site that
// reaches this specialization passed, which is part of its key.
inline VarDef *TypeCheck::PoolOf(VarDef *r) {
    if (!r) return nullptr;
    if (r->isglobal) return poolglobals.count(r) ? r : nullptr;
    return r->classpool;
}

// The pool must be storage a `T` can live in, and one whose base never
// moves: a grow-only resizable global grows by bumping its stack's top.
inline void TypeCheck::ValidatePool(TypeExpr *t) {
    auto pool = t->ref->pool;
    auto pt = pool->type;
    if (!pt || !IsArrayKind(pt, A_GROW))
        Error(t->line, cat("in ", pool->name, ": a relative reference's pool must be a "
                           "grow-only resizable global (", pool->name, ": T[>..]), not ",
                           pt ? TypeStr(pt) : string("an unresolved type"), " (§3.9)"));
    if (!pool->isvar)
        Error(t->line, cat("in ", pool->name, ": a relative reference's pool must be a "
                           "var (§3.9)"));
    if (!CanContain(pt, t->ref->sub))
        Error(t->line, cat("in ", pool->name, ": ", TypeStr(pool->type),
                           " cannot hold a value of type ", TypeStr(t->ref->sub),
                           ", so nothing in it can be pointed at (§3.9)"));
}

// ------------------------------------------------------------------
// Read-back roots (§9.5): what a reference or slice loaded out of a
// container points into.
//
// The container names a scope the pointee outlives, not the storage that
// owns it, so the checker re-derives the owner from the one thing it does
// know about that scope: which variables in it can hold the pointee type
// by value. A variable that only holds *references* to it cannot be its
// owner. Where exactly one such candidate exists the read-back is that
// variable, and rules that need identity (a relative-reference store,
// §3.9) may use it; otherwise the candidates only bound the lifetime.

// Can a value of type `t` contain an `of` by value anywhere inside it? A
// reference or slice field ends the search: what is behind one belongs to
// its own root. A slice's pointee is its element type as stored, which for a
// relative reference is not the plain reference a load yields, so the stored
// type matches too.
inline bool TypeCheck::CanContain(TypeExpr *t, TypeExpr *of) {
    if (!t) return false;
    if (TypeEq(t, of) || TypeEq(LoadType(t), of)) return true;
    if (t->kind == TY_ARRAY) return CanContain(t->arr->sub, of);
    return AnyField(t, [&](TypeExpr *ft) { return CanContain(ft, of); });
}

// The pointee type a reference or slice type reaches: for a slice, its
// elements (a candidate must hold a run of those).
inline TypeExpr *TypeCheck::PointeeOf(TypeExpr *t) {
    if (t->kind == TY_REF) return LoadType(t->ref->sub);
    if (t->kind == TY_SLICE) return t->sub;
    return nullptr;
}

// Every variable in scope in the body being checked and in its lexical
// parents at the call (§7.5): all LookupVar reaches that is still in scope,
// and for a nested function also what its declaration does not see, since a
// reference handed to the body may point there (a later nested function's
// result, a function value written at the call). Globals are enumerated
// separately.
inline void TypeCheck::VisibleVars(const function<void(VarDef *)> &f) {
    for (auto fi = (int)frames.size() - 1; fi >= 0;) {
        auto &fr = frames[fi];
        auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                  : frames[fi + 1].varbase;
        for (auto i = limit - 1; i >= fr.varbase; i--) f(vars[i]);
        fi = fr.isdefault ? fi - 1 : fr.lexframe;
    }
}

// A string literal is a run of u8s, so static data owns anything a
// literal could supply.
inline bool TypeCheck::StaticCanContain(TypeExpr *of) {
    return of->kind == TY_INT && of->intstorage == IS_U8;
}

// The types of the storage a value of type t leads to through the plain
// references and slices it holds, at any remove, each once.
inline void TypeCheck::ReachedThroughRefs(TypeExpr *t, vector<TypeExpr *> &out) {
    vector<TypeExpr *> work;
    RefPointees(t, work);
    while (!work.empty()) {
        auto p = work.back();
        work.pop_back();
        auto again = false;
        for (auto s : out) again = again || TypeEq(s, p);
        if (again) continue;
        out.push_back(p);
        RefPointees(p, work);
    }
}

// Whether storage that can hold an `of` is among those.
inline bool TypeCheck::ReachesThroughRefs(TypeExpr *t, TypeExpr *of) {
    vector<TypeExpr *> reached;
    ReachedThroughRefs(t, reached);
    for (auto p : reached)
        if (CanContain(p, of)) return true;
    return false;
}

// The candidates for a pointee of type `of`, each an alternative of where it
// may point (§9.5): every global whose own storage can hold one, exactly;
// static data where a literal could supply one; and, unless `globalsonly`,
// every named local at scope depth `d` or shallower that can hold one,
// exactly, and the roots of the references and slices in scope whose
// pointees can (a parameter's caller-side storage is reachable only through
// it), as those references have them. The references a parameter holds by
// value or points at lead on into more of the caller's storage, which this
// function cannot enumerate: everything stored there outlives the
// parameter's root, so that root stands in for it, as a bound.
inline Roots TypeCheck::RootCandidates(TypeExpr *of, int d, bool globalsonly, bool writable) {
    Roots out;
    auto beyond = [&](VarDef *v, TypeExpr *t, int rd) {
        if (!v->isparam || !v->refrootknown || !ReachesThroughRefs(t, of)) return;
        for (auto &a : v->ref.alts)
            if (Depth(a.root) <= rd) out.Add({ a.root, false });
    };
    auto consider = [&](VarDef *v, int rd) {
        if (!v->type) return;
        if (IsRefOrSlice(v->type)) {
            if (!v->refrootknown) return;   // No commitment yet; nothing stored from it.
            auto pt = PointeeOf(v->type);
            if (!pt) return;
            if (CanContain(pt, of))
                for (auto &a : v->ref.alts)
                    if (Depth(a.root) <= rd) out.Add({ a.root, a.exact, a.from });
            beyond(v, pt, rd);
        } else {
            if (Depth(v) <= rd && CanContain(v->type, of)) out.Add({ v, true });
            // A holder parameter's contents: its class root (CheckSpecBody).
            beyond(v, v->type, rd);
        }
    };
    if (!globalsonly) VisibleVars([&](VarDef *v) { if (!v->isglobal) consider(v, d); });
    for (auto g : ast.globals) for (auto gd : g->defs) consider(gd, 0);
    // A writable reference or slice is never given static data but a null or an
    // empty slice (§9.5: literals only go into const slots), which point at no
    // storage, so static data does not stand beside a real candidate for one.
    auto hasstatic = out.Has(nullptr) || StaticCanContain(of);
    out.alts.erase(std::remove_if(out.alts.begin(), out.alts.end(),
                                  [](const RootAlt &a) { return !a.root; }),
                   out.alts.end());
    if (writable && !out.None()) hasstatic = false;
    if (hasstatic) out.Add({ nullptr, true });
    return out;
}

// Whether v is a value in a temporary of its own (a literal, a call result,
// a copy, TempCopy), and if so where a reference or slice loaded out of it
// points: not into the temporary, since a literal's initializers, a
// callee's result or the copy's source supplied everything it holds, but
// where they point, its holder root (§9.2).
inline bool TypeCheck::TempContents(const Val &v, ReadBack &contents) {
    if (!IsTemp(v.Root()) || IsRefOrSlice(v.type)) return false;
    contents.roots = ContentsOf(v);
    contents.from = v.holderfrom;
    return true;
}

// Where a reference/slice of type `rt` loaded out of a container rooted at
// `container` points; a temporary's `contents` are its holder root.
inline Roots TypeCheck::ReadBackRoot(TypeExpr *rt, const Roots &container, bool byteview,
                                     const ReadBack *contents) {
    Roots out;
    // A container that points nowhere yet (Roots::unknown): neither do its
    // contents.
    if (container.Unknown()) {
        out.unknown = true;
        return out;
    }
    auto relative = rt->kind == TY_REF && rt->ref->lenstorage >= 0;
    if (contents && !relative && container.Any([&](const RootAlt &a) { return IsTemp(a.root); }))
        return contents->roots;
    // A relative reference that names a pool points into that pool, exactly,
    // wherever the container sits (§3.9).
    if (relative && rt->ref->pool) {
        out.Set(rt->ref->pool, true);
        return out;
    }
    auto of = PointeeOf(rt);
    for (auto &c : container.alts) {
        auto croot = c.root;
        // A holder whose contents point nowhere yet (Roots::unknown).
        if (croot && !croot->isglobal && croot->contents.Unknown()) {
            out.unknown = true;
            continue;
        }
        if (byteview) {
            // A byte view can point at any typed storage: the container's
            // contents where they are known, else the container as a bound.
            if (croot && !croot->isglobal && !croot->contents.None()) {
                for (auto &a : croot->contents.alts) out.Add({ a.root, false, croot });
            } else {
                out.Add({ croot, false, croot });
            }
            continue;
        }
        // A self-relative reference points within its own root array by
        // construction (§3.9), so it inherits the container's root outright.
        if (relative) {
            out.Add({ croot, c.exact });
            continue;
        }
        if (!of || !croot || IsTemp(croot)) {
            out.Add({ croot, false });
            continue;
        }
        // Case 3: the container came from a caller, or its own root is only a
        // bound -- storage this function cannot enumerate may be behind it.
        // Read out of it; its stores say what it holds.
        auto global = croot->isglobal;
        if (!global && (!c.exact || croot->ownerspec != CurRealFrame().spec)) {
            out.Add({ croot, false, croot });
            continue;
        }
        // Only globals outlive globals (§11.1), so a global container's
        // pointee is owned by a global or by static data, whatever local scope
        // is open here. A local container's was reachable from this frame and
        // had to outlive the container, so its owner is a candidate at the
        // container's depth or shallower: each is an alternative, exact where
        // it is a variable's own storage, a bound where it is a parameter's.
        auto cands = RootCandidates(of, Depth(croot), global, !rt->cq);
        if (cands.None()) {
            // Nothing can own the pointee: the container itself bounds it.
            out.Add({ croot, false, croot });
            continue;
        }
        for (auto &a : cands.alts) out.Add({ a.root, a.exact, croot });
    }
    return out;
}

// "was read out of `slots` and may point into `pool` or `spare`": the
// alternatives a read-back could not choose between, for the diagnostics of
// the rules that need one (§3.9).
inline string TypeCheck::ReadBackWhy(const Roots &r) {
    auto from = r.From();
    if (!from) return {};
    auto n = r.alts.size();
    if (n == 0) return cat("it was read out of ", from->name, ", whose contents this function cannot trace");
    string s = cat("it was read out of ", from->name, " and may point into ");
    for (size_t i = 0; i < n; i++) {
        auto &a = r.alts[i];
        if (i) s += i + 1 == n ? " or " : ", ";
        if (!a.root) { s += "static data"; continue; }
        if (!a.exact && !a.root->type && !a.root->isglobal) s += "the caller's storage behind ";
        s += a.root->name;
    }
    return s;
}

// Whether a reference or slice handed back to the array member called on is
// known to point into that very array: rooted at it exactly (§9.2). A
// parameter in a pool class points into that global pool (§3.9), so it is
// rooted there as exactly as a local one.
inline bool TypeCheck::RootedAtReceiver(const Val &rv, const Val &av) {
    if (!av.Exact() || !rv.Exact()) return false;
    auto recvroot = rv.Root(), aroot = av.Root();
    return aroot == recvroot || (recvroot && recvroot->isglobal && PoolOf(aroot) == recvroot);
}

// The same, required of what member `op` is handed.
inline void TypeCheck::CheckRootedAtReceiver(Call *c, const char *op, const Val &rv,
                                             const Val &av, const char *what,
                                             const char *sec) {
    if (RootedAtReceiver(rv, av)) return;
    if (av.None() || rv.None()) return;   // Nowhere yet (RefProvOf).
    auto why = av.Exact() ? string() : ReadBackWhy(av);
    auto root = av.Root() ? av.Root()->name : string_view("static data");
    Error(c, cat(".", op, " needs ", what, " rooted at the array itself (", sec, "); ",
                 !why.empty() ? why
                 : !av.Exact() ? cat("this one's root is not known exactly, only that it "
                                     "outlives ", root)
                 : cat("this one is rooted at ", root)));
}

}  // namespace goose
