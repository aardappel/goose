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

// A generic type must reach finitely many instantiations (§3.2), but fields
// naming its declaration again with a larger type argument each round would
// instantiate it without end, each instance needed by the one before. As for
// polymorphic recursion (§7.8), t's instance is refused where the chain of
// instantiations that led to it holds MAXNESTEDSPECS of its declaration.
inline void TypeCheck::LimitNestedInsts(TypeExpr *t) {
    auto decl = [](TypeExpr *x) -> const void * {
        return x->kind == TY_STRUCT ? (const void *)x->struc->st : x->enu->en;
    };
    vector<TypeExpr *> nested;
    for (auto c : typechain) if (decl(c) == decl(t)) nested.push_back(c);
    if ((int)nested.size() < MAXNESTEDSPECS) return;
    string s = "instantiating ";
    DumpShort(s, t);
    Append(s, " would chain more than ", MAXNESTEDSPECS, " instantiations of ",
           t->kind == TY_STRUCT ? t->struc->st->qname : t->enu->en->qname,
           ", each needed by the one before (");
    for (auto j = 0; j < 3; j++) {
        DumpShort(s, nested[j]);
        s += ", ";
    }
    s += "...): a generic type must reach a finite set of instantiations (§3.2)";
    Error(t->line, s);
}

inline StructInst *TypeCheck::GetStructInst(TypeExpr *t) {
    auto st = t->struc->st;
    if (t->struc->inst) return t->struc->inst;
    for (auto inst : st->insts)
        if (TypeArgsEq(inst->args, t->struc->args)) return t->struc->inst = inst;
    LimitNestedInsts(t);
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
    typechain.push_back(t);
    BeginTypeBuild();
    for (auto i = 0; i < (int)st->fields.size(); i++) {
        if (st->fields[i].ispad) continue;
        auto ft = inst->ftypes[i];
        ValidateType(ft, st->line, VT_FIELD);
        if (auto zs = ZeroSizeKind(ft))
            Error(st->line, cat("field ", st->fields[i].name, " of struct ", st->name,
                                " cannot be ", zs, ": ", TypeStr(ft), " (§3.4)"));
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
    typechain.pop_back();
    EndTypeBuild();
    return inst;
}

inline EnumInst *TypeCheck::GetEnumInst(TypeExpr *t) {
    auto en = t->enu->en;
    if (t->enu->inst) return t->enu->inst;
    for (auto inst : en->insts)
        if (TypeArgsEq(inst->args, t->enu->args)) return t->enu->inst = inst;
    LimitNestedInsts(t);
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
    inst->vbuilt.assign(en->variants.size(), 0);
    typechain.push_back(t);
    BeginTypeBuild();
    for (size_t vi = 0; vi < en->variants.size(); vi++) BuildVariant(inst, vi);
    inst->validated = true;
    typechain.pop_back();
    EndTypeBuild();
    // A whole assignment may replace a resizable ADT's variant (§4.4), so its
    // payloads bind by value only (§8.1), and a copy does not keep
    // self-relative references (§3.9): nothing could read those of a payload
    // that binds. A resizable payload, which does not bind at all yet
    // (CheckMatch), is left alone.
    if (inst->selfrel && inst->varclass == SC_RESIZABLE) {
        auto resizable = [&](size_t vi) {
            return AnyFieldOf({ RunOf(inst, (int)vi) },
                              [&](TypeExpr *ft) { return ClassOf(ft) == SC_RESIZABLE; });
        };
        size_t tail = 0;
        while (!resizable(tail)) tail++;
        for (size_t vi = 0; vi < en->variants.size(); vi++)
            if (!resizable(vi) &&
                AnyFieldOf({ RunOf(inst, (int)vi) }, [&](TypeExpr *ft) { return HasRelRefT(ft); }))
                Error(en->line, cat("enum ", TypeStr(ast.EnumOf(en, inst->args, false, en->line)),
                                    " is resizable (its variant ", en->variants[tail].name,
                                    "'s payload is), so a whole assignment may replace its "
                                    "variant (§4.4) and its payloads bind only by value (§8.1); "
                                    "the payload of its variant ", en->variants[vi].name,
                                    " holds self-relative references, which a copy does not "
                                    "keep (§3.9): make them plain or `in pool` references"));
    }
    return inst;
}

// A variant's payload is validated where the enum's build reaches the
// variant, or earlier where a payload of the same enum holds the variant
// type by value (ValidateType): one met again while its own payload is
// being validated holds itself.
inline void TypeCheck::BuildVariant(EnumInst *inst, size_t vi) {
    if (inst->vbuilt[vi]) return;
    inst->vbuilt[vi] = 1;
    auto en = inst->en;
    auto &v = en->variants[vi];
    auto lastreal = LastRealField(v.fields);
    for (auto i = 0; i < (int)v.fields.size(); i++) {
        if (v.fields[i].ispad) continue;
        auto ft = inst->vftypes[vi][i];
        ValidateType(ft, en->line, VT_FIELD);
        if (auto zs = ZeroSizeKind(ft))
            Error(en->line, cat("field ", v.fields[i].name, " of variant ", en->name, ".", v.name,
                                " cannot be ", zs, ": ", TypeStr(ft), " (§3.4)"));
        auto c = ClassOf(ft);
        if (c == SC_RESIZABLE) {
            if (i != lastreal)
                Error(en->line, cat("resizable field ", v.fields[i].name, " of variant ",
                                    en->name, ".", v.name, " must be the final field"));
            inst->varclass = SC_RESIZABLE;
        }
        if (c != SC_FIXED) inst->allfixed = false;
        if (HasRelRefT(ft)) inst->selfrel = true;
        inst->flat = inst->flat && IsFlat(ft);
    }
    inst->vbuilt[vi] = 2;
}

// The outermost instance validates the pointees its build left, and those
// the instances they build leave in turn, which join the end of the list:
// it still counts as building, so they wait too.
inline void TypeCheck::EndTypeBuild() {
    if (typesbuilding == 1) {
        for (size_t i = 0; i < laterpointees.size(); i++) {
            auto [t, l, chain] = laterpointees[i];
            swap(typechain, chain);
            ValidatePointee(t, l);
            swap(typechain, chain);
        }
        laterpointees.clear();
    }
    typesbuilding--;
}

inline TypeCheck::DefaultScope::DefaultScope(TypeCheck &t, FnSpec *env, Line callline) : tc(t) {
    Frame f;
    f.spec = tc.CurRealFrame().spec;
    f.lexspec = env;
    f.isfunval = true;  // Effects belong to the caller, lexical lookup does not.
    f.isdefault = true;
    f.scopebase = (int)tc.scopes.size();
    f.varbase = (int)tc.vars.size();
    f.callline = callline;
    tc.frames.push_back(f);
}

// Defaults are checked at their execution sites, after the surrounding
// globals have initialized, with the actual destination and live values:
// that of `field`, for literal sl building an `owner`.
inline Val TypeCheck::CheckDefaultInit(Node *&n, TypeExpr *ft, TypeExpr *owner,
                                       const StructLit *sl, const Field &field) {
    EachDefaultInPlace([&](Frame &fr) {
        if (fr.defaultfield == &field)
            Error(sl, cat("the default of field ", field.name, " of ",
                          LitTypeStr(fr.defaultlit, fr.defaultowner),
                          " leads to this construction of ", LitTypeStr(sl, owner),
                          ", which takes it again (§3.2)"));
    });
    auto t = owner->kind == TY_VARIANT ? owner->var->adt : owner;
    auto env = ast.NewFunValEnv();
    if (t->kind == TY_STRUCT)
        BindGenerics(t->struc->st->generics, t->struc->args, "struct", t->struc->st->name,
                     n->line, env->bindings);
    else
        BindGenerics(t->enu->en->generics, t->enu->args, "enum", t->enu->en->name,
                     n->line, env->bindings);
    DefaultScope ds(*this, env, Line {});
    auto &fr = frames.back();
    fr.defaultfield = &field;
    fr.defaultlit = sl;
    fr.defaultowner = owner;
    return CheckValue(n, ft);
}

inline Call *TypeCheck::DefaultCall(TypeExpr *t, Line line) {
    auto c = ast.New<Call>(line, ast.New<Ident>(line, "::default"));
    c->tyargs.push_back(t);
    c->implicit = true;
    return c;
}

// The default value of a type that has one (HasDefault), as the expression
// that builds it where a literal's `..` or a global's missing initializer
// asks for it: null for an optional reference; default<T>() of another
// fixed-size type; otherwise `[]`, 0 for a varint, or a literal of the
// struct, variant or first variant that fills in its own fields the same way.
inline Node *TypeCheck::DefaultValue(TypeExpr *t, Line line) {
    if (t->kind == TY_REF) return ast.New<NullLit>(line);
    if (ClassOf(t) == SC_FIXED) return DefaultCall(t, line);
    switch (t->kind) {
        case TY_ARRAY: return ast.New<ArrayLit>(line);
        case TY_INT:   return ast.New<IntLit>(line, 0);
        default: {
            auto lt = t->kind == TY_ENUM
                          ? ast.VariantTypeOf(t, &t->enu->en->variants[0], line) : t;
            auto sl = ast.New<StructLit>(line, lt);
            sl->defaultall = true;
            sl->implicit = true;
            return sl;
        }
    }
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

// Does a type have a default value (§4.2)? Everything does except a
// non-optional reference, which has nothing to point at, and so anything
// containing one without a declared field default. default<T>() gives it
// for a fixed-size T only; DefaultValue builds it for any.
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
            if (t->arr->akind != A_FIXED) return true;   // Empty.
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
    return ast.ArrayOf(ast.NewType(TY_VOID, l), A_GROW, l);
}

// Distinct from an empty array literal's `void[0]`, which is fixed-size.
inline bool TypeCheck::IsPendingArray(TypeExpr *t) {
    return t && t->kind == TY_ARRAY && t->arr->akind == A_GROW && t->arr->sub->kind == TY_VOID;
}

// An empty array literal's `void[0]`: a [] that nothing gave an element type,
// or a construct whose branches are all such literals.
inline bool TypeCheck::IsUntypedEmptyArray(const TypeExpr *t) {
    return t && t->kind == TY_ARRAY && t->arr->akind == A_FIXED && t->arr->sub->kind == TY_VOID;
}

// A [] that nothing gave an element type, taken by `use`, which gives it none
// either: only a destination's array type does, or the first push into a
// `var` it initializes (§4.2).
inline void TypeCheck::NoUntypedEmptyArray(const Val &v, Node *at, string_view use) {
    if (IsUntypedEmptyArray(v.type))
        Error(at, cat("cannot infer the element type of []: ", use, " gives it none (§4.2)"));
}

// The element type an argument value supplies to a pending array: a
// string literal makes it an array of owned strings (u8[]), the natural
// element to be pushing literals into; [] and null say nothing.
inline TypeExpr *TypeCheck::PendingElemFrom(const Val &av, Node *at) {
    if (av.emptyarr || av.isnull || av.type->kind == TY_VOID || av.type == fntype)
        Error(at, "cannot infer the element type of this array from this value");
    if (av.strlit) return ast.ArrayOf(ast.inttypes[IS_U8], A_VAR, at->line);
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
            if (inst->validated && !t->enu->varmode && inst->selfrel)
                Error(l, cat("enum ", t->enu->en->name, " has payloads holding self-relative "
                             "references, which a copy does not keep (§3.9), and a fixed-mode ",
                             t->enu->en->name, " binds its payloads only by value (§3.5): use "
                             "it in variable mode (", t->enu->en->name, "..), whose payloads "
                             "bind by reference, or make the references `in pool`"));
            return;
        }
        case TY_VARIANT: {
            if (t->var->adt->kind != TY_ENUM)
                Error(l, cat("variant type of non-ADT type ", TypeStr(t->var->adt)));
            auto inst = GetEnumInst(t->var->adt);
            if (!inst->validated) {
                auto vi = (size_t)inst->en->VariantIndex(t->var->variant);
                if (inst->vbuilt[vi] == 1)
                    Error(l, cat("variant ", inst->en->name, ".", t->var->name,
                                 " contains itself by value"));
                BuildVariant(inst, vi);
            }
            break;
        }
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
            break;
        }
        case TY_SLICE: case TY_REF:
            if (typesbuilding) laterpointees.push_back({ t, l, typechain });
            else ValidatePointee(t, l);
            return;
    }
    if (pos == VT_ELEM) NoZeroSizeElement(t, l);
}

inline void TypeCheck::ValidatePointee(TypeExpr *t, Line l) {
    if (t->kind == TY_SLICE) {
        ValidateType(t->sub, l, VT_ELEM);
        return;
    }
    // A pool's own type is only known once the globals are checked; the
    // driver revisits every concrete in-pool type then, and this catches the
    // ones substitution makes later.
    if (t->ref->pool && t->ref->pool->type && !HasGenerics(t->ref->sub)) ValidatePool(t);
    ValidateType(t->ref->sub, l, VT_POINTEE);
}

// ------------------------------------------------------------------
// Small type constructors and views.

// A fresh `u8[>..]`: the text str() builds and the image to_bytes() writes
// (§3.7, docs/design/serialization.md). Fresh per call, since a result type
// is the call's own.
inline TypeExpr *TypeCheck::GrowU8Array(Line l) {
    return ast.ArrayOf(ast.inttypes[IS_U8], A_GROW, l);
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
        auto r = ast.RefTo(t->ref->sub, t->line, t->ref->optional);
        r->cq = t->cq;
        return r;
    }
    return t;
}

// The type a value that a type argument gives type t has: a function result
// or a default<T>(). A relative reference is only ever storage, so as a
// value it is the plain reference a load of it gives (§3.9).
inline TypeExpr *TypeCheck::ValueType(TypeExpr *t) {
    return t->kind == TY_REF && t->ref->lenstorage >= 0 ? LoadType(t) : t;
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

// The variables a shrink at the point being checked may leave pointing into
// freed storage: those VisibleVars gives, and inside a function value's body
// those of the function running it, which the body cannot name but runs in
// the middle of (§7.6), with that function's lexical parents in turn.
inline void TypeCheck::ShrinkScanVars(const function<void(VarDef *)> &f) {
    vector<bool> seen(frames.size(), false);
    auto walk = [&](int fi) {
        while (fi >= 0 && !seen[fi]) {
            seen[fi] = true;
            auto &fr = frames[fi];
            auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                      : frames[fi + 1].varbase;
            for (auto i = limit - 1; i >= fr.varbase; i--) f(vars[i]);
            fi = fr.isdefault ? fi - 1 : fr.lexframe;
        }
    };
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        walk(fi);
        if (!frames[fi].isfunval) break;
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

// The types of the storage an inexact root r stands for beside its own: a
// bound names a scope, and what it bounds may lie anywhere r's references
// lead (a read-back out of a container, the contents a holder copied out
// of one, a result a callee read out of its parameter). A parameter's class
// has no type of its own; what its parameters' types lead to is recorded
// on it (VarDef::classreach).
inline void TypeCheck::BoundReach(VarDef *r, vector<TypeExpr *> &out) {
    if (!r) return;
    if (r->type) ReachedThroughRefs(r->type, out);
    else out.insert(out.end(), r->classreach.begin(), r->classreach.end());
}

// The candidates for a pointee of type `of`, each an alternative of where it
// may point (§9.5): every global whose own storage can hold one, exactly;
// static data where a literal could supply one; and, unless `globalsonly`,
// every named local at scope depth `d` or shallower that can hold one,
// exactly, and the roots of the references and slices in scope whose
// pointees can (a parameter's caller-side storage is reachable only through
// it), as those references have them. Where the pointee is the slot a
// reference names (`slots`), a slice variable's own storage holds a slice
// (§3.8): a local's once a reference to it has been made (VarDef::slotref),
// before which nothing can hold one, a global's always, since a function
// checked later may make one. The references a parameter holds by value or
// points at lead on into more of the caller's storage, which this function
// cannot enumerate: everything stored there outlives the parameter's root,
// so that root stands in for it, as a bound.
inline Roots TypeCheck::RootCandidates(TypeExpr *of, int d, bool globalsonly, bool writable,
                                       bool slots) {
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
            if (slots && SliceVarOf(v) && (v->slotref || v->isglobal) && Depth(v) <= rd &&
                CanContain(v->type, of))
                out.Add({ v, true });
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
    auto unchecked = false;
    for (auto g : ast.globals)
        for (auto gd : g->defs) {
            consider(gd, 0);
            unchecked = unchecked || (!gd->type && !(g->type && IsRefOrSlice(g->type)));
        }
    // A writable reference or slice is never given static data but a null or an
    // empty slice (§9.5: literals only go into const slots), which point at no
    // storage, so static data does not stand beside a real candidate for one.
    auto hasstatic = out.Has(nullptr) || StaticCanContain(of);
    out.alts.erase(std::remove_if(out.alts.begin(), out.alts.end(),
                                  [](const RootAlt &a) { return !a.root; }),
                   out.alts.end());
    if (writable && !out.None()) hasstatic = false;
    if (hasstatic) out.Add({ nullptr, true });
    // A function's body checked for a global initializer's call runs before
    // the later globals exist, but its check serves the calls after them
    // too, when any of them may hold one: a global whose declaration is not
    // checked yet has no type to ask, so the globals are a bound there (as a
    // global holder's contents are).
    if (unchecked && CurRealFrame().spec) out.Add({ nullptr, false });
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
// `slotread`: it was loaded out of a field, an element or a global
// (RootAlt::slotread).
inline Roots TypeCheck::ReadBackRoot(TypeExpr *rt, const Roots &container, bool byteview,
                                     const ReadBack *contents, bool slotread, bool inplace) {
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
        // The container the value is read out of, whose stores say what it
        // holds: a root that only bounds the container is none. Out of the
        // storage a parameter's class stands for, the value is one of the
        // views that storage holds (RootAlt::classread).
        auto from = c.exact ? croot : nullptr;
        auto classread = from && IsClassRoot(croot);
        // A holder whose contents point nowhere yet (Roots::unknown).
        if (croot && !croot->isglobal && croot->contents.Unknown()) {
            out.unknown = true;
            continue;
        }
        if (byteview) {
            // A byte view can point at any typed storage: the container's
            // contents where they are known, else the container as a bound.
            if (from && !croot->isglobal && !croot->contents.None()) {
                for (auto &a : croot->contents.alts) out.Add({ a.root, false, from });
            } else {
                out.Add({ croot, false, from, false, classread });
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
        // Read out of a caller's container, its stores say what it holds.
        auto global = croot->isglobal;
        if (!global && (!c.exact || croot->ownerspec != CurRealFrame().spec)) {
            out.Add({ croot, false, from, false, classread });
            continue;
        }
        // A holder of the activation's that holds only what the storage of
        // parameters' classes holds: one of the views that storage holds, as
        // a read out of it would be.
        if (!global && inplace && ClassCopyReadBack(croot, out)) continue;
        // Only globals outlive globals (§11.1), so a global container's
        // pointee is owned by a global or by static data, whatever local scope
        // is open here. A local container's was reachable from this frame and
        // had to outlive the container, so its owner is a candidate at the
        // container's depth or shallower: each is an alternative, exact where
        // it is a variable's own storage, a bound where it is a parameter's.
        auto cands = RootCandidates(of, Depth(croot), global, !rt->cq, rt->kind == TY_REF);
        // Out of a slot, the pointee lies in no grow-shrink array's
        // elements: the store rule keeps every reference rooted at storage
        // whose grow-shrink array could hold one out of slots (§5.2), so that
        // storage owns nothing a slot holds. A bound stays, for the lifetime
        // it gives, and so does the owner of a whole grow-shrink array.
        if (slotread)
            std::erase_if(cands.alts, [&](const RootAlt &a) {
                return a.exact && GrowShrinkCanHold(a.root, of);
            });
        if (cands.None()) {
            // Nothing can own the pointee: the container itself bounds it.
            out.Add({ croot, false, from });
            continue;
        }
        for (auto &a : cands.alts) out.Add({ a.root, a.exact, from });
    }
    return out;
}

// Where a reference or slice read out of h, a holder of the activation's
// named exactly, points by what h holds (VarDef::contents): where every root
// stored there since h was made is a class whose storage's views they are
// (RootAlt::classread, AddContents), or static data beside one, what is
// read out of h is one of those views, as a read out of the storage itself
// is, though read out of h (RootAlt::from). A store later in a loop body
// reaches the read on the next iteration, and a store of anything else
// there adds a root to h's contents or takes the mark off one, which the
// loop feeds back: it checks the read again. Else false, and the read takes
// every candidate.
inline bool TypeCheck::ClassCopyReadBack(VarDef *h, Roots &out) {
    if (!h->type || IsRefOrSlice(h->type)) return false;
    Roots r;
    auto classes = false;
    for (auto &a : h->contents.alts) {
        if (a.classread) r.Add({ a.root, false, h, false, true });
        else if (!a.root) r.Add({ nullptr, a.exact });
        else return false;
        classes = classes || a.classread;
    }
    if (!classes) return false;
    out.Add(r);
    return true;
}

// "was read out of `slots` and may point into `pool` or `spare`": the
// alternatives a read-back could not choose between, for the diagnostics of
// the rules that need one (§3.9).
inline string TypeCheck::ReadBackWhy(const Roots &r) {
    auto from = r.From();
    if (!from) return {};
    auto n = r.alts.size();
    if (n == 0) return cat("it was read out of ", from->name, ", whose contents this function cannot trace");
    auto global = BoundAnywhere(from);
    string s = global ? cat(from->name, " is a global var, which any function may bind, and may "
                                        "point into ")
                      : cat("it was read out of ", from->name, " and may point into ");
    for (size_t i = 0; i < n; i++) {
        auto &a = r.alts[i];
        if (i) s += i + 1 == n ? " or " : ", ";
        if (!a.root) { s += a.exact ? "static data" : "any global"; continue; }
        if (global && a.root == from) { s += "any global"; continue; }
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
