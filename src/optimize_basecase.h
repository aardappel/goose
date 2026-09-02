// Goose compiler — base-case inlining for self-recursive functions, a pass of
// the optimizer (optimize.h) that runs inside its walk: the driver sets the
// pass up once per specialization, and the Call node's Opt offers it every
// call it did not inline.
//
// Cycle members never inline, so the leaf call of a recursive tree walk is
// always a real frame. Where a self-recursive body *starts* with
// `if c { return e; }` — or the `guard c else { return e; }` spelling of the
// same test, negated — and both c and e read nothing but the parameters,
// globals and their own bindings, every direct self-call `f(a...)` is
// rewritten to
//
//     { let p1 = a1; ...; if c[p := t] { e[p := t] } else { f(t...) } }
//
// which removes half the calls of a complete tree walk. Both arms construct
// into the call's own destination (the ordinary rule for `if` as a value), so
// reference and non-fixed results need no special case. Binding the arguments
// first keeps them evaluated once and in order; c is the exception — the
// recursing arm runs it a second time inside the callee — which is why it is
// restricted to pure reads.
#pragma once

namespace goose {

// Everything the pass needs from the optimizer — the Inliner copier, the
// inline thresholds, the node constructors, the VarDef facts and the walk
// itself — it reaches through o.
struct BaseCaseInliner {
    Optimizer &o;
    Ast &ast;

    FnSpec *basespec = nullptr;   // Spec the template below belongs to.
    Node *basecond = nullptr;     // Private copy of its base-case test, and
    Node *baseexpr = nullptr;     //   of the result (null: a valueless one).
    bool basenot = false;         // The `guard` form: the arms swap.

    BaseCaseInliner(Optimizer &_o) : o(_o), ast(_o.ast) {}

    // A value of this type is fixed-size (§1.1), so binding one to a local
    // adds no storage the function has to own. Conservative wherever the size
    // class needs an instantiation the checker's per-type cache does not have
    // to hand: missing one only skips a rewrite.
    static bool FixedType(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT:  return t->intstorage != IS_VARINT;
            case TY_REF:  return t->ref->lenstorage != IS_VARINT;
            case TY_FLT: case TY_BOOL: case TY_SLICE: return true;
            case TY_STRUCT: return t->struc->inst && t->struc->inst->sclass == SC_FIXED;
            case TY_ENUM: return !t->enu->varmode ||
                                 (t->enu->inst && t->enu->inst->varclass == SC_FIXED);
            case TY_ARRAY: return t->arr->akind == A_FIXED;
            default: return false;
        }
    }

    static int TreeSize(Node *n) {
        if (!n) return 0;
        auto k = 1;
        if (auto c = Is<Call>(n)) {
            k += TreeSize(c->callee);
            for (auto a : c->args) k += TreeSize(a);
            k += TreeSize(c->fvbody);
            return k;   // c->trailing is an unchecked template.
        }
        n->Children([&](Node *ch) { k += TreeSize(ch); });
        return k;
    }

    // The direct self-call sites the rewrite would copy the base case to.
    static int SelfCalls(Node *n, FnSpec *sp) {
        if (!n) return 0;
        auto k = 0;
        if (auto c = Is<Call>(n)) {
            if (c->spec == sp && c->builtin < 0 && c->dispatch.empty() &&
                Is<Ident>(c->callee)) k++;
            k += SelfCalls(c->callee, sp);   // A UFCS receiver can be one too.
            for (auto a : c->args) k += SelfCalls(a, sp);
            k += SelfCalls(c->fvbody, sp);
            return k;
        }
        n->Children([&](Node *ch) { k += SelfCalls(ch, sp); });
        return k;
    }

    // Is this part of the base case copyable to a call site? Free names must
    // be parameters or globals (no other local of ours is in scope there),
    // nothing may jump out of the subtree, and nothing may call back into the
    // cycle. `pure` restricts the node kinds to reads on top of that, for the
    // test, which the recursing arm evaluates a second time.
    bool BaseOK(Node *n, set<VarDef *> &bound, vector<SFunction *> &ibs, bool pure) {
        if (!n) return true;
        if (auto id = Is<Ident>(n)) {
            auto v = id->vdef;   // Null: a function reference.
            if (!v || v->isglobal || v->ownerspec != o.curspec) return true;
            return v->isparam || bound.count(v) > 0;
        }
        if (pure && !(Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) || Is<StrLit>(n) ||
                      Is<NullLit>(n) || Is<Unary>(n) || Is<Binary>(n) || Is<Dot>(n) ||
                      Is<AsCast>(n) || Is<Index>(n) || Is<SliceExpr>(n)))
            return false;
        // A jump out of the subtree would land in the caller instead of the
        // callee; a nested declaration is a separate tree left behind.
        if (Is<Break>(n) || Is<Continue>(n) || Is<FnDecl>(n) || Is<FunVal>(n)) return false;
        if (auto g = Is<Guard>(n); g && g->implicitexit) return false;
        if (auto r = Is<Return>(n)) {
            auto inside = false;
            for (auto sf : ibs) inside = inside || sf == r->target;
            if (!inside) return false;
        }
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) bound.insert(d);
        if (auto fl = Is<ForLoop>(n)) {
            bound.insert(fl->vdef);
            if (fl->idxdef) bound.insert(fl->idxdef);
        }
        if (auto m = Is<MatchExpr>(n)) for (auto &a : m->arms) if (a.binder) bound.insert(a.binder);
        if (auto ib = Is<InlineBlock>(n)) {
            ibs.push_back(ib->sf);
            auto ok = BaseOK(ib->body, bound, ibs, pure);
            ibs.pop_back();
            return ok;
        }
        if (auto c = Is<Call>(n)) {
            auto cyc = [](FnSpec *k) { return k && (k->incycle || k->sf->isrec); };
            if (cyc(c->spec)) return false;
            for (auto d : c->dispatch) if (cyc(d)) return false;
            for (auto p : c->fvparams) bound.insert(p);
            if (!BaseOK(c->callee, bound, ibs, pure)) return false;
            for (auto a : c->args) if (!BaseOK(a, bound, ibs, pure)) return false;
            return BaseOK(c->fvbody, bound, ibs, pure);
        }
        auto ok = true;
        n->Children([&](Node *ch) { ok = ok && BaseOK(ch, bound, ibs, pure); });
        return ok;
    }

    // Records a private copy of sp's base case when it has the shape above.
    // Called with o.curspec == sp before its body is optimized, so the copy
    // taken here is not disturbed by the rewriting that then happens in place.
    void Setup(FnSpec *sp) {
        basespec = nullptr;
        basecond = baseexpr = nullptr;
        basenot = false;
        if (!o.caninline || !sp->sf->isrec || !sp->body || sp->body->stmts.empty()) return;
        // Binding a non-fixed parameter to a temporary would give a cycle
        // member a non-fixed local of its own, which §7.8 forbids — and copy
        // the value at every level besides, where passing it costs nothing.
        for (auto pv : sp->params) if (!FixedType(pv->type)) return;
        Node *cond = nullptr;
        Block *arm = nullptr;
        auto neg = false;
        if (auto ife = Is<IfExpr>(sp->body->stmts[0])) {
            if (ife->elseb) return;
            cond = ife->cond;
            arm = ife->thenb;
        } else if (auto g = Is<Guard>(sp->body->stmts[0]); g && g->elseb) {
            cond = g->cond;
            arm = g->elseb;
            neg = true;
        } else return;
        if (arm->tail || arm->stmts.size() != 1) return;
        auto r = Is<Return>(arm->stmts[0]);
        if (!r || r->target != sp->sf || !r->from.empty()) return;
        if (r->vals.size() > 1 || r->vals.size() != sp->rets.size()) return;
        auto expr = r->vals.empty() ? nullptr : r->vals[0];
        set<VarDef *> bound;
        vector<SFunction *> ibs;
        if (!BaseOK(cond, bound, ibs, true)) return;
        if (!BaseOK(expr, bound, ibs, false)) return;
        // The inliner's size rule, against what this duplicates: the test and
        // the result, once per self-call site.
        auto sz = TreeSize(cond) + TreeSize(expr);
        auto sites = SelfCalls(sp->body, sp);
        if (!sites || !(sz < o.nc || sz * sites < o.ncu)) return;
        Inliner inl { o, ast, sp, sp, {}, {} };
        for (auto pv : sp->params) inl.vmap[pv] = pv;   // Parameters stay themselves.
        basecond = inl.Cp(cond);
        if (expr) baseexpr = inl.Cp(expr);
        basenot = neg;
        basespec = sp;
    }

    // Rewrites one self-call against the recorded base case.
    Node *Try(Call *c) {
        // The template is only good inside the body it was set up for: the
        // global initializers the driver optimizes after its loop are not it.
        if (!basespec || basespec != o.curspec) return nullptr;
        if (c->spec != basespec || c->builtin >= 0 || !c->dispatch.empty()) return nullptr;
        // A plain call only: a UFCS receiver arrives as the Dot's object, which
        // is not in general the same expression shape as the bound temporary.
        if (!Is<Ident>(c->callee)) return nullptr;
        auto K = basespec;
        if (c->args.size() != K->params.size()) return nullptr;
        Inliner inl { o, ast, K, o.curspec, {}, {} };
        vector<Node *> decls;
        for (size_t i = 0; i < K->params.size(); i++) {
            auto pv = K->params[i];
            auto arg = c->args[i];
            auto &f = o.facts[pv];
            if (Optimizer::AsLiteral(arg) && Optimizer::ScalarType(pv->type) &&
                f.writes == 0 && f.addrof == 0) {
                inl.subst[pv] = arg;  // Constant argument: substitute, no binding.
                continue;
            }
            auto nv = ast.NewVarDef();
            *nv = *pv;
            nv->ownerspec = o.curspec;
            nv->isparam = false;
            nv->captured = false;
            inl.vmap[pv] = nv;
            o.facts[nv] = f;
            auto vd = ast.New<VarDecl>(c->line, pv->isvar);
            vd->names.push_back(pv->name);
            vd->defs.push_back(nv);
            vd->inits.push_back(arg);
            vd->exprtype = ast.voidtype;
            decls.push_back(vd);
            // The recursing arm passes the binding on, so whichever arm runs,
            // the argument was evaluated exactly once and before the test.
            auto id = ast.New<Ident>(c->line, pv->name);
            id->vdef = nv;
            id->exprtype = pv->type;
            c->args[i] = id;
        }
        auto vt = c->exprtype ? c->exprtype : ast.voidtype;
        auto cond = o.Opt(inl.Cp(basecond));
        auto val = baseexpr ? o.Opt(inl.Cp(baseexpr)) : nullptr;
        auto arm = [&](Node *v) {
            auto b = ast.New<Block>(c->line);
            if (v) b->tail = v;
            b->exprtype = vt;
            return b;
        };
        // The guard spelling tests the continuing case, so its arms are the
        // other way round.
        auto ife = ast.New<IfExpr>(c->line, cond, arm(basenot ? (Node *)c : val),
                                   arm(basenot ? val : (Node *)c));
        ife->exprtype = vt;
        o.basecases++;
        if (decls.empty()) return ife;
        auto blk = ast.New<Block>(c->line);
        blk->stmts = std::move(decls);
        blk->tail = ife;
        blk->exprtype = vt;
        return blk;
    }
};

// The optimizer's two hooks into the pass. The object is made on first use:
// it needs the Inliner, so it cannot be a member of the Optimizer itself.
inline void Optimizer::SetupBaseCase(FnSpec *sp) {
    if (!basecase) basecase = make_unique<BaseCaseInliner>(*this);
    basecase->Setup(sp);
}

inline Node *Optimizer::TryBaseCase(Call *c) {
    return basecase ? basecase->Try(c) : nullptr;
}

}  // namespace goose
