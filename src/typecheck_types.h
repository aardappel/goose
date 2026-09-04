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
    auto lastreal = -1;
    for (auto i = 0; i < (int)st->fields.size(); i++) if (!st->fields[i].ispad) lastreal = i;
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
    CheckFieldDefaults(st->fields, inst->ftypes, inst->defaults, bindings);
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
        auto lastreal = -1;
        for (auto i = 0; i < (int)v.fields.size(); i++) if (!v.fields[i].ispad) lastreal = i;
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
    vector<pair<string_view, TypeExpr *>> b2 = bindings;
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
        inst->vdefaults.emplace_back();
        CheckFieldDefaults(en->variants[vi].fields, inst->vftypes[vi],
                           inst->vdefaults.back(), b2);
    }
    return inst;
}

// Field defaults are checked once per instance, on clones, in a pristine
// frame that sees only globals (plus the instance's generic bindings).
inline void TypeCheck::CheckFieldDefaults(vector<Field> &fields, vector<TypeExpr *> &ftypes,
                                          vector<Node *> &out,
                                          vector<pair<string_view, TypeExpr *>> &bindings) {
    auto any = false;
    for (auto &f : fields) any |= f.defaultval != nullptr;
    if (!any) {
        out.resize(fields.size(), nullptr);
        return;
    }
    auto savereach = reachable;
    DestScope ds(*this, Dest {});
    reachable = true;
    auto sp = ast.NewFnSpec();  // Bindings holder for the pseudo frame.
    sp->bindings = bindings;
    Frame f;
    f.lexspec = sp;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    frames.push_back(f);
    for (size_t i = 0; i < fields.size(); i++) {
        if (!fields[i].defaultval) { out.push_back(nullptr); continue; }
        auto clone = fields[i].defaultval->Clone(ast);
        CheckValue(clone, ftypes[i]);
        out.push_back(clone);
    }
    frames.pop_back();
    reachable = savereach;
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
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
            auto c = SC_FIXED;
            for (auto ft : inst->vftypes[vi])
                if (ft) c = std::max(c, ClassOf(ft));
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
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
            for (auto ft : inst->vftypes[vi]) if (ft && !IsFlat(ft)) return false;
            return true;
        }
        default: return true;
    }
}

inline int TypeCheck::VariantIndex(SEnum *en, SVariant *v) {
    for (size_t i = 0; i < en->variants.size(); i++)
        if (&en->variants[i] == v) return (int)i;
    assert(false);
    return 0;
}

// Does a fixed-size type have a default value (§4.2)? Everything does
// except a non-optional reference, which has nothing to point at, and so
// anything containing one without a declared field default.
inline bool TypeCheck::HasDefault(TypeExpr *t, string &why) {
    auto fields = [&](const vector<Field> &fs, const vector<TypeExpr *> &fts) {
        for (size_t i = 0; i < fs.size(); i++) {
            if (fs[i].ispad || fs[i].defaultval) continue;
            if (!HasDefault(fts[i], why)) {
                why = cat("field ", fs[i].name, " has no declared default and ", why);
                return false;
            }
        }
        return true;
    };
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: case TY_SLICE: return true;
        case TY_REF:
            if (t->ref->optional) return true;
            why = cat(TypeStr(t), " is a non-optional reference");
            return false;
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            return fields(inst->st->fields, inst->ftypes);
        }
        case TY_ENUM: {
            // Variant 0 is the default variant.
            auto inst = GetEnumInst(t);
            return fields(inst->en->variants[0].fields, inst->vftypes[0]);
        }
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(inst->en, t->var->variant);
            return fields(t->var->variant->fields, inst->vftypes[vi]);
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

inline bool TypeCheck::IsPlainRef(TypeExpr *t) {
    return t->kind == TY_REF && !t->ref->optional && t->ref->lenstorage < 0;
}

inline bool TypeCheck::IsArrayKind(TypeExpr *t, ArrayKind k) {
    return t->kind == TY_ARRAY && t->arr->akind == k;
}

// The value type a load from storage yields: numeric types load as
// themselves, varint decodes to i64 (§3.6), and relative references load
// as ordinary references (§3.9).
inline TypeExpr *TypeCheck::LoadType(TypeExpr *t) {
    if (t->kind == TY_INT && t->intstorage == IS_VARINT) return ast.inttypes[IS_I64];
    if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
        auto r = ast.NewType(TY_REF, t->line);
        r->ref = ast.NewDetail<TypeRef>();
        r->ref->sub = t->ref->sub;
        r->ref->optional = t->ref->optional;
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
// `in pool` offset is measured from the pool and copies fine (§3.9).
inline bool TypeCheck::HasRelRefT(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0 && !t->ref->pool;
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            for (size_t i = 0; i < inst->ftypes.size(); i++)
                if (inst->ftypes[i] && HasRelRefT(inst->ftypes[i])) return true;
            return false;
        }
        case TY_ENUM: {
            auto inst = GetEnumInst(t);
            for (auto &vf : inst->vftypes)
                for (auto ft : vf)
                    if (ft && HasRelRefT(ft)) return true;
            return false;
        }
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
            for (auto ft : inst->vftypes[vi]) if (ft && HasRelRefT(ft)) return true;
            return false;
        }
        case TY_ARRAY: return HasRelRefT(t->arr->sub);
        default: return false;
    }
}

// A copy of a value containing self-relative references would carry
// offsets measured from the source location; only in-place construction
// (a literal) is allowed for now. TODO: track the region a relative
// reference ranges over so whole-region copies can be permitted.
inline void TypeCheck::NoRelRefCopy(Node *n, TypeExpr *t) {
    if (!reachable || !t) return;
    if (t->kind == TY_REF || t->kind == TY_SLICE || !HasRelRefT(t)) return;
    if (Is<StructLit>(n) || Is<ArrayLit>(n)) return;   // Constructed in place.
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
        auto git = ast.globalmap.find(t->ref->poolname);
        if (git == ast.globalmap.end() || git->second->defs.empty())
            Error(t->line, cat("in ", t->ref->poolname,
                               ": a relative reference's pool must be a global variable; a "
                               "local or parameter pool has no name at this declaration, so "
                               "use the self-relative form ", TypeStr(t->ref->sub), "&<",
                               IntStorageName(t->ref->lenstorage), "> instead (§3.9)"));
        t->ref->pool = git->second->defs[0];
        poolglobals.insert(t->ref->pool);
    }
}

// The pool a reference rooted at `r` points into, or null. A global names
// itself; a synthetic parameter class names what every call site that
// reaches this specialization passed, which is part of its key.
inline VarDef *TypeCheck::PoolOf(VarDef *r) {
    r = CanonRoot(r);
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
// its own root.
inline bool TypeCheck::CanContain(TypeExpr *t, TypeExpr *of) {
    if (!t) return false;
    if (TypeEq(LoadType(t), of)) return true;
    switch (t->kind) {
        case TY_ARRAY: return CanContain(t->arr->sub, of);
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            for (auto ft : inst->ftypes) if (CanContain(ft, of)) return true;
            return false;
        }
        case TY_ENUM: {
            auto inst = GetEnumInst(t);
            for (auto &vf : inst->vftypes)
                for (auto ft : vf) if (CanContain(ft, of)) return true;
            return false;
        }
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
            for (auto ft : inst->vftypes[vi]) if (CanContain(ft, of)) return true;
            return false;
        }
        default: return false;
    }
}

// The pointee type a reference or slice type reaches: for a slice, its
// elements (a candidate must hold a run of those).
inline TypeExpr *TypeCheck::PointeeOf(TypeExpr *t) {
    if (t->kind == TY_REF) return LoadType(t->ref->sub);
    if (t->kind == TY_SLICE) return t->sub;
    return nullptr;
}

// Every variable the body being checked can name, exactly the set
// LookupVar reaches: this frame's scopes plus the lexical parent chain
// (§7.5). Globals are enumerated separately.
inline void TypeCheck::VisibleVars(const function<void(VarDef *)> &f) {
    for (auto fi = (int)frames.size() - 1; fi >= 0;) {
        auto &fr = frames[fi];
        auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                  : frames[fi + 1].varbase;
        for (auto i = limit - 1; i >= fr.varbase; i--) f(vars[i]);
        fi = fr.lexframe;
    }
}

// A string literal is a run of u8s, so static data owns anything a
// literal could supply.
inline bool TypeCheck::StaticCanContain(TypeExpr *of) {
    return of->kind == TY_INT && of->intstorage == IS_U8;
}

// The candidates for a pointee of type `of`: every global whose own
// storage can hold one, static data where a literal could supply one and,
// unless `globalsonly`, every named local at scope depth `d` or shallower
// that can hold one plus the pointee of every reference/slice in scope (a
// parameter's caller-side storage is reachable only through it).
// Deduplicated by root; the caller picks the deepest.
inline void TypeCheck::RootCandidates(TypeExpr *of, int d, bool globalsonly, vector<VarDef *> &out,
                                      bool &hasstatic) {
    hasstatic = false;
    auto add = [&](VarDef *r) {
        r = CanonRoot(r);
        if (!r) { hasstatic = true; return; }
        for (auto o : out) if (o == r) return;
        out.push_back(r);
    };
    auto consider = [&](VarDef *v, int rd) {
        if (!v->type) return;
        if (v->type->kind == TY_REF || v->type->kind == TY_SLICE) {
            if (!v->refrootknown) return;   // No commitment yet; nothing stored from it.
            auto r = CanonRoot(v->ref.root);
            if (Depth(r) > rd) return;
            auto pt = PointeeOf(v->type);
            if (pt && CanContain(pt, of)) add(r);
        } else if (Depth(v) <= rd && CanContain(v->type, of)) {
            add(v);
        }
    };
    if (!globalsonly) VisibleVars([&](VarDef *v) { if (!v->isglobal) consider(v, d); });
    for (auto g : ast.globals) for (auto gd : g->defs) consider(gd, 0);
    if (StaticCanContain(of)) hasstatic = true;
}

// The root of a reference/slice of type `rt` loaded out of a container
// whose own root is (croot, cexact).
inline TypeCheck::ReadBack TypeCheck::ReadBackRoot(TypeExpr *rt, VarDef *croot, bool cexact) {
    ReadBack rb;
    croot = CanonRoot(croot);
    rb.root = croot;
    // A relative reference points within its own root array by
    // construction (§3.9), so it inherits the container's root outright —
    // unless it named a pool, in which case the pool *is* the root, and
    // exactly, wherever the container sits.
    if (rt->kind == TY_REF && rt->ref->lenstorage >= 0) {
        if (rt->ref->pool) { rb.root = rt->ref->pool; rb.exact = true; return rb; }
        rb.exact = cexact;
        return rb;
    }
    auto of = PointeeOf(rt);
    if (!of || !croot || croot == temproot || croot == cycleroot) return rb;
    // Case 3: the container came from a caller, or its own root is only a
    // bound -- storage this function cannot enumerate may be behind it.
    auto global = croot->isglobal;
    if (!global && (!cexact || croot->ownerspec != CurRealFrame().spec)) return rb;
    // Only globals outlive globals (§11.1), so a global container's
    // pointee is owned by a global or by static data, whatever local scope
    // is open here. A local container's was reachable from this frame and
    // had to outlive the container, so its owner is a candidate at the
    // container's depth or shallower.
    vector<VarDef *> cands;
    auto hasstatic = false;
    RootCandidates(of, Depth(croot), global, cands, hasstatic);
    rb.from = croot;
    if (cands.empty()) {
        // Static data alone: null is its root, and it outlives everything.
        if (hasstatic) { rb.root = nullptr; rb.exact = true; }
        return rb;
    }
    // The deepest candidate is the conservative choice: the owner is that
    // one or one further out, so its depth bounds every possibility.
    rb.root = cands[0];
    for (auto c : cands) if (Depth(c) > Depth(rb.root)) rb.root = c;
    rb.exact = cands.size() == 1 && !hasstatic;
    return rb;
}

// "was read out of `slots` and may point into `pool` or `spare`": the
// candidates a read-back could not choose between, for the diagnostics of
// the rules that need one (§3.9).
inline string TypeCheck::ReadBackWhy(TypeExpr *rt, VarDef *from) {
    auto of = PointeeOf(rt);
    if (!from || !of) return {};
    vector<VarDef *> cands;
    auto hasstatic = false;
    RootCandidates(of, Depth(from), from->isglobal, cands, hasstatic);
    string s = cat("it was read out of ", from->name, " and may point into ");
    auto n = cands.size() + (hasstatic ? 1 : 0);
    for (size_t i = 0; i < cands.size(); i++) {
        if (i) s += i + 1 == n ? " or " : ", ";
        s += cands[i]->name;
    }
    if (hasstatic) { if (n > 1) s += " or "; s += "static data"; }
    return s;
}

// ------------------------------------------------------------------
// Lvalue paths: names, fields, elements, optionally through references.

inline TypeCheck::LVal TypeCheck::CheckLValue(Node *n) {
    if (auto id = Is<Ident>(n)) {
        auto vd = LookupVar(id->name);
        if (!vd) Error(n, cat("unknown variable: ", id->name));
        id->vdef = vd;
        LVal lv;
        lv.type = vd->narrowed ? vd->narrowed : vd->type;
        lv.var = vd;
        lv.root = vd;
        lv.rootexact = true;
        lv.writable = vd->isvar;
        lv.reusable = vd->reusable;
        n->exprtype = vd->type;
        return lv;
    }
    if (auto d = Is<Dot>(n)) {
        auto lv = LValueBase(d->obj);
        DerefLValue(lv, d->obj);
        ResolveMemberLValue(lv, d);
        n->exprtype = lv.type;
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
        lv.type = elem;
        lv.var = nullptr;
        lv.fromstorage = true;
        lv.isvarint = elem->kind == TY_INT && elem->intstorage == IS_VARINT;
        n->exprtype = lv.type;
        return lv;
    }
    Error(n, "not an assignable location");
}

}  // namespace goose
