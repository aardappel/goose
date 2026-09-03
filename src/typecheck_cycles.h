// Goose compiler — cycle return roots (§7.8). A back edge lands in a
// specialization whose own returns have not been checked yet, so the root its
// result should carry is recorded nowhere. Before checking a `recursive fn`
// body the checker therefore *predicts* every return root, from a purely
// syntactic scan of the returns of the function and of the functions those
// returns call, iterated to a fixpoint (a cycle's functions define each
// other's roots, so one pass does not settle it). The prediction seeds
// FnSpec::retroots, and every real return verifies it as it is checked. What
// the scan cannot pin down becomes the `cycleroot` sentinel: like a temporary
// it outlives nothing, so such a result may only be passed down, never stored
// or returned.
//
// The scan is purely syntactic, so it needs no checker state beyond the two
// things it cannot derive itself: the sentinel, and what root the checker has
// recorded for one of its variables (RootOfVar). Its per-function results are
// cached on SFunction (ast.h), so a fresh instance per use is correct.
#pragma once

namespace goose {

struct CycleRoots {
    // The checker's root for one of its own VarDefs: the pointee's root when
    // the variable holds a reference or slice, its own storage otherwise.
    using RootOfVar = function<VarDef *(VarDef *vd, bool isref)>;

    Ast &ast;
    VarDef *cycleroot;
    RootOfVar rootof;

    CycleRoots(Ast &_ast, VarDef *_cycleroot, RootOfVar _rootof)
        : ast(_ast), cycleroot(_cycleroot), rootof(_rootof) {}

    static RootDesc UnknownDesc() {
        RootDesc d;
        d.kind = RD_UNKNOWN;
        return d;
    }

    // All returns of one function must agree on the root (§9.2), so any
    // disagreement between two of them is already the top of this lattice.
    static RootDesc JoinDesc(const RootDesc &a, const RootDesc &b) {
        if (a.kind == RD_NONE) return b;
        if (b.kind == RD_NONE || a == b) return a;
        return UnknownDesc();
    }

    LocalBind *FindBind(SFunction *f, string_view name) {
        for (auto &b : f->locals) if (b.name == name) return &b;
        return nullptr;
    }

    LocalBind &BindSlot(SFunction *f, string_view name) {
        if (auto b = FindBind(f, name)) return *b;
        f->locals.push_back(LocalBind { name });
        return f->locals.back();
    }

    void EnsureBinds(SFunction *f) {
        if (f->bindsscanned) return;
        f->bindsscanned = true;
        if (f->body) CollectBinds(f, f->body);
        for (auto &p : f->params)
            if (auto b = FindBind(f, p.name)) b->opaque = true;
        for (auto &b : f->locals)
            if (!b.declared || ast.globalmap.count(b.name)) b.opaque = true;
    }

    void CollectBinds(SFunction *f, Node *n) {
        if (auto vd = Is<VarDecl>(n)) {
            for (size_t i = 0; i < vd->names.size(); i++) {
                auto &b = BindSlot(f, vd->names[i]);
                if (b.declared) b.opaque = true;   // Two declarations of one name.
                b.declared = true;
                b.type = vd->type;
                if (vd->inits.size() == vd->names.size()) b.binds.push_back(vd->inits[i]);
                else b.opaque = true;              // Multi-value or absent initializer.
            }
        } else if (auto a = Is<Assign>(n)) {
            if (a->op == T_ASSIGN || a->op == T_DOTASSIGN)
                if (auto id = Is<Ident>(a->lval)) BindSlot(f, id->name).binds.push_back(a->rhs);
        } else if (auto fl = Is<ForLoop>(n)) {
            BindSlot(f, fl->var).opaque = true;
            if (!fl->idxvar.empty()) BindSlot(f, fl->idxvar).opaque = true;
        } else if (auto m = Is<MatchExpr>(n)) {
            for (auto &arm : m->arms)
                if (!arm.pat.binder.empty()) BindSlot(f, arm.pat.binder).opaque = true;
        } else if (auto fv = Is<FunVal>(n)) {
            if (fv->explicit_params) for (auto &p : fv->params) BindSlot(f, p.name).opaque = true;
            else BindSlot(f, "it").opaque = true;
        } else if (auto fd = Is<FnDecl>(n)) {
            f->localfns.push_back(fd->sf->name);
        }
        n->Children([&](Node *c) { CollectBinds(f, c); });
    }

    // Mirrors Ident::Check: a global holding a reference or slice contributes
    // its pointee's root, any other global its own storage.
    VarDef *RootOfGlobal(VarDef *vd) {
        auto t = vd->type;
        return rootof(vd, t && (t->kind == TY_REF || t->kind == TY_SLICE));
    }

    // Only single-name globals: LookupVar resolves a multi-name declaration's
    // uses to its first VarDef, which the scan will not second-guess.
    RootDesc GlobalDesc(string_view name) {
        auto git = ast.globalmap.find(name);
        if (git == ast.globalmap.end()) return UnknownDesc();
        auto g = git->second;
        if (g->names.size() != 1 || g->defs.size() != 1) return UnknownDesc();
        RootDesc d;
        d.kind = RD_GLOBAL;
        d.glob = g->defs[0];
        return d;
    }

    // The root of the reference `n` evaluates to. `busy` breaks the recursion
    // through locals defined in terms of each other.
    RootDesc ScanExpr(SFunction *f, Node *n, vector<string_view> &busy, int depth) {
        if (!n || depth > 16) return UnknownDesc();
        if (Is<Dot>(n) || Is<Index>(n) || Is<SliceExpr>(n)) return ScanBase(f, n, busy, depth);
        if (auto u = Is<Unary>(n))
            return u->op == T_BITAND ? ScanBase(f, u->child, busy, depth) : UnknownDesc();
        if (auto c = Is<Call>(n)) return ScanCall(f, c, busy, depth);
        auto id = Is<Ident>(n);
        if (!id) return UnknownDesc();
        EnsureBinds(f);
        if (auto b = FindBind(f, id->name)) {
            // Only a reference or slice variable carries a root the caller can
            // name; anything else is rooted at storage this function owns.
            if (b->opaque || !b->type ||
                (b->type->kind != TY_REF && b->type->kind != TY_SLICE))
                return UnknownDesc();
            for (auto nm : busy) if (nm == id->name) return UnknownDesc();
            busy.push_back(id->name);
            RootDesc d;
            for (auto e : b->binds) {
                if (Is<NullLit>(e)) continue;   // Null binds no provenance.
                d = JoinDesc(d, ScanExpr(f, e, busy, depth + 1));
            }
            busy.pop_back();
            return d;
        }
        for (size_t i = 0; i < f->params.size(); i++) {
            if (f->params[i].name != id->name) continue;
            auto pt = f->params[i].type;
            if (!pt || (pt->kind != TY_REF && pt->kind != TY_SLICE)) return UnknownDesc();
            RootDesc d;
            d.kind = RD_PARAM;
            d.param = (int)i;
            return d;
        }
        return GlobalDesc(id->name);
    }

    // A path's root is its base's (a reference read out of a container is
    // rooted at the container, §9.5).
    RootDesc ScanBase(SFunction *f, Node *n, vector<string_view> &busy, int depth) {
        for (;;) {
            if (auto d = Is<Dot>(n)) { n = d->obj; continue; }
            if (auto ix = Is<Index>(n)) { n = ix->obj; continue; }
            if (auto se = Is<SliceExpr>(n)) { n = se->obj; continue; }
            break;
        }
        return ScanExpr(f, n, busy, depth + 1);
    }

    RootDesc ScanCall(SFunction *f, Call *c, vector<string_view> &busy, int depth) {
        // The argument list resolution builds: a UFCS receiver is argument 0.
        string_view name;
        vector<Node *> args;
        if (auto d = Is<Dot>(c->callee)) {
            name = d->name;
            args.push_back(d->obj);
        } else if (auto id = Is<Ident>(c->callee)) {
            name = id->name;
        } else {
            return UnknownDesc();
        }
        for (auto a : c->args) args.push_back(a);
        EnsureBinds(f);
        for (auto nm : f->localfns) if (nm == name) return UnknownDesc();
        auto fit = ast.functionmap.find(name);
        if (auto bd = LookupBuiltin(name)) {
            // A member builtin wins over a same-named function at a.f() sites
            // when the receiver is an array, which the scan cannot tell.
            if (fit != ast.functionmap.end()) return UnknownDesc();
            if ((bd->kind != B_PUSH && bd->kind != B_ALLOC_REF) || args.empty())
                return UnknownDesc();
            return ScanBase(f, args[0], busy, depth);
        }
        if (fit == ast.functionmap.end() || fit->second.size() != 1) return UnknownDesc();
        auto callee = fit->second[0];
        EnrollDescs(callee);
        if (callee->retdescs.empty()) return UnknownDesc();
        auto cd = callee->retdescs[0];
        if (cd.kind == RD_NONE || cd.kind == RD_GLOBAL) return cd;
        // The callee's result is rooted at one of its own parameters: follow
        // the argument this call site passes there.
        if (cd.kind == RD_PARAM && cd.param < (int)args.size())
            return ScanExpr(f, args[cd.param], busy, depth + 1);
        return UnknownDesc();
    }

    vector<SFunction *> descqueue;   // Functions in the running fixpoint.
    bool descchanged = false;

    // Bring a function into that fixpoint on first sight, so its own returns
    // get scanned too.
    void EnrollDescs(SFunction *f) {
        if (f->descstate) return;
        f->descstate = 1;
        f->retdescs.assign(f->has_rets ? f->rets.size() : 0, RootDesc {});
        descqueue.push_back(f);
        descchanged = true;
    }

    void ScanReturns(SFunction *f, Node *n, vector<RootDesc> &ds, bool &usable) {
        if (auto r = Is<Return>(n)) {
            // `return … from g` exits another frame entirely; only a `from`
            // naming this function targets it (its innermost activation).
            if (r->from.empty() || r->from == f->name) {
                if (r->vals.size() != ds.size()) {
                    usable = false;
                } else {
                    vector<string_view> busy;
                    for (size_t i = 0; i < ds.size(); i++)
                        ds[i] = JoinDesc(ds[i], ScanExpr(f, r->vals[i], busy, 0));
                }
            }
        }
        n->Children([&](Node *c) { ScanReturns(f, c, ds, usable); });
    }

    vector<RootDesc> ComputeDescs(SFunction *f) {
        vector<RootDesc> ds(f->has_rets ? f->rets.size() : 0);
        if (ds.empty()) return ds;
        // A nested function's free variables resolve outside its own body,
        // which this scan does not follow.
        if (f->isnested || f->isthread || !f->body) {
            for (auto &d : ds) d = UnknownDesc();
            return ds;
        }
        auto usable = true;
        ScanReturns(f, f->body, ds, usable);
        // A value-producing body tail is exactly `return tail` (§7.3).
        if (auto tail = f->body->tail) {
            auto fi = Is<IfExpr>(tail);
            auto asvalue = !(fi && !fi->elseb) && !Is<Guard>(tail) && !Is<Return>(tail);
            if (asvalue) {
                if (ds.size() != 1) {
                    usable = false;
                } else {
                    vector<string_view> busy;
                    ds[0] = JoinDesc(ds[0], ScanExpr(f, tail, busy, 0));
                }
            }
        }
        if (!usable) for (auto &d : ds) d = UnknownDesc();
        return ds;
    }

    // Iterate the closure reachable from f's returns until nothing moves; a
    // round cap keeps a pathological program from spinning, at the cost of
    // predicting nothing for it.
    void ReturnRootDescs(SFunction *f) {
        if (f->descstate == 2) return;
        descqueue.clear();
        EnrollDescs(f);
        auto settled = false;
        for (auto round = 0; round < 64 && !settled; round++) {
            descchanged = false;
            for (size_t i = 0; i < descqueue.size(); i++) {
                auto g = descqueue[i];
                auto nd = ComputeDescs(g);
                if (nd != g->retdescs) {
                    g->retdescs = nd;
                    descchanged = true;
                }
            }
            settled = !descchanged;
        }
        for (auto g : descqueue) {
            if (!settled) for (auto &d : g->retdescs) d = UnknownDesc();
            g->descstate = 2;
        }
        descqueue.clear();
    }

    // The prediction as a root of this specialization, with the exactness
    // that root carries (§9.5): a global owns its own storage, a parameter
    // stands for one call-site array only where that argument's root did. A
    // parameter's root class may legitimately be null (the call site passed
    // static data).
    VarDef *ResolveDesc(FnSpec *spec, const RootDesc &d, bool &exact) {
        exact = false;
        if (d.kind == RD_PARAM && d.param < (int)spec->params.size() &&
            d.param < (int)spec->argtypes.size()) {
            auto pt = spec->argtypes[d.param];
            if (pt->kind == TY_REF || pt->kind == TY_SLICE) {
                auto vd = spec->params[d.param];
                exact = vd->refrootknown && vd->refrootexact;
                return rootof(vd, true);
            }
        }
        if (d.kind == RD_GLOBAL) { exact = true; return RootOfGlobal(d.glob); }
        return cycleroot;
    }

    void Seed(FnSpec *spec) {
        auto anyref = false;
        for (auto rt : spec->rets)
            anyref |= rt->kind == TY_REF || rt->kind == TY_SLICE;
        if (!anyref) return;
        ReturnRootDescs(spec->sf);
        auto &ds = spec->sf->retdescs;
        for (size_t i = 0; i < spec->rets.size() && i < spec->retroots.size(); i++) {
            auto rk = spec->rets[i]->kind;
            if (rk != TY_REF && rk != TY_SLICE) continue;
            auto exact = false;
            spec->retroots[i] = i < ds.size() ? ResolveDesc(spec, ds[i], exact) : cycleroot;
            if (i < spec->retrootexact.size()) spec->retrootexact[i] = exact;
            spec->retrootseeded[i] = true;
        }
    }

    // The prediction the cycle's back edges were given must hold: what is
    // wrong with a checked return whose root contradicts it, empty when it
    // agrees. A sentinel prediction promised nothing to contradict.
    string ReturnConflict(FnSpec *spec, size_t i, VarDef *root, bool exact) {
        if (spec->checkedreturn || i >= spec->retroots.size() ||
            i >= spec->retrootseeded.size() || !spec->retrootseeded[i]) return {};
        auto seeded = spec->retroots[i];
        if (seeded == cycleroot) return {};
        if (seeded == root) {
            // A back edge may already have stored the result relatively on the
            // strength of the prediction's exactness (§3.9).
            if (!exact && i < spec->retrootexact.size() && spec->retrootexact[i])
                return cat("this return's reference only outlives ",
                           root ? root->name : string_view("static data"),
                           ", but the recursive cycle's returns give storage it owns "
                           "(§7.8); use one source");
            return {};
        }
        return cat("this return's reference is rooted at ",
                   root ? root->name : string_view("static data"),
                   ", but the recursive cycle's returns give ",
                   seeded ? seeded->name : string_view("static data"),
                   " (§7.8); use one source");
    }
};

}  // namespace goose
