// Goose compiler — the last uses of a function's variables, on its final
// (optimized, inlined) tree, for codegen to hand a variable's storage over
// instead of copying it (CodeGen::MovableArg).
//
// A use of variable v is *last* when no path from it reads v again before v
// is assigned a whole new value or the function exits. The analysis is the
// classic backward liveness, over the tree rather than a control-flow graph:
// a node's live-before set is computed from its live-after set, children in
// reverse evaluation order, a loop to a fixpoint, and the jumps (`break`,
// `continue`, `return`, an inlined body's `return`) taking the live set of
// where they go.
//
// Which variables are tracked: a function's own locals and parameters that
// nothing but its own statements can reach. A variable read by a nested
// function or a function value (captured), a pool, a root's component, or a
// variable a deferred body names is never reported, as the analysis does not
// see when those reads happen -- nor is one that such a variable, a
// reference or a holder of references, may lead into. A read of a reference
// that may point into a tracked variable is a read of that variable
// (MayReachLocal's provenance test): a variable is not last-used while a
// reference into it is read later.
//
// Evaluation order. Codegen evaluates a call's arguments, an operator's
// operands, an index, an assignment's target and value, and a declaration's
// initializers in the order Children lists them. Children whose order is not
// so certain -- a literal's fields, a builtin's arguments, any node not
// listed below -- are taken as unordered: each sees what its siblings read as
// read after it. But what an operand reads is not always read where it is
// evaluated: an indexed or sliced variable's elements, or a reference
// argument's pointee, are read through an address taken before the later
// operands run and used after them (CodeGen::IndexLoc, a `T&` parameter).
// So a use is reported only where nothing else in its full expression -- the
// statement, or a declaration, return or condition, around it, through any
// value-giving block inside it -- names the variable or a reference that may
// lead into it; a whole assignment's target does not count (`x = f(x)`).
#pragma once

namespace goose {

struct Liveness {
    // The Idents that are last uses (see above).
    set<const Ident *> last;

    explicit Liveness(FnSpec *sp) {
        if (!sp || !sp->body) return;
        Track(sp);
        if (vars.empty()) return;
        exitlive = Set(words(), 0);
        Back(sp->body, exitlive, true);
        // Loops nested deep enough to make the fixpoints too costly give
        // no answer rather than a slow one.
        if (overrun) {
            last.clear();
            return;
        }
        // The marking pass reaches each node of a tree once; one reached
        // twice is shared by two places in it, and its mark would be the
        // later place's for both.
        for (auto id : shared) last.erase(id);
        Alone(sp->body);
    }

  private:
    using Set = vector<uint64_t>;
    unordered_map<const VarDef *, size_t> index;   // Tracked variable -> bit.
    vector<const VarDef *> vars;
    Set exitlive;
    // The live sets a jump takes: the innermost breakable's (a loop's or a
    // `block`'s), the innermost loop's head for `continue`, and each open
    // inlined body's end for a `return` to its function.
    struct Target {
        Node *n;
        bool loop;
        Set brk, cont;
    };
    vector<Target> targets;
    vector<pair<SFunction *, Set>> inlined;
    // What a subtree reads (Uses), memoized.
    unordered_map<const Node *, Set> usecache;
    set<const Ident *> marked, shared;
    // Back's work so far, and whether it gave up (Back's cap).
    int64_t steps = 0;
    bool overrun = false;
    static constexpr int64_t MAXSTEPS = 4000000;

    size_t words() const { return (vars.size() + 63) / 64; }
    void Add(Set &s, size_t i) { s[i / 64] |= 1ull << (i % 64); }
    void AddAll(Set &s) { for (size_t i = 0; i < vars.size(); i++) Add(s, i); }
    void Del(Set &s, size_t i) { s[i / 64] &= ~(1ull << (i % 64)); }
    bool Has(const Set &s, size_t i) const { return s[i / 64] >> (i % 64) & 1; }
    static void Or(Set &a, const Set &b) {
        for (size_t i = 0; i < a.size(); i++) a[i] |= b[i];
    }

    void Track(FnSpec *sp) {
        // The variables read where the analysis does not see: named in a
        // deferred body, or captured by a nested function or function value.
        set<const VarDef *> spoiled;
        function<void(Node *, bool)> scan = [&](Node *n, bool indefer) {
            if (!n) return;
            if (auto id = Is<Ident>(n); id && id->vdef && (indefer || id->vdef->captured))
                spoiled.insert(id->vdef);
            auto d = indefer || Is<Defer>(n);
            RunChildren(n, [&](Node *c) { scan(c, d); });
        };
        scan(sp->body, false);
        auto consider = [&](const VarDef *v) {
            if (!v) return;
            if (v->captured) spoiled.insert(v);
            if (index.count(v) || spoiled.count(v)) return;
            if (v->isglobal || v->ownerspec != sp || v->captured || v->reusable ||
                v->componentowner || !v->components.empty() || v->istemp || !v->type)
                return;
            index[v] = vars.size();
            vars.push_back(v);
        };
        for (auto p : sp->params) consider(p);
        function<void(Node *)> decls = [&](Node *n) {
            if (!n || Is<FunVal>(n) || Is<FnDecl>(n)) return;
            if (auto vd = Is<VarDecl>(n)) for (auto v : vd->defs) consider(v);
            if (auto f = Is<ForLoop>(n)) {
                consider(f->vdef);
                consider(f->idxdef);
            }
            if (auto m = Is<MatchExpr>(n)) for (auto &a : m->arms) consider(a.binder);
            RunChildren(n, decls);
        };
        decls(sp->body);
        // And whatever those may lead into, through references they are or
        // hold: untracked as well.
        Set reached(words(), 0);
        for (auto v : spoiled) Reach(v, reached);
        auto kept = vars;
        vars.clear();
        index.clear();
        for (size_t i = 0; i < kept.size(); i++) {
            if (Has(reached, i)) continue;
            index[kept[i]] = vars.size();
            vars.push_back(kept[i]);
        }
    }

    // The full expression around each Ident (see the header): the nearest
    // statement of a block that is itself run as a statement, a block's
    // tail, or the body of a control construct that is one. A block giving
    // a value inside an expression belongs to that expression.
    unordered_map<const Ident *, const Node *> rootof;
    void FullExprs(Node *n, const Node *root) {
        if (!n || Is<FunVal>(n) || Is<FnDecl>(n) || Is<Defer>(n)) return;
        if (auto id = Is<Ident>(n)) {
            rootof[id] = root;
            return;
        }
        auto isroot = n == root;
        if (auto b = Is<Block>(n)) {
            for (auto st : b->stmts) FullExprs(st, isroot ? st : root);
            FullExprs(b->tail, isroot ? b->tail : root);
            return;
        }
        auto control = [](Node *m) {
            return Is<IfExpr>(m) || Is<MatchExpr>(m) || Is<While>(m) || Is<LoopExpr>(m) ||
                   Is<ForLoop>(m) || Is<EarlyBlock>(m) || Is<InlineBlock>(m) || Is<Block>(m);
        };
        // A construct that is the whole value of a declaration, a return, a
        // break or a whole assignment has nothing pending around it.
        if (isroot) {
            Node *whole = nullptr;
            if (auto vd = Is<VarDecl>(n); vd && vd->inits.size() == 1) whole = vd->inits[0];
            else if (auto r = Is<Return>(n); r && r->vals.size() == 1) whole = r->vals[0];
            else if (auto br = Is<Break>(n)) whole = br->val;
            else if (auto a = Is<Assign>(n); a && Killed(a)) whole = a->rhs;
            if (whole && control(whole)) {
                if (auto a = Is<Assign>(n)) FullExprs(a->lval, n);
                FullExprs(whole, whole);
                return;
            }
        }
        // A match's arms are separate paths, each run whole after the
        // scrutinee and the patterns.
        if (auto m = Is<MatchExpr>(n); m && isroot) {
            FullExprs(m->scrutinee, n);
            for (auto &arm : m->arms) {
                for (auto &pi : arm.pat.items) {
                    FullExprs(pi.lo, n);
                    FullExprs(pi.hi, n);
                }
                FullExprs(arm.body, arm.body);
            }
            return;
        }
        if (isroot && control(n)) {
            // Its condition, scrutinee or iteration is evaluated whole
            // before a body runs; each body block is a statement list.
            RunChildren(n, [&](Node *c) {
                auto body = Is<Block>(c) || (Is<IfExpr>(n) && c == Is<IfExpr>(n)->elseb);
                FullExprs(c, body ? c : root);
            });
            return;
        }
        RunChildren(n, [&](Node *c) { FullExprs(c, root); });
    }

    // Drops each last use whose full expression names its variable, or a
    // reference that may lead into it, anywhere else.
    void Alone(Block *body) {
        FullExprs(body, body);
        set<const Ident *> killed;
        function<void(Node *)> kills = [&](Node *n) {
            if (!n || Is<FunVal>(n) || Is<FnDecl>(n) || Is<Defer>(n)) return;
            if (auto a = Is<Assign>(n); a && Killed(a)) killed.insert(Is<Ident>(a->lval));
            RunChildren(n, kills);
        };
        kills(body);
        map<pair<const Node *, size_t>, int> count;
        for (auto &[id, root] : rootof) {
            if (killed.count(id)) continue;
            Set s(words(), 0);
            Reads(id, s);
            // Mostly the one variable: visit the set bits only.
            for (size_t w = 0; w < s.size(); w++)
                for (auto bits = s[w]; bits; bits &= bits - 1)
                    count[{ root, w * 64 + (size_t)std::countr_zero(bits) }]++;
        }
        for (auto it = last.begin(); it != last.end();) {
            auto r = rootof.find(*it);
            auto v = index.find((*it)->vdef);
            if (r == rootof.end() || v == index.end() || count[{ r->second, v->second }] != 1)
                it = last.erase(it);
            else
                ++it;
        }
    }

    // The tracked variables an Ident reads: its own, and those a reference
    // or a holder of references it names may lead into (MayReachLocal).
    void Reads(const Ident *id, Set &s) {
        if (id->vdef) Reach(id->vdef, s);
    }

    // A variable's own bit, and those it may lead into as a reference or a
    // holder of references.
    void Reach(const VarDef *d, Set &s) {
        if (auto it = index.find(d); it != index.end()) Add(s, it->second);
        if (d->isglobal || d->isparam || !d->type) return;
        auto reach = [&](const Roots &r) {
            if (r.unknown) {
                AddAll(s);
                return;
            }
            for (auto &a : r.alts) {
                if (!a.exact) {
                    AddAll(s);
                    return;
                }
                if (auto it = index.find(a.root); it != index.end()) Add(s, it->second);
            }
        };
        auto refs = d->type->kind == TY_REF || d->type->kind == TY_SLICE;
        if (refs && (!d->refrootknown || d->ref.alts.empty())) {
            AddAll(s);
            return;
        }
        if (refs) reach(d->ref);
        reach(d->contents);
    }

    const Set &Uses(Node *n) {
        auto it = usecache.find(n);
        if (it != usecache.end()) return it->second;
        Set s(words(), 0);
        function<void(Node *)> walk = [&](Node *m) {
            if (!m || Is<FunVal>(m) || Is<FnDecl>(m) || Is<Defer>(m)) return;
            if (auto id = Is<Ident>(m)) Reads(id, s);
            RunChildren(m, walk);
        };
        walk(n);
        return usecache.emplace(n, std::move(s)).first->second;
    }

    // An Ident as a whole-value assignment's target: a definition, not a read.
    const VarDef *Killed(Assign *a) {
        if (a->op != T_ASSIGN || a->pointee) return nullptr;
        auto id = Is<Ident>(a->lval);
        if (!id || !id->vdef || !index.count(id->vdef)) return nullptr;
        return id->vdef;
    }

    void Kill(Set &s, const VarDef *v) {
        if (auto it = index.find(v); it != index.end()) Del(s, it->second);
    }

    // Children in evaluation order, each from what the later ones leave live.
    Set Ordered(const vector<Node *> &kids, Set live, bool mark) {
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) live = Back(*it, live, mark);
        return live;
    }

    // Children in no certain order: each sees its siblings' reads as later.
    Set Unordered(const vector<Node *> &kids, const Set &after, bool mark) {
        Set all = after;
        for (auto k : kids) if (k) Or(all, Uses(k));
        for (auto k : kids) {
            if (!k) continue;
            auto others = after;
            for (auto o : kids) if (o && o != k) Or(others, Uses(o));
            Back(k, others, mark);
        }
        return all;
    }

    static vector<Node *> Kids(Node *n) {
        vector<Node *> v;
        RunChildren(n, [&](Node *c) { if (c) v.push_back(c); });
        return v;
    }

    // A loop: `step` takes the live set at the loop's head (where `continue`
    // goes, and the next iteration starts) to the head's set again; iterated
    // from nothing up to the fixpoint without marking, then once marking.
    template <typename F> Set Loop(Node *n, const Set &after, bool mark, F step) {
        Set head(words(), 0);
        for (;;) {
            targets.push_back({ n, true, after, head });
            auto next = step(head, false);
            targets.pop_back();
            Or(next, head);
            if (next == head) break;
            head = next;
        }
        if (mark) {
            targets.push_back({ n, true, after, head });
            step(head, true);
            targets.pop_back();
        }
        return head;
    }

    Set Back(Node *n, const Set &after, bool mark) {
        if (overrun || ++steps > MAXSTEPS) {
            overrun = true;
            return after;
        }
        if (!n || Is<FunVal>(n) || Is<FnDecl>(n) || Is<Defer>(n) || Is<StructDecl>(n) ||
            Is<EnumDecl>(n) || Is<AliasDecl>(n))
            return after;
        if (auto id = Is<Ident>(n)) {
            auto live = after;
            if (mark && !marked.insert(id).second) shared.insert(id);
            if (id->vdef) {
                auto it = index.find(id->vdef);
                if (mark && it != index.end() && !Has(after, it->second)) last.insert(id);
                Reads(id, live);
            }
            return live;
        }
        if (auto b = Is<Block>(n)) {
            auto live = Back(b->tail, after, mark);
            for (auto it = b->stmts.rbegin(); it != b->stmts.rend(); ++it)
                live = Back(*it, live, mark);
            return live;
        }
        if (auto vd = Is<VarDecl>(n)) {
            auto live = after;
            for (auto v : vd->defs) Kill(live, v);
            return Ordered(vd->inits, live, mark);
        }
        if (auto a = Is<Assign>(n)) {
            if (auto v = Killed(a)) {
                auto live = after;
                Kill(live, v);
                return Back(a->rhs, live, mark);
            }
            return Ordered({ a->lval, a->rhs }, after, mark);
        }
        if (auto bin = Is<Binary>(n)) {
            if (bin->op == T_ANDAND || bin->op == T_OROR) {
                auto live = after;
                Or(live, Back(bin->right, after, mark));
                return Back(bin->left, live, mark);
            }
            return Ordered({ bin->left, bin->right }, after, mark);
        }
        if (auto c = Is<Call>(n)) {
            // A builtin may read its arguments in any order. A call's
            // function-value body (Call::fvbody) runs inside it.
            auto kids = Kids(n);
            if (c->builtin >= 0) return Unordered(kids, after, mark);
            return Ordered(kids, after, mark);
        }
        if (Is<Unary>(n) || Is<Dot>(n) || Is<Index>(n) || Is<SliceExpr>(n) || Is<AsCast>(n) ||
            Is<RangeExpr>(n))
            return Ordered(Kids(n), after, mark);
        if (auto i = Is<IfExpr>(n)) {
            auto live = Back(i->thenb, after, mark);
            Or(live, i->elseb ? Back(i->elseb, after, mark) : after);
            return Back(i->cond, live, mark);
        }
        if (auto m = Is<MatchExpr>(n)) {
            Set live(words(), 0);
            for (auto &arm : m->arms) {
                auto a = Back(arm.body, after, mark);
                if (arm.binder) Kill(a, arm.binder);
                Or(live, a);
            }
            if (m->arms.empty()) live = after;
            // The patterns' values are compared against before any arm runs.
            for (auto &arm : m->arms)
                for (auto &pi : arm.pat.items) {
                    if (pi.lo) Or(live, Uses(pi.lo));
                    if (pi.hi) Or(live, Uses(pi.hi));
                }
            return Back(m->scrutinee, live, mark);
        }
        if (auto e = Is<EarlyBlock>(n)) {
            targets.push_back({ n, false, after, {} });
            auto live = Back(e->body, after, mark);
            targets.pop_back();
            return live;
        }
        if (auto ib = Is<InlineBlock>(n)) {
            inlined.push_back({ ib->sf, after });
            auto live = Back(ib->body, after, mark);
            inlined.pop_back();
            return live;
        }
        if (auto w = Is<While>(n)) {
            return Loop(n, after, mark, [&](const Set &head, bool mk) {
                auto live = Back(w->body, head, mk);
                Or(live, after);
                return Back(w->cond, live, mk);
            });
        }
        if (auto l = Is<LoopExpr>(n)) {
            return Loop(n, after, mark, [&](const Set &head, bool mk) {
                return Back(l->body, head, mk);
            });
        }
        if (auto f = Is<ForLoop>(n)) {
            // The iteration may read the iterated value again each time
            // round (§6.5): it is read at the head, like a condition.
            auto head = Loop(n, after, mark, [&](const Set &h, bool mk) {
                auto live = Back(f->body, h, mk);
                if (f->vdef) Kill(live, f->vdef);
                if (f->idxdef) Kill(live, f->idxdef);
                Or(live, after);
                Or(live, Uses(f->iter));
                return live;
            });
            return Back(f->iter, head, mark);
        }
        if (auto br = Is<Break>(n))
            return Back(br->val, targets.empty() ? exitlive : targets.back().brk, mark);
        if (Is<Continue>(n)) {
            for (auto it = targets.rbegin(); it != targets.rend(); ++it)
                if (it->loop) return it->cont;
            return exitlive;
        }
        if (auto r = Is<Return>(n)) {
            // To an open inlined body's end, or out of the function.
            const Set *to = &exitlive;
            for (auto it = inlined.rbegin(); it != inlined.rend(); ++it)
                if (it->first == r->target) {
                    to = &it->second;
                    break;
                }
            return Ordered(r->vals, *to, mark);
        }
        // A literal's fields, an array's elements and anything else.
        return Unordered(Kids(n), after, mark);
    }
};

}  // namespace goose
