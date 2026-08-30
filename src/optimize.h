// Goose compiler — the optimizer: function inlining, constant folding, and
// constant propagation, applied in place to the typechecked specialization
// bodies that codegen consumes. The one hard rule: no rewrite may change the
// program's semantics, aborts included — an operation that would abort at
// runtime (division by zero, debug-build overflow, a failed `as` range check)
// is never folded into a value or folded away.
//
// Shape of the pass: reachable specializations are visited once each, in
// call-graph postorder (callees before callers), so a caller's inline
// decisions see the callee's final post-optimization size. Newly inlined
// copies are the one re-visit: each is re-folded so constant arguments
// cascade through the spliced body.
//
// Inlining replaces a call with an InlineBlock: parameter bindings as
// VarDecls (or direct constant substitution), then a copy of the callee body
// with its VarDefs remapped. Returns are not rewritten — a Return whose
// target matches an enclosing InlineBlock's sf exits that block (ast.h), so
// early returns, `return from`, and function-value returns all keep working
// through any nesting of inlined bodies.
//
// A body must not be inlined while a *separate* tree still references its
// locals: a remaining (non-inlined) call to a nested function or to a spec
// with bound function values means that callee's own body reaches our locals
// as free variables, so splicing us elsewhere (which remaps our VarDefs)
// would strand it. Ditto a remaining callee that does `return ... from` us.
// Once such calls are themselves inlined the references live inside our own
// tree, get remapped with everything else, and the restriction lifts.
//
// Thresholds per call site of callee K: inline if K is used once anywhere,
// or nodecount(K) < NC, or nodecount(K) * uses(K) < NCU.
// -O0: no inlining; -O1: NC=8, NCU=48; -O2: NC=16, NCU=96.
// Folding and propagation run at every level.
//
// The per-node work is the Cp1 and Opt virtuals (declared in ast.h); their
// implementations live together at the end of this file — all of Cp1 (the
// inline copier), then all of Opt (fold/propagate/inline) — so each sub-pass
// reads top to bottom. The Optimizer/Inliner structs hold the shared state
// and machinery those implementations use.
#pragma once

namespace goose {

struct Optimizer {
    Ast &ast;
    int nc = 0, ncu = 0;         // Inline thresholds; 0 = inlining off.
    bool caninline = false;
    FnSpec *curspec = nullptr;   // Specialization being optimized (null: globals).
    SFunction *cursf = nullptr;
    int inlined = 0, folded = 0, propagated = 0;

    // Per-VarDef write/address facts, collected across every live body before
    // any rewriting (so writes to captured variables and globals from other
    // specializations' function-value bodies are all visible).
    struct VarFacts { int writes = 0; int addrof = 0; };
    unordered_map<VarDef *, VarFacts> facts;

    // Propagatable constants: a variable's VarDef -> its literal. Entries are
    // added at the declaration as the walk passes it, so uses seen later in
    // the same walk (the only in-scope ones) pick them up.
    unordered_map<VarDef *, Node *> consts;

    vector<FnSpec *> postorder;  // Live specs, callees before callers.

    // ------------------------------------------------------------------
    // Small helpers.

    static bool IsF32T(TypeExpr *t) { return t && t->kind == TY_FLT && t->fltstorage == FS_F32; }

    static bool ScalarType(TypeExpr *t) {
        return t && (t->kind == TY_BOOL || t->kind == TY_FLT ||
                     (t->kind == TY_INT && t->intstorage != IS_VARINT));
    }

    static Node *AsLiteral(Node *n) {
        return Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) ? n : nullptr;
    }

    // Same-type check without the typechecker's context: exact for scalars,
    // pointer identity otherwise. A miss just skips a fold.
    static bool SameType(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (!a || !b || a->kind != b->kind) return false;
        switch (a->kind) {
            case TY_INT:  return TypeCheck::CanonI(a->intstorage) == TypeCheck::CanonI(b->intstorage);
            case TY_FLT:  return TypeCheck::CanonF(a->fltstorage) == TypeCheck::CanonF(b->fltstorage);
            case TY_BOOL: case TY_VOID: return true;
            default: return false;
        }
    }

    // Wrap-checked int64 arithmetic (overflow aborts in debug builds, §6.2,
    // so an overflowing constant op is left for the runtime to decide).
    static bool AddOv(int64_t a, int64_t b, int64_t &r) {
        r = (int64_t)((uint64_t)a + (uint64_t)b);
        return ((a ^ r) & (b ^ r)) < 0;
    }
    static bool SubOv(int64_t a, int64_t b, int64_t &r) {
        r = (int64_t)((uint64_t)a - (uint64_t)b);
        return ((a ^ b) & (a ^ r)) < 0;
    }
    static bool MulOv(int64_t a, int64_t b, int64_t &r) {
        r = (int64_t)((uint64_t)a * (uint64_t)b);
        if (a == 0 || b == 0) return false;
        if (a == -1) return b == INT64_MIN;
        if (b == -1) return a == INT64_MIN;
        return r / b != a;
    }

    static int64_t WrapStorage(int64_t v, IntStorage s) {
        switch (s) {
            case IS_I8:  return (int8_t)v;   case IS_U8:  return (uint8_t)v;
            case IS_I16: return (int16_t)v;  case IS_U16: return (uint16_t)v;
            case IS_I32: return (int32_t)v;  case IS_U32: return (int64_t)(uint32_t)v;
            default:     return v;
        }
    }

    Node *NewInt(Node *at, int64_t v) {
        auto r = ast.New<IntLit>(at->line, v);
        r->exprtype = at->exprtype;
        folded++;
        return r;
    }
    Node *NewFlt(Node *at, double v) {
        auto r = ast.New<FltLit>(at->line, v);
        r->exprtype = at->exprtype;
        folded++;
        return r;
    }
    Node *NewBool(Node *at, bool v) {
        auto r = ast.New<BoolLit>(at->line, v);
        r->exprtype = at->exprtype;
        folded++;
        return r;
    }
    Block *EmptyBlock(Node *at) {
        auto b = ast.New<Block>(at->line);
        b->exprtype = ast.voidtype;
        return b;
    }

    Node *CloneLit(Node *lit, TypeExpr *usetype) {
        Node *r;
        if (auto i = Is<IntLit>(lit)) r = ast.New<IntLit>(lit->line, i->val, i->text);
        else if (auto f = Is<FltLit>(lit)) r = ast.New<FltLit>(lit->line, f->val);
        else r = ast.New<BoolLit>(lit->line, ((BoolLit *)lit)->val);
        r->exprtype = usetype ? usetype : lit->exprtype;
        return r;
    }

    // ------------------------------------------------------------------
    // Reachability + per-spec use counts, one walk. Postorder w.r.t. the call
    // graph (cycles cut at the back edge; their members never inline anyway).

    void Reach(FnSpec *sp) {
        if (!sp || sp->live) return;
        sp->live = true;
        ReachTree(sp->body);
        postorder.push_back(sp);
    }

    void ReachTree(Node *n) {
        if (!n) return;
        if (auto c = Is<Call>(n)) {
            if (c->spec) {
                if (c->builtin < 0) c->spec->uses++;  // thread_spawn is not a call site.
                Reach(c->spec);
            }
            for (auto d : c->dispatch) { d->uses++; Reach(d); }
            ReachTree(c->callee);
            for (auto a : c->args) ReachTree(a);
            ReachTree(c->fvbody);
            return;  // c->trailing is an unchecked template; nothing live in it.
        }
        n->Children([&](Node *ch) { ReachTree(ch); });
    }

    // ------------------------------------------------------------------
    // Write/address analysis. Writes through references and member growth ops
    // don't need tracking here: only scalar variables are ever propagated,
    // and a reference to a scalar requires an explicit &x (counted below).

    void Analyze(Node *n) {
        if (!n) return;
        if (auto a = Is<Assign>(n)) {
            if (auto id = Is<Ident>(a->lval)) if (id->vdef) facts[id->vdef].writes++;
        } else if (auto x = Is<IncDec>(n)) {
            if (auto id = Is<Ident>(x->lval)) if (id->vdef) facts[id->vdef].writes++;
        } else if (auto u = Is<Unary>(n)) {
            if (u->op == T_BITAND)
                if (auto id = Is<Ident>(u->child)) if (id->vdef) facts[id->vdef].addrof++;
        } else if (auto c = Is<Call>(n)) {
            Analyze(c->fvbody);
        }
        n->Children([&](Node *ch) { Analyze(ch); });
    }

    // ------------------------------------------------------------------
    // Inlining support.

    // Does this tree contain a Return that would exit an InlineBlock for sf?
    bool ReturnsFor(Node *n, SFunction *sf) {
        if (!n) return false;
        if (auto r = Is<Return>(n)) if (r->target == sf) return true;
        auto found = false;
        if (auto c = Is<Call>(n)) found = ReturnsFor(c->fvbody, sf);
        n->Children([&](Node *ch) { found = found || ReturnsFor(ch, sf); });
        return found;
    }

    Node *TryInline(Call *c);   // Defined after Inliner below.

    // ------------------------------------------------------------------
    // The fold/propagate/inline walk. Opt returns the (possibly replaced)
    // node; statements go through OptStmt, which may drop them.

    Node *Opt(Node *n) {
        assert(n);
        return n->Opt(*this);
    }

    void OptBlock(Block *b) {
        vector<Node *> out;
        auto dead = false;
        for (auto st : b->stmts) {
            if (dead) break;  // Statically unreachable after a hard jump.
            auto r = OptStmt(st);
            if (!r) continue;
            out.push_back(r);
            if (Is<Return>(r) || Is<Break>(r) || Is<Continue>(r)) dead = true;
        }
        b->stmts = std::move(out);
        if (b->tail) {
            if (dead) {
                // Folding introduced divergence before the tail; the block no
                // longer produces a value (mirrors the checker's rule).
                b->tail = nullptr;
                b->exprtype = ast.voidtype;
            } else {
                b->tail = Opt(b->tail);
            }
        }
    }

    Node *OptStmt(Node *n) {
        if (auto vd = Is<VarDecl>(n)) {
            for (auto &i : vd->inits) i = Opt(i);
            if (vd->names.size() == 1 && vd->inits.size() == 1 && vd->defs.size() == 1) {
                auto d = vd->defs[0];
                auto lit = AsLiteral(vd->inits[0]);
                auto &f = facts[d];
                if (lit && ScalarType(d->type) && f.writes == 0 && f.addrof == 0) {
                    consts[d] = lit;
                    // Uses outside this walk exist only via capture; otherwise
                    // every use gets replaced and the binding is dead.
                    if (!d->captured) return nullptr;
                }
            }
            return n;
        }
        auto r = Opt(n);
        if (auto b = Is<Block>(r); b && b->stmts.empty() && !b->tail) return nullptr;
        return r;
    }

    // ------------------------------------------------------------------
    // Post-optimization classification: final node count, and whether this
    // body may be spliced into callers (see the header comment).

    void Scan(FnSpec *sp) {
        sp->nodecount = 0;
        auto noin = sp->sf->isrec || sp->incycle || sp->sf->isthread || sp->rets.size() > 1;
        function<void(Node *)> rec = [&](Node *n) {
            if (!n) return;
            sp->nodecount++;
            if (auto c = Is<Call>(n)) {
                auto callee = [&](FnSpec *k) {
                    if (!k) return;
                    // A separate body referencing our locals (nested fn or
                    // bound function values), or unwinding to us: our body
                    // must stay a real frame.
                    if (k->lexparent || !k->fnvals.empty()) noin = true;
                    if (k->needs.count(sp->sf)) noin = true;
                };
                if (c->builtin < 0) callee(c->spec);
                for (auto d : c->dispatch) callee(d);
                rec(c->callee);
                for (auto a : c->args) rec(a);
                rec(c->fvbody);
                return;
            }
            n->Children([&](Node *ch) { rec(ch); });
        };
        rec(sp->body);
        sp->noinline = noin;
    }

    // ------------------------------------------------------------------
    // Global initializers: fold (and on the second pass inline into) each
    // init, and register constant scalar globals for propagation everywhere.

    void OptGlobal(VarDecl *g) {
        for (auto &i : g->inits) i = Opt(i);
        if (g->names.size() == 1 && g->inits.size() == 1 && !g->defs.empty()) {
            auto d = g->defs[0];
            auto lit = AsLiteral(g->inits[0]);
            auto &f = facts[d];
            if (lit && ScalarType(d->type) && f.writes == 0 && f.addrof == 0)
                consts[d] = lit;  // The declaration itself always stays.
        }
    }

    // ------------------------------------------------------------------
    // Driver: constructing the Optimizer runs the whole pass.

    Optimizer(Ast &_ast, int level) : ast(_ast) {
        switch (level) {
            case 0:  nc = 0;  ncu = 0;  break;
            case 1:  nc = 8;  ncu = 48; break;
            default: nc = 16; ncu = 96; break;
        }
        // Reachability and use counts from the roots.
        auto mit = ast.functionmap.find("main");
        if (mit != ast.functionmap.end() && !mit->second[0]->specs.empty())
            Reach(mit->second[0]->specs[0]);
        for (auto sf : ast.functions)
            if (sf->isthread && !sf->specs.empty()) Reach(sf->specs[0]);
        for (auto g : ast.globals)
            for (auto i : g->inits) ReachTree(i);
        for (auto si : ast.structinsts)
            for (auto d : si->defaults) if (d) ReachTree(d);
        for (auto ei : ast.enuminsts)
            for (auto &vd : ei->vdefaults) for (auto d : vd) if (d) ReachTree(d);
        // Write/address facts across every live body, before any rewriting.
        for (auto sp : postorder) Analyze(sp->body);
        for (auto g : ast.globals)
            for (auto i : g->inits) Analyze(i);
        for (auto si : ast.structinsts)
            for (auto d : si->defaults) if (d) Analyze(d);
        for (auto ei : ast.enuminsts)
            for (auto &vd : ei->vdefaults) for (auto d : vd) if (d) Analyze(d);
        // Globals first (fold only), so constant let globals propagate into
        // every body below.
        caninline = false;
        for (auto g : ast.globals) OptGlobal(g);
        // Every reachable specialization, callees before callers.
        caninline = nc > 0;
        for (auto sp : postorder) {
            curspec = sp;
            cursf = sp->sf;
            OptBlock(sp->body);
            Scan(sp);
        }
        curspec = nullptr;
        cursf = nullptr;
        // Globals again, now able to inline into their initializers. Field
        // defaults fold only: their trees are shared across construction
        // sites, so they must not grow variable bindings.
        for (auto g : ast.globals) OptGlobal(g);
        caninline = false;
        for (auto si : ast.structinsts)
            for (auto &d : si->defaults) if (d) d = Opt(d);
        for (auto ei : ast.enuminsts)
            for (auto &vd : ei->vdefaults) for (auto &d : vd) if (d) d = Opt(d);
        // Final liveness over the rewritten trees: specs whose every call got
        // inlined (or folded away) go dead, so codegen can skip them.
        for (auto sp : ast.fnspecs) { sp->live = false; sp->uses = 0; }
        postorder.clear();
        if (mit != ast.functionmap.end() && !mit->second[0]->specs.empty())
            Reach(mit->second[0]->specs[0]);
        for (auto sf : ast.functions)
            if (sf->isthread && !sf->specs.empty()) Reach(sf->specs[0]);
        for (auto g : ast.globals)
            for (auto i : g->inits) ReachTree(i);
        for (auto si : ast.structinsts)
            for (auto d : si->defaults) if (d) ReachTree(d);
        for (auto ei : ast.enuminsts)
            for (auto &vd : ei->vdefaults) for (auto d : vd) if (d) ReachTree(d);
    }

    // Optimized bodies for eyeballing (--specs); not reparseable.
    void DumpSpecs(string &s) {
        for (auto sp : ast.fnspecs) {
            if (!sp->live) continue;
            Append(s, "// spec ", sp->id, ": uses ", sp->uses, ", nodes ", sp->nodecount,
                   sp->noinline ? ", noinline" : "", "\n");
            Append(s, "fn ", sp->sf->name, "(");
            for (size_t i = 0; i < sp->params.size(); i++) {
                if (i) s += ", ";
                Append(s, sp->params[i]->name, ": ");
                sp->params[i]->type->Dump(s);
            }
            s += ") ";
            sp->body->Dump(s, 0);
            s += "\n\n";
        }
    }
};

// ---------------------------------------------------------------------------
// The inline copier's state: an annotation-preserving deep copy of a callee
// body, with the callee's own VarDefs remapped to fresh ones (VarDefs owned
// by outer functions — captured free variables — stay, and stay valid, since
// this spec's only call sites lie inside its capture environment's tree).
// The per-node work is the Cp1 overrides at the end of this file.

struct Inliner {
    Optimizer &o;
    Ast &ast;
    FnSpec *src;             // The spec whose body is being copied.
    FnSpec *dst;             // The spec receiving the copy (null: a global init).
    unordered_map<VarDef *, VarDef *> vmap;
    unordered_map<VarDef *, Node *> subst;   // Param -> constant argument.

    VarDef *Remap(VarDef *v) {
        if (!v || v->ownerspec != src) return v;
        auto it = vmap.find(v);
        if (it != vmap.end()) return it->second;
        auto nv = ast.NewVarDef();
        *nv = *v;
        nv->ownerspec = dst;
        nv->isparam = false;
        nv->captured = false;  // Every use of the copy lives in the copied tree.
        vmap[v] = nv;
        auto fit = o.facts.find(v);
        if (fit != o.facts.end()) o.facts[nv] = fit->second;
        return nv;
    }

    Node *Cp(const Node *n) {
        auto r = n->Cp1(*this);
        r->exprtype = n->exprtype;
        return r;
    }

    Block *CpBlock(const Block *b) { return (Block *)Cp(b); }
};

inline Node *Optimizer::TryInline(Call *c) {
    if (!caninline || !c->spec || c->builtin >= 0 || !c->dispatch.empty()) return nullptr;
    auto K = c->spec;
    if (!K->live || K->noinline || !K->body) return nullptr;
    // Never into a recursive cycle: the inlined body's locals would become
    // the cycle function's own, upsetting the §7.8 stack-assignment rule.
    if (curspec && (curspec->incycle || curspec->sf->isrec)) return nullptr;
    if (!(K->uses == 1 || K->nodecount < nc || K->nodecount * K->uses < ncu)) return nullptr;
    // The argument list; a UFCS receiver is the first parameter (§7.1).
    vector<Node *> argnodes;
    if (auto d = Is<Dot>(c->callee)) argnodes.push_back(d->obj);
    for (auto a : c->args) argnodes.push_back(a);
    if (argnodes.size() != K->params.size()) return nullptr;
    Inliner inl { *this, ast, K, curspec, {}, {} };
    vector<Node *> decls;
    for (size_t i = 0; i < K->params.size(); i++) {
        auto pv = K->params[i];
        auto arg = argnodes[i];
        auto &f = facts[pv];
        if (AsLiteral(arg) && ScalarType(pv->type) && f.writes == 0 && f.addrof == 0) {
            inl.subst[pv] = arg;  // Constant argument: substitute, no binding.
            continue;
        }
        auto nv = ast.NewVarDef();
        *nv = *pv;
        nv->ownerspec = curspec;
        nv->isparam = false;
        nv->captured = false;
        inl.vmap[pv] = nv;
        facts[nv] = f;
        auto vd = ast.New<VarDecl>(c->line, pv->isvar);
        vd->names.push_back(pv->name);
        vd->defs.push_back(nv);
        vd->inits.push_back(arg);
        vd->exprtype = ast.voidtype;
        decls.push_back(vd);
    }
    auto body = inl.CpBlock(K->body);
    body->stmts.insert(body->stmts.begin(), decls.begin(), decls.end());
    auto ib = ast.New<InlineBlock>(c->line, K->sf, K, body);
    ib->exprtype = c->exprtype;
    inlined++;
    // Re-fold the copy so substituted constants cascade — the one re-visit
    // the single-pass design allows, and only over fresh nodes.
    OptBlock(body);
    // Trivial results unwrap to plain expressions — but only when the value's
    // type survives as-is (a reference return decayed at the call site must
    // keep the InlineBlock, whose exprtype records the decayed type).
    auto unwrapok = [&](Node *v) {
        if (SameType(v->exprtype, c->exprtype)) return true;
        auto a = v->exprtype, b = c->exprtype;
        return a && b && a->kind == b->kind && a->kind != TY_REF;
    };
    if (body->stmts.empty() && body->tail && !ReturnsFor(body->tail, K->sf) &&
        unwrapok(body->tail))
        return body->tail;
    if (body->stmts.empty() && !body->tail) return EmptyBlock(c);
    if (body->stmts.size() == 1 && !body->tail) {
        if (auto r = Is<Return>(body->stmts[0]); r && r->target == K->sf) {
            if (r->vals.empty()) return EmptyBlock(c);
            if (r->vals.size() == 1 && !ReturnsFor(r->vals[0], K->sf) &&
                unwrapok(r->vals[0]))
                return r->vals[0];
        }
    }
    return ib;
}

// ---------------------------------------------------------------------------
// Cp1: the annotation-preserving copy behind inlining, one override per node
// kind. Inliner::Cp wraps every call and carries exprtype over; TypeExprs and
// symbols are shared, VarDefs remap via Inliner::Remap.

inline Node *IntLit::Cp1(Inliner &inl) const { return inl.ast.New<IntLit>(line, val, text); }
inline Node *FltLit::Cp1(Inliner &inl) const { return inl.ast.New<FltLit>(line, val); }
inline Node *BoolLit::Cp1(Inliner &inl) const { return inl.ast.New<BoolLit>(line, val); }
inline Node *StrLit::Cp1(Inliner &inl) const { return inl.ast.New<StrLit>(line, val); }
inline Node *NullLit::Cp1(Inliner &inl) const { return inl.ast.New<NullLit>(line); }
inline Node *Continue::Cp1(Inliner &inl) const { return inl.ast.New<Continue>(line); }

inline Node *Ident::Cp1(Inliner &inl) const {
    if (vdef) {
        // A parameter bound to a constant argument substitutes right here.
        auto it = inl.subst.find(vdef);
        if (it != inl.subst.end()) {
            inl.o.propagated++;
            return inl.o.CloneLit(it->second, exprtype);
        }
    }
    auto c = inl.ast.New<Ident>(line, name);
    c->vdef = inl.Remap(vdef);
    c->fnref = fnref;
    return c;
}

inline Node *ArrayLit::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<ArrayLit>(line);
    for (auto e : elems) c->elems.push_back(inl.Cp(e));
    if (fillval) c->fillval = inl.Cp(fillval);
    if (fillcount) c->fillcount = inl.Cp(fillcount);
    if (capexpr) c->capexpr = inl.Cp(capexpr);
    return c;
}

inline Node *StructLit::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<StructLit>(line, type);
    for (auto &fi : inits) c->inits.push_back({ fi.name, inl.Cp(fi.val) });
    c->sinst = sinst;
    c->einst = einst;
    c->variant = variant;
    c->fieldindices = fieldindices;
    return c;
}

inline Node *Unary::Cp1(Inliner &inl) const {
    return inl.ast.New<Unary>(line, op, inl.Cp(child));
}

inline Node *Binary::Cp1(Inliner &inl) const {
    return inl.ast.New<Binary>(line, op, inl.Cp(left), inl.Cp(right));
}

inline Node *Dot::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Dot>(line, inl.Cp(obj), name);
    c->fieldidx = fieldidx;
    c->member = member;
    c->variantconst = variantconst;
    c->einst = einst;
    return c;
}

inline Node *Call::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Call>(line, inl.Cp(callee));
    c->tyargs = tyargs;
    for (auto a : args) c->args.push_back(inl.Cp(a));
    c->trailing = trailing;   // Shared template; the checked fvbody below is what runs.
    c->spec = spec;
    c->dispatch = dispatch;
    c->dispatcharg = dispatcharg;
    c->builtin = builtin;
    c->fvtarget = fvtarget;
    c->rettypes = rettypes;
    for (auto p : fvparams) c->fvparams.push_back(inl.Remap(p));
    c->fvbody = fvbody ? inl.CpBlock(fvbody) : nullptr;
    return c;
}

inline Node *Index::Cp1(Inliner &inl) const {
    return inl.ast.New<Index>(line, inl.Cp(obj), inl.Cp(idx));
}

inline Node *SliceExpr::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<SliceExpr>(line, inl.Cp(obj));
    if (lo) c->lo = inl.Cp(lo);
    if (hi) c->hi = inl.Cp(hi);
    c->lo_from_end = lo_from_end;
    c->hi_from_end = hi_from_end;
    return c;
}

inline Node *AsCast::Cp1(Inliner &inl) const {
    return inl.ast.New<AsCast>(line, inl.Cp(child), type, unchecked);
}

inline Node *RangeExpr::Cp1(Inliner &inl) const {
    return inl.ast.New<RangeExpr>(line, inl.Cp(lo), inl.Cp(hi));
}

inline Node *Block::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Block>(line);
    for (auto st : stmts) c->stmts.push_back(inl.Cp(st));
    if (tail) c->tail = inl.Cp(tail);
    return c;
}

inline Node *IfExpr::Cp1(Inliner &inl) const {
    return inl.ast.New<IfExpr>(line, inl.Cp(cond), inl.CpBlock(thenb),
                               elseb ? inl.Cp(elseb) : nullptr);
}

inline Node *MatchExpr::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<MatchExpr>(line, inl.Cp(scrutinee));
    for (auto &arm : arms) {
        MatchArm a;
        a.pat = arm.pat;
        if (a.pat.lo) a.pat.lo = inl.Cp(a.pat.lo);
        if (a.pat.hi) a.pat.hi = inl.Cp(a.pat.hi);
        a.variant = arm.variant;
        a.binder = inl.Remap(arm.binder);
        a.lo = arm.lo;
        a.hi = arm.hi;
        a.body = inl.Cp(arm.body);
        c->arms.push_back(a);
    }
    return c;
}

inline Node *EarlyBlock::Cp1(Inliner &inl) const {
    return inl.ast.New<EarlyBlock>(line, inl.CpBlock(body));
}

inline Node *While::Cp1(Inliner &inl) const {
    return inl.ast.New<While>(line, inl.Cp(cond), inl.CpBlock(body));
}

inline Node *LoopExpr::Cp1(Inliner &inl) const {
    return inl.ast.New<LoopExpr>(line, inl.CpBlock(body));
}

inline Node *ForLoop::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<ForLoop>(line, byref, var, idxvar, inl.Cp(iter), inl.CpBlock(body));
    c->vdef = inl.Remap(vdef);
    c->idxdef = inl.Remap(idxdef);
    c->iterkind = iterkind;
    return c;
}

inline Node *Guard::Cp1(Inliner &inl) const {
    auto g = inl.ast.New<Guard>(line, inl.Cp(cond), elseb ? inl.CpBlock(elseb) : nullptr);
    g->implicitexit = implicitexit;
    if (!g->elseb && implicitexit == 2) {
        // The bare form's implicit `return` exits the function being inlined;
        // make it explicit so it targets the InlineBlock and not the new host
        // function.
        auto ret = inl.ast.New<Return>(line);
        ret->target = inl.src->sf;
        ret->exprtype = inl.ast.voidtype;
        auto b = inl.ast.New<Block>(line);
        b->stmts.push_back(ret);
        b->exprtype = inl.ast.voidtype;
        g->elseb = b;
        g->implicitexit = 0;
    }
    return g;
}

inline Node *Return::Cp1(Inliner &inl) const {
    auto r = inl.ast.New<Return>(line);
    for (auto v : vals) r->vals.push_back(inl.Cp(v));
    r->from = from;
    r->target = target;
    return r;
}

inline Node *Break::Cp1(Inliner &inl) const {
    return inl.ast.New<Break>(line, val ? inl.Cp(val) : nullptr);
}

inline Node *VarDecl::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<VarDecl>(line, isvar);
    c->reusable = reusable;
    c->isglobal = isglobal;
    c->names = names;
    c->type = type;
    for (auto i : inits) c->inits.push_back(inl.Cp(i));
    for (auto d : defs) c->defs.push_back(inl.Remap(d));
    return c;
}

inline Node *Assign::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Assign>(line, op, inl.Cp(lval), inl.Cp(rhs));
    c->pointee = pointee;
    return c;
}

inline Node *IncDec::Cp1(Inliner &inl) const {
    return inl.ast.New<IncDec>(line, op, inl.Cp(lval));
}

inline Node *InlineBlock::Cp1(Inliner &inl) const {
    return inl.ast.New<InlineBlock>(line, sf, spec, inl.CpBlock(body));
}

// A nested declaration copies inert; its own specializations are separate
// trees (and any still-called one blocks inlining, see the header comment).
inline Node *FnDecl::Cp1(Inliner &inl) const { return inl.ast.New<FnDecl>(line, sf); }

// FunVal templates and top-level declarations never appear inside a checked,
// inlinable body.
inline Node *FunVal::Cp1(Inliner &) const { assert(false); return const_cast<FunVal *>(this); }
inline Node *StructDecl::Cp1(Inliner &) const { assert(false); return const_cast<StructDecl *>(this); }
inline Node *EnumDecl::Cp1(Inliner &) const { assert(false); return const_cast<EnumDecl *>(this); }
inline Node *AliasDecl::Cp1(Inliner &) const { assert(false); return const_cast<AliasDecl *>(this); }

// ---------------------------------------------------------------------------
// Opt: fold, propagate, and inline below each node, one override per node
// kind; each returns the possibly replaced node. Children are optimized
// first, then constant operands combine bottom-up in the same visit.

inline Node *IntLit::Opt(Optimizer &) { return this; }
inline Node *FltLit::Opt(Optimizer &) { return this; }
inline Node *BoolLit::Opt(Optimizer &) { return this; }
inline Node *StrLit::Opt(Optimizer &) { return this; }
inline Node *NullLit::Opt(Optimizer &) { return this; }
inline Node *Continue::Opt(Optimizer &) { return this; }
inline Node *FnDecl::Opt(Optimizer &) { return this; }   // Inert statement.

inline Node *Ident::Opt(Optimizer &o) {
    if (vdef) {
        auto it = o.consts.find(vdef);
        if (it != o.consts.end()) {
            o.propagated++;
            return o.CloneLit(it->second, exprtype);
        }
    }
    return this;
}

inline Node *Unary::Opt(Optimizer &o) {
    child = o.Opt(child);
    switch (op) {
        case T_MINUS:
            if (auto i = Is<IntLit>(child)) {
                if (i->val == INT64_MIN) break;  // Overflows; runtime decides.
                return o.NewInt(this, -i->val);
            }
            if (auto fl = Is<FltLit>(child)) return o.NewFlt(this, -fl->val);
            break;
        case T_BITNOT:
            if (auto i = Is<IntLit>(child)) return o.NewInt(this, ~i->val);
            break;
        case T_NOT:
            if (auto b = Is<BoolLit>(child)) return o.NewBool(this, !b->val);
            break;
        default: break;  // & — the child is a location; nothing folds.
    }
    return this;
}

inline Node *Binary::Opt(Optimizer &o) {
    if (op == T_ANDAND || op == T_OROR) {
        left = o.Opt(left);
        if (auto lb = Is<BoolLit>(left)) {
            // A constant left either selects the right operand or leaves it
            // unevaluated (short-circuit), so dropping it is exact.
            o.folded++;
            if (op == T_ANDAND) return lb->val ? o.Opt(right) : left;
            return lb->val ? left : o.Opt(right);
        }
        right = o.Opt(right);
        if (auto rb = Is<BoolLit>(right)) {
            // The left still evaluates; only the trivial combine drops.
            if (op == T_ANDAND && rb->val) { o.folded++; return left; }
            if (op == T_OROR && !rb->val) { o.folded++; return left; }
        }
        return this;
    }
    left = o.Opt(left);
    right = o.Opt(right);
    auto li = Is<IntLit>(left), ri = Is<IntLit>(right);
    if (li && ri) {
        auto a = li->val, b = ri->val;
        int64_t r;
        switch (op) {
            // Overflow and division aborts stay runtime behavior: those
            // cases are simply not folded.
            case T_PLUS:  if (!Optimizer::AddOv(a, b, r)) return o.NewInt(this, r); break;
            case T_MINUS: if (!Optimizer::SubOv(a, b, r)) return o.NewInt(this, r); break;
            case T_MUL:   if (!Optimizer::MulOv(a, b, r)) return o.NewInt(this, r); break;
            case T_DIV:   if (b && !(a == INT64_MIN && b == -1)) return o.NewInt(this, a / b); break;
            case T_MOD:   if (b && !(a == INT64_MIN && b == -1)) return o.NewInt(this, a % b); break;
            case T_BITAND: return o.NewInt(this, a & b);
            case T_BITOR:  return o.NewInt(this, a | b);
            case T_XOR:    return o.NewInt(this, a ^ b);
            case T_SHL:    return o.NewInt(this, (int64_t)((uint64_t)a << (b & 63)));
            case T_SHR:    return o.NewInt(this, a >> (b & 63));
            case T_LT:     return o.NewBool(this, a < b);
            case T_GT:     return o.NewBool(this, a > b);
            case T_LTEQ:   return o.NewBool(this, a <= b);
            case T_GTEQ:   return o.NewBool(this, a >= b);
            case T_EQ:     return o.NewBool(this, a == b);
            case T_NEQ:    return o.NewBool(this, a != b);
            default: break;
        }
        return this;
    }
    auto lf = Is<FltLit>(left), rf = Is<FltLit>(right);
    if (lf && rf) {
        // An all-f32 expression computes in 32 bits (§3.1): fold at that
        // precision when both operands are f32-typed.
        auto f32 = Optimizer::IsF32T(left->exprtype) && Optimizer::IsF32T(right->exprtype);
        auto a = lf->val, b = rf->val;
        if (f32) { a = (float)a; b = (float)b; }
        switch (op) {
            case T_PLUS:  return o.NewFlt(this, f32 ? (double)(float)(a + b) : a + b);
            case T_MINUS: return o.NewFlt(this, f32 ? (double)(float)(a - b) : a - b);
            case T_MUL:   return o.NewFlt(this, f32 ? (double)(float)(a * b) : a * b);
            case T_DIV:   return o.NewFlt(this, f32 ? (double)(float)(a / b) : a / b);
            case T_LT:    return o.NewBool(this, a < b);
            case T_GT:    return o.NewBool(this, a > b);
            case T_LTEQ:  return o.NewBool(this, a <= b);
            case T_GTEQ:  return o.NewBool(this, a >= b);
            case T_EQ:    return o.NewBool(this, a == b);
            case T_NEQ:   return o.NewBool(this, a != b);
            default: break;  // % (fmod) is left to the runtime.
        }
        return this;
    }
    auto lb = Is<BoolLit>(left), rb = Is<BoolLit>(right);
    if (lb && rb && (op == T_EQ || op == T_NEQ))
        return o.NewBool(this, (lb->val == rb->val) == (op == T_EQ));
    return this;
}

inline Node *Dot::Opt(Optimizer &o) {
    obj = o.Opt(obj);
    if ((member == B_LEN || member == B_CAP) && Is<Ident>(obj)) {
        // .len of a fixed array / .cap of a static-capacity limited array are
        // compile-time constants; a plain variable receiver guarantees no
        // side effect is dropped with the access.
        auto t = obj->exprtype;
        if (t && t->kind == TY_REF && !t->ref->optional) t = t->ref->sub;
        if (t && t->kind == TY_ARRAY) {
            auto &a = *t->arr;
            if (member == B_LEN && a.akind == A_FIXED && a.size >= 0)
                return o.NewInt(this, a.size);
            if (member == B_CAP && a.akind == A_LIMITED && a.sizeexpr && a.size >= 0)
                return o.NewInt(this, a.size);
        }
    }
    return this;
}

inline Node *Call::Opt(Optimizer &o) {
    if (auto d = Is<Dot>(callee)) d->obj = o.Opt(d->obj);
    for (auto &a : args) a = o.Opt(a);
    if (fvbody) o.OptBlock(fvbody);
    if (builtin == B_ASSERT) {
        if (auto b = Is<BoolLit>(args[0]); b && b->val) {
            o.folded++;
            return o.EmptyBlock(this);
        }
    }
    if (auto r = o.TryInline(this)) return r;
    return this;
}

inline Node *Index::Opt(Optimizer &o) {
    obj = o.Opt(obj);
    idx = o.Opt(idx);
    return this;
}

inline Node *SliceExpr::Opt(Optimizer &o) {
    obj = o.Opt(obj);
    if (lo) lo = o.Opt(lo);
    if (hi) hi = o.Opt(hi);
    return this;
}

inline Node *AsCast::Opt(Optimizer &o) {
    child = o.Opt(child);
    auto tt = exprtype;  // The concrete target type, set at checking.
    if (auto i = Is<IntLit>(child)) {
        if (tt->kind == TY_INT) {
            if (unchecked) return o.NewInt(this, Optimizer::WrapStorage(i->val, tt->intstorage));
            // `as` range-checks in debug: fold only a fitting value.
            if (TypeCheck::FitsIntStorage(i->val, tt->intstorage)) return o.NewInt(this, i->val);
        } else if (tt->kind == TY_FLT) {
            auto d = (double)i->val;
            if (Optimizer::IsF32T(tt)) d = (double)(float)d;
            // Checked casts fold only when the conversion is exact; ±2^53
            // (2^24 for f32) guarantees that without round-trip games.
            auto lim = Optimizer::IsF32T(tt) ? (int64_t)1 << 24 : (int64_t)1 << 53;
            if (unchecked || (i->val > -lim && i->val < lim)) return o.NewFlt(this, d);
        }
        return this;
    }
    if (auto fl = Is<FltLit>(child)) {
        if (tt->kind == TY_INT) {
            auto d = fl->val;
            // In-range is required either way (out-of-range truncation is for
            // the runtime to define); `as` additionally needs an integral
            // value that fits the storage exactly.
            if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
                auto t = (int64_t)d;
                if (unchecked) return o.NewInt(this, Optimizer::WrapStorage(t, tt->intstorage));
                if ((double)t == d && TypeCheck::FitsIntStorage(t, tt->intstorage))
                    return o.NewInt(this, t);
            }
        } else if (tt->kind == TY_FLT) {
            if (!Optimizer::IsF32T(tt)) return o.NewFlt(this, fl->val);
            auto f = (double)(float)fl->val;
            if (unchecked || f == fl->val) return o.NewFlt(this, f);
        }
        return this;
    }
    return this;
}

inline Node *RangeExpr::Opt(Optimizer &o) {
    lo = o.Opt(lo);
    hi = o.Opt(hi);
    return this;
}

inline Node *ArrayLit::Opt(Optimizer &o) {
    for (auto &e : elems) e = o.Opt(e);
    if (fillval) fillval = o.Opt(fillval);
    if (fillcount) fillcount = o.Opt(fillcount);
    if (capexpr) capexpr = o.Opt(capexpr);
    return this;
}

inline Node *StructLit::Opt(Optimizer &o) {
    for (auto &fi : inits) fi.val = o.Opt(fi.val);
    return this;
}

inline Node *Block::Opt(Optimizer &o) {
    o.OptBlock(this);
    // A statement-less block is just its tail expression. Branch and body
    // slots are typed Block* and optimized in place elsewhere, so this
    // replacement only lands in general expression positions.
    if (stmts.empty() && tail) return tail;
    return this;
}

inline Node *IfExpr::Opt(Optimizer &o) {
    cond = o.Opt(cond);
    if (auto b = Is<BoolLit>(cond)) {
        Node *taken = b->val ? (Node *)thenb : elseb;
        if (!taken) { o.folded++; return o.EmptyBlock(this); }
        // The branch replaces the if only when its value type agrees (a
        // merged flt/f32 branch keeps the if).
        if (exprtype->kind == TY_VOID || Optimizer::SameType(taken->exprtype, exprtype)) {
            o.folded++;
            return o.Opt(taken);
        }
    }
    o.OptBlock(thenb);
    if (elseb) elseb = o.Opt(elseb);
    return this;
}

inline Node *MatchExpr::Opt(Optimizer &o) {
    scrutinee = o.Opt(scrutinee);
    if (auto iv = Is<IntLit>(scrutinee)) {
        // An integer match on a constant selects its arm statically (first
        // matching arm; a _ arm is guaranteed present).
        MatchArm *sel = nullptr;
        for (auto &arm : arms) {
            auto hit = arm.pat.kind == P_WILDCARD ||
                       ((arm.pat.kind == P_INT || arm.pat.kind == P_RANGE) &&
                        iv->val >= arm.lo && iv->val < arm.hi);
            if (hit) { sel = &arm; break; }
        }
        if (sel && (exprtype->kind == TY_VOID ||
                    Optimizer::SameType(sel->body->exprtype, exprtype))) {
            o.folded++;
            return o.Opt(sel->body);
        }
    }
    for (auto &arm : arms) arm.body = o.Opt(arm.body);
    return this;
}

inline Node *EarlyBlock::Opt(Optimizer &o) {
    o.OptBlock(body);
    return this;
}

inline Node *While::Opt(Optimizer &o) {
    cond = o.Opt(cond);
    if (auto b = Is<BoolLit>(cond); b && !b->val) {
        o.folded++;
        return o.EmptyBlock(this);
    }
    o.OptBlock(body);
    return this;
}

inline Node *LoopExpr::Opt(Optimizer &o) {
    o.OptBlock(body);
    return this;
}

inline Node *ForLoop::Opt(Optimizer &o) {
    iter = o.Opt(iter);
    o.OptBlock(body);
    return this;
}

inline Node *Guard::Opt(Optimizer &o) {
    cond = o.Opt(cond);
    if (auto b = Is<BoolLit>(cond)) {
        o.folded++;
        if (b->val) return o.EmptyBlock(this);   // Always passes.
        if (elseb) return o.Opt(elseb);          // Always takes the (diverging) else.
        if (implicitexit == 1) {
            auto br = o.ast.New<Break>(line, nullptr);
            br->exprtype = o.ast.voidtype;
            return br;
        }
        assert(implicitexit == 2 && o.cursf);
        auto r = o.ast.New<Return>(line);
        r->target = o.cursf;
        r->exprtype = o.ast.voidtype;
        return r;
    }
    if (elseb) o.OptBlock(elseb);
    return this;
}

inline Node *Return::Opt(Optimizer &o) {
    for (auto &v : vals) v = o.Opt(v);
    return this;
}

inline Node *Break::Opt(Optimizer &o) {
    if (val) val = o.Opt(val);
    return this;
}

inline Node *Assign::Opt(Optimizer &o) {
    // The lval is safe to walk: written or address-taken variables are never
    // in consts, so their Idents pass through untouched.
    lval = o.Opt(lval);
    rhs = o.Opt(rhs);
    return this;
}

inline Node *IncDec::Opt(Optimizer &o) {
    lval = o.Opt(lval);
    return this;
}

inline Node *InlineBlock::Opt(Optimizer &o) {
    o.OptBlock(body);
    return this;
}

// VarDecls are handled by Optimizer::OptStmt (they can be dropped, which the
// return-a-node shape cannot express); the rest never appear inside bodies.
inline Node *VarDecl::Opt(Optimizer &) { assert(false); return this; }
inline Node *FunVal::Opt(Optimizer &) { assert(false); return this; }
inline Node *StructDecl::Opt(Optimizer &) { assert(false); return this; }
inline Node *EnumDecl::Opt(Optimizer &) { assert(false); return this; }
inline Node *AliasDecl::Opt(Optimizer &) { assert(false); return this; }

}  // namespace goose
