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
    TempScope argscope(*this);
    // A node may be re-checked in argument phase 2; reset annotations.
    c->spec = nullptr;
    c->dispatch.clear();
    c->builtin = -1;
    c->rettypes.clear();
    c->fmtspecs.clear();
    c->fmtcontexts.clear();
    lastcallrets.clear();
    // Arguments construct into parameter slots, not whatever destination
    // encloses this call; member ops re-set curdst for element pushes.
    DestScope ds(*this, Dest {});
    if (auto d = Is<Dot>(c->callee)) return CheckUfcsCall(c, d);
    if (auto id = Is<Ident>(c->callee)) return CheckNamedCall(c, id);
    Error(c, "this expression cannot be called");
}

inline Val TypeCheck::CheckNamedCall(Call *c, Ident *id) {
    if (LookupVar(id->name, id->ns))
        Error(c, cat(id->name, " is a variable, not a function"));
    if (auto fb = LookupFnVal(id->name)) {
        id->vdef = nullptr;
        return CheckFunValCall(c, *fb);
    }
    FnSpec *env = nullptr;
    vector<SFunction *> cands;
    if (auto nf = LookupLocalFnEnv(id->name, env)) cands.push_back(nf);
    else cands = ast.LookupFunctions(id->name, id->ns);
    // The builtins are global: `::f` reaches one past a namespaced f, and
    // `ns::f` never names one.
    auto bd = LookupBuiltin(GlobalLeaf(id->name));
    if (!cands.empty()) {
        for (auto sf : cands)
            if (sf->isthread)
                Error(c, cat("thread_fn ", id->name, " is spawned with thread_spawn, "
                             "not called"));
        Node *nopre = nullptr;
        // A user function set sharing a builtin's name (a `format`
        // overload, §3.7) takes the calls it matches; the builtin the rest.
        auto nomatch = false;
        auto v = ResolveCall(c, cands, env, id->name, nullptr, nopre, bd ? &nomatch : nullptr);
        if (!nomatch) return v;
        return CheckBuiltin(c, *bd, c->args, nullptr);
    }
    if (!bd) Error(c, cat("unknown function: ", id->name));
    if (bd->flags & BF_PROPERTY)
        Error(c, cat(id->name, " is a property (use a.", id->name, "), not a call"));
    return CheckBuiltin(c, *bd, c->args, nullptr);
}

// A nested function visible from the current point, with the lexical
// environment of the frame that declares it: one declared so far in this
// frame's scopes, else one the body sees outside them (ForOuterFns).
inline SFunction *TypeCheck::LookupLocalFnEnv(string_view name, FnSpec *&env) {
    auto top = (int)frames.size() - 1;
    for (auto i = (int)localfns.size() - 1; i >= 0 && localfns[i].first >= frames[top].scopebase;
         i--) {
        if (localfns[i].second->name != name) continue;
        env = frames[top].lexspec;
        return localfns[i].second;
    }
    SFunction *found = nullptr;
    ForOuterFns(top, [&](SFunction *sf, FnSpec *e) {
        if (sf->name != name) return false;
        found = sf;
        env = e;
        return true;
    });
    return found;
}

inline Val TypeCheck::CheckUfcsCall(Call *c, Dot *d) {
    auto ov = CheckV(d->obj, nullptr);
    d->obj->exprtype = ov.type;
    auto rt = ov.type;
    auto optional = IsOptional(rt);
    if (rt->kind == TY_REF) rt = rt->ref->sub;
    // Built-in members first (§7.1), then free functions, then the
    // remaining builtins (a.f(b) is exactly f(a, b)). Only a member needs
    // an optional receiver narrowed first: the others take it as f(a, b)
    // takes a, so r.assert() is what narrows r (§3.8).
    auto bd = LookupBuiltin(d->name);
    if (bd && (bd->flags & BF_MEMBER) && !(bd->flags & BF_PROPERTY) &&
        rt->kind == TY_ARRAY) {
        if (optional) Error(c, "optional value must be narrowed (if/guard/assert) before use");
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
    if (auto nf = LookupLocalFnEnv(d->name, env)) cands.push_back(nf);
    else cands = ast.LookupFunctions(d->name, d->ns);
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
    LoadSliceArgs(argvals, best.paramtypes);
    auto spec = GetOrCreateSpec(best, argvals, c);
    ApplyCalleeShrinks(c, spec, argvals, name);
    ApplyCalleeGrows(c, spec, argvals, name);
    // Phase 2: re-check arguments against the resolved parameter types.
    // Arguments construct into fresh parameter slots, not curdst.
    {
        DestScope ds(*this, Dest {});
        for (size_t i = 0; i < best.paramtypes.size(); i++) {
            UnrefForValueParam(argnodes[i], best.paramtypes[i]);
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
    // A C function's parameters are what they say (§7.10): a read-only
    // reference or slice needs the parameter declared const.
    if (best.sf->isextern) {
        for (size_t i = 0; i < best.paramtypes.size() && i < argvals.size(); i++) {
            auto pt = best.paramtypes[i];
            if (!IsRefOrSlice(pt) || pt->cq || argvals[i].writable)
                continue;
            Error(argnodes[i], cat("extern fn ", name, ": parameter ", best.sf->params[i].name,
                                   " of type ", TypeStr(pt), " takes a writable value; a "
                                   "read-only one needs the parameter declared const (§9.5)"));
        }
    }
    // Arguments the re-check rebound by reference replace the originals.
    {
        size_t off = 0;
        if (prenode) { prenode = argnodes[0]; off = 1; }
        for (size_t i = 0; i < c->args.size(); i++) c->args[i] = argnodes[i + off];
    }
    c->spec = spec;
    ApplyCalleeRebinds(spec);
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
    // A literal argument (or a literal parameter passed on) to a parameter
    // whose type is a bare type variable no typed argument binds, or that
    // is untyped, stays a literal inside the specialization (§7.7): the
    // variable binds to the literal's default type, and the parameter
    // reads as a constant of unknown value that adapts where it is used.
    auto owngeneric = [&](TypeExpr *t) {
        if (!t || t->kind != TY_GENERIC) return false;
        for (auto &g : sf->generics) if (g.name == t->named->name) return true;
        return false;
    };
    auto isliteral = [](const Val &av) { return av.ck != CK_NONE || av.unsized; };
    set<string_view> litbound;   // Type variables bound by literals alone.
    // Trying a candidate records nothing: the chosen overload's
    // argument re-check does.
    auto saverecord = litrecord;
    litrecord = false;
    auto paramsok = [&]() {
        // A literal argument adapts to whatever type the other arguments
        // give a type parameter (§3.1), so they unify last.
        vector<size_t> order;
        for (size_t i = 0; i < P; i++) if (!isliteral(argvals[i])) order.push_back(i);
        for (size_t i = 0; i < P; i++) if (isliteral(argvals[i])) order.push_back(i);
        for (auto i : order) {
            auto &p = sf->params[i];
            auto &av = argvals[i];
            auto unsized = false;
            if (isliteral(av) && !p.isvar && !sf->isextern) {
                if (!p.type) {
                    unsized = true;
                } else if (owngeneric(p.type)) {
                    auto name = p.type->named->name;
                    auto bound = false;
                    for (auto &[n, t] : mi.bindings) bound |= n == name;
                    unsized = !bound || litbound.count(name);
                    if (unsized) litbound.insert(name);
                }
            }
            if (unsized) mi.litparams.push_back((int)i);
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
    litrecord = saverecord;
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
    if (av.strlit) return cu8slice;
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
    if (TopConstEq(at, ct)) {
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
            if (at->kind != TY_VARIANT || at->var->name != pt->var->name) return false;
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
    if (ClassOf(enumtype) == SC_RESIZABLE)
        Error(c, "dispatching a resizable ADT payload is not supported by the C backend "
                 "yet; match its tag without a payload binder, or use a standalone "
                 "resizable struct");
    // Specialize every arm; return types and the other parameters must
    // agree across the set.
    c->dispatcharg = found;
    LoadSliceArgs(argvals, matches[0].paramtypes);
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
        ApplyCalleeGrows(c, spec, armvals, name);
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
            if ((int)i != found) {
                UnrefForValueParam(argnodes[i], matches[0].paramtypes[i]);
                CheckArg(argnodes[i], matches[0].paramtypes[i]);
            } else {
                // Value cases receive an enum snapshot before later arguments.
                // A reference case retains the original storage instead.
                auto byreference = false;
                for (auto sp : c->dispatch)
                    byreference |= sp->argtypes[i]->kind == TY_REF;
                HoldValue(argnodes[i], byreference ? argvals[i] : DecayRef(argvals[i]));
            }
    }
    // Like an ordinary call's, the cases' rebinds follow the argument checks.
    for (auto sp : c->dispatch) ApplyCalleeRebinds(sp);
    // The cases are alternatives of one call, like a match's arms: the result
    // is only as long-lived, exact and writable as every case's result. A case
    // checked without recording a root for a result only returns null there or
    // never returns, and like such an arm it does not constrain the result.
    vector<Val> results;
    vector<bool> reached;
    for (auto spec : c->dispatch) {
        CallResult(c, spec, argvals);
        results.resize(lastcallrets.size());
        reached.resize(lastcallrets.size());
        for (size_t r = 0; r < lastcallrets.size(); r++) {
            auto reaches = spec->inprogress ||
                           (r < spec->retroots.size() && spec->retroots[r].set);
            results[r] = MergeVals(results[r], reached[r], lastcallrets[r], reaches, c, true,
                                   nullptr, nullptr);
            reached[r] = reached[r] || reaches;
        }
    }
    lastcallrets = results;
    return results.empty() ? VoidVal() : results[0];
}

// A slice parameter takes a copy of the slice a reference argument points at
// (§4.1), so the argument's provenance is that slice's (SlotView), however the
// reference names the variable holding it.
inline void TypeCheck::LoadSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes) {
    for (size_t i = 0; i < argvals.size() && i < ptypes.size(); i++) {
        auto &av = argvals[i];
        if (ptypes[i]->kind == TY_SLICE && IsPlainRef(av.type) &&
            av.type->ref->sub->kind == TY_SLICE)
            av.SetProv(SlotView(av, av.type->ref->sub));
    }
}

// ------------------------------------------------------------------
// Specialization: find or create the FnSpec for a resolved call and
// check its body (once) in call-graph order.

inline FnSpec *TypeCheck::GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode) {
    auto sf = mi.sf;
    // A nested function is specialized where it is declared (§7.5). A call
    // that reaches the declaration ahead of the declaring body (a nested
    // function declared earlier calling it) would have it name variables
    // not bound yet; one after its declaring scope ended is keyed apart.
    auto escaped = false;
    if (sf->isnested && mi.env && LexFrame(mi.env) >= 0) {
        auto it = declsiteof.find({ mi.env, sf });
        if (it == declsiteof.end())
            Error(callnode, cat(sf->name, " (declared at ", Where(sf->line),
                                ") is called before its declaration is reached (§7.5)"));
        escaped = ScopeEnded(*it->second);
    }
    vector<VarDef *> narrowedenv;
    for (auto v : ExternalOptionals(mi.env, &mi.fnvals))
        if (v->narrowed) narrowedenv.push_back(v);
    // Root classes: distinct roots of ref/slice args ordered by depth.
    vector<RootArg> roots(mi.paramtypes.size());
    vector<VarDef *> argroots(mi.paramtypes.size(), nullptr);
    vector<VarDef *> distinct;
    for (size_t i = 0; i < mi.paramtypes.size(); i++) {
        auto pt = mi.paramtypes[i];
        auto isrs = IsRefOrSlice(pt);
        // A by-value parameter holding references (§9.2's holder values)
        // is keyed by the root bounding its contents, as the spec's
        // "implicitly generic over those fields' roots" says.
        auto holder = !isrs && HoldsPlainRef(pt);
        if (!isrs && !holder) continue;
        auto r = CanonRoot(holder ? HolderRootOf(argvals[i]) : argvals[i].root);
        argroots[i] = r;
        auto &ra = roots[i];
        // A `const` parameter is read-only whatever the argument (§9.5).
        ra.writable = argvals[i].writable && !pt->cq;
        ra.reusable = argvals[i].reusable;
        ra.exact = holder ? argvals[i].holderset && argvals[i].holderexact
                          : argvals[i].rootexact;
        ra.heldexact = holder && ra.exact && !sf->isrec;
        ra.growshrink = IsGrowShrinkRoot(r);
        ra.viewslot = pt->kind == TY_REF && pt->ref->sub->kind == TY_SLICE && r && r->type &&
                      IsRefOrSlice(r->type);
        ra.byteview = argvals[i].byteview || (ra.viewslot && r->ref.byteview);
        // What the argument, or a slice it refers to, may point into beyond
        // what growshrink says of its root (Prov::intogs).
        ra.intogs = isrs && (argvals[i].intogs ||
                             (pt->kind == TY_REF && pt->ref->sub->kind == TY_SLICE &&
                              GrowShrinkTaint(SlotView(argvals[i], pt->ref->sub),
                                              pt->ref->sub)));
        if (ra.exact) ra.pool = PoolOf(r);
        if (!r) {
            ra.cls = 0;
        } else {
            auto idx = -1;
            // Sharing a class says the two arguments point into the same
            // array, which an inexactly rooted one does not establish: it
            // names a scope its pointee outlives, not the storage that
            // owns it (§9.5). Such an argument gets a class to itself, one
            // an exact argument with the same root does not join either, so
            // a class is always one array whatever the call site.
            if (ra.exact)
                for (size_t j = 0; j < i; j++)
                    if (argroots[j] == r && roots[j].exact) idx = roots[j].cls - 1;
            if (idx < 0) {
                // Keep distinct ordered by the depths the classes take in
                // the body, so classes mean outlives-rank there.
                auto ins = distinct.size();
                while (ins > 0 && ClassDepth(distinct[ins - 1]) > ClassDepth(r)) ins--;
                distinct.insert(distinct.begin() + ins, r);
                for (auto &rr : roots) if (rr.cls > (int)ins) rr.cls++;
                idx = (int)ins;
            }
            ra.cls = idx + 1;
        }
    }
    // Class numbers alone cannot tell equal depths from a strict order, or a
    // global from a local, and a body that sees a lexical environment
    // compares its classes with that environment's variables too; the
    // depth keys say all of that (RootArg::depthkey). An extern function's
    // body is C, which compares none.
    auto reach = EnvReach(mi);
    vector<int> depthkeys(distinct.size());
    for (size_t k = 0, rank = 0; k < distinct.size() && !sf->isextern; k++) {
        auto d = ClassDepth(distinct[k]);
        if (d <= reach) {
            depthkeys[k] = d;
            continue;
        }
        if (k == 0 || ClassDepth(distinct[k - 1]) != d) rank++;
        depthkeys[k] = -(int)rank;
    }
    for (auto &ra : roots) {
        if (!ra.cls) continue;
        ra.depth = ClassDepth(distinct[ra.cls - 1]);
        ra.depthkey = depthkeys[ra.cls - 1];
    }
    // A class of a parameter names whatever that parameter does, so it is as
    // concrete as the parameter (`via`, settled after checking), provided
    // nothing beside it here can be the same array. The classes must all be
    // parameters of one specialization P: this call is in P's body, in a
    // function nested in P, or in a function value written in P, and all of
    // them see the classes of one activation of P. A variable of P or of a
    // function nested in P lives inside that activation, so it is a different
    // array from each of them; a global, a variable of P's lexical ancestors,
    // another synthetic root or a class of a second specialization may be the
    // same array as one of them.
    vector<pair<FnSpec *, int>> classes(roots.size(), { nullptr, -1 });
    for (size_t i = 0; i < roots.size(); i++)
        for (auto fi = (int)frames.size() - 1; argroots[i] && !classes[i].first && fi >= 0; fi--)
            for (size_t j = 0; frames[fi].spec && j < frames[fi].spec->params.size(); j++)
                if (frames[fi].spec->params[j]->ref.root == argroots[i]) {
                    classes[i] = { frames[fi].spec, (int)j };
                    break;
                }
    auto within = [](FnSpec *s, FnSpec *p) {
        for (; s; s = s->lexparent) if (s == p) return true;
        return false;
    };
    auto fat = [&](size_t i) {
        auto pt = mi.paramtypes[i];
        return argroots[i] && pt->kind == TY_REF && pt->ref->lenstorage < 0 &&
               ClassOf(pt->ref->sub) == SC_RESIZABLE;
    };
    FnSpec *owner = nullptr;
    auto external = false;
    for (size_t i = 0; i < roots.size(); i++) {
        if (!fat(i) || !classes[i].first) continue;
        external = external || (owner && classes[i].first != owner);
        owner = classes[i].first;
    }
    for (size_t i = 0; i < roots.size(); i++) {
        auto r = argroots[i];
        if (owner && fat(i) && !classes[i].first && !(r->ownerspec && within(r->ownerspec, owner)))
            external = true;
    }
    for (size_t i = 0; i < roots.size(); i++) {
        auto r = argroots[i];
        if (!r) continue;
        if (classes[i].first) {
            roots[i].concrete = !external;
            roots[i].via.push_back(classes[i]);
        } else {
            roots[i].concrete = (r->isglobal || r->ownerspec) &&
                                (!owner || (!external && r->ownerspec && within(r->ownerspec, owner)));
        }
    }
    for (auto spec : sf->specs) {
        if (spec->lexparent != mi.env || spec->escaped != escaped) continue;
        if (!spec->inprogress && spec->narrowedenv != narrowedenv) continue;
        if (!TypeArgsEq(spec->argtypes, mi.paramtypes)) continue;
        // A type argument no parameter type mentions (`size<u8>()`) shows
        // only in the bindings.
        if (!BindingsEq(spec->bindings, mi.bindings)) continue;
        if (spec->litparams != mi.litparams) continue;
        if (spec->fnvals.size() != mi.fnvals.size()) continue;
        auto fvok = true;
        for (size_t i = 0; i < mi.fnvals.size(); i++)
            fvok &= spec->fnvals[i].second == mi.fnvals[i].second;
        if (!fvok) continue;
        auto rootsok = spec->roots == roots;
        auto depthsok = rootsok;
        for (size_t i = 0; depthsok && i < roots.size(); i++)
            depthsok = spec->roots[i].depthkey == roots[i].depthkey;
        // A back edge must reuse the in-progress spec whatever the roots
        // (§7.8): inside a cycle, references rooted at cycle locals may
        // not be stored or returned, so their identity is irrelevant, and
        // the pool parameters that may be stored are checked below to be
        // the same ones the entry call passed.
        if (!depthsok && !spec->inprogress) continue;
        // A long-distance return was checked against a concrete enclosing
        // specialization. Reusing this body under another one would keep
        // its old return types and roots, even when its own arguments are
        // identical (a parameterless helper returning a literal, for example).
        auto needsok = true;
        for (auto target : spec->needs) {
            for (auto i = (int)frames.size() - 1; i >= 0; i--) {
                if (frames[i].sf != target->sf || frames[i].isfunval) continue;
                needsok &= frames[i].spec == target;
                break;
            }
        }
        if (!needsok && !spec->inprogress) continue;
        // Exactness and concreteness are not part of the key, so what the
        // specialization records is what every call site that reaches it
        // agrees on. A back edge with other classes than the key's passes
        // arrays the key's classes do not describe.
        for (size_t i = 0; i < roots.size() && i < spec->roots.size(); i++) {
            auto &sr = spec->roots[i];
            sr.exact = sr.exact && roots[i].exact;
            sr.concrete = sr.concrete && roots[i].concrete && rootsok;
            for (auto &v : roots[i].via)
                if (find(sr.via.begin(), sr.via.end(), v) == sr.via.end()) sr.via.push_back(v);
        }
        if (spec->inprogress) {
            for (auto v : spec->narrowedenv)
                if (!v->narrowed)
                    Error(callnode, cat("recursive call requires optional ", v->name,
                                        " to remain narrowed (§3.8)"));
            ValidateCycle(spec, callnode);
            ValidatePoolArgs(spec, argvals, callnode);
        } else if (CycleHead(spec)->inprogress) {
            // A finished member of a cycle still being checked leads back
            // into it, as a back edge does.
            JoinCycle(spec, callnode);
        }
        vector<pair<SFunction *, FnSpec *>> path;
        for (auto &f : frames) path.push_back({ f.isfunval ? nullptr : f.sf, f.spec });
        spec->neededges.push_back({ callnode, std::move(path) });
        ValidateNeeds(spec, callnode);
        NoteLitArgs(spec, argvals, callnode);
        return spec;
    }
    // The specializations in progress are the ones this call path is
    // checking, each nested in the one before.
    auto nested = 0;
    for (auto s : sf->specs) nested += s->inprogress;
    if (nested >= MAXNESTEDSPECS) {
        string inst;
        DumpInstance(inst, sf, mi.paramtypes, mi.litparams, mi.bindings, mi.fnvals);
        Error(callnode, cat("instantiating ", inst, " would put more than ", MAXNESTEDSPECS,
                            " specializations of ", sf->qname, " in progress on this call path: "
                            "a recursive call must reach a finite set of instantiations (§7.8)"));
    }
    auto spec = ast.NewFnSpec();
    spec->sf = sf;
    spec->lexparent = mi.env;
    spec->escaped = escaped;
    spec->narrowedenv = narrowedenv;
    spec->argtypes = mi.paramtypes;
    spec->roots = roots;
    spec->litparams = mi.litparams;
    spec->fnvals = mi.fnvals;
    spec->bindings = mi.bindings;
    sf->specs.push_back(spec);
    NoteLitArgs(spec, argvals, callnode);
    CheckSpecBody(spec, &argvals, callnode->line);
    return spec;
}

// Only the callee's lexical environment is shared; ordinary caller locals
// are separate from a non-nested function. Globals are always shared.
inline vector<VarDef *> TypeCheck::ExternalOptionals(
    FnSpec *env, const vector<pair<string_view, FnValBind>> *fnvals) {
    vector<VarDef *> out;
    set<VarDef *> seenvars;
    set<FnSpec *> seenenvs;
    auto add = [&](VarDef *v) {
        if (v->type && v->type->kind == TY_REF && v->type->ref->optional &&
            seenvars.insert(v).second)
            out.push_back(v);
    };
    for (auto g : ast.globals) for (auto v : g->defs) add(v);
    // A function value reaches the environment it was written in, whichever
    // function it is handed to.
    function<void(FnSpec *)> addenv = [&](FnSpec *e) {
        if (!e || !seenenvs.insert(e).second) return;
        for (auto fi = LexFrame(e); fi >= 0; fi = frames[fi].lexframe) {
            auto end = fi + 1 < (int)frames.size() ? frames[fi + 1].varbase : (int)vars.size();
            for (auto i = frames[fi].varbase; i < end; i++) add(vars[i]);
        }
        for (auto sp = e; sp; sp = sp->lexparent)
            for (auto &fv : sp->fnvals) addenv(fv.second.env);
    };
    addenv(env);
    if (fnvals) for (auto &fv : *fnvals) addenv(fv.second.env);
    return out;
}

inline void TypeCheck::ApplyCalleeRebinds(FnSpec *spec) {
    // A recursive body's summary may be incomplete until the cycle has
    // finished checking.
    auto changed = spec->reboundoptionals;
    if (spec->inprogress)
        for (auto v : InProgressRebinds(spec)) changed.insert(v);
    auto caller = CurRealFrame().spec;
    for (auto v : changed) {
        v->narrowed = nullptr;
        if (caller && v->ownerspec != caller) caller->reboundoptionals.insert(v);
    }
}

// What a call to a specialization still being checked may rebind: the
// optionals it reaches that some `.=` names in code the call can run. That
// code is found by name, without checking it: the body; the function values
// bound to it, to its lexical parents, and to the environments those values
// were written in; every function any of it names; the field defaults that
// literals and default<T>() fill in; and the format overloads that printing
// calls.
inline vector<VarDef *> TypeCheck::InProgressRebinds(FnSpec *spec) {
    set<string_view> names, callees = { "format" };
    set<Node *> seen;
    auto leaf = [](string_view name) { return SplitName(name, {}).leaf; };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !seen.insert(n).second) return;
        if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN)
            if (auto id = Is<Ident>(a->lval)) names.insert(leaf(id->name));
        // A named function reaches a call through any expression that yields
        // it, a block's tail or a break's value as much as an argument, so
        // every name may be a callee.
        if (auto id = Is<Ident>(n)) callees.insert(leaf(id->name));
        if (auto c = Is<Call>(n))
            if (auto d = Is<Dot>(c->callee)) callees.insert(d->name);
        n->Children(walk);
    };
    walk(spec->sf->body);
    // Field defaults run where no code names them. Every instance checks a
    // copy of its declaration's default expressions.
    for (auto st : ast.structs)
        for (auto &f : st->fields) walk(f.defaultval);
    for (auto en : ast.enums)
        for (auto &v : en->variants)
            for (auto &f : v.fields) walk(f.defaultval);
    set<FnSpec *> envs;
    function<void(FnSpec *)> addenv = [&](FnSpec *e) {
        if (!e || !envs.insert(e).second) return;
        for (auto sp = e; sp; sp = sp->lexparent)
            for (auto &fv : sp->fnvals) {
                if (fv.second.fv) walk(fv.second.fv->body);
                if (fv.second.named) walk(fv.second.named->body);
                addenv(fv.second.env);
            }
    };
    addenv(spec);
    for (size_t known = 0; known != callees.size();) {
        known = callees.size();
        for (auto sf : ast.functions) if (callees.count(sf->name)) walk(sf->body);
    }
    vector<VarDef *> out;
    if (names.empty()) return out;
    for (auto v : ExternalOptionals(spec->lexparent, &spec->fnvals))
        if (names.count(v->name)) out.push_back(v);
    return out;
}

// A literal parameter's adaptation to a type (§7.7), recorded on the
// specialization that owns the parameter.
inline void TypeCheck::RecordLitAdapt(const Val &v, TypeExpr *t, Line at) {
    if (!litrecord || !v.unsizedparam) return;
    auto vd = v.unsizedparam;
    auto spec = vd->ownerspec;
    if (!spec) return;
    for (size_t i = 0; i < spec->params.size(); i++) {
        if (spec->params[i] != vd) continue;
        for (auto &la : spec->litadapts)
            if (la.param == (int)i && TypeEq(la.type, t)) return;
        spec->litadapts.push_back(LitAdapt { (int)i, t, at });
    }
}

// The literal arguments of a call: a literal is checked against the
// parameter's adaptations once the program is checked; a literal
// parameter passed on adds the callee's adaptations to its own.
inline void TypeCheck::NoteLitArgs(FnSpec *spec, vector<Val> &argvals, Node *at) {
    for (auto li : spec->litparams) {
        if (li >= (int)argvals.size()) continue;
        auto &av = argvals[li];
        if (av.ck != CK_NONE) {
            litchecks.push_back(LitCheck { spec, li, av, at });
        } else if (av.unsized && av.unsizedparam && av.unsizedparam->ownerspec) {
            auto from = av.unsizedparam->ownerspec;
            for (size_t i = 0; i < from->params.size(); i++)
                if (from->params[i] == av.unsizedparam)
                    from->litflows.push_back(LitFlow { (int)i, spec, li });
        }
    }
}

inline void TypeCheck::VerifyLiterals() {
    for (auto &lc : litchecks) {
        set<pair<FnSpec *, int>> seen;
        vector<pair<FnSpec *, int>> todo { { lc.spec, lc.param } };
        while (!todo.empty()) {
            auto [spec, param] = todo.back();
            todo.pop_back();
            if (!seen.insert({ spec, param }).second) continue;
            for (auto &la : spec->litadapts) {
                if (la.param != param || la.type->kind != TY_INT || lc.lit.ck != CK_INT) continue;
                if (FitsIntStorage(lc.lit.ival, lc.lit.uns, la.type->intstorage)) continue;
                Error(lc.at, cat("argument ", (int64_t)lc.param + 1, " of ", lc.spec->sf->name,
                                 ": constant ", ConstStr(lc.lit), " does not fit ",
                                 TypeStr(la.type), ", which parameter ",
                                 spec->sf->params[param].name, " takes at ", Where(la.at),
                                 " (§7.7)"));
            }
            for (auto &lf : spec->litflows)
                if (lf.param == param) todo.push_back({ lf.to, lf.toparam });
        }
    }
}

inline void TypeCheck::ValidateCycle(FnSpec *spec, Node *callnode) {
    if (!spec->sf->isrec)
        Error(callnode, cat("recursive call cycle through ", spec->sf->name,
                            ", which is not declared `recursive fn` (§7.8)"));
    auto fi = FrameOfSpec(spec);
    if (fi < 0) fi = 0;
    for (auto i = fi; i < (int)frames.size(); i++) {
        if (!frames[i].spec || !frames[i].sf || frames[i].isfunval) continue;
        for (auto &p : frames[i].sf->params)
            if (!p.type)
                Error(callnode, cat("function ", frames[i].sf->name, " is in a recursive "
                                    "cycle and needs fully explicit parameter types"));
    }
    JoinCycle(spec, callnode);
    // A cycle function without an explicit return type is committed to
    // returning nothing at the back edge; a later `return v` then errors
    // with a mismatch (return-type inference cannot cross the back edge).
    if (!spec->retsknown) spec->retsknown = true;
}

inline FnSpec *TypeCheck::CycleHead(FnSpec *s) {
    while (s->cyclelink) s = s->cyclelink;
    return s;
}

// A call into the recursive cycle spec belongs to (§7.8): a back edge, or a
// call reaching a finished member of a cycle whose outermost member is still
// in progress. Every frame from that member inward is inside a call into the
// cycle, or is making this one, so it joins the cycle. A non-fixed-size
// variable any of them has in scope keeps its data stack across that call,
// which would take a stack per activation: an error at the frame's call.
inline void TypeCheck::JoinCycle(FnSpec *spec, Node *callnode) {
    auto head = CycleHead(spec);
    auto fi = FrameOfSpec(head);
    if (fi < 0) fi = 0;
    auto last = (int)frames.size() - 1;
    for (auto i = fi; i <= last; i++) {
        auto &f = frames[i];
        // The frame's call into the cycle is the one the next frame checks;
        // a field default's frame records none, and is part of the call
        // below it.
        f.cyclecall = callnode->line;
        for (auto j = i + 1; j <= last; j++)
            if (frames[j].callline.line > 0) { f.cyclecall = frames[j].callline; break; }
        f.cyclecalls++;
        if (!f.spec || !f.sf || f.isfunval) continue;
        f.spec->incycle = true;
        if (auto h = CycleHead(f.spec); h != head) h->cyclelink = head;
    }
    for (auto i = fi; i <= last; i++) {
        auto end = i < last ? frames[i + 1].varbase : (int)vars.size();
        for (auto vi = frames[i].varbase; vi < end; vi++) {
            auto v = vars[vi];
            if (!v->type || ClassOf(v->type) == SC_FIXED) continue;
            if (v->isparam)
                Error(frames[i].cyclecall,
                      cat(FrameFnName(i), " calls into its recursive cycle while by-value "
                          "non-fixed-size parameter ", v->name, " is in scope, as it is for "
                          "the whole body (§7.8): take it by reference or as a slice"));
            Error(frames[i].cyclecall,
                  cat(FrameFnName(i), " calls into its recursive cycle while non-fixed-size "
                      "local ", v->name, " (declared at ", Where(v->line), ") is in scope "
                      "(§7.8): end its scope before the call"));
        }
    }
}

// The function whose body frame fi checks: a function value's is the one it
// is written in, a field default's the one constructing the value.
inline string_view TypeCheck::FrameFnName(int fi) {
    for (; fi >= 0; fi--)
        if (frames[fi].sf) return frames[fi].sf->name;
    return "global initialization";
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
            if (frames[i].sf == t->sf && !frames[i].isfunval) { found = i; break; }
        if (found < 0)
            Error(callnode, cat("call to ", spec->sf->name, " requires an enclosing call "
                                "of ", t->sf->name, " (it does `return ... from ", t->sf->name,
                                "`)"));
        if (frames[found].spec != t)
            Error(callnode, "recursive call changes the enclosing specialization of a "
                            "long-distance return");
        for (auto i = found + 1; i < (int)frames.size(); i++) AddNeed(frames[i].spec, t);
    }
}

// Records a `return from` target on a spec, and on every path a call reached
// it by before the target was known.
inline void TypeCheck::AddNeed(FnSpec *s, FnSpec *t) {
    if (!s || !s->needs.insert(t).second) return;
    for (auto &e : s->neededges) {
        auto &path = e.second;
        auto found = -1;
        for (auto i = (int)path.size() - 1; i >= 0; i--)
            if (path[i].first == t->sf) { found = i; break; }
        if (found < 0)
            Error(e.first, cat("call to ", s->sf->name, " requires an enclosing call of ",
                               t->sf->name, " (it does `return ... from ", t->sf->name, "`)"));
        if (path[found].second != t)
            Error(e.first, "recursive call changes the enclosing specialization of a "
                           "long-distance return");
        for (auto i = found + 1; i < (int)path.size(); i++) AddNeed(path[i].second, t);
    }
}

// The §7.8 cycle return-root analysis, handed the one piece of checker
// state it cannot derive from the syntax: the root a variable holds.
inline CycleRoots TypeCheck::Cycles() {
    return CycleRoots(ast, cyclecache, cycleroot, [this](VarDef *vd, bool isref) {
        return CanonRoot(isref ? RefRootOf(vd) : vd);
    }, [this](string_view name) { return LookupVar(name, CurNs()); });
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
    spec->inprogress = false;
}

// The depth a parameter class takes in the body a call here enters: its
// call-site root's. A temporary of the calling statement outlives every
// activation that statement starts, so it takes the body's own outermost
// scope, the one past this: the callee may keep it in its locals, but not
// in the caller's storage.
inline int TypeCheck::ClassDepth(VarDef *r) {
    return IsTemp(r) ? CurDepth() + 1 : Depth(r);
}

// How deep the variables a body can name outside itself may be, besides the
// globals: those of the lexical environment it is nested in and of the ones
// its function values were written in. Each of those is a frame on the call
// path, and its variables lie within the scopes that frame has open.
inline int TypeCheck::EnvReach(const MatchInfo &mi) {
    auto reach = 0;
    auto add = [&](FnSpec *env) {
        if (!env) return;
        auto fi = LexFrame(env);
        auto open = fi < 0 || fi + 1 == (int)frames.size() ? CurDepth() : frames[fi + 1].scopebase;
        reach = max(reach, open);
    };
    add(mi.env);
    for (auto &fv : mi.fnvals) add(fv.second.env);
    return reach;
}

inline void TypeCheck::CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline) {
    // A body is checked inside the call that first reaches it, so the native
    // stack holds one of these per call on the compile-time call path.
    if (StackLow()) {
        auto depth = 0;
        for (auto &f : frames) depth += f.sf && !f.isfunval;
        Error(callline, cat("compile-time call path too deep for the compiler's stack (",
                            depth, " nested calls)"));
    }
    auto sf = spec->sf;
    if (sf->isextern) { CheckExternSpec(spec); return; }
    spec->inprogress = true;
    spec->eventstart = storeevents.size();
    // A caller learns nothing about optionals from where this body ends: its
    // early returns never get there, and a cached body is not checked again.
    // What it rebinds reaches callers through ApplyCalleeRebinds.
    vector<pair<VarDef *, TypeExpr *>> outernarrowed;
    for (auto v : vars) outernarrowed.push_back({ v, v->narrowed });
    for (auto g : ast.globals) for (auto v : g->defs) outernarrowed.push_back({ v, v->narrowed });
    Frame f;
    f.sf = sf;
    f.spec = spec;
    f.lexspec = spec;
    f.lexframe = spec->lexparent ? LexFrame(spec->lexparent) : -1;
    if (f.lexframe >= 0)
        if (auto it = declsiteof.find({ spec->lexparent, sf }); it != declsiteof.end())
            f.decl = it->second;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = callline;
    frames.push_back(f);
    auto savepending = std::move(pendingshrinks);
    pendingshrinks.clear();
    // The caller's constructions are its own too: the call site logs this
    // body's growths against them from the summary.
    auto savegrowlog = std::move(growlog);
    growlog.clear();
    // The caller's pending temporaries and value region are its own; the
    // call site replays this body's shrinks against them.
    auto saveheld = std::move(heldtemps);
    heldtemps.clear();
    auto saveinvalue = invalue;
    invalue = false;
    auto savereach = reachable;
    DestScope ds(*this, Dest {});
    // Whatever the call's result is for -- the slot it lands in, the return
    // it is a value of -- is the caller's business: this body's own
    // statements say where their values go, and its tail is a return of its
    // own, whose constness is inferred (§9.5).
    SlotScope ss(*this, false);
    FlagScope rs(inreturn, false);
    reachable = true;
    PushScope(SK_FN);
    // Parameters. For reference/slice parameters, a synthetic root
    // VarDef per call-site root class carries the caller-side depth.
    vector<VarDef *> classroots(spec->roots.size() + 1, nullptr);
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto &p = sf->params[i];
        auto pt = spec->argtypes[i];
        ValidateType(pt, sf->line, VT_PARAM);
        auto vd = NewVar(p.name, pt, sf->line, p.isvar);
        vd->isparam = true;
        vd->assigned = true;
        for (auto li : spec->litparams) if (li == (int)i) vd->unsized = true;
        if (IsRefOrSlice(pt)) {
            auto &ra = spec->roots[i];
            if (ra.cls == 0) {
                vd->ref.root = nullptr;  // Static data.
            } else {
                if (!classroots[ra.cls]) {
                    auto rv = ast.NewVarDef();
                    rv->name = p.name;
                    rv->depth = ra.depth;
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
                classroots[ra.cls]->contentbyteview |= ra.byteview;
                classroots[ra.cls]->viewslot |= ra.viewslot;
            }
            vd->refrootknown = true;
            // Every member of a class points into one array (see
            // GetOrCreateSpec), so within this body the class names that
            // array. Whether it is the array some *other* class names is a
            // different question, and only ra.exact answers it.
            vd->ref.rootexact = true;
            vd->ref.writable = ra.writable;
            vd->ref.reusable = ra.reusable;
            vd->ref.byteview = ra.byteview;
            if (ra.intogs) vd->ref.intogs = ra.cls ? classroots[ra.cls] : vd;
        } else if (HoldsPlainRef(pt)) {
            // A holder parameter: its contents are bounded by the class
            // root its call sites agreed on, and are that array exactly only
            // where they agreed its references all point into one
            // (RootArg::heldexact).
            auto &ra = spec->roots[i];
            VarDef *cr = nullptr;
            if (ra.cls != 0) {
                if (!classroots[ra.cls]) {
                    auto rv = ast.NewVarDef();
                    rv->name = p.name;
                    rv->depth = ra.depth;
                    rv->classfrom = argvals ? CanonRoot(HolderRootOf((*argvals)[i])) : nullptr;
                    rv->growshrink = ra.growshrink;
                    classroots[ra.cls] = rv;
                }
                classroots[ra.cls]->poolclass = false;
                cr = classroots[ra.cls];
            }
            vd->contentroot = cr;
            vd->contentexact = ra.heldexact;
            vd->contentset = true;
            vd->contentbyteview = ra.byteview;
            // A returned holder maps back at the call site through it, and a
            // read-back out of the parameter, or out of anything its contents
            // were copied into, is bounded by it (RootCandidates).
            vd->ref.root = cr;
            vd->refrootknown = true;
            // Its contents are whatever the call site's value pointed at:
            // bounded by the class root, as an event of its own.
            Val hv;
            hv.root = cr;
            hv.rootexact = ra.heldexact;
            RecordStore(vd, hv, nullptr, false);
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
    // like `return tail`). A tail that produces on no path is a statement
    // instead, so a void function may end in one.
    BlockScope bs(*this, spec->body);
    CheckStmts(spec->body);
    if (spec->body->tail) {
        auto tail = spec->body->tail;
        auto asvalue = !(spec->retsknown && spec->rets.empty());
        if (IsValuelessTail(tail)) asvalue = false;
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
    pendingshrinks = std::move(savepending);
    growlog = std::move(savegrowlog);
    heldtemps = std::move(saveheld);
    invalue = saveinvalue;
    frames.pop_back();
    for (auto [v, n] : outernarrowed) v->narrowed = n;
    reachable = savereach;
    spec->inprogress = false;
    // The pairs a failed assumption brings back go into records the cycle's
    // calls are mapped from again, so they are settled first.
    if (assumedopen.erase(spec) && assumedopen.empty()) SettleAssumedShrinks();
    if (spec->incycle && !cyclesites.empty() && !CycleOpen()) ResolveCycleSites();
}

// Shared by `return` statements and body tails: agree the values with
// the target's return types (setting them on first sight), and record
// reference roots for the caller to map (§9.2).
inline void TypeCheck::RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at) {
    // A single call forwards all its return values.
    vector<TypeExpr *> types;
    for (auto &v : vals) {
        if (v.type == fntype) Error(at, "function values cannot be returned (§7.6)");
        types.push_back(v.type);
    }
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
        auto isrs = IsRefOrSlice(rt);
        auto holder = !isrs && HoldsPlainRef(rt);
        if (!isrs && !holder) continue;
        // A null return names no root: it agrees with every other return.
        if (vals[i].isnull) continue;
        // A holder value's contents must outlive the caller like a
        // returned reference would.
        auto root = CanonRoot(holder ? HolderRootOf(vals[i]) : vals[i].root);
        // What the result promises its callers is about that same pointee:
        // a holder variable's own storage is exact, its contents may not be.
        auto exact = holder ? vals[i].holderset && vals[i].holderexact : vals[i].rootexact;
        // Anything whose storage the callee's frame owns dies on return;
        // reference parameters' pointee roots are synthetic per-class
        // VarDefs (no ownerspec), so they pass and map at the call site.
        if (root && root->ownerspec == tspec)
            Error(at, cat("returning a reference rooted in ", root->name,
                          ", which dies with this function (§9.2)"));
        if (IsTemp(root))
            Error(at, "returning a reference into a temporary");
        if (root && root == cycleroot)
            Error(at, "returning the result of a recursive call whose returned "
                      "reference's root the cycle's returns do not determine (§7.8)");
        RetAlt ret { root, exact, vals[i].writable, isrs ? vals[i].intogs : nullptr,
                     vals[i].cyclelocal, vals[i].hidesclass, isrs && vals[i].slotread };
        auto &rr = tspec->retroots[i];
        if (rr.seeded) {
            // Where a root the back edges were not given joins them, a store
            // of it meets its own storage too.
            auto gs = holder ? IntoGrowShrink(vals[i], root, rt, true)
                             : StoredIntoGrowShrink(vals[i], root, rt, false) != nullptr;
            auto local = !CycleStorable(root) || ret.cyclelocal;
            if (auto bad = Cycles().ReturnConflict(tspec, i, ret, gs, local); !bad.empty())
                Error(at, bad);
        }
        // A result may come from any return: every root one gives is kept,
        // with the guarantees that hold on all paths to it.
        auto known = false;
        for (auto &a : rr.alts) {
            if (a.root != root) continue;
            a.exact = a.exact && ret.exact;
            a.writable = a.writable && ret.writable;
            if (!a.intogs) a.intogs = ret.intogs;
            a.cyclelocal = a.cyclelocal || ret.cyclelocal;
            a.hidesclass = a.hidesclass || ret.hidesclass;
            a.slotread = a.slotread && ret.slotread;
            known = true;
        }
        if (!known) rr.alts.push_back(ret);
        rr.byteview = rr.byteview || vals[i].byteview;
        rr.set = true;
    }
}

// One root a result may have, as this call sees it: a parameter's class root
// maps back to the argument's root -- at a back edge, which reuses the body
// whatever it passes (§7.8), to every argument the class's parameters get,
// merged; anything else is itself.
inline Val TypeCheck::RetAltVal(FnSpec *spec, const RetAlt &alt, vector<Val> &argvals,
                                TypeExpr *t, Node *at) {
    Val m;
    m.type = t;
    m.root = alt.root;
    m.rootexact = alt.exact && alt.root != nullptr;   // Static data is no array to name.
    m.writable = alt.writable;
    m.intogs = alt.intogs;
    m.cyclelocal = alt.cyclelocal;
    m.hidesclass = alt.hidesclass;
    m.slotread = alt.slotread;
    if (!alt.root || alt.root->isglobal || alt.root->ownerspec) return m;
    auto v = m;
    auto first = true;
    for (size_t p = 0; p < spec->params.size() && p < argvals.size(); p++) {
        if (spec->params[p]->ref.root != alt.root) continue;
        auto ph = !IsRefOrSlice(spec->argtypes[p]);
        auto &a = argvals[p];
        auto x = m;
        x.root = CanonRoot(ph ? HolderRootOf(a) : a.root);
        x.rootexact = alt.exact && (ph ? a.holderexact : a.rootexact);
        x.rootfrom = a.rootfrom;
        x.writable = alt.writable && a.writable;
        if (!ph) {
            if (!x.intogs) x.intogs = a.intogs;
            x.cyclelocal = x.cyclelocal || a.cyclelocal;
            x.hidesclass = x.hidesclass || a.hidesclass;
        }
        v = first ? x : MergeVals(v, true, x, true, at, true, nullptr, nullptr);
        first = false;
        if (!spec->inprogress) break;
    }
    return v;
}

inline Val TypeCheck::CallResult(Call *c, FnSpec *spec, vector<Val> &argvals) {
    c->rettypes = spec->rets;
    lastcallrets.clear();
    for (size_t i = 0; i < spec->rets.size(); i++) {
        Val v;
        v.type = spec->rets[i];
        auto holder = !IsRefOrSlice(v.type) && HoldsPlainRef(v.type);
        if (IsRefOrSlice(v.type) || holder) {
            RetRoot none;
            auto &ri = i < spec->retroots.size() ? spec->retroots[i] : none;
            // A back edge's target is still being checked: it maps the roots
            // its returns were predicted to give (§7.8), and where there are
            // none to map, it outlives nothing, which is not static data.
            auto backedge = spec->inprogress;
            if (backedge && (!ri.seeded || ri.predlost || ri.pred.empty())) {
                v.root = cycleroot;
            } else {
                // Each root a return gives, merged as branches are (§9.2).
                auto first = true;
                for (auto &alt : backedge ? ri.pred : ri.alts) {
                    auto m = RetAltVal(spec, alt, argvals, v.type, c);
                    v = first ? m : MergeVals(v, true, m, true, c, true, nullptr, nullptr);
                    first = false;
                }
            }
            v.writable = v.writable && !v.type->cq;
            // A back edge's returns are not all known yet: any u8 view the
            // result holds, directly or inside a holder, may be a byte view.
            auto u8view = false;
            if (backedge) {
                vector<TypeExpr *> ps;
                if (holder) RefPointees(v.type, ps); else ps.push_back(PointeeOf(v.type));
                for (auto pt : ps) u8view |= pt && IsU8(pt);
            }
            v.byteview = v.byteview || ri.byteview || u8view;
            // A slot read where every return is one, as MergeVals keeps it;
            // a back edge's returns are not all checked yet.
            v.slotread = v.slotread && !holder && !backedge;
            if (holder) {
                // The bound travels as the holder root; the value itself is
                // a temporary.
                v.holderroot = v.root;
                v.holderexact = v.rootexact;
                v.holderset = true;
                v.root = TempRoot();
                v.rootexact = false;
                v.writable = false;
            }
        } else {
            v.root = TempRoot();
            v.writable = false;
        }
        if (spec->inprogress && i < spec->retroots.size()) {
            // What a back edge was given, the returns checked after it may
            // not take back (CycleRoots::ReturnConflict).
            auto &rr = spec->retroots[i];
            auto root = CanonRoot(HolderRootOf(v));
            if ((IsRefOrSlice(v.type) || holder) && root != cycleroot) {
                rr.used = true;
                rr.useddepth = min(rr.useddepth, Depth(root));
                rr.usedexact |= holder ? v.holderexact : v.rootexact;
                rr.usedwritable |= v.writable;
                rr.usedclean |= IsRefOrSlice(v.type) && !GrowShrinkTaint(v, v.type);
                rr.usedstorable |= CycleStorable(root) && !v.cyclelocal;
            }
        }
        lastcallrets.push_back(v);
    }
    if (spec->rets.empty()) return VoidVal();
    return lastcallrets[0];
}

inline void TypeCheck::CheckReturn(Return *r) {
    if (frames.back().isdefault) Error(r, "return outside of a function");
    TempScope temps(*this);
    // Which function does this exit? `from f` names one on the current
    // compile-time path; a plain return inside a function value exits the
    // lexically enclosing named function (§7.6, §7.9).
    auto tf = -1;
    if (!r->from.empty()) {
        // `f` resolves in this function's definition context: a nested
        // function in scope, else the overload set the name reaches from
        // here (docs/design/namespaces.md). The target is the innermost
        // enclosing call of any of those declarations, so an unrelated
        // function sharing the leaf name cannot catch the return.
        FnSpec *env = nullptr;
        vector<SFunction *> targets;
        if (auto nf = LookupLocalFnEnv(r->from, env)) targets.push_back(nf);
        else targets = ast.LookupFunctions(r->from, r->ns);
        if (targets.empty()) Error(r, cat("return from ", r->from, ": unknown function"));
        for (auto i = (int)frames.size() - 1; i >= 1 && tf < 0; i--) {
            if (frames[i].isfunval || !frames[i].sf) continue;
            for (auto t : targets) if (frames[i].sf == t) tf = i;
        }
        if (tf < 0)
            Error(r, cat("return from ", r->from, ": no enclosing call of ", r->from,
                         " on this compile-time call path"));
    } else if (frames.back().isfunval) {
        tf = FrameOfSpec(NamedSpec(frames.back().lexspec));
        if (tf < 0) Error(r, "cannot resolve the enclosing function of this value");
    } else {
        tf = (int)frames.size() - 1;
        if (!frames[tf].sf) Error(r, "return outside of a function");
    }
    auto tspec = frames[tf].spec;
    r->target = frames[tf].sf;
    r->targetspec = tspec;
    for (auto i = tf + 1; i < (int)frames.size(); i++) AddNeed(frames[i].spec, tspec);
    // Values.
    vector<Val> vals;
    auto expectone = [&](size_t i) -> TypeExpr * {
        return tspec->retsknown && i < tspec->rets.size() ? tspec->rets[i] : nullptr;
    };
    SlotScope ss(*this, false);   // A result's constness is the returns' (§9.5).
    {
        FlagScope rs(inreturn, true);
        if (r->vals.size() == 1) {
            auto v = CheckValue(r->vals[0], tspec->retsknown && tspec->rets.size() == 1
                                                ? tspec->rets[0] : nullptr);
            if (auto call = Is<Call>(r->vals[0]); call && call->rettypes.size() > 1) {
                vals = lastcallrets;  // Forward a multi-value call.
                // Each value meets its return type as a value of its own
                // would; codegen converts the ones that adapt.
                if (tspec->retsknown && vals.size() == tspec->rets.size()) {
                    for (size_t i = 0; i < vals.size(); i++) {
                        auto rt = tspec->rets[i];
                        RequireCopyable(vals[i], r->vals[0], rt);
                        if (!KeepsRef(vals[i], rt)) vals[i] = DecayRef(vals[i]);
                        MustFit(vals[i], r->vals[0], rt, false);
                    }
                }
            } else {
                if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
                vals.push_back(v);
            }
        } else {
            for (size_t i = 0; i < r->vals.size(); i++) {
                auto v = CheckValue(r->vals[i], expectone(i));
                if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
                HoldValue(r->vals[i], v);
                vals.push_back(v);
            }
        }
    }
    if (vals.empty() && tspec->retsknown && !tspec->rets.empty())
        Error(r, cat("function ", frames[tf].sf->name, " must return value(s)"));
    if (!vals.empty() || !tspec->retsknown) {
        // For long-distance returns, references, including those a returned
        // value holds, must not be rooted in frames that unwind;
        // conservatively require globals/static.
        if (tf != (int)frames.size() - 1 && !frames.back().isfunval) {
            for (auto &v : vals) {
                auto isrs = IsRefOrSlice(v.type);
                if (!isrs && !HoldsPlainRef(v.type)) continue;
                auto root = isrs ? v.root : HolderRootOf(v);
                if (root && !root->isglobal)
                    Error(r, "a long-distance return may only carry references to "
                             "globals or static data");
            }
        }
        RecordReturn(tspec, vals, r);
    }
    reachable = false;
}

// ------------------------------------------------------------------
// Thread entry points (§11.2): one specialization per thread_fn, whose
// body is checked like any other, reached from a spawn or from the
// driver rather than from a call.

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
    spec->roots.resize(spec->argtypes.size());
    sf->specs.push_back(spec);
    CheckSpecBody(spec, nullptr, l);
    return spec;
}

// ------------------------------------------------------------------
// Calling a function value F(a): the body is cloned and checked inline
// in the lexical environment it was written in (§7.6).

inline Val TypeCheck::CheckFunValCall(Call *c, const FnValBind &fb) {
    if (c->trailing)
        Error(c, "a function value call cannot itself take a trailing block");
    if (fb.named) {
        vector<SFunction *> cands = { fb.named };
        Node *nopre = nullptr;
        return ResolveCall(c, cands, fb.env, fb.named->name, nullptr, nopre);
    }
    auto fv = fb.fv;
    if (!c->tyargs.empty()) Error(c, "a block takes no type arguments");
    vector<Val> argvals;
    for (auto a : c->args) {
        auto v = CheckV(a, nullptr);
        a->exprtype = v.type;
        argvals.push_back(v);
    }
    vector<Param> params;
    if (fv->explicit_params) {
        params = fv->params;
        if (params.size() != argvals.size())
            Error(c, cat("this function value takes ", (int64_t)params.size(),
                         " argument(s), ", (int64_t)argvals.size(), " given"));
    } else if (argvals.size() == 1) {
        Param p;
        p.name = "it";
        params.push_back(p);
    } else if (!argvals.empty()) {
        Error(c, "a block with multiple arguments needs named parameters (x, y => ...)");
    }
    // Parameter types: annotations resolve in the defining environment.
    vector<TypeExpr *> ptypes;
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].type) {
            auto t = SubstEnv(params[i].type, fb.env);
            ValidateType(t, c->line, VT_PARAM);
            ptypes.push_back(t);
        } else {
            auto nt = NaturalType(argvals[i]);
            if (!nt || nt->kind == TY_VOID || nt == fntype)
                Error(c->args[i], "cannot infer a type for this argument");
            ptypes.push_back(nt);
        }
    }
    {
        DestScope ds(*this, Dest {});
        TempScope argscope(*this);
        for (size_t i = 0; i < ptypes.size(); i++) CheckArg(c->args[i], ptypes[i]);
    }
    // Check the body inline, with lookups chaining to the definer. The body
    // checked here is an environment of its own (FnSpec::isfunval), so what
    // it declares and specializes captures this clone's variables.
    auto named = NamedSpec(fb.env);
    auto env = ast.NewFunValEnv();
    env->sf = named ? named->sf : nullptr;
    env->lexparent = fb.env;
    Frame f;
    f.sf = named ? named->sf : CurRealFrame().sf;
    f.spec = CurRealFrame().spec;
    f.lexspec = env;
    f.lexframe = fb.env ? LexFrame(fb.env) : 0;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = c->line;
    f.isfunval = true;
    frames.push_back(f);
    PushScope(SK_FN);
    c->fvparams.clear();
    for (size_t i = 0; i < params.size(); i++) {
        auto vd = NewVar(params[i].name, ptypes[i], c->line, params[i].isvar);
        vd->assigned = true;
        if (IsRefOrSlice(ptypes[i])) {
            BindRefProvenance(vd, argvals[i]);
            if (ptypes[i]->cq) vd->ref.writable = false;
        }
        // A literal parameter handed to the block stays one inside it.
        if (argvals[i].unsized && !params[i].type && !params[i].isvar) {
            vd->unsized = true;
            vd->unsizedorigin = argvals[i].unsizedparam;
        }
        c->fvparams.push_back(vd);
    }
    c->fvtarget = env->sf;
    c->fvbody = (Block *)fv->body->Clone(ast);
    ValueRegion vr(*this, true);   // The body runs inside this call's expression.
    BlockScope bs(*this, c->fvbody);
    CheckStmts(c->fvbody);
    Val v = VoidVal();
    if (auto tail = c->fvbody->tail) {
        if (IsValuelessTail(tail)) CheckStmtExpr(tail);
        else v = CheckValue(c->fvbody->tail, nullptr);
    }
    c->fvbody->exprtype = v.type;
    PopScope();
    frames.pop_back();
    return v;
}

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
