// Goose compiler — the optimizer: function inlining, constant folding, and
// constant propagation, applied in place to the typechecked specialization
// bodies that codegen consumes. The one hard rule: no rewrite may change
// what a release build computes, and an operation that would abort at
// runtime (division by zero, a failed `as` range check, an out-of-bounds
// index) is never folded into a value or folded away. Debug-build signed
// overflow is the one detection not pinned to source order: it happens per
// operation as it executes (§6.2), so regrouping an associative chain moves
// which intermediate value can trip it.
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
// locals in a way the copy cannot follow: a remaining (non-inlined) call to a
// spec with bound function values means those bodies reach our locals as
// free variables, so splicing us elsewhere (which remaps our VarDefs) would
// strand them. Ditto a remaining callee that does `return ... from` us. A
// remaining call to a nested function reaches them too, but only through
// what the call passes: the copy of such a call records the copies of our
// variables it passes in their place (Call::fvremap). Once such calls are
// themselves inlined the references live inside our own tree, get remapped
// with everything else, and the restrictions lift.
//
// Thresholds per call site of callee K: inline if K is used once anywhere,
// or nodecount(K) < NC, or nodecount(K) * uses(K) < NCU, which a call site
// inside a loop raises to LOOPNCU * NCU. A K used once that only that first
// rule would inline stays out of line where the call is in a cold branch,
// the else of a guard or the body of an early return or break, and where
// its body would make a caller with several call sites too big to inline
// at them (OptBody).
// -O0: no inlining; -O1: NC=8, NCU=48; -O2: NC=16, NCU=96.
// Whatever the size, the C blocks around the call plus those K's body nests
// must stay within MAXNEST (see Around): C compilers limit how deep blocks
// nest in one function, and a chain of single-use functions, each calling
// the next, would otherwise fold into one body as deep as the chain is long.
// Folding and propagation run at every level.
//
// The per-node work is the Cp1 and Opt virtuals (declared in ast.h); their
// implementations live together at the end of this file — all of Cp1 (the
// inline copier), then all of Opt (fold/propagate/inline) — so each sub-pass
// reads top to bottom. The Optimizer/Inliner structs hold the shared state
// and machinery those implementations use.
#pragma once

namespace goose {

struct BaseCaseInliner;   // optimize_basecase.h.

struct Optimizer {
    Ast &ast;
    int nc = 0, ncu = 0;         // Inline thresholds; 0 = inlining off.
    bool caninline = false;
    FnSpec *curspec = nullptr;   // Specialization being optimized (null: globals).
    SFunction *cursf = nullptr;
    int inlined = 0, folded = 0, propagated = 0, tailloops = 0;

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

    // Inlining decisions use only this optimizer's classification of a body.
    // Keep it here rather than as annotations exposed to subsequent passes.
    // nestedcalls: a remaining call to a nested function, which reaches the
    // body's captured variables through pointers the call passes.
    struct InlineInfo {
        int nodecount = 0;
        int nest = 0;
        bool noinline = false;
        // Goose's inliner cannot paste this body into a caller, but the C
        // compiler could inline the function: TryInline asks it to
        // (FnSpec::cinline).
        bool c_inlinable = false;
        bool nestedcalls = false;
    };
    unordered_map<FnSpec *, InlineInfo> inlineinfo;

    // The C blocks around the node being optimized (Around).
    int depth = 0;
    // The loops around it, in the body being optimized and, for a copy
    // being re-folded, around the call it replaced. A call site in a loop,
    // outside its cold branches, runs once per iteration: a callee LOOPNCU
    // times the size the use rule allows is worth its copy there, which is
    // what keeps a small wrapper around inlined code from staying a call
    // inside a hot loop.
    int loopdepth = 0;
    static constexpr int LOOPNCU = 4;
    // The cold branches around it (IfExpr::Opt): a call there runs at most
    // once per run of the body or the loop it leaves, mostly on an error
    // path, so a callee inlined there only because it has no other caller
    // would grow the hot path of its caller for nothing.
    int colddepth = 0;
    // The branches around it that a run of the body may not take: the arms
    // of an `if` or `match`, the right operand of && and ||.
    int branchdepth = 0;
    // While set, a callee only the used-once rule would inline, called on
    // such a branch outside any loop, is not inlined but collected here, for
    // OptBody to weigh against the size of the body it is optimizing.
    bool deferonce = false;
    vector<FnSpec *> deferred;

    // Whether block b ends by leaving: a return, a break, or abort or exit.
    static bool Leaves(Node *b) {
        auto blk = Is<Block>(b);
        if (!blk || blk->tail || blk->stmts.empty()) return false;
        auto last = blk->stmts.back();
        if (Is<Return>(last) || Is<Break>(last)) return true;
        auto c = Is<Call>(last);
        return c && (c->builtin == B_ABORT || c->builtin == B_EXIT);
    }
    // The deepest C nesting an inlined body may reach: half of MSVC's limit
    // of 128 blocks in a function. The rest is for the blocks codegen opens
    // around runtime work, which Around does not count.
    static constexpr int MAXNEST = 64;

    // ------------------------------------------------------------------
    // Small helpers.

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
            case TY_INT:  return a->intstorage == b->intstorage;
            case TY_FLT:  return a->fltstorage == b->fltstorage;
            case TY_BOOL: case TY_VOID: return true;
            default: return false;
        }
    }

    // The integer type an already-checked node computes at; null when it is
    // not a (non-varint) integer.
    static TypeExpr *IntTypeOf(Node *n) {
        auto t = n->exprtype;
        return t && t->kind == TY_INT && t->intstorage != IS_VARINT ? t : nullptr;
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
        // A named constant `~c` reads at the width its use adapted it to
        // (Val::notconst), where its value is i64's ~c wrapped.
        auto it = usetype && usetype->kind == TY_INT ? usetype->intstorage : IS_I64;
        if (auto i = Is<IntLit>(lit); i && it != IS_VARINT && !i->uns) {
            auto v = WrapStorage(i->val, it);
            r = ast.New<IntLit>(lit->line, v, v == i->val ? i->text : string_view {},
                                it == IS_U64 && v < 0);
        } else if (auto i = Is<IntLit>(lit)) r = ast.New<IntLit>(lit->line, i->val, i->text, i->uns);
        else if (auto f = Is<FltLit>(lit)) r = ast.New<FltLit>(lit->line, f->val, f->text);
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
        if (!sp->body) return;   // An extern fn: no body, never inlined (§7.10).
        ReachTree(sp->body);
        postorder.push_back(sp);
    }

    // What the program runs from: main, the thread entry points, and every
    // tree outside a function body.
    void ReachRoots() {
        auto mainsf = ast.MainFunction();
        if (mainsf && !mainsf->specs.empty()) Reach(mainsf->specs[0]);
        for (auto sf : ast.functions)
            if ((sf->isthread || sf->isexport) && !sf->specs.empty()) Reach(sf->specs[0]);
        ast.ForEachRootTree([&](Node *n) { ReachTree(n); });
    }

    void ReachTree(Node *n) {
        if (!n) return;
        if (auto c = Is<Call>(n)) {
            if (c->spec) {
                if (c->builtin < 0) c->spec->uses++;  // thread_spawn is not a call site.
                Reach(c->spec);
            }
            for (auto &fs : c->fmtspecs) {   // User format overloads print reaches (§3.7).
                fs.second->uses++;
                Reach(fs.second);
            }
            for (auto d : c->dispatch) { d->uses++; Reach(d); }
        }
        RunChildren(n, [&](Node *ch) { ReachTree(ch); });
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
            // A format overload taking by reference a variable print, str or
            // format renders is handed the variable (§3.7), as `&x` would be.
            auto an = c->ArgNodes();
            for (size_t k = 0; k < an.size(); k++)
                if (auto id = Is<Ident>(an[k]); id && id->vdef && HookedByRef(c, k))
                    facts[id->vdef].addrof++;
        }
        RunChildren(n, [&](Node *ch) { Analyze(ch); });
    }

    // ------------------------------------------------------------------
    // Inlining support.

    Node *TryInline(Call *c);   // Defined after Inliner below.

    // C nesting. Codegen opens a C block for every Block -- a function or
    // inlined body, an arm, a loop body -- but the then-block of a flat if
    // (IfExpr::flat), whose contents go in the block the if is in, which
    // Around takes back. It opens one around a child of some other nodes
    // too, which Around counts: an `else` that is not a Block, a match arm
    // (two: a switch and its case), the right operand of && and ||, a while
    // condition (tested inside the loop), the arguments of a function-value
    // call (bound inside its block) and an array's fill value (built in a
    // loop). Scan and the Opt walk count both to keep inlining within
    // MAXNEST.
    static int Around(Node *n, Node *ch) {
        if (auto fi = Is<IfExpr>(n)) {
            if (ch == fi->thenb) return fi->flat ? -1 : 0;
            return ch == fi->elseb && !Is<Block>(ch) ? 1 : 0;
        }
        if (auto m = Is<MatchExpr>(n)) return ch != m->scrutinee ? 2 : 0;
        if (auto b = Is<Binary>(n))
            return ch == b->right && (b->op == T_ANDAND || b->op == T_OROR) ? 1 : 0;
        if (auto w = Is<While>(n)) return ch == w->cond ? 1 : 0;
        if (auto c = Is<Call>(n)) return c->fvbody && ch != c->callee && ch != c->fvbody ? 1 : 0;
        if (auto al = Is<ArrayLit>(n)) return ch == al->fillval ? 1 : 0;
        return 0;
    }

    // Base-case inlining for self-recursive functions: the pass and what it
    // is for live in optimize_basecase.h, which defines these two — it needs
    // the Inliner below, so it is included after us and holds the state.
    unique_ptr<BaseCaseInliner> basecase;
    void SetupBaseCase(FnSpec *sp);   // Records the base case of sp, if any.
    Node *TryBaseCase(Call *c);       // Rewrites one self-call against it.
    int basecases = 0;

    // ------------------------------------------------------------------
    // The fold/propagate/inline walk. Opt returns the (possibly replaced)
    // node; statements go through OptStmt, which may drop them.

    Node *Opt(Node *n) {
        assert(n);
        return n->Opt(*this);
    }

    // Opt for ch, a child of n, at the depth codegen nests it.
    Node *OptIn(Node *n, Node *ch) {
        auto k = Around(n, ch);
        depth += k;
        ch = Opt(ch);
        depth -= k;
        return ch;
    }

    // The operand OptViewed is at, so that a field or element viewed in turn
    // views its base (Dot::Opt, Index::Opt).
    Node *viewed = nullptr;

    // Whether the field or element n is viewed: it is the operand OptViewed
    // is at, or an array a slice destination takes whole, which has the
    // slice's type (§3.10).
    bool Viewed(Node *n) {
        return viewed == n || (n->exprtype && n->exprtype->kind == TY_SLICE);
    }

    // Opt for an operand viewed where it stands rather than copied out
    // (§9.2): sliced, iterated by a `for`, referenced, a member builtin's
    // receiver, indexed by an index that runs code, or a field or element
    // of such. The value of a call, bare block, `if` or `match` is a
    // temporary there, a copy nothing else can write or shrink while the
    // view lasts. Inlining a single-expression body (TryInline) or folding
    // the construct to the branch taken can reduce it to the storage it was
    // copied from, which the rest of the statement may write or shrink
    // under the view: such a path goes back into a block, which codegen
    // evaluates into a temporary as the construct would have -- one to an
    // aggregate a view sees into (NamesStorage), or with `anytype` one of
    // any type (OptRendered).
    Node *OptViewed(Node *n, bool anytype = false) {
        auto outer = viewed;
        viewed = n;
        auto r = Opt(n);
        viewed = outer;
        if (r == n || !(anytype ? IsPath(r) : NamesStorage(r))) return r;
        auto b = ast.New<Block>(r->line);
        b->tail = r;
        b->exprtype = r->exprtype;
        return b;
    }

    // Opt for an argument print, str or format renders while user format
    // overloads run (§3.7): it is read where it stands around them, and one
    // taking it, or a part of it, by reference is handed where it lies. They
    // may write that storage, and the checker takes the value of a call,
    // bare block, `if` or `match` for a temporary there too (CheckPrintable),
    // a copy they cannot reach, so a path it is reduced to goes back into a
    // block as a viewed one does, a slice's or a scalar's as well.
    Node *OptRendered(Node *n) { return OptViewed(n, true); }

    // Whether argument k of c, in ArgNodes' numbering, is one print, str or
    // format renders with user format overloads among what it runs.
    static bool Hooked(Call *c, size_t k) {
        size_t first = c->builtin == B_FORMAT;   // format's destination is not rendered.
        return k >= first && k - first < c->fmtcontexts.size() &&
               !c->fmtcontexts[k - first]->fmtspecs.empty();
    }

    // Whether one of those overloads takes what it renders by reference.
    static bool HookedByRef(Call *c, size_t k) {
        if (!Hooked(c, k)) return false;
        for (auto &fs : c->fmtcontexts[k - (c->builtin == B_FORMAT)]->fmtspecs)
            if (fs.second->argtypes[1]->kind == TY_REF) return true;
        return false;
    }

    // What codegen addresses where it lies (GenLoc): a variable, a field or
    // element of one, directly or through a reference or slice, or `&` of
    // such a path.
    static bool IsPath(Node *n) {
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND && u->child->exprtype &&
                                   u->child->exprtype->kind != TY_REF)
            n = u->child;
        auto d = Is<Dot>(n);
        return Is<Ident>(n) || (d && d->IsField()) || Is<Index>(n);
    }

    // A path to a value in storage that a view can see into.
    static bool NamesStorage(Node *n) {
        if (!IsPath(n)) return false;
        auto t = n->exprtype;
        return t && (t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_ENUM ||
                     t->kind == TY_VARIANT);
    }

    // Whether evaluating n runs nothing that could write storage: literals
    // and reads of variables, fields and elements, combined by operators.
    static bool CodeFree(Node *n) {
        if (AsLiteral(n) || Is<Ident>(n)) return true;
        if (auto u = Is<Unary>(n)) return CodeFree(u->child);
        if (auto b = Is<Binary>(n)) return CodeFree(b->left) && CodeFree(b->right);
        if (auto c = Is<AsCast>(n)) return CodeFree(c->child);
        if (auto d = Is<Dot>(n)) return CodeFree(d->obj);
        if (auto ix = Is<Index>(n)) return CodeFree(ix->obj) && CodeFree(ix->idx);
        return false;
    }

    void OptBlock(Block *b) {
        depth++;
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
        depth--;
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
    // Post-optimization classification: final node count, C nesting, and
    // whether this body may be spliced into callers (see the header comment).

    void Scan(FnSpec *sp) {
        auto &info = inlineinfo[sp];
        info.nodecount = 0;
        info.nest = 0;
        info.nestedcalls = false;
        // A simd body inlined into a caller would lose its versions (§7.12).
        auto noin = sp->sf->isrec || sp->incycle || sp->sf->isthread || sp->sf->isexport ||
                    sp->sf->issimd || sp->relnamedresult;
        auto multiret = sp->rets.size() > 1;
        function<void(Node *, int)> rec = [&](Node *n, int d) {
            if (!n) return;
            info.nodecount++;
            if (Is<Block>(n)) info.nest = max(info.nest, ++d);   // Its children sit inside it.
            if (auto c = Is<Call>(n)) {
                auto callee = [&](FnSpec *k) {
                    if (!k) return;
                    // A separate body referencing our locals that a copy
                    // of the call cannot point elsewhere (bound function
                    // values), or unwinding to us: our body must stay a
                    // real frame.
                    if (!k->fnvals.empty()) noin = true;
                    if (k->needs.count(sp)) noin = true;
                    if (k->lexparent) info.nestedcalls = true;
                };
                if (c->builtin < 0) callee(c->spec);
                for (auto k : c->dispatch) callee(k);
            }
            RunChildren(n, [&](Node *ch) { rec(ch, d + Around(n, ch)); });
        };
        rec(sp->body, 0);
        info.noinline = noin || multiret;
        info.c_inlinable = multiret && !noin;
    }

    // Optimizes the body of sp. A callee K used once that only the used-once
    // rule would inline adds no code where sp is a real function, but sp's
    // copies carry K's body wherever sp is inlined, and a sp small enough to
    // inline at its several call sites (in a loop, at the most) may no longer
    // be once it holds K. Where a run of sp need not call K, the call to K
    // costs less than the call to sp it would bring back. So for such a sp
    // the body is first optimized without those callees, and they are
    // inlined, in the order the walk met them, only while sp stays small
    // enough or where it never was. The rest keep their calls, which each
    // copy of sp repeats: K then has as many uses as sp, which is what its
    // ordinary inlining is decided by.
    void OptBody(FnSpec *sp) {
        deferonce = sp->uses > 1;
        deferred.clear();
        OptBlock(sp->body);
        deferonce = false;
        if (deferred.empty()) return;
        Scan(sp);
        auto &info = inlineinfo[sp];
        auto maxsize = max(nc - 1, (ncu * LOOPNCU - 1) / sp->uses);
        auto inlinable = !info.noinline && info.nodecount <= maxsize;
        auto size = info.nodecount;
        auto any = false;
        for (auto K : deferred) {
            auto ksize = inlineinfo[K].nodecount;
            if (!inlinable || size + ksize <= maxsize) {
                size += ksize;
                any = true;
            } else {
                K->uses = sp->uses;
            }
        }
        // Once more, where the callees still used once now inline: the walk
        // takes the same decisions as before for everything else.
        if (any) OptBlock(sp->body);
    }

    // Accumulator tail-recursion elimination, defined in optimize_tre.h.
    void TailRecurse(FnSpec *sp);

    // Marks the loops codegen restates for the C compiler, over the final
    // live bodies; defined in optimize_loops.h.
    void ShapeLoops();

    // ------------------------------------------------------------------
    // Global initializers: fold (and on the second pass inline into) each
    // init, and register constant scalar globals for propagation everywhere.

    void OptGlobal(VarDecl *g) {
        depth = 1;   // In the body of the C function that sets the globals up.
        for (auto &i : g->inits) i = Opt(i);
        depth = 0;
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
        ReachRoots();
        // Write/address facts across every live body, before any rewriting.
        for (auto sp : postorder) if (sp->body) Analyze(sp->body);
        ast.ForEachRootTree([&](Node *n) { Analyze(n); });
        // Globals first (fold only), so constant let globals propagate into
        // every body below.
        caninline = false;
        for (auto g : ast.globals) OptGlobal(g);
        // Every reachable specialization, callees before callers.
        caninline = nc > 0;
        for (auto sp : postorder) {
            curspec = sp;
            cursf = sp->sf;
            SetupBaseCase(sp);
            OptBody(sp);
            TailRecurse(sp);
            Scan(sp);
        }
        curspec = nullptr;
        cursf = nullptr;
        // Globals again, now able to inline into their initializers.
        for (auto g : ast.globals) OptGlobal(g);
        // Final liveness over the rewritten trees: specs whose every call got
        // inlined (or folded away) go dead, so codegen can skip them.
        for (auto sp : ast.fnspecs) { sp->live = false; sp->uses = 0; }
        postorder.clear();
        ReachRoots();
        ShapeLoops();
    }

    // Optimized bodies for eyeballing (--specs); not reparseable.
    void DumpSpecs(string &s) {
        for (auto sp : ast.fnspecs) {
            if (!sp->live) continue;
            auto &info = inlineinfo[sp];
            Append(s, "// spec ", sp->id, ": uses ", sp->uses, ", nodes ", info.nodecount,
                   ", nest ", info.nest, info.noinline ? ", noinline" : "", "\n");
            Append(s, "fn ", sp->sf->qname, "(");
            for (size_t i = 0; i < sp->params.size(); i++) {
                if (i) s += ", ";
                Append(s, sp->params[i]->name, ": ");
                sp->params[i]->type->Dump(s);
            }
            s += ") ";
            if (sp->body) sp->body->Dump(s, 0);
            else s += ";";
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
    // The body still calls a nested function, which reaches the captured
    // variables of the copy through pointers (Call::fvremap): they stay
    // captured, and none is substituted away.
    bool keepcaptured = false;
    // Variables src does not own that the copy names under another one: the
    // copies the call being inlined passes for them (Call::fvremap).
    unordered_map<VarDef *, VarDef *> outer;

    VarDef *Remap(VarDef *v) {
        if (!v) return v;
        if (v->ownerspec != src) {
            auto it = outer.find(v);
            return it != outer.end() ? it->second : v;
        }
        auto it = vmap.find(v);
        if (it != vmap.end()) return it->second;
        auto nv = ast.NewVarDef();
        *nv = *v;
        nv->ownerspec = dst;
        nv->isparam = false;
        // Otherwise every use of the copy lives in the copied tree.
        nv->captured = keepcaptured && v->captured;
        vmap[v] = nv;
        auto fit = o.facts.find(v);
        if (fit != o.facts.end()) o.facts[nv] = fit->second;
        return nv;
    }

    // The free variables a copied call to a nested function passes: what the
    // original call passed, as copied, and every captured variable of the
    // body this copy replaced.
    void RemapFreeVars(const Call *from, Call *to) {
        auto add = [&](VarDef *v, VarDef *nv) {
            if (v == nv) return;
            for (auto &p : to->fvremap) if (p.first == v) return;
            to->fvremap.push_back({ v, nv });
        };
        for (auto &p : from->fvremap) add(p.first, Remap(p.second));
        for (auto &kv : vmap) if (kv.first->captured) add(kv.first, kv.second);
        for (auto &kv : outer) add(kv.first, kv.second);
    }

    // Bind one runtime argument, or substitute an immutable scalar literal.
    // Remap owns all copied-variable state, including the write/address facts
    // that later folds use, for ordinary and recursive base-case inlining.
    VarDecl *BindArg(VarDef *pv, Node *arg, Line ln) {
        auto &f = o.facts[pv];
        if (Optimizer::AsLiteral(arg) && Optimizer::ScalarType(pv->type) &&
            f.writes == 0 && f.addrof == 0 && !(keepcaptured && pv->captured)) {
            subst[pv] = arg;
            return nullptr;
        }
        auto vd = ast.New<VarDecl>(ln, pv->isvar);
        vd->names.push_back(pv->name);
        vd->defs.push_back(Remap(pv));
        vd->inits.push_back(arg);
        vd->exprtype = ast.voidtype;
        return vd;
    }

    Node *Cp(const Node *n) {
        auto r = n->Cp1(*this);
        r->exprtype = n->exprtype;
        return r;
    }

    Block *CpBlock(const Block *b) { return (Block *)Cp(b); }

    // A copied variable's provenance still names the variables of the body
    // it was copied from; point it at their copies, so that an analysis
    // following a copied reference to its root (BCE's aliasing) lands on the
    // variable the copied body declares. A root the copy does not own -- an
    // outer local, a parameter's class root -- stays as it is.
    void RemapProvenance() {
        auto remap = [&](VarDef *&r) {
            auto it = r ? vmap.find(r) : vmap.end();
            if (it != vmap.end()) r = it->second;
        };
        for (auto &kv : vmap) {
            auto nv = kv.second;
            if (nv == kv.first) continue;
            for (auto &a : nv->ref.alts) {
                remap(a.root);
                remap(a.from);
            }
            for (auto &a : nv->contents.alts) {
                remap(a.root);
                remap(a.from);
            }
        }
    }
};

inline Node *Optimizer::TryInline(Call *c) {
    if (!caninline || !c->spec || c->builtin >= 0 || !c->dispatch.empty()) return nullptr;
    auto K = c->spec;
    auto &info = inlineinfo[K];
    if (!K->live || !K->body) return nullptr;
    auto ncuhere = loopdepth > 0 && !colddepth ? ncu * LOOPNCU : ncu;
    // An `inline fn` passes the size rules wherever inlining is on.
    auto small = (K->sf->isinline && nc > 0) || info.nodecount < nc || info.nodecount * K->uses < ncuhere;
    if (info.noinline) {
        // Goose would inline it here but cannot: let the C compiler do it.
        if (info.c_inlinable && small && !K->outofline) K->cinline = true;
        return nullptr;
    }
    // Never into a recursive cycle: the inlined body's locals would become
    // the cycle function's own, upsetting the §7.8 stack-assignment rule.
    if (curspec && (curspec->incycle || curspec->sf->isrec)) return nullptr;
    if (!small && K->uses == 1 && colddepth > 0) {
        K->outofline = true;
        return nullptr;
    }
    if (!(K->uses == 1 || small)) return nullptr;
    // The body's blocks would open `depth` deep. Past the limit the call
    // stays, and a chain of single-use functions folds into one body per
    // MAXNEST levels rather than one as deep as the chain.
    if (depth + info.nest > MAXNEST) return nullptr;
    // A global initializer's locals belong to no specialization, so its
    // copies could not be told from the variables they replace.
    if (info.nestedcalls && !curspec) return nullptr;
    auto argnodes = c->ArgNodes();
    if (argnodes.size() != K->params.size()) return nullptr;
    if (!small && deferonce && branchdepth > 0 && loopdepth == 0) {
        deferred.push_back(K);
        return nullptr;
    }
    Inliner inl { *this, ast, K, curspec, {}, {} };
    inl.keepcaptured = info.nestedcalls;
    for (auto &p : c->fvremap) inl.outer[p.first] = p.second;
    vector<Node *> decls;
    for (size_t i = 0; i < K->params.size(); i++) {
        if (auto vd = inl.BindArg(K->params[i], argnodes[i], c->line)) {
            vd->inline_arg = true;
            decls.push_back(vd);
        }
    }
    auto body = inl.CpBlock(K->body);
    inl.RemapProvenance();
    body->stmts.insert(body->stmts.begin(), decls.begin(), decls.end());
    auto ib = ast.New<InlineBlock>(c->line, K->sf, K, body);
    ib->exprtype = c->exprtype;
    inlined++;
    // Re-fold the copy so substituted constants cascade — the one re-visit
    // the single-pass design allows, and only over fresh nodes.
    OptBlock(body);
    // Trivial results unwrap to plain expressions — but only when the value's
    // type survives as-is (a reference return decayed at the call site, or an
    // ADT result received in its other mode, must keep the InlineBlock, whose
    // exprtype records the type it converts to).
    auto unwrapok = [&](Node *v) {
        if (SameType(v->exprtype, c->exprtype)) return true;
        auto a = v->exprtype, b = c->exprtype;
        return a && b && a->kind == b->kind && a->kind != TY_REF && a->kind != TY_ENUM;
    };
    if (body->stmts.empty() && body->tail && !ReturnsTo(body->tail, K->sf) &&
        unwrapok(body->tail))
        return body->tail;
    if (body->stmts.empty() && !body->tail) return EmptyBlock(c);
    if (body->stmts.size() == 1 && !body->tail) {
        if (auto r = Is<Return>(body->stmts[0]); r && r->target == K->sf) {
            if (r->vals.empty()) return EmptyBlock(c);
            if (r->vals.size() == 1 && !ReturnsTo(r->vals[0], K->sf) &&
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

inline Node *IntLit::Cp1(Inliner &inl) const { return inl.ast.New<IntLit>(line, val, text, uns); }
inline Node *FltLit::Cp1(Inliner &inl) const { return inl.ast.New<FltLit>(line, val, text); }
inline Node *BoolLit::Cp1(Inliner &inl) const { return inl.ast.New<BoolLit>(line, val); }
inline Node *StrLit::Cp1(Inliner &inl) const {
    return inl.ast.New<StrLit>(line, val, multiline);
}
inline Node *NullLit::Cp1(Inliner &inl) const { return inl.ast.New<NullLit>(line); }
inline Node *SelfRef::Cp1(Inliner &inl) const { return inl.ast.New<SelfRef>(line); }
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
    auto c = inl.ast.New<Ident>(line, name, ns);
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
    for (auto &fi : inits) c->inits.push_back({ fi.name, inl.Cp(fi.val), fi.fromdefault });
    c->defaultall = defaultall;
    c->sinst = sinst;
    c->einst = einst;
    c->variant = variant;
    c->fieldindices = fieldindices;
    c->sourcefieldindices = sourcefieldindices;
    return c;
}

inline Node *Unary::Cp1(Inliner &inl) const {
    return inl.ast.New<Unary>(line, op, inl.Cp(child));
}

inline Node *Binary::Cp1(Inliner &inl) const {
    return inl.ast.New<Binary>(line, op, inl.Cp(left), inl.Cp(right));
}

inline Node *Dot::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Dot>(line, inl.Cp(obj), name, ns);
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
    c->firstdefault = firstdefault;
    c->ndefaults = ndefaults;
    c->trailing = trailing;   // Shared template; the checked fvbody below is what runs.
    c->spec = spec;
    c->dispatch = dispatch;
    c->dispatcharg = dispatcharg;
    c->builtin = builtin;
    c->poolcheck = poolcheck;
    c->shaderblob = shaderblob;
    c->fvtarget = fvtarget;
    c->rettypes = rettypes;
    c->fmtspecs = fmtspecs;
    c->fmtcontexts = fmtcontexts;
    for (auto p : fvparams) c->fvparams.push_back(inl.Remap(p));
    c->fvbody = fvbody ? inl.CpBlock(fvbody) : nullptr;
    // Case functions nested in one scope are dispatch targets too.
    auto nested = spec && spec->lexparent;
    for (auto d : dispatch) nested = nested || d->lexparent;
    if (nested) inl.RemapFreeVars(this, c);
    c->defaultinit = defaultinit ? inl.Cp(defaultinit) : nullptr;
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
    c->cmpview = cmpview;
    return c;
}

inline Node *AsCast::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<AsCast>(line, inl.Cp(child), type, unchecked);
    c->implicit = implicit;
    c->totype = totype;
    return c;
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
    auto c = inl.ast.New<IfExpr>(line, inl.Cp(cond), inl.CpBlock(thenb),
                                 elseb ? inl.Cp(elseb) : nullptr);
    c->flat = flat;
    return c;
}

inline Node *MatchExpr::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<MatchExpr>(line, inl.Cp(scrutinee));
    for (auto &arm : arms) {
        MatchArm a;
        a.pat = arm.pat;
        for (auto &pi : a.pat.items) {
            pi.lo = inl.Cp(pi.lo);
            if (pi.hi) pi.hi = inl.Cp(pi.hi);
        }
        a.variants = arm.variants;
        a.binder = inl.Remap(arm.binder);
        a.ranges = arm.ranges;
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
    c->vartype = vartype;
    c->idxtype = idxtype;
    c->vdef = inl.Remap(vdef);
    c->idxdef = inl.Remap(idxdef);
    c->iterkind = iterkind;
    return c;
}

inline Node *Return::Cp1(Inliner &inl) const {
    auto r = inl.ast.New<Return>(line);
    for (auto v : vals) r->vals.push_back(inl.Cp(v));
    r->from = from;
    r->ns = ns;
    r->target = target;
    r->targetspec = targetspec;
    return r;
}

inline Node *Break::Cp1(Inliner &inl) const {
    return inl.ast.New<Break>(line, val ? inl.Cp(val) : nullptr);
}

inline Node *VarDecl::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<VarDecl>(line, isvar);
    c->isconst = isconst;
    c->reusable = reusable;
    c->isglobal = isglobal;
    c->byref = byref;
    c->inline_arg = inline_arg;
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
inline Node *SelfRef::Opt(Optimizer &) { return this; }
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
    child = op == T_BITAND ? o.OptViewed(child) : o.Opt(child);
    // - and ~ compute at their operand's type (§6.1); the node's own exprtype
    // is the slot the result lands in, which can be wider.
    switch (op) {
        case T_MINUS:
            if (auto i = Is<IntLit>(child)) {
                auto t = Optimizer::IntTypeOf(child);
                if (!t) break;
                // The literal 2^63 carries i64.min's bits and is the one u64
                // constant the checker lets `-` take: its negation is i64.min
                // exactly. Any other negation that leaves the type overflows;
                // the runtime decides.
                if (i->uns) {
                    if (i->val != INT64_MIN) break;
                    return o.NewInt(this, INT64_MIN);
                }
                if (i->val == INT64_MIN || !FitsIntStorage(-i->val, false, t->intstorage))
                    break;
                return o.NewInt(this, -i->val);
            }
            if (auto fl = Is<FltLit>(child)) return o.NewFlt(this, -fl->val);
            break;
        case T_BITNOT:
            if (auto i = Is<IntLit>(child)) {
                auto t = Optimizer::IntTypeOf(child);
                if (!t) break;
                return o.NewInt(this, WrapStorage(~i->val, t->intstorage));
            }
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
        o.branchdepth++;
        right = o.OptIn(this, right);
        o.branchdepth--;
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
        // Folds compute at the operands' checked type (the typechecker
        // unified both sides), by the same rules the checker folds constants
        // with (FoldIntOp, ast.h). Overflow and division aborts stay runtime
        // behavior: those cases are simply not folded.
        auto ot = Optimizer::IntTypeOf(left);
        if (!ot) return this;
        auto s = ot->intstorage;
        int64_t r;
        if (FoldIntOp(op, li->val, ri->val, s, r)) return o.NewInt(this, r);
        // A comparison has no integer result to fold, so it is not one of
        // FoldIntOp's; its operands compare at their own signedness.
        if (IsUnsigned(s)) {
            auto a = (uint64_t)li->val, b = (uint64_t)ri->val;
            switch (op) {
                case T_LT:   return o.NewBool(this, a < b);
                case T_GT:   return o.NewBool(this, a > b);
                case T_LTEQ: return o.NewBool(this, a <= b);
                case T_GTEQ: return o.NewBool(this, a >= b);
                case T_EQ:   return o.NewBool(this, a == b);
                case T_NEQ:  return o.NewBool(this, a != b);
                default: break;
            }
            return this;
        }
        auto a = li->val, b = ri->val;
        switch (op) {
            case T_LT:   return o.NewBool(this, a < b);
            case T_GT:   return o.NewBool(this, a > b);
            case T_LTEQ: return o.NewBool(this, a <= b);
            case T_GTEQ: return o.NewBool(this, a >= b);
            case T_EQ:   return o.NewBool(this, a == b);
            case T_NEQ:  return o.NewBool(this, a != b);
            default: break;
        }
        return this;
    }
    auto lf = Is<FltLit>(left), rf = Is<FltLit>(right);
    if (lf && rf) {
        // An all-f32 expression computes in 32 bits (§3.1): fold at that
        // precision when both operands are f32-typed.
        auto f32 = IsF32(left->exprtype) && IsF32(right->exprtype);
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
    obj = IsField() && o.Viewed(this) ? o.OptViewed(obj) : o.Opt(obj);
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
    // A member builtin works on its receiver where it stands (bytes_of
    // returns a view of it), and print, str and format render an argument
    // where it stands around the user format overloads they run.
    auto recv = builtin >= 0 && (builtindefs[builtin].flags & BF_MEMBER);
    auto d = Is<Dot>(callee);
    if (d) d->obj = Optimizer::Hooked(this, 0) ? o.OptRendered(d->obj)
                    : recv                     ? o.OptViewed(d->obj)
                                               : o.Opt(d->obj);
    for (size_t i = 0; i < args.size(); i++)
        args[i] = Optimizer::Hooked(this, i + (d ? 1 : 0)) ? o.OptRendered(args[i])
                  : recv && !d && !i                       ? o.OptViewed(args[i])
                                                           : o.OptIn(this, args[i]);
    if (fvbody) o.OptBlock(fvbody);
    if (defaultinit) defaultinit = o.Opt(defaultinit);
    if (builtin == B_ASSERT) {
        if (auto b = Is<BoolLit>(FirstArg()); b && b->val) {
            o.folded++;
            return o.EmptyBlock(this);
        }
    }
    if (auto r = o.TryInline(this)) return r;
    if (auto r = o.TryBaseCase(this)) return r;
    return this;
}

inline Node *Index::Opt(Optimizer &o) {
    // The element is read after the index runs.
    obj = o.Viewed(this) || !Optimizer::CodeFree(idx) ? o.OptViewed(obj) : o.Opt(obj);
    idx = o.Opt(idx);
    return this;
}

inline Node *SliceExpr::Opt(Optimizer &o) {
    obj = o.OptViewed(obj);
    if (lo) lo = o.Opt(lo);
    if (hi) hi = o.Opt(hi);
    return this;
}

inline Node *AsCast::Opt(Optimizer &o) {
    child = o.Opt(child);
    auto tt = totype;
    if (auto i = Is<IntLit>(child)) {
        // Whether the source value's bits read as a u64 above i64.max.
        auto st = Optimizer::IntTypeOf(child);
        auto suns = i->val < 0 && (i->uns || (st && st->intstorage == IS_U64));
        if (tt->kind == TY_INT) {
            if (unchecked) return o.NewInt(this, WrapStorage(i->val, tt->intstorage));
            // `as` range-checks in debug: fold only a fitting value.
            if (FitsIntStorage(i->val, suns, tt->intstorage))
                return o.NewInt(this, i->val);
        } else if (tt->kind == TY_FLT) {
            if (suns) return this;   // u64-range sources are for the runtime.
            // Rounded once, straight from the integer, as the C cast does.
            if (IsF32(tt)) return o.NewFlt(this, (double)(float)i->val);
            return o.NewFlt(this, (double)i->val);
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
                if (unchecked) return o.NewInt(this, WrapStorage(t, tt->intstorage));
                if ((double)t == d && FitsIntStorage(t, false, tt->intstorage))
                    return o.NewInt(this, t);
            }
        } else if (tt->kind == TY_FLT) {
            return o.NewFlt(this, IsF32(tt) ? (double)(float)fl->val : fl->val);
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
    if (fillval) fillval = o.OptIn(this, fillval);
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
    // A branch that leaves where the other goes on is cold: the else of a
    // guard, or the body of an early return.
    auto thenleaves = Optimizer::Leaves(thenb), elseleaves = elseb && Optimizer::Leaves(elseb);
    auto thencold = thenleaves && !elseleaves && !flat;
    auto elsecold = elseleaves && (flat || !thenleaves);
    auto k = Optimizer::Around(this, thenb);
    o.branchdepth++;
    o.depth += k;
    o.colddepth += thencold;
    o.OptBlock(thenb);
    o.colddepth -= thencold;
    o.depth -= k;
    o.colddepth += elsecold;
    if (elseb) elseb = o.OptIn(this, elseb);
    o.colddepth -= elsecold;
    o.branchdepth--;
    return this;
}

inline Node *MatchExpr::Opt(Optimizer &o) {
    scrutinee = o.Opt(scrutinee);
    if (auto iv = Is<IntLit>(scrutinee)) {
        // An integer match on a constant selects its arm statically (first
        // matching arm; a _ arm is guaranteed present). Compare at the
        // scrutinee's signedness.
        auto st = Optimizer::IntTypeOf(scrutinee);
        auto uns = st && IsUnsigned(st->intstorage);
        auto inrange = [&](const MatchArm &arm) {
            for (auto &r : arm.ranges)
                if (uns ? (uint64_t)iv->val >= (uint64_t)r.lo && (uint64_t)iv->val <= (uint64_t)r.hi
                        : iv->val >= r.lo && iv->val <= r.hi)
                    return true;
            return false;
        };
        MatchArm *sel = nullptr;
        for (auto &arm : arms) {
            if (arm.pat.kind == P_WILDCARD || inrange(arm)) { sel = &arm; break; }
        }
        if (sel && (exprtype->kind == TY_VOID ||
                    Optimizer::SameType(sel->body->exprtype, exprtype))) {
            o.folded++;
            return o.Opt(sel->body);
        }
    }
    o.branchdepth++;
    for (auto &arm : arms) arm.body = o.OptIn(this, arm.body);
    o.branchdepth--;
    return this;
}

inline Node *EarlyBlock::Opt(Optimizer &o) {
    o.OptBlock(body);
    return this;
}

inline Node *While::Opt(Optimizer &o) {
    o.loopdepth++;
    cond = o.OptIn(this, cond);
    if (auto b = Is<BoolLit>(cond); b && !b->val) {
        o.loopdepth--;
        o.folded++;
        return o.EmptyBlock(this);
    }
    o.OptBlock(body);
    o.loopdepth--;
    return this;
}

inline Node *LoopExpr::Opt(Optimizer &o) {
    o.loopdepth++;
    o.OptBlock(body);
    o.loopdepth--;
    return this;
}

inline Node *ForLoop::Opt(Optimizer &o) {
    iter = o.OptViewed(iter);
    o.loopdepth++;
    o.OptBlock(body);
    o.loopdepth--;
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
