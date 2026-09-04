// Goose compiler — accumulator tail-recursion elimination, one pass of the
// optimizer. optimize.h declares the entry point (Optimizer::TailRecurse)
// and calls it once per specialization; the pass itself lives here.
//
// A directly self-recursive specialization returning one integer, whose
// every result is either a base value or `E op self(args)` with `op`
// associative, becomes a loop over an accumulator: each tail call turns
// into `acc op= E`, a rebinding of the parameters and a `continue`, and
// every base result `X` into `acc op X`. Self-calls in non-tail position
// — the left half of `1 + check(l) + check(r)` — stay calls, so a tree
// walk loses its right spine rather than all of its recursion.
//
// `E` is evaluated where the call's arguments were, so its side effects
// and aborts keep their order relative to everything the call would have
// done. Floats are excluded: regrouping changes the result. Integers give
// the same value in every grouping — two's-complement `+` and `*` and the
// bitwise operators are associative at every width, signedness included —
// with one difference a debug build can observe: the regrouped chain forms
// different intermediate values, so a signed overflow check can fire where
// the original did not, and the other way round. Debug overflow detection
// is per operation as it executes, not per operation as written (§6.2).
// Release builds, where signed arithmetic wraps, are bit-identical.
#pragma once

namespace goose {

// One specialization's rewrite, from the optimizer's driver loop. `facts` is
// the shared write/address analysis, which the rewrite reads and extends with
// the assignments it introduces.
struct TailRecursion {
    Ast &ast;
    FnSpec *sp;                                          // The spec being rewritten.
    unordered_map<VarDef *, Optimizer::VarFacts> &facts;
    int &tailloops;                                      // Bumped once per rewrite.

    TypeExpr *rt = nullptr;     // The single integer result type.
    VarDef *acc = nullptr;      // Null when no tail return accumulates anything.
    TType op = T_PLUS, comp = T_PLUSEQ;
    int64_t identity = 0;

    struct Tail {
        Call *call = nullptr;   // The self-call the loop replaces.
        Node *e = nullptr;      // The operand accumulated first; null = no fold.
        TType op = T_PLUS;      // Meaningful when e is set.
    };

    unordered_map<Return *, Tail> tails;

    // The associative operators, with their compound-assignment spelling and
    // the identity the accumulator starts at (all-ones for `&`).
    static bool AssocOp(TType op, TType &comp, int64_t &identity) {
        switch (op) {
            case T_PLUS:   comp = T_PLUSEQ; identity =  0; return true;
            case T_MUL:    comp = T_MULEQ;  identity =  1; return true;
            case T_BITAND: comp = T_ANDEQ;  identity = -1; return true;
            case T_BITOR:  comp = T_OREQ;   identity =  0; return true;
            case T_XOR:    comp = T_XOREQ;  identity =  0; return true;
            default: return false;
        }
    }

    static bool CompoundBase(TType comp, TType &op) {
        switch (comp) {
            case T_PLUSEQ: op = T_PLUS;   return true;
            case T_MULEQ:  op = T_MUL;    return true;
            case T_ANDEQ:  op = T_BITAND; return true;
            case T_OREQ:   op = T_BITOR;  return true;
            case T_XOREQ:  op = T_XOR;    return true;
            default: return false;
        }
    }

    // A direct call to the specialization itself; tag dispatch and builtins
    // are not one.
    Call *SelfCall(Node *n) {
        auto c = Is<Call>(n);
        return c && c->builtin < 0 && c->dispatch.empty() && c->spec == sp ? c : nullptr;
    }

    // Enough of codegen's size classification to tell a reference that
    // carries its stack (fat, §C.3) from a plain-pointer one.
    static bool ReszType(TypeExpr *t) {
        switch (t->kind) {
            case TY_ARRAY:   return t->arr->akind == A_GROW || t->arr->akind == A_GROWSHRINK;
            case TY_STRUCT:  return !t->struc->inst || t->struc->inst->sclass == SC_RESIZABLE;
            case TY_ENUM:    return t->enu->varmode &&
                                    (!t->enu->inst || t->enu->inst->varclass == SC_RESIZABLE);
            case TY_VARIANT: return !t->var->adt || ReszType(t->var->adt);
            default:         return false;
        }
    }

    // A parameter the loop can carry: a scalar, or a reference that is one
    // plain pointer (a relative one would need encoding, a fat or pool one
    // carries a stack the caching discipline tracks per function).
    static bool Rebindable(VarDef *p) {
        auto t = p->type;
        if (!t || p->captured) return false;
        if (Optimizer::ScalarType(t)) return true;
        return t->kind == TY_REF && t->ref->lenstorage < 0 && !p->reusable &&
               !p->ref.reusable && !ReszType(t->ref->sub);
    }

    // Safe to evaluate one step ahead of the call it followed: no abort, no
    // side effect, and nothing the call could have changed under it.
    bool Pure(Node *n) {
        if (Optimizer::AsLiteral(n)) return true;
        auto id = Is<Ident>(n);
        if (!id || !id->vdef || id->vdef->isglobal) return false;
        auto &f = facts[id->vdef];
        return f.writes == 0 && f.addrof == 0;
    }

    // A rebound reference must still point at something after the iteration's
    // statement scopes unwind. Anything reached from a variable, or handed
    // back by a callee, does; a sub-reference of a nonfixed temporary dies
    // with the statement that built it.
    static bool RootStable(Node *n) {
        for (;;) {
            if (Is<Ident>(n) || Is<NullLit>(n)) return true;
            if (auto c = Is<Call>(n))
                return c->exprtype && c->exprtype->kind == TY_REF;
            if (auto d = Is<Dot>(n)) { n = d->obj; continue; }
            if (auto ix = Is<Index>(n)) { n = ix->obj; continue; }
            if (auto se = Is<SliceExpr>(n)) { n = se->obj; continue; }
            if (auto u = Is<Unary>(n); u && u->op == T_BITAND) { n = u->child; continue; }
            return false;
        }
    }

    // The parameter rebindings one tail call needs; arguments that are
    // already the parameter itself need none. Storing into a reference
    // parameter is not something the source may write inside a cycle (§7.8),
    // but the value stored is one the call already passed to that same
    // parameter of this same specialization, whose root class is part of the
    // specialization key — so the reference keeps the root the checker gave
    // it, and the rule it exists to protect still holds.
    bool Rebinds(Call *c, vector<pair<VarDef *, Node *>> &out) {
        vector<Node *> an;
        if (auto d = Is<Dot>(c->callee)) an.push_back(d->obj);
        for (auto a : c->args) an.push_back(a);
        if (an.size() != sp->params.size()) return false;
        for (size_t i = 0; i < an.size(); i++) {
            auto p = sp->params[i];
            if (auto id = Is<Ident>(an[i]); id && id->vdef == p) continue;
            if (!Rebindable(p)) return false;
            if (p->type->kind == TY_REF && !RootStable(an[i])) return false;
            out.push_back({ p, an[i] });
        }
        return true;
    }

    // One result expression: a tail call, or a base value (false).
    bool Classify(Node *v, Tail &t) {
        if (!Optimizer::SameType(v->exprtype, rt)) return false;
        if (auto c = SelfCall(v)) { t.call = c; t.e = nullptr; return true; }
        auto b = Is<Binary>(v);
        TType compop;
        int64_t ident;
        if (!b || !AssocOp(b->op, compop, ident)) return false;
        if (!Optimizer::SameType(b->left->exprtype, rt) ||
            !Optimizer::SameType(b->right->exprtype, rt)) return false;
        if (auto c = SelfCall(b->right)) {
            t.call = c; t.e = b->left; t.op = b->op;
            return true;
        }
        // `self(args) op E` moves E ahead of the call, so E must be inert.
        if (auto c = SelfCall(b->left); c && Pure(b->right)) {
            t.call = c; t.e = b->right; t.op = b->op;
            return true;
        }
        return false;
    }

    Node *AccRef(Line ln) {
        auto id = ast.New<Ident>(ln, acc->name);
        id->vdef = acc;
        id->exprtype = rt;
        return id;
    }

    Node *VarRef(VarDef *v, Line ln) {
        auto id = ast.New<Ident>(ln, v->name);
        id->vdef = v;
        id->exprtype = v->type;
        return id;
    }

    // `var t = ...; ...; if c { t op= self(args) } t` — the accumulating-
    // variable spelling of a tail fold — restated as the return it is, so the
    // matcher above sees it. Sound on its own: `t` is a local the call cannot
    // reach, so reading it before or after the call gives the same value.
    void AccVarForm() {
        auto body = sp->body;
        if (body->stmts.empty() || !body->tail) return;
        auto tid = Is<Ident>(body->tail);
        if (!tid || !tid->vdef || tid->vdef->isglobal) return;
        auto v = tid->vdef;
        auto &vf = facts[v];
        if (!Optimizer::SameType(v->type, rt) ||
            !Optimizer::SameType(body->tail->exprtype, rt)) return;
        if (vf.addrof || v->captured) return;
        Node **slot = &body->stmts.back();
        auto wholestmt = true;
        if (auto fi = Is<IfExpr>(*slot);
            fi && !fi->elseb && fi->thenb->stmts.size() == 1 && !fi->thenb->tail) {
            slot = &fi->thenb->stmts[0];
            wholestmt = false;
        }
        auto a = Is<Assign>(*slot);
        if (!a || a->pointee) return;
        TType binop;
        if (!CompoundBase(a->op, binop)) return;
        auto lid = Is<Ident>(a->lval);
        if (!lid || lid->vdef != v) return;
        auto c = SelfCall(a->rhs);
        if (!c || !Optimizer::SameType(a->rhs->exprtype, rt)) return;
        vector<pair<VarDef *, Node *>> rb;
        if (!Rebinds(c, rb)) return;   // Nothing to gain from restating it.
        auto bin = ast.New<Binary>(a->line, binop, VarRef(v, a->line), a->rhs);
        bin->exprtype = rt;
        auto r = ast.New<Return>(a->line);
        r->target = sp->sf;
        r->vals.push_back(bin);
        r->exprtype = ast.voidtype;
        *slot = r;
        if (wholestmt) {
            // Nothing follows the unconditional form, so the tail is gone.
            body->tail = nullptr;
            body->exprtype = ast.voidtype;
        }
    }

    // The loop steps replacing one tail return.
    Block *Step(Return *r, const Tail &tl) {
        auto ln = r->line;
        auto b = ast.New<Block>(ln);
        b->exprtype = ast.voidtype;
        if (tl.e) {
            auto a = ast.New<Assign>(ln, comp, AccRef(ln), tl.e);
            a->exprtype = ast.voidtype;
            b->stmts.push_back(a);
        }
        vector<pair<VarDef *, Node *>> rb;
        Rebinds(tl.call, rb);
        vector<VarDef *> tmps;
        if (rb.size() > 1) {
            // Several parameters change at once and an argument may read
            // another of them, so every new value lands before any of them do.
            for (auto &[p, val] : rb) {
                auto nv = ast.NewVarDef();
                *nv = *p;
                nv->isparam = false;
                nv->isvar = false;
                nv->captured = false;
                nv->ownerspec = sp;
                auto vd = ast.New<VarDecl>(ln, false);
                vd->names.push_back(nv->name);
                vd->defs.push_back(nv);
                vd->inits.push_back(val);
                vd->exprtype = ast.voidtype;
                b->stmts.push_back(vd);
                tmps.push_back(nv);
            }
        }
        for (size_t i = 0; i < rb.size(); i++) {
            auto p = rb[i].first;
            auto val = tmps.empty() ? rb[i].second : VarRef(tmps[i], ln);
            auto a = ast.New<Assign>(ln, p->type->kind == TY_REF ? T_DOTASSIGN : T_ASSIGN,
                                     VarRef(p, ln), val);
            a->exprtype = ast.voidtype;
            b->stmts.push_back(a);
            facts[p].writes++;
        }
        auto cont = ast.New<Continue>(ln);
        cont->exprtype = ast.voidtype;
        b->stmts.push_back(cont);
        return b;
    }

    void RwSlot(Node *&slot) {
        if (auto r = Is<Return>(slot)) {
            auto it = tails.find(r);
            if (it != tails.end()) slot = Step(r, it->second);
        }
        RwWalk(slot);
    }

    // Reaches every statement slot a return can occupy, through expressions
    // as well (a block used as a value carries statements too).
    void RwWalk(Node *n) {
        if (!n) return;
        if (auto b = Is<Block>(n)) {
            for (auto &st : b->stmts) RwSlot(st);
            if (b->tail) RwSlot(b->tail);
            return;
        }
        if (auto fi = Is<IfExpr>(n)) {
            RwWalk(fi->thenb);
            if (fi->elseb) RwSlot(fi->elseb);
            return;
        }
        if (auto me = Is<MatchExpr>(n)) {
            RwWalk(me->scrutinee);
            for (auto &arm : me->arms) RwSlot(arm.body);
            return;
        }
        if (auto c = Is<Call>(n)) {
            RwWalk(c->callee);
            for (auto a : c->args) RwWalk(a);
            RwWalk(c->fvbody);
            return;
        }
        n->Children([&](Node *ch) { RwWalk(ch); });
    }

    // Every result that still returns folds the accumulator in on the way
    // out — including one inside a tail step's own operand, which abandons
    // the pending call exactly as the original return did.
    void FoldReturns(Node *n) {
        if (!n) return;
        if (auto r = Is<Return>(n); r && r->target == sp->sf && r->vals.size() == 1) {
            auto val = r->vals[0];
            FoldReturns(val);
            auto lit = Is<IntLit>(val);
            if (lit && lit->val == identity) {
                r->vals[0] = AccRef(r->line);   // The base contributes nothing.
                return;
            }
            auto bin = ast.New<Binary>(r->line, op, AccRef(r->line), val);
            bin->exprtype = rt;
            r->vals[0] = bin;
            return;
        }
        if (auto c = Is<Call>(n)) {
            FoldReturns(c->callee);
            for (auto a : c->args) FoldReturns(a);
            FoldReturns(c->fvbody);
            return;
        }
        n->Children([&](Node *ch) { FoldReturns(ch); });
    }

    // Control provably leaves this statement rather than falling past it.
    static bool Diverges(Node *n) {
        if (!n) return false;
        if (Is<Return>(n) || Is<Break>(n) || Is<Continue>(n)) return true;
        auto b = Is<Block>(n);
        return b && !b->tail && !b->stmts.empty() && Diverges(b->stmts.back());
    }

    // Whether the transform is applicable at all below this node, and (out)
    // whether a direct self-call was seen.
    bool Applicable(Node *n, bool &selfcalls) {
        if (!n) return true;
        auto ok = true;
        if (auto r = Is<Return>(n)) {
            if (r->target == sp->sf && r->vals.size() != 1) return false;
        } else if (auto ib = Is<InlineBlock>(n)) {
            if (ib->sf == sp->sf) return false;   // Our own body, spliced in.
        } else if (auto c = Is<Call>(n)) {
            if (SelfCall(c)) selfcalls = true;
            // A `return ... from` us lands in the innermost active frame
            // (§7.9); folding frames away would move where it lands.
            if (c->builtin < 0 && c->spec && c->spec->needs.count(sp->sf)) return false;
            for (auto d : c->dispatch) if (d->needs.count(sp->sf)) return false;
            ok = Applicable(c->callee, selfcalls) && Applicable(c->fvbody, selfcalls);
            for (auto a : c->args) ok = ok && Applicable(a, selfcalls);
            return ok;
        }
        n->Children([&](Node *ch) { ok = ok && Applicable(ch, selfcalls); });
        return ok;
    }

    // Candidate tail returns. Only those the loop's own `continue` can reach:
    // inside an existing loop the jump would restart that one instead, and a
    // function value's spliced body is a return path of its own. Anything
    // skipped stays an ordinary return and folds the accumulator in, which is
    // still correct — the call it keeps returns the whole remaining fold.
    void Collect(Node *n, bool tailok, vector<pair<Return *, Tail>> &found) {
        if (!n) return;
        if (tailok) {
            if (auto r = Is<Return>(n); r && r->target == sp->sf && r->vals.size() == 1) {
                Tail tl;
                if (Classify(r->vals[0], tl)) found.push_back({ r, tl });
            }
        }
        if (auto c = Is<Call>(n)) {
            Collect(c->callee, tailok, found);
            for (auto a : c->args) Collect(a, tailok, found);
            Collect(c->fvbody, false, found);
            return;
        }
        auto inner = tailok && !Is<While>(n) && !Is<LoopExpr>(n) && !Is<ForLoop>(n);
        n->Children([&](Node *ch) { Collect(ch, inner, found); });
    }

    void Run() {
        if (sp->rets.size() != 1) return;
        rt = sp->rets[0];
        if (rt->kind != TY_INT || rt->intstorage == IS_VARINT) return;
        auto selfcalls = false;
        if (!Applicable(sp->body, selfcalls) || !selfcalls) return;
        AccVarForm();
        // Classify every result; the accumulating operator is the first one a
        // tail call folds with, and a tail on any other operator stays a call.
        // The body's value tail is a result too (codegen makes a return of it
        // anyway), but it is not a Return node yet, so it is classified here
        // and restated below once the rewrite is going ahead.
        auto tail = sp->body->tail;
        auto tailisvalue = tail && tail->exprtype && tail->exprtype->kind != TY_VOID &&
                           !Is<Guard>(tail);
        if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) tailisvalue = false;
        vector<pair<Return *, Tail>> found, cands;
        Collect(sp->body, true, found);
        for (auto &[r, tl] : found) {
            vector<pair<VarDef *, Node *>> rb;
            if (Rebinds(tl.call, rb)) cands.push_back({ r, tl });
        }
        Tail tailtl;
        auto tailistail = false;
        if (tailisvalue && Classify(tail, tailtl)) {
            vector<pair<VarDef *, Node *>> rb;
            tailistail = Rebinds(tailtl.call, rb);
        }
        auto haveop = false;
        for (auto &[r, tl] : cands)
            if (tl.e) { op = tl.op; haveop = true; break; }
        if (!haveop && tailistail && tailtl.e) { op = tailtl.op; haveop = true; }
        if (haveop) AssocOp(op, comp, identity);
        auto usable = [&](const Tail &tl) { return !tl.e || tl.op == op; };
        auto ntails = tailistail && usable(tailtl) ? 1 : 0;
        for (auto &[r, tl] : cands) if (usable(tl)) ntails++;
        if (!ntails) return;
        // Committed: everything below rewrites the body.
        if (tailisvalue) {
            auto r = ast.New<Return>(tail->line);
            r->target = sp->sf;
            r->vals.push_back(tail);
            r->exprtype = ast.voidtype;
            sp->body->stmts.push_back(r);
            sp->body->tail = nullptr;
            sp->body->exprtype = ast.voidtype;
            if (tailistail && usable(tailtl)) tails[r] = tailtl;
        }
        for (auto &[r, tl] : cands)
            if (usable(tl)) tails[r] = tl;
        if (haveop) {
            acc = ast.NewVarDef();
            acc->name = "acc";
            acc->type = rt;
            acc->line = sp->sf->line;
            acc->isvar = true;
            acc->assigned = true;
            acc->ownerspec = sp;
            facts[acc].writes = 1;   // Never a propagation candidate.
        }
        RwWalk(sp->body);
        if (acc) FoldReturns(sp->body);
        // `var acc = <identity>; loop { <body> }`.
        auto ln = sp->body->line;
        auto inner = ast.New<Block>(ln);
        inner->stmts = std::move(sp->body->stmts);
        inner->exprtype = ast.voidtype;
        if (sp->body->tail) inner->stmts.push_back(sp->body->tail);
        if (inner->stmts.empty() || !Diverges(inner->stmts.back())) {
            // Falling off the end would repeat the body; the original reached
            // the function's own unreachable epilogue, so leave for it.
            auto br = ast.New<Break>(ln, nullptr);
            br->exprtype = ast.voidtype;
            inner->stmts.push_back(br);
        }
        auto loop = ast.New<LoopExpr>(ln, inner);
        loop->exprtype = ast.voidtype;
        sp->body->stmts.clear();
        sp->body->tail = nullptr;
        sp->body->exprtype = ast.voidtype;
        if (acc) {
            auto init = ast.New<IntLit>(ln, Optimizer::WrapStorage(identity, rt->intstorage));
            init->exprtype = rt;
            auto vd = ast.New<VarDecl>(ln, true);
            vd->names.push_back(acc->name);
            vd->defs.push_back(acc);
            vd->inits.push_back(init);
            vd->exprtype = ast.voidtype;
            sp->body->stmts.push_back(vd);
        }
        sp->body->stmts.push_back(loop);
        tailloops++;
    }
};

inline void Optimizer::TailRecurse(FnSpec *sp) {
    if (!nc) return;   // Off at -O0, like inlining.
    TailRecursion tre { ast, sp, facts, tailloops };
    tre.Run();
}

}  // namespace goose
