// Goose compiler — the typechecker's calls (definitions of TypeCheck members,
// typecheck.h): overload resolution with generic inference (§7.1, §7.7),
// case-function dispatch (§8.2), specialization in call-graph order (§10.1,
// §10.2) with the recursion rules (§7.8) and return roots (§9.2), extern
// declarations (§7.10), thread entry points (§11.2), and function values
// (§7.6).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Calls: builtin members, builtins, UFCS, overload resolution with
// generic inference (§7.1, §7.7), and case-function tag dispatch (§8.2).

inline Val TypeCheck::CheckCall(Call *c) {
    // A node may be re-checked in argument phase 2; reset annotations.
    c->spec = nullptr;
    c->dispatch.clear();
    c->builtin = -1;
    c->rettypes.clear();
    lastcallrets.clear();
    // Arguments construct into parameter slots, not whatever destination
    // encloses this call; member ops re-set curdst for element pushes.
    DestScope ds(*this, Dest {});
    if (auto d = Is<Dot>(c->callee)) return CheckUfcsCall(c, d);
    if (auto id = Is<Ident>(c->callee)) return CheckNamedCall(c, id);
    Error(c, "this expression cannot be called");
}

inline Val TypeCheck::CheckNamedCall(Call *c, Ident *id) {
    if (LookupVar(id->name))
        Error(c, cat(id->name, " is a variable, not a function"));
    if (auto fb = LookupFnVal(id->name)) {
        id->vdef = nullptr;
        return CheckFunValCall(c, *fb);
    }
    FnSpec *env = nullptr;
    vector<SFunction *> cands;
    if (auto nf = LookupLocalFnEnv(id->name, env)) {
        cands.push_back(nf);
    } else {
        auto fit = ast.functionmap.find(id->name);
        if (fit != ast.functionmap.end()) cands = fit->second;
    }
    if (!cands.empty()) {
        for (auto sf : cands)
            if (sf->isthread)
                Error(c, cat("thread_fn ", id->name, " is spawned with thread_spawn, "
                             "not called"));
        Node *nopre = nullptr;
        // A user function set sharing a builtin's name (a `format`
        // overload, §3.7) takes the calls it matches; the builtin the rest.
        auto bd = LookupBuiltin(id->name);
        auto nomatch = false;
        auto v = ResolveCall(c, cands, env, id->name, nullptr, nopre, bd ? &nomatch : nullptr);
        if (!nomatch) return v;
        return CheckBuiltin(c, *bd, c->args, nullptr);
    }
    auto bd = LookupBuiltin(id->name);
    if (!bd) Error(c, cat("unknown function: ", id->name));
    if (bd->flags & BF_PROPERTY)
        Error(c, cat(id->name, " is a property (use a.", id->name, "), not a call"));
    return CheckBuiltin(c, *bd, c->args, nullptr);
}

// A nested function visible from the current point, with the lexical
// environment of the frame that declares it. A frame's scopes are
// [f.scopebase, next frame's scopebase).
inline SFunction *TypeCheck::LookupLocalFnEnv(string_view name, FnSpec *&env) {
    for (auto fi = (int)frames.size() - 1; fi >= 0;) {
        auto &f = frames[fi];
        auto scopelimit = fi == (int)frames.size() - 1 ? (int)scopes.size()
                                                       : frames[fi + 1].scopebase;
        for (auto i = (int)localfns.size() - 1; i >= 0; i--) {
            auto &[si, sf] = localfns[i];
            if (si >= f.scopebase && si < scopelimit && sf->name == name) {
                env = f.lexspec;
                return sf;
            }
        }
        fi = f.lexframe;
    }
    return nullptr;
}

inline Val TypeCheck::CheckUfcsCall(Call *c, Dot *d) {
    auto ov = CheckV(d->obj, nullptr);
    d->obj->exprtype = ov.type;
    auto rt = ov.type;
    if (rt->kind == TY_REF) {
        if (rt->ref->optional)
            Error(c, "optional value must be narrowed (if/guard/assert) before use");
        rt = rt->ref->sub;
    }
    // Built-in members first (§7.1), then free functions, then the
    // remaining builtins (a.f(b) is exactly f(a, b)).
    auto bd = LookupBuiltin(d->name);
    if (bd && (bd->flags & BF_MEMBER) && !(bd->flags & BF_PROPERTY) &&
        rt->kind == TY_ARRAY) {
        vector<Node *> argnodes = { d->obj };
        for (auto a : c->args) argnodes.push_back(a);
        d->member = bd->kind;
        auto v = CheckBuiltin(c, *bd, argnodes, &ov);
        WriteBackArgs(c, d, argnodes);
        return v;
    }
    if (rt->kind == TY_STRUCT) {
        for (auto &f : rt->struc->st->fields)
            if (!f.ispad && f.name == d->name)
                Error(c, cat("field ", d->name, " is not callable"));
    }
    FnSpec *env = nullptr;
    vector<SFunction *> cands;
    if (auto nf = LookupLocalFnEnv(d->name, env)) {
        cands.push_back(nf);
    } else {
        auto fit = ast.functionmap.find(d->name);
        if (fit != ast.functionmap.end()) cands = fit->second;
    }
    if (!cands.empty()) return ResolveCall(c, cands, env, d->name, &ov, d->obj);
    if (bd && !(bd->flags & BF_PROPERTY)) {
        vector<Node *> argnodes = { d->obj };
        for (auto a : c->args) argnodes.push_back(a);
        auto v = CheckBuiltin(c, *bd, argnodes, &ov);
        WriteBackArgs(c, d, argnodes);
        return v;
    }
    if (bd) Error(c, cat(".", d->name, " is a property, not a call"));
    Error(c, cat("unknown function or member: ", d->name));
}

// Phase 1 checks arguments bottom-up for resolution; phase 2 re-checks
// each against its concrete parameter type (adapting literals etc.).
inline Val TypeCheck::ResolveCall(Call *c, vector<SFunction *> &cands, FnSpec *env,
                                  string_view name, Val *preval, Node *&prenode, bool *nomatch) {
    vector<Node *> argnodes;
    vector<Val> argvals;
    // A non-fixed lvalue argument passes by reference (§4.1): it becomes
    // `&a` before any candidate sees it. The user's own `&` on one is
    // redundant.
    auto byref = [&](Node *&a, Val &v) {
        // A resizable without a header of its own (C.2) stays a value,
        // which a slice parameter still takes whole.
        if (IsNonFixedLValue(v) && Referenceable(a, v)) a = AutoRef(a, v);
        else if (UserRefOf(a) && IsPlainRef(v.type) && ClassOf(v.type->ref->sub) != SC_FIXED)
            Warn(a, cat("redundant &: ", ExprStr(Is<Unary>(a)->child),
                        " is passed by reference without it (§4.1)"));
    };
    if (prenode) {
        auto v = *preval;
        byref(prenode, v);
        argnodes.push_back(prenode);
        argvals.push_back(v);
    }
    for (auto &a : c->args) {
        auto v = CheckV(a, nullptr);
        // A pending array (`var out = []`) is completed by the builtin
        // sharing this name (push, append, format), never by user code.
        if (nomatch && IsPendingArray(v.type)) {
            *nomatch = true;
            return Val {};
        }
        RequireComplete(v.type, a->line);
        byref(a, v);
        argnodes.push_back(a);
        a->exprtype = v.type;
        argvals.push_back(v);
    }
    MatchInfo best;
    auto bestcount = 0;
    string failures;
    for (auto sf : cands) {
        MatchInfo mi;
        mi.sf = sf;
        mi.env = env;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why)) {
            Append(failures, "\n  candidate ", name, " at ", Where(sf->line), ": ", why);
            continue;
        }
        if (!bestcount || mi.tier < best.tier) {
            best = mi;
            bestcount = 1;
        } else if (mi.tier == best.tier) {
            bestcount++;
        }
    }
    if (bestcount > 1)
        Error(c, cat("ambiguous call to ", name, ": multiple overloads match equally well"));
    if (!bestcount) {
        // Tag dispatch (§8.2): the match-as-overload-set form.
        auto v = TryDispatch(c, cands, argnodes, argvals, name);
        if (v.type) return v;
        if (nomatch) {   // The caller has a builtin of this name to fall back on.
            *nomatch = true;
            return Val {};
        }
        Error(c, cat("no matching overload for call to ", name, failures));
    }
    auto spec = GetOrCreateSpec(best, argvals, c);
    ApplyCalleeShrinks(c, spec, argvals, name);
    // Phase 2: re-check arguments against the resolved parameter types.
    // Arguments construct into fresh parameter slots, not curdst.
    {
        DestScope ds(*this, Dest {});
        for (size_t i = 0; i < best.paramtypes.size(); i++) {
            // A slice parameter takes the array itself: the reference the
            // argument loop made of it is undone, so the coercion is the
            // plain array-to-slice one.
            if (best.paramtypes[i]->kind == TY_SLICE)
                if (auto u = Is<Unary>(argnodes[i]); u && u->synth) argnodes[i] = u->child;
            // A `&` at a parameter declared as a reference is redundant
            // (§4.1) -- unless it picked this overload.
            auto &p = best.sf->params[i];
            if (UserRefOf(argnodes[i]) && cands.size() == 1 && p.type &&
                !HasGenerics(p.type) && p.type->kind == TY_REF &&
                ClassOf(p.type->ref->sub) == SC_FIXED)
                Warn(argnodes[i], cat("redundant &: ", ExprStr(Is<Unary>(argnodes[i])->child),
                                      " is passed by reference without it (§4.1)"));
            CheckArg(argnodes[i], best.paramtypes[i]);
        }
    }
    // Arguments the re-check rebound by reference replace the originals.
    {
        size_t off = 0;
        if (prenode) { prenode = argnodes[0]; off = 1; }
        for (size_t i = 0; i < c->args.size(); i++) c->args[i] = argnodes[i + off];
    }
    c->spec = spec;
    return CallResult(c, spec, argvals);
}

inline bool TypeCheck::TryMatch(SFunction *sf, Call *c, vector<Val> &argvals, MatchInfo &mi,
                                string &why) {
    auto P = sf->params.size();
    auto N = argvals.size();
    if (N < P) { why = "too few arguments"; return false; }
    for (auto i = P; i < N; i++) {
        if (argvals[i].type != fntype) { why = "too many arguments"; return false; }
    }
    if (c->tyargs.size() > sf->generics.size()) {
        why = "too many explicit type arguments";
        return false;
    }
    for (size_t i = 0; i < c->tyargs.size(); i++) {
        auto t = Subst(c->tyargs[i]);
        ValidateType(t, c->line, VT_LOCAL);
        mi.bindings.push_back({ sf->generics[i].name, t });
    }
    mi.paramtypes.resize(P);
    // The callee's own generic names must not resolve to same-named
    // enclosing bindings while unifying (the recursive-generic case).
    auto saveex = ownexclude;
    ownexclude = &sf->generics;
    auto paramsok = [&]() {
        for (size_t i = 0; i < P; i++) {
            auto &p = sf->params[i];
            auto &av = argvals[i];
            if (!p.type) {
                // Untyped parameter: an anonymous type variable (§7.1),
                // bound to the argument's exact type — a reference
                // argument binds as a reference, like `<T>` does.
                if (av.type == fntype) {
                    why = "function values bind to generic parameters, not untyped ones";
                    return false;
                }
                auto nt = NaturalType(av);
                if (!nt) {
                    why = cat("cannot infer a type for argument ", (int64_t)i + 1);
                    return false;
                }
                mi.paramtypes[i] = nt;
                mi.tier = std::max(mi.tier, 1);
                continue;
            }
            auto ct = UnifyArg(p.type, av, mi.bindings, mi.tier);
            if (!ct) {
                why = cat("argument ", (int64_t)i + 1, ": cannot pass ", TypeStr(av.type),
                          " as ", TypeStr(p.type));
                return false;
            }
            mi.paramtypes[i] = ct;
        }
        return true;
    }();
    ownexclude = saveex;
    if (!paramsok) return false;
    // Leftover generics bind function values, in order (§7.6).
    vector<string_view> unbound;
    for (auto &g : sf->generics) {
        auto found = false;
        for (auto &[n, t] : mi.bindings) found |= n == g.name;
        if (!found) unbound.push_back(g.name);
    }
    auto need = (N - P) + (c->trailing ? 1 : 0);
    if (unbound.size() != need) {
        why = need ? cat((int64_t)need, " function value(s) for ",
                         (int64_t)unbound.size(), " unbound generic parameter(s)")
                   : "cannot infer all generic parameters (use explicit <...>)";
        return false;
    }
    for (size_t k = 0; P + k < N; k++)
        mi.fnvals.push_back({ unbound[k], argvals[P + k].fnv });
    if (c->trailing) {
        FnValBind fb;
        fb.fv = c->trailing;
        fb.env = frames.back().lexspec;
        mi.fnvals.push_back({ unbound.back(), fb });
    }
    return true;
}

// The type an argument value contributes to inference; null when it has
// none ([] and null literals).
inline TypeExpr *TypeCheck::NaturalType(const Val &av) {
    if (av.emptyarr || av.isnull) return nullptr;
    if (av.strlit) return u8slice;
    return av.type;
}

// Makes parameter type pt concrete against argument av, binding this
// call's own generics into b. Returns null when it cannot match. A
// reference argument that fails to match as a reference retries as its
// pointee (transparency).
inline TypeExpr *TypeCheck::UnifyArg(TypeExpr *pt, Val &av,
                                     vector<pair<string_view, TypeExpr *>> &b, int &tier) {
    auto nbind = b.size();
    if (auto ct = UnifyArgRaw(pt, av, b, tier)) return ct;
    if (IsPlainRef(av.type)) {
        b.resize(nbind);  // Discard bindings from the failed attempt.
        auto dv = DecayRef(av);
        if (auto ct = UnifyArgRaw(pt, dv, b, tier)) return ct;
    }
    return nullptr;
}

inline TypeExpr *TypeCheck::UnifyArgRaw(TypeExpr *pt, Val &av,
                                        vector<pair<string_view, TypeExpr *>> &b, int &tier) {
    pt = Subst(pt);  // Enclosing functions' generics are already bound.
    if (av.isnull) {
        auto ct = SubstOwn(pt, b);
        if (!ct || HasGenerics(ct) || !IsOptional(ct)) return nullptr;
        tier = std::max(tier, 2);
        return ct;
    }
    if (av.emptyarr) {
        auto ct = SubstOwn(pt, b);
        if (!ct || HasGenerics(ct) || ct->kind != TY_ARRAY) return nullptr;
        tier = std::max(tier, 2);
        return ct;
    }
    // An lvalue meeting a reference parameter binds by reference (§4.1),
    // so a generic pointee unifies with the argument's own type.
    if (av.lvalue && pt->kind == TY_REF && pt->ref->lenstorage < 0 &&
        av.type->kind != TY_REF) {
        Val rv = av;
        rv.type = RefTo(av.type, pt->line);
        rv.lvalue = false;
        return UnifyArgRaw(pt, rv, b, tier);
    }
    auto at = NaturalType(av);
    auto hadgen = HasGenerics(pt);
    if (hadgen) {
        if (!BindTypes(pt, at, b)) {
            // The one shape-changing coercion generics see through:
            // whole-array arguments to slice parameters (§3.10).
            if (pt->kind == TY_SLICE && at->kind == TY_ARRAY)
                BindTypes(pt->sub, at->arr->sub, b);
        }
    }
    auto ct = SubstOwn(pt, b);
    if (!ct || HasGenerics(ct)) return nullptr;
    if (TypeEq(at, ct)) {
        if (hadgen) tier = std::max(tier, 1);
        return ct;
    }
    Val tmp = av;
    if (!FitsAt(tmp, ct, true)) return nullptr;
    tier = std::max(tier, 2);
    return ct;
}

inline bool TypeCheck::HasGenerics(TypeExpr *t) {
    switch (t->kind) {
        case TY_GENERIC: return true;
        case TY_STRUCT: for (auto a : t->struc->args) if (HasGenerics(a)) return true; return false;
        case TY_ENUM:   for (auto a : t->enu->args) if (HasGenerics(a)) return true; return false;
        case TY_ARRAY:  return HasGenerics(t->arr->sub);
        case TY_SLICE:  return HasGenerics(t->sub);
        case TY_REF:    return HasGenerics(t->ref->sub);
        case TY_VARIANT: return HasGenerics(t->var->adt);
        default: return false;
    }
}

// Structural binding of pt's generic leaves against concrete at. Loose:
// the caller re-validates with FitsAt after substitution.
inline bool TypeCheck::BindTypes(TypeExpr *pt, TypeExpr *at,
                                 vector<pair<string_view, TypeExpr *>> &b) {
    switch (pt->kind) {
        case TY_GENERIC: {
            auto name = pt->named->name;
            auto bindto = at;
            if (pt->named->varmode) {
                if (at->kind != TY_ENUM) return false;
                if (at->enu->varmode) {
                    auto base = ast.NewType(TY_ENUM, at->line);
                    base->enu = ast.NewDetail<TypeEnum>();
                    base->enu->en = at->enu->en;
                    base->enu->args = at->enu->args;
                    bindto = base;
                }
            }
            for (auto &[n, t] : b)
                if (n == name) return TypeEq(t, bindto);
            b.push_back({ name, bindto });
            return true;
        }
        case TY_STRUCT:
            if (at->kind != TY_STRUCT || at->struc->st != pt->struc->st ||
                at->struc->args.size() != pt->struc->args.size()) return false;
            for (size_t i = 0; i < pt->struc->args.size(); i++)
                if (!BindTypes(pt->struc->args[i], at->struc->args[i], b)) return false;
            return true;
        case TY_ENUM:
            if (at->kind != TY_ENUM || at->enu->en != pt->enu->en ||
                at->enu->args.size() != pt->enu->args.size()) return false;
            for (size_t i = 0; i < pt->enu->args.size(); i++)
                if (!BindTypes(pt->enu->args[i], at->enu->args[i], b)) return false;
            return true;
        case TY_ARRAY:
            if (at->kind != TY_ARRAY || at->arr->akind != pt->arr->akind) return false;
            return BindTypes(pt->arr->sub, at->arr->sub, b);
        case TY_SLICE:
            if (at->kind != TY_SLICE) return false;
            return BindTypes(pt->sub, at->sub, b);
        case TY_REF:
            if (at->kind != TY_REF) return false;
            return BindTypes(pt->ref->sub, at->ref->sub, b);
        case TY_VARIANT: {
            // The template's union holds a bare name while its ADT is generic.
            auto ptname = pt->var->adt->kind == TY_ENUM ? pt->var->variant->name
                                                        : pt->var->name;
            if (at->kind != TY_VARIANT || at->var->variant->name != ptname) return false;
            return BindTypes(pt->var->adt, at->var->adt, b);
        }
        default:
            return TypeEq(pt, at);
    }
}

inline TypeExpr *TypeCheck::SubstOwn(TypeExpr *pt, vector<pair<string_view, TypeExpr *>> &b) {
    TypeExpr *r = nullptr;
    WithBindings(b, [&]() { r = Subst(pt); });
    return r;
}

// Case-function tag dispatch (§8.2): calling an overload set of variant
// types with the ADT (or a reference to it) dispatches on the tag.
// Returns a Val with null type when no dispatch position exists.
inline Val TypeCheck::TryDispatch(Call *c, vector<SFunction *> &cands, vector<Node *> &argnodes,
                                  vector<Val> &argvals, string_view name) {
    auto found = -1;
    vector<MatchInfo> matches;  // Per variant, for the found position.
    TypeExpr *enumtype = nullptr;
    auto byref = false;
    for (size_t pos = 0; pos < argvals.size(); pos++) {
        auto at = argvals[pos].type;
        TypeExpr *et = nullptr;
        auto isref = false;
        if (at->kind == TY_ENUM) et = at;
        else if (IsPlainRef(at) && at->ref->sub->kind == TY_ENUM) { et = at->ref->sub; isref = true; }
        if (!et) continue;
        // Fixed-mode payloads pass by copy even through a reference, for
        // the same soundness reason as match binders (§3.5).
        isref = isref && et->enu->varmode;
        auto en = et->enu->en;
        vector<MatchInfo> vm;
        auto allok = true;
        for (auto &var : en->variants) {
            auto vt = VariantTypeOf(et, &var, c->line);
            auto argt = isref ? RefTo(vt, c->line) : vt;
            Val vv = argvals[pos];
            vv.type = argt;
            auto saved = argvals[pos];
            argvals[pos] = vv;
            MatchInfo onlymatch;
            auto count = 0;
            for (auto sf : cands) {
                MatchInfo mi;
                mi.sf = sf;
                string why;
                if (TryMatch(sf, c, argvals, mi, why)) {
                    onlymatch = mi;
                    count++;
                }
            }
            argvals[pos] = saved;
            if (count != 1) { allok = false; break; }
            vm.push_back(onlymatch);
        }
        if (!allok) continue;
        if (found >= 0)
            Error(c, cat("call to ", name, " could dispatch on more than one argument "
                         "(v1 allows a single dispatch position)"));
        found = (int)pos;
        matches = std::move(vm);
        enumtype = et;
        byref = isref;
    }
    if (found < 0) return Val {};
    // Specialize every arm; return types and the other parameters must
    // agree across the set.
    c->dispatcharg = found;
    vector<Val> armvals = argvals;
    auto en = enumtype->enu->en;
    FnSpec *first = nullptr;
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
        auto &mi = matches[vi];
        auto vt = VariantTypeOf(enumtype, &en->variants[vi], c->line);
        armvals[found] = argvals[found];
        armvals[found].type = byref ? RefTo(vt, c->line) : vt;
        auto spec = GetOrCreateSpec(mi, armvals, c);
        ApplyCalleeShrinks(c, spec, armvals, name);
        if (first) {
            if (spec->rets.size() != first->rets.size())
                Error(c, cat("case functions of ", name, " disagree on return counts"));
            for (size_t r = 0; r < spec->rets.size(); r++)
                if (!TypeEq(spec->rets[r], first->rets[r]))
                    Error(c, cat("case functions of ", name, " disagree on return types: ",
                                 TypeStr(spec->rets[r]), " vs ", TypeStr(first->rets[r])));
            for (size_t p = 0; p < matches[vi].paramtypes.size(); p++)
                if ((int)p != found &&
                    !TypeEq(matches[vi].paramtypes[p], matches[0].paramtypes[p]))
                    Error(c, cat("case functions of ", name,
                                 " disagree on non-dispatch parameter types"));
        } else {
            first = spec;
        }
        c->dispatch.push_back(spec);
    }
    // Phase 2 for the non-dispatch args (before CallResult, so nested
    // calls cannot clobber lastcallrets); the dispatch arg keeps its type.
    {
        DestScope ds(*this, Dest {});
        for (size_t i = 0; i < matches[0].paramtypes.size(); i++)
            if ((int)i != found) CheckArg(argnodes[i], matches[0].paramtypes[i]);
    }
    return CallResult(c, first, argvals);
}

// ------------------------------------------------------------------
// Specialization: find or create the FnSpec for a resolved call and
// check its body (once) in call-graph order.

inline FnSpec *TypeCheck::GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode) {
    auto sf = mi.sf;
    // Root classes: distinct roots of ref/slice args ordered by depth.
    vector<RootArg> roots;
    vector<VarDef *> distinct;
    for (size_t i = 0; i < mi.paramtypes.size(); i++) {
        auto pt = mi.paramtypes[i];
        if (pt->kind != TY_REF && pt->kind != TY_SLICE) continue;
        auto r = CanonRoot(argvals[i].root);
        RootArg ra;
        ra.writable = argvals[i].writable;
        ra.reusable = argvals[i].reusable;
        ra.exact = argvals[i].rootexact;
        ra.growshrink = IsGrowShrinkRoot(r);
        if (ra.exact) ra.pool = PoolOf(r);
        if (!r) {
            ra.cls = 0;
        } else {
            auto idx = -1;
            // Sharing a class says the two arguments point into the same
            // array, which an inexactly rooted one does not establish: it
            // names a scope its pointee outlives, not the storage that
            // owns it (§9.5). Such an argument gets a class to itself, so
            // a class is always one array whatever the call site.
            if (ra.exact)
                for (size_t k = 0; k < distinct.size(); k++)
                    if (distinct[k] == r) idx = (int)k;
            if (idx < 0) {
                // Keep distinct ordered by depth so classes mean outlives-rank.
                auto ins = distinct.size();
                while (ins > 0 && Depth(distinct[ins - 1]) > Depth(r)) ins--;
                distinct.insert(distinct.begin() + ins, r);
                for (auto &rr : roots) if (rr.cls > (int)ins) rr.cls++;
                idx = (int)ins;
            }
            ra.cls = idx + 1;
        }
        roots.push_back(ra);
    }
    for (auto spec : sf->specs) {
        if (spec->lexparent != mi.env) continue;
        if (!TypeArgsEq(spec->argtypes, mi.paramtypes)) continue;
        if (spec->fnvals.size() != mi.fnvals.size()) continue;
        auto fvok = true;
        for (size_t i = 0; i < mi.fnvals.size(); i++)
            fvok &= spec->fnvals[i].second == mi.fnvals[i].second;
        if (!fvok) continue;
        auto rootsok = spec->roots.size() == roots.size();
        for (size_t i = 0; rootsok && i < roots.size(); i++)
            rootsok &= spec->roots[i] == roots[i];
        // A back edge must reuse the in-progress spec whatever the roots
        // (§7.8): inside a cycle, references rooted at cycle locals may
        // not be stored or returned, so their identity is irrelevant, and
        // the pool parameters that may be stored are checked below to be
        // the same ones the entry call passed.
        if (!rootsok && !spec->inprogress) continue;
        // Exactness is not part of the key, so what the specialization
        // records is what every call site that reaches it agrees on.
        for (size_t i = 0; i < roots.size() && i < spec->roots.size(); i++)
            spec->roots[i].exact = spec->roots[i].exact && roots[i].exact;
        if (spec->inprogress) {
            ValidateCycle(spec, callnode);
            ValidatePoolArgs(spec, argvals, callnode);
        }
        ValidateNeeds(spec, callnode);
        return spec;
    }
    auto spec = ast.NewFnSpec();
    spec->sf = sf;
    spec->lexparent = mi.env;
    spec->argtypes = mi.paramtypes;
    spec->roots = roots;
    spec->fnvals = mi.fnvals;
    spec->bindings = mi.bindings;
    sf->specs.push_back(spec);
    CheckSpecBody(spec, &argvals, callnode->line);
    return spec;
}

inline void TypeCheck::ValidateCycle(FnSpec *spec, Node *callnode) {
    if (!spec->sf->isrec)
        Error(callnode, cat("recursive call cycle through ", spec->sf->name,
                            ", which is not declared `recursive fn` (§7.8)"));
    auto fi = FrameOfSpec(spec);
    if (fi < 0) fi = 0;
    for (auto i = fi; i < (int)frames.size(); i++) {
        auto s = frames[i].spec;
        if (!s || !frames[i].sf || frames[i].isfunval) continue;
        s->incycle = true;
        for (auto &p : frames[i].sf->params)
            if (!p.type)
                Error(callnode, cat("function ", frames[i].sf->name, " is in a recursive "
                                    "cycle and needs fully explicit parameter types"));
        if (s->has_nonfixed_local)
            Error(s->nonfixedline, cat("function ", frames[i].sf->name, " is in a "
                                       "recursive cycle and may not own non-fixed-size "
                                       "locals (§7.8)"));
    }
    // A cycle function without an explicit return type is committed to
    // returning nothing at the back edge; a later `return v` then errors
    // with a mismatch (return-type inference cannot cross the back edge).
    if (!spec->retsknown) spec->retsknown = true;
}

// The root a synthetic parameter class stands for, followed back through
// the call sites that created it to a real variable (or null for static
// data).
inline VarDef *TypeCheck::UltimateRoot(VarDef *v) {
    while (v && v->classfrom) v = CanonRoot(v->classfrom);
    return v;
}

// References in a pool class (VarDef::poolclass) may be stored inside a
// recursive cycle, which is sound only while every activation's pool
// parameters name the pools the entry call passed: the reused spec's
// stores were proven against those. A back edge that passes a different
// pool, or swaps two, is rejected here.
inline void TypeCheck::ValidatePoolArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode) {
    for (size_t i = 0; i < spec->params.size() && i < argvals.size(); i++) {
        auto pr = spec->params[i]->ref.root;
        if (!pr) continue;
        // A named pool is part of the specialization key everywhere else,
        // but a back edge reuses the in-progress spec whatever its roots.
        if (pr->classpool &&
            (!argvals[i].rootexact || PoolOf(CanonRoot(argvals[i].root)) != pr->classpool))
            Error(callnode, cat("recursive call passes ", spec->params[i]->name,
                                " rooted outside ", pr->classpool->name,
                                ", which the cycle's entry call rooted there (§3.9)"));
        if (!pr->poolclass) continue;
        if (!argvals[i].rootexact ||
            UltimateRoot(pr) != UltimateRoot(CanonRoot(argvals[i].root)))
            Error(callnode, cat("recursive call passes ", spec->params[i]->name,
                                " rooted differently from the cycle's entry call, which "
                                "stored references into it (§7.8)"));
    }
}

// `return from` targets recorded by a callee must be live on every
// compile-time call path (§7.9); cached reuse re-validates here.
inline void TypeCheck::ValidateNeeds(FnSpec *spec, Node *callnode) {
    for (auto t : spec->needs) {
        auto found = -1;
        for (auto i = (int)frames.size() - 1; i >= 0; i--)
            if (frames[i].sf == t && !frames[i].isfunval) { found = i; break; }
        if (found < 0)
            Error(callnode, cat("call to ", spec->sf->name, " requires an enclosing call "
                                "of ", t->name, " (it does `return ... from ", t->name,
                                "`)"));
        for (auto i = found + 1; i < (int)frames.size(); i++)
            if (frames[i].spec) frames[i].spec->needs.insert(t);
    }
}

// The §7.8 cycle return-root analysis, handed the one piece of checker
// state it cannot derive from the syntax: the root a variable holds.
inline CycleRoots TypeCheck::Cycles() {
    return CycleRoots(ast, cycleroot, [this](VarDef *vd, bool isref) {
        return CanonRoot(isref ? RefRootOf(vd) : vd);
    });
}

// A fixed-size value C takes by value (§7.10): a scalar, bool, or a flat
// fixed struct or array (packed, no references).
inline bool TypeCheck::ExternValueOk(TypeExpr *t, string &why) {
    switch (t->kind) {
        case TY_INT:
            if (t->intstorage == IS_VARINT) { why = "varints have no C form"; return false; }
            return true;
        case TY_FLT: case TY_BOOL: return true;
        case TY_REF: case TY_SLICE: why = "nested references and slices do not cross"; return false;
        default: break;
    }
    if (ClassOf(t) != SC_FIXED) { why = "only fixed-size values cross by value"; return false; }
    if (!IsFlat(t)) { why = "values holding references do not cross"; return false; }
    return true;
}

inline bool TypeCheck::ExternParamOk(TypeExpr *t, string &why) {
    if (t->kind == TY_REF) {
        if (t->ref->optional || t->ref->lenstorage >= 0) {
            why = "only plain references cross";
            return false;
        }
        auto s = t->ref->sub;
        if (s->kind == TY_ARRAY && s->arr->akind == A_GROW && IsU8(s->arr->sub)) return true;
        return ExternValueOk(s, why);
    }
    if (t->kind == TY_SLICE) return ExternValueOk(t->sub, why);
    return ExternValueOk(t, why);
}

// An extern declaration (§7.10) has typed parameters of C-crossing
// shapes, at most one fixed-size return, and no body to check.
inline void TypeCheck::CheckExternSpec(FnSpec *spec) {
    auto sf = spec->sf;
    if (!sf->generics.empty())
        Error(sf->line, cat("extern fn ", sf->name, " cannot be generic"));
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto &p = sf->params[i];
        if (!p.type) Error(sf->line, cat("extern fn ", sf->name, ": parameter ", p.name,
                                         " needs a type"));
        auto pt = spec->argtypes[i];
        ValidateType(pt, sf->line, VT_PARAM);
        string why;
        if (!ExternParamOk(pt, why))
            Error(sf->line, cat("extern fn ", sf->name, ": parameter ", p.name, " of type ",
                                TypeStr(pt), " cannot cross to C: ", why));
        auto vd = ast.NewVarDef();
        vd->name = p.name;
        vd->type = pt;
        vd->line = sf->line;
        vd->isparam = true;
        vd->ownerspec = spec;
        spec->params.push_back(vd);
    }
    if (sf->rets.size() > 1)
        Error(sf->line, cat("extern fn ", sf->name, " returns at most one value"));
    spec->rets.clear();
    for (auto rt : sf->rets) {
        auto t = Subst(rt);
        ValidateType(t, sf->line, VT_LOCAL);
        string why;
        if (!ExternValueOk(t, why))
            Error(sf->line, cat("extern fn ", sf->name, " cannot return ", TypeStr(t), ": ",
                                why));
        spec->rets.push_back(t);
    }
    spec->retsknown = true;
    spec->checkedreturn = true;
    spec->checked = true;
    spec->inprogress = false;
}

inline void TypeCheck::CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline) {
    auto sf = spec->sf;
    if (sf->isextern) { CheckExternSpec(spec); return; }
    spec->inprogress = true;
    Frame f;
    f.sf = sf;
    f.spec = spec;
    f.lexspec = spec;
    f.lexframe = spec->lexparent ? FrameOfSpec(spec->lexparent) : -1;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = callline;
    frames.push_back(f);
    auto savereach = reachable;
    DestScope ds(*this, Dest {});
    reachable = true;
    PushScope(SK_FN);
    // Parameters. For reference/slice parameters, a synthetic root
    // VarDef per call-site root class carries the caller-side depth.
    vector<VarDef *> classroots(spec->roots.size() + 1, nullptr);
    auto rootidx = 0;
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto &p = sf->params[i];
        auto pt = spec->argtypes[i];
        ValidateType(pt, sf->line, VT_PARAM);
        auto vd = NewVar(p.name, pt, sf->line, p.isvar);
        vd->isparam = true;
        vd->assigned = true;
        if (pt->kind == TY_REF || pt->kind == TY_SLICE) {
            auto &ra = spec->roots[rootidx++];
            if (ra.cls == 0) {
                vd->ref.root = nullptr;  // Static data.
            } else {
                if (!classroots[ra.cls]) {
                    auto rv = ast.NewVarDef();
                    rv->name = p.name;
                    rv->depth = argvals ? Depth(CanonRoot((*argvals)[i].root)) : 0;
                    rv->classfrom = argvals ? CanonRoot((*argvals)[i].root) : nullptr;
                    rv->poolclass = true;
                    rv->classpool = ra.pool;
                    rv->growshrink = ra.growshrink;
                    classroots[ra.cls] = rv;
                }
                // Members of one class share a root, so they agree on the
                // pool; a member that names none settles it for all.
                if (!ra.pool) classroots[ra.cls]->classpool = nullptr;
                // A pool class holds only references to resizable-class
                // values; a slice or a reference to anything smaller may
                // point at a cycle function's own storage.
                if (pt->kind != TY_REF || ClassOf(pt->ref->sub) != SC_RESIZABLE ||
                    !ra.exact)
                    classroots[ra.cls]->poolclass = false;
                vd->ref.root = classroots[ra.cls];
            }
            vd->refrootknown = true;
            // Every member of a class points into one array (see
            // GetOrCreateSpec), so within this body the class names that
            // array. Whether it is the array some *other* class names is a
            // different question, and only ra.exact answers it.
            vd->ref.rootexact = true;
            vd->ref.writable = ra.writable;
            vd->ref.reusable = ra.reusable;
        }
        spec->params.push_back(vd);
    }
    if (sf->has_rets) {
        for (auto rt : sf->rets) {
            auto ct = Subst(rt);
            ValidateType(ct, sf->line, VT_RET);
            spec->rets.push_back(ct);
        }
        spec->retsknown = true;
    }
    // A cycle's back edges reach this specialization before any of its own
    // returns are checked, so predict their roots first (§7.8).
    if (sf->isrec && spec->retsknown) Cycles().Seed(spec);
    spec->body = (Block *)sf->body->Clone(ast);
    // The body: statements plus a value-producing tail (treated exactly
    // like `return tail`). An else-less if tail cannot be a value, so a
    // void function may end in one.
    for (auto st : spec->body->stmts) CheckStmt(st);
    if (spec->body->tail) {
        auto tail = spec->body->tail;
        auto asvalue = !(spec->retsknown && spec->rets.empty());
        if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) asvalue = false;
        if (Is<Guard>(tail)) asvalue = false;
        if (!asvalue) {
            CheckStmtExpr(tail);
            if (reachable && spec->retsknown && !spec->rets.empty())
                Error(tail, cat("function ", sf->name, " must return value(s)"));
        } else {
            auto expected = spec->retsknown && spec->rets.size() == 1 ? spec->rets[0]
                                                                      : nullptr;
            auto tv = CheckValue(spec->body->tail, expected);
            tail = spec->body->tail;
            if (reachable) {
                if (tv.type->kind == TY_VOID) {
                    if (spec->retsknown && !spec->rets.empty())
                        Error(tail, cat("function ", sf->name, " must return value(s)"));
                } else {
                    vector<Val> vals = { tv };
                    RecordReturn(spec, vals, tail);
                    reachable = false;
                }
            }
        }
    }
    if (reachable) {
        if (spec->retsknown && !spec->rets.empty())
            Error(sf->body, cat("function ", sf->name,
                                " can fall off the end without returning value(s)"));
        if (!spec->retsknown) spec->retsknown = true;  // No returns at all: void.
    }
    if (!spec->retsknown) spec->retsknown = true;
    PopScope();
    frames.pop_back();
    reachable = savereach;
    spec->inprogress = false;
    spec->checked = true;
}

// Shared by `return` statements and body tails: agree the values with
// the target's return types (setting them on first sight), and record
// reference roots for the caller to map (§9.2).
inline void TypeCheck::RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at) {
    // A single call forwards all its return values.
    vector<TypeExpr *> types;
    for (auto &v : vals) types.push_back(v.type);
    if (!tspec->retsknown) {
        for (auto &v : vals) {
            if (v.type->kind == TY_VOID)
                Error(at, "cannot return a valueless expression");
            tspec->rets.push_back(v.type);
        }
        tspec->retsknown = true;
    } else {
        if (vals.size() != tspec->rets.size())
            Error(at, cat("returning ", (int64_t)vals.size(), " value(s), function ",
                          tspec->sf ? tspec->sf->name : string_view("?"), " has ",
                          (int64_t)tspec->rets.size()));
    }
    if (tspec->retroots.size() < tspec->rets.size()) tspec->retroots.resize(tspec->rets.size());
    for (size_t i = 0; i < vals.size(); i++) {
        auto rt = tspec->rets[i];
        auto rk = rt->kind;
        if (rk != TY_REF && rk != TY_SLICE) continue;
        // A null return names no root: it agrees with every other return.
        if (vals[i].isnull) continue;
        auto root = CanonRoot(vals[i].root);
        // Anything whose storage the callee's frame owns dies on return;
        // reference parameters' pointee roots are synthetic per-class
        // VarDefs (no ownerspec), so they pass and map at the call site.
        if (root && root->ownerspec == tspec)
            Error(at, cat("returning a reference rooted in ", root->name,
                          ", which dies with this function (§9.2)"));
        if (root && root == temproot)
            Error(at, "returning a reference into a temporary");
        if (root && root == cycleroot)
            Error(at, "returning the result of a recursive call whose returned "
                      "reference's root the cycle's returns do not determine (§7.8)");
        auto &rr = tspec->retroots[i];
        if (rr.set && rr.root != root)
            Error(at, "returns disagree on the returned reference's root "
                      "(not yet supported; use one source)");
        rr.set = true;
        if (auto bad = Cycles().ReturnConflict(tspec, i, root, vals[i].rootexact);
            !bad.empty())
            Error(at, bad);
        rr.root = root;
        rr.exact = vals[i].rootexact;
        rr.writable = vals[i].writable;
        rr.seeded = false;
    }
    tspec->checkedreturn = true;
}

inline Val TypeCheck::CallResult(Call *c, FnSpec *spec, vector<Val> &argvals) {
    c->rettypes = spec->rets;
    lastcallrets.clear();
    for (size_t i = 0; i < spec->rets.size(); i++) {
        Val v;
        v.type = spec->rets[i];
        if (v.type->kind == TY_REF || v.type->kind == TY_SLICE) {
            auto ri = i < spec->retroots.size() ? spec->retroots[i] : RetRoot {};
            auto rr = ri.root;
            v.writable = ri.writable;
            if (!rr) {
                // A back edge whose target has neither recorded nor
                // predicted this root: unknown, which is not static data.
                auto known = spec->checkedreturn || ri.seeded;
                v.root = spec->inprogress && !known ? cycleroot : nullptr;
            } else if (!rr->isglobal && !rr->ownerspec) {
                // A synthetic per-class parameter root: map back to this
                // call site's argument root.
                v.root = rr;
                v.rootexact = ri.exact;
                for (size_t p = 0; p < spec->params.size(); p++) {
                    if (spec->params[p]->ref.root == rr) {
                        if (p < argvals.size()) {
                            v.root = CanonRoot(argvals[p].root);
                            v.rootexact = ri.exact && argvals[p].rootexact;
                            v.rootfrom = argvals[p].rootfrom;
                            v.writable = argvals[p].writable;
                        }
                        break;
                    }
                }
            } else {
                v.root = rr;  // A global or a captured outer local.
                v.rootexact = ri.exact;
            }
        } else {
            v.root = temproot;
            v.writable = false;
        }
        lastcallrets.push_back(v);
    }
    if (spec->rets.empty()) return VoidVal();
    return lastcallrets[0];
}

inline void TypeCheck::CheckReturn(Return *r) {
    // Which function does this exit? `from f` names one on the current
    // compile-time path; a plain return inside a function value exits the
    // lexically enclosing named function (§7.6, §7.9).
    auto tf = -1;
    if (!r->from.empty()) {
        for (auto i = (int)frames.size() - 1; i >= 1; i--)
            if (!frames[i].isfunval && frames[i].sf && frames[i].sf->name == r->from) {
                tf = i;
                break;
            }
        if (tf < 0)
            Error(r, cat("return from ", r->from, ": no enclosing call of ", r->from,
                         " on this compile-time call path"));
    } else if (frames.back().isfunval) {
        tf = FrameOfSpec(frames.back().lexspec);
        if (tf < 0) Error(r, "cannot resolve the enclosing function of this value");
    } else {
        tf = (int)frames.size() - 1;
        if (!frames[tf].sf) Error(r, "return outside of a function");
    }
    auto tspec = frames[tf].spec;
    r->target = frames[tf].sf;
    for (auto i = tf + 1; i < (int)frames.size(); i++)
        if (frames[i].spec) frames[i].spec->needs.insert(frames[tf].sf);
    // Values.
    vector<Val> vals;
    auto expectone = [&](size_t i) -> TypeExpr * {
        return tspec->retsknown && i < tspec->rets.size() ? tspec->rets[i] : nullptr;
    };
    inreturn = true;
    if (r->vals.size() == 1) {
        auto v = CheckValue(r->vals[0], tspec->retsknown && tspec->rets.size() == 1
                                            ? tspec->rets[0] : nullptr);
        if (auto call = Is<Call>(r->vals[0]); call && call->rettypes.size() > 1) {
            vals = lastcallrets;  // Forward a multi-value call.
        } else {
            if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
            vals.push_back(v);
        }
    } else {
        for (size_t i = 0; i < r->vals.size(); i++) {
            auto v = CheckValue(r->vals[i], expectone(i));
            if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
            vals.push_back(v);
        }
    }
    inreturn = false;
    if (vals.empty() && tspec->retsknown && !tspec->rets.empty())
        Error(r, cat("function ", frames[tf].sf->name, " must return value(s)"));
    if (!vals.empty() || !tspec->retsknown) {
        // For long-distance returns, references must not be rooted in
        // frames that unwind; conservatively require globals/static.
        if (tf != (int)frames.size() - 1 && !frames.back().isfunval) {
            for (auto &v : vals) {
                auto rk = v.type->kind;
                if ((rk == TY_REF || rk == TY_SLICE) && v.root && !v.root->isglobal)
                    Error(r, "a long-distance return may only carry references to "
                             "globals or static data");
            }
        }
        RecordReturn(tspec, vals, r);
    }
    reachable = false;
}

// ------------------------------------------------------------------
// Struct and variant literals (§4.2). The per-node entry is
// StructLit::Check at the end of this file.

// `selft` is the type of the value this literal constructs (the enum type
// for a variant literal in fixed enum mode), which is what `self` names.
inline void TypeCheck::CheckInits(StructLit *sl, vector<Field> &fields, vector<TypeExpr *> &ftypes,
                                  string_view what, TypeExpr *selft) {
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
        if (Is<SelfRef>(fi.val)) { CheckSelfInit(fi.val, ftypes[idx], selft); continue; }
        CheckValue(fi.val, ftypes[idx]);
    }
    for (auto i = 0; i < (int)fields.size(); i++) {
        if (fields[i].ispad || got[i]) continue;
        // Optional fields default to null (there is no null literal to
        // spell it with); anything else needs a declared default.
        if (!fields[i].defaultval && !IsOptional(ftypes[i]))
            Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                          " (it has no default)"));
    }
}

// ------------------------------------------------------------------
// Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

inline FnSpec *TypeCheck::EnsureThreadSpec(SFunction *sf, Line l) {
    if (!sf->specs.empty()) return sf->specs[0];
    if (!sf->generics.empty()) Error(l, cat("thread_fn ", sf->name, " cannot be generic"));
    if (sf->has_rets) Error(l, cat("thread_fn ", sf->name, " cannot return values"));
    auto spec = ast.NewFnSpec();
    spec->sf = sf;
    for (auto &p : sf->params) {
        if (!p.type)
            Error(l, cat("thread_fn ", sf->name, " needs fully typed parameters"));
        auto t = Subst(p.type);
        ValidateType(t, sf->line, VT_PARAM);
        if (!IsFlat(t))
            Error(l, cat("thread_fn parameters must be flat (§11.2), not ", TypeStr(t)));
        spec->argtypes.push_back(t);
    }
    sf->specs.push_back(spec);
    CheckSpecBody(spec, nullptr, l);
    return spec;
}

// ------------------------------------------------------------------
// Calling a function value F(a): the body is cloned and checked inline
// in the lexical environment it was written in (§7.6).

inline TypeExpr *TypeCheck::SubstEnv(TypeExpr *t, FnSpec *env) {
    Frame f;
    f.lexspec = env;
    f.lexframe = -1;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    frames.push_back(f);
    auto r = Subst(t);
    frames.pop_back();
    return r;
}

}  // namespace goose
