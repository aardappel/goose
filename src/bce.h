// Goose compiler — bounds-check elimination. Runs between the optimizer and
// codegen over every live specialization body (plus global initializers), and
// marks Index / SliceExpr nodes whose runtime check provably cannot fire
// (Index::nobc / SliceExpr::nobc); codegen then omits those checks. The pass
// never changes semantics: an elided check is one that could not have aborted.
//
// The analysis is a flow-sensitive difference-constraint domain over four
// base kinds: the constant zero, integer variables, array lengths
// (len(place)), and one-shot bases for expressions the operation itself
// bounds, with facts of the form `l <= r + c`. Queries are shortest paths
// over the fact graph (Bellman-Ford with saturating weights), extended by
// always-true axioms: sub-64-bit integer storage ranges, `0 <= len <= 2^61`
// (an array length is bounded by the address space), and the invariants
// inferred below.
//
// Value pinning. Every base carries a generation; a base (v, g) denotes the
// value the variable/length held while generation g was current at that
// program point, so a kill merely opens a new generation and existing facts
// stay true about the pinned old value — which is exactly what makes the
// snapshot bounds of `for i in n` loops sound across iterations. Length
// mutations additionally add a bridging fact between the generations when
// their direction is known (grow: old <= new, shrink: new <= old), so facts
// survive pure growth — indexing a grow-only array stays provable across
// pushes — and are cut by shrinks. Monotone variables (all writes are
// guarded, non-wrapping increments, or all decrements) get the same bridges
// across their kill points.
//
// Facts come from: `for` headers (0 <= i < n at the appropriate snapshot),
// while/if/guard/assert conditions and their negations (through !/&&/||),
// integer match arms, declaration and assignment equalities with
// recognizable right-hand sides, and ++/--/+=/-= which shift facts in place
// when the pre-state provably cannot wrap at the variable's width. Condition
// facts are suppressed when evaluating the condition itself may have changed
// tracked state (a mutating call inside it), since the comparison then ran
// against pre-kill values.
//
// Lengths are tracked exactly where the program states them: a literal's
// element count, `resize`/`clear`, a push or a constant-length append as a
// delta on the previous length, and a counted loop whose body pushes a fixed
// number of times per iteration. Values likewise: `a % b` and `a & b` land in
// [0, b], and a cast whose value the facts already place in the target's
// range carries its operand's term across — together these prove the
// reduce-a-hash-into-a-table idiom without any guard in the source.
//
// Aliasing uses the checker's reference provenance (VarDef::refroot chains):
// a write or grow/shrink kills the length bases of every place it may name.
// Storage is unreachable to a callee, and to unknown references, unless its
// owner is a global, is captured, or has its address taken (creating any
// reference into a variable's storage requires one of those); reference
// parameters use the specialization's root classes, where distinct classes
// are provably distinct roots. Unknown callees kill everything reachable.
//
// Loops: a body is first walked in a kills-only mode to invalidate whatever
// any earlier iteration may have changed, then walked for real; while-loop
// condition facts re-establish at every body entry. Invariants (v >= 0,
// v <= len(P), and wrap-freedom for the bridges above) are inferred by a
// recording pass that checks every write of an eligible variable preserves
// them, then granted as axioms to the judging pass — this is what proves the
// classic `var i = 0; while i < a.len { ... a[i] ...; i++; }` and the
// post-loop `a[start..i]` slice. Growth alone never breaks `v <= len(P)`, so
// only a shrink disqualifies P. Globals get the same treatment across the
// whole program (RunAll's first phase), which is what a cursor kept in a
// global needs: its upper bound comes from the loop condition, its lower
// bound from nothing else.
#pragma once

namespace goose {

struct BCE {
    Ast &ast;

    // Reporting: totals plus per-source-line outcomes for --bce-test.
    int idxelided = 0, idxtotal = 0, slelided = 0, sltotal = 0;
    map<pair<int, int>, pair<int, int>> lineout;   // (file, line) -> (elided, kept).

    BCE(Ast &_ast) : ast(_ast) {}

    // ------------------------------------------------------------------
    // Bases, facts, terms.

    enum BaseKind : uint8_t { BK_ZERO, BK_VAR, BK_LEN, BK_TMP };

    struct Base {
        uint8_t kind = BK_ZERO;
        int id = 0;
        int gen = 0;
        bool operator==(const Base &o) const {
            return kind == o.kind && id == o.id && gen == o.gen;
        }
    };

    struct Fact { Base l, r; int64_t c; };   // l <= r + c.
    struct Term { bool ok = false; Base b; int64_t off = 0; };

    struct Flow {
        vector<Fact> facts;
        unordered_map<int, int> vgen, pgen;   // var id / place id -> generation.
    };
    Flow flow;
    int nextgen = 0;

    static constexpr int64_t INF = INT64_MAX;
    static constexpr int64_t LENMAX = int64_t(1) << 61;   // Bytes exceed any address space beyond this.
    static constexpr int64_t OFFCAP = int64_t(1) << 32;
    static constexpr int64_t CCAP = int64_t(1) << 60;
    static constexpr size_t MAXFACTS = 200;

    // Saturating adds: INF is an absorbing "unprovable" sentinel, positive
    // overflow saturates to it (weakens, never strengthens, a <= bound), and
    // the negative side clamps finitely.
    static int64_t SatAdd(int64_t a, int64_t b) {
        if (a == INF || b == INF) return INF;
        if (b > 0 && a > INF - 1 - b) return INF;
        if (b < 0 && a < INT64_MIN - b) return -CCAP;
        auto r = a + b;
        return r < -CCAP ? -CCAP : r;
    }
    static int64_t SatSub(int64_t a, int64_t b) {
        if (b == INT64_MIN) return a >= 0 ? INF : (a + INT64_MAX) + 1;
        return SatAdd(a, -b);
    }
    static bool SmallOff(int64_t c) { return c > -OFFCAP && c < OFFCAP; }

    static pair<int64_t, int64_t> RangeOf(IntStorage s) {
        switch (s) {
            case IS_I8:  return { -128, 127 };
            case IS_I16: return { -32768, 32767 };
            case IS_I32: return { INT32_MIN, INT32_MAX };
            case IS_U8:  return { 0, 255 };
            case IS_U16: return { 0, 65535 };
            case IS_U32: return { 0, (int64_t)UINT32_MAX };
            default:     return { INT64_MIN, INT64_MAX };
        }
    }

    // Variable interning (Base holds compact ids).
    unordered_map<const VarDef *, int> varid;
    vector<VarDef *> varof;
    int VarId(VarDef *v) {
        auto it = varid.find(v);
        if (it != varid.end()) return it->second;
        auto id = (int)varof.size();
        varid[v] = id;
        varof.push_back(v);
        return id;
    }
    static Base Zero() { return Base { BK_ZERO, 0, 0 }; }
    // An anonymous base for one evaluation of an expression whose value range
    // the operation itself bounds (§6.2 modulo and mask). A fresh base per
    // evaluation, so a later evaluation of the same node never inherits the
    // earlier one's facts.
    int nexttmp = 0;
    Base TmpBase() { return Base { BK_TMP, nexttmp++, 0 }; }
    Base VarBase(VarDef *v) {
        auto id = VarId(v);
        auto it = flow.vgen.find(id);
        return Base { BK_VAR, id, it == flow.vgen.end() ? 0 : it->second };
    }
    Base LenBase(int pid) {
        auto it = flow.pgen.find(pid);
        return Base { BK_LEN, pid, it == flow.pgen.end() ? 0 : it->second };
    }

    void AddFactB(Base l, Base r, int64_t c) {
        if (l == r) return;
        if (c >= CCAP) return;              // Too weak to matter.
        if (c < -CCAP) c = -CCAP;
        for (auto &f : flow.facts)
            if (f.l == l && f.r == r) {
                if (c < f.c) f.c = c;       // Keep the strongest.
                return;
            }
        if (flow.facts.size() >= MAXFACTS) flow.facts.erase(flow.facts.begin());
        flow.facts.push_back({ l, r, c });
    }

    // ------------------------------------------------------------------
    // Places: an array/slice location nameable as a variable plus a chain of
    // struct fields, with reference crossings marked (-1 path entries). The
    // "ultimate" storage owner drives aliasing.

    enum UltKind { UK_OWNED, UK_STATIC, UK_OPAQUE };

    struct Place {
        VarDef *rootv = nullptr;
        vector<int> path;
        VarDef *ultv = nullptr;
        int ultkind = UK_OPAQUE;
        bool refcrossed = false;
        bool lenmut = false;    // A grow/shrink operation can target it.
    };
    vector<Place> places;
    map<pair<VarDef *, vector<int>>, int> placeids;

    // Follows reference/slice provenance to the storage-owning variable.
    // A synthetic parameter root class (typeless VarDef) is opaque, but two
    // references in distinct classes are known-distinct roots (§10.2).
    pair<int, VarDef *> UltOf(VarDef *v) {
        for (auto guard = 0; guard < 16; guard++) {
            if (!v) return { UK_STATIC, nullptr };
            auto t = v->type;
            if (!t) return { UK_OPAQUE, v };
            if (t->kind != TY_REF && t->kind != TY_SLICE) return { UK_OWNED, v };
            if (!v->refrootknown) return { UK_OPAQUE, nullptr };
            v = v->refroot;
        }
        return { UK_OPAQUE, nullptr };
    }

    int PlaceOf(Node *n, bool *failidx = nullptr) {
        vector<int> path;
        VarDef *root = nullptr;
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) { root = id->vdef; break; }
            if (auto d = Is<Dot>(cur); d && d->fieldidx >= 0) {
                path.push_back(d->fieldidx);
                if (d->obj->exprtype && d->obj->exprtype->kind == TY_REF) path.push_back(-1);
                cur = d->obj;
                continue;
            }
            if (failidx && Is<Index>(cur)) *failidx = true;
            return -1;
        }
        if (!root) return -1;
        std::reverse(path.begin(), path.end());
        auto t = n->exprtype;
        if (t && t->kind == TY_REF) t = t->ref->sub;
        auto slice = t && t->kind == TY_SLICE;
        auto arr = t && t->kind == TY_ARRAY && t->arr->akind != A_FIXED;
        if (!slice && !arr) return -1;
        auto key = pair<VarDef *, vector<int>>(root, path);
        auto it = placeids.find(key);
        if (it != placeids.end()) return it->second;
        Place P;
        P.rootv = root;
        P.path = path;
        auto midcross = std::find(path.begin(), path.end(), -1) != path.end();
        P.refcrossed = midcross || (root->type && (root->type->kind == TY_REF));
        if (!P.refcrossed) {
            P.ultkind = UK_OWNED;
            P.ultv = root;
        } else if (midcross) {
            P.ultkind = UK_OPAQUE;   // Reads a stored reference: target root unknown here.
        } else {
            auto [k, u] = UltOf(root);
            P.ultkind = k;
            P.ultv = u;
        }
        P.lenmut = arr && t->arr->akind != A_VAR;
        auto id2 = (int)places.size();
        places.push_back(P);
        placeids[key] = id2;
        return id2;
    }

    // The place a variable's own array/slice value occupies (used at
    // declarations and whole-variable assignments, where no obj node exists).
    int PlaceOfVar(VarDef *v) {
        if (!v || !v->type) return -1;
        auto t = v->type;
        if (t->kind == TY_REF) t = t->ref->sub;
        if (t->kind != TY_SLICE && (t->kind != TY_ARRAY || t->arr->akind == A_FIXED))
            return -1;
        auto key = pair<VarDef *, vector<int>>(v, {});
        auto it = placeids.find(key);
        if (it != placeids.end()) return it->second;
        Place P;
        P.rootv = v;
        P.refcrossed = v->type->kind == TY_REF;
        if (!P.refcrossed) {
            P.ultkind = UK_OWNED;
            P.ultv = v;
        } else {
            auto [k, u] = UltOf(v);
            P.ultkind = k;
            P.ultv = u;
        }
        P.lenmut = t->kind == TY_ARRAY && t->arr->akind != A_VAR;
        auto id2 = (int)places.size();
        places.push_back(P);
        placeids[key] = id2;
        return id2;
    }

    // The exact length of a fresh array value, when its construction states
    // it: a literal's element count, a [v; n] fill count, [..cap]'s zero, a
    // string literal's byte count.
    Term FreshLenOf(Node *init) {
        if (auto al = Is<ArrayLit>(init)) {
            if (al->capexpr) return Term { true, Zero(), 0 };
            if (al->fillval) return TermOf(al->fillcount);
            return Term { true, Zero(), (int64_t)al->elems.size() };
        }
        if (auto sl = Is<StrLit>(init))
            return Term { true, Zero(), (int64_t)sl->val.size() };
        return {};
    }

    // ------------------------------------------------------------------
    // Per-spec analysis state.

    set<VarDef *> addrof;     // Address taken (&x, for &x, match &b), incl. owned ults.
    set<VarDef *> intvars;    // Integer variables seen (call-kill candidates).
    set<VarDef *> wdecl;      // Single-name, initialized declarations.
    set<VarDef *> wbad;       // A write shape that defeats invariant tracking.
    struct WKinds { bool inc = false, dec = false, setw = false; };
    map<VarDef *, WKinds> wkinds;
    set<int> specpids;        // Places referenced by this spec.
    map<VarDef *, set<int>> relpids;   // Var -> places it indexes or bounds.

    struct Cand {
        bool ge0 = true;         // v >= 0 preserved by every write.
        bool wrapfree = true;    // No write can wrap at the variable's width.
        vector<int> le;          // Surviving `v <= len(P)` candidates.
        bool declseen = false;
    };
    map<VarDef *, Cand> cands;
    // Global integer variables that are nonnegative at every program point:
    // established by their initializer and preserved by every write in every
    // live body (validated in RunAll's first phase, granted in the second).
    // Globals are the idiomatic home for parser/cursor state, whose upper
    // bound comes from a loop condition but whose lower bound has nowhere
    // else to come from.
    map<VarDef *, bool> gge0;
    set<VarDef *> gaddr;   // Globals whose address is taken anywhere.

    // Granted invariants for the judging pass.
    set<VarDef *> ge0;
    map<VarDef *, vector<int>> lelen;
    map<VarDef *, int> mono;   // +1 nondecreasing / -1 nonincreasing, wrap-free.

    bool Reach(VarDef *v) {
        return v && (v->isglobal || v->captured || addrof.count(v) != 0);
    }

    // ------------------------------------------------------------------
    // The query: prove l <= r + c from facts + axioms, by shortest path.

    bool Query(Base l, Base r, int64_t c) {
        if (l == r) return c >= 0;
        vector<Base> nodes { Zero(), l, r };
        auto add = [&](const Base &b) {
            for (auto &x : nodes) if (x == b) return;
            nodes.push_back(b);
        };
        for (auto &f : flow.facts) { add(f.l); add(f.r); }
        // Length bases contributed by granted v <= len(P) invariants.
        if (!lelen.empty())
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].kind != BK_VAR) continue;
                auto it = lelen.find(varof[nodes[i].id]);
                if (it != lelen.end())
                    for (auto pid : it->second) add(LenBase(pid));
            }
        auto idx = [&](const Base &b) {
            for (size_t i = 0; i < nodes.size(); i++) if (nodes[i] == b) return (int)i;
            return -1;
        };
        struct Edge { int a, b; int64_t w; };
        vector<Edge> edges;
        for (auto &f : flow.facts) edges.push_back({ idx(f.l), idx(f.r), f.c });
        for (size_t i = 0; i < nodes.size(); i++) {
            auto &b = nodes[i];
            if (b.kind == BK_LEN) {
                edges.push_back({ 0, (int)i, 0 });         // 0 <= len.
                edges.push_back({ (int)i, 0, LENMAX });    // len <= 2^61.
            } else if (b.kind == BK_VAR) {
                auto v = varof[b.id];
                auto t = v->type;
                if (t && t->kind == TY_INT && IntBits(t->intstorage) < 64) {
                    auto [lo, hi] = RangeOf(t->intstorage);
                    edges.push_back({ 0, (int)i, -lo });
                    edges.push_back({ (int)i, 0, hi });
                }
                if (ge0.count(v)) edges.push_back({ 0, (int)i, 0 });
                auto it = lelen.find(v);
                if (it != lelen.end())
                    for (auto pid : it->second) {
                        auto li = idx(LenBase(pid));
                        if (li >= 0) edges.push_back({ (int)i, li, 0 });
                    }
            }
        }
        vector<int64_t> dist(nodes.size(), INF);
        dist[idx(l)] = 0;
        for (size_t round = 0; round < nodes.size(); round++) {
            auto changed = false;
            for (auto &e : edges) {
                if (dist[e.a] == INF) continue;
                auto nd = SatAdd(dist[e.a], e.w);
                if (nd < dist[e.b]) { dist[e.b] = nd; changed = true; }
            }
            if (!changed) break;
        }
        auto ri = idx(r);
        return dist[ri] != INF && dist[ri] <= c;
    }

    // ------------------------------------------------------------------
    // Term extraction: a machine integer expression as base + offset. Offset
    // arithmetic is accepted at i64 only (narrower widths wrap below the
    // 64-bit math the facts are stated in).

    Term LenTermOf(Node *obj) {
        auto t = obj->exprtype;
        if (t && t->kind == TY_REF) t = t->ref->sub;
        if (t && t->kind == TY_ARRAY && t->arr->akind == A_FIXED && t->arr->size >= 0)
            return Term { true, Zero(), t->arr->size };
        auto pid = PlaceOf(obj);
        if (pid >= 0) return Term { true, LenBase(pid), 0 };
        return Term {};
    }

    Term TermOf(Node *n) {
        if (!n) return {};
        if (auto i = Is<IntLit>(n)) {
            if (i->uns || i->val >= CCAP || i->val <= -CCAP) return {};
            return Term { true, Zero(), i->val };
        }
        if (auto id = Is<Ident>(n)) {
            auto v = id->vdef;
            if (!v || !v->type || v->type->kind != TY_INT) return {};
            auto s = v->type->intstorage;
            if (s == IS_U64 || s == IS_VARINT) return {};
            return Term { true, VarBase(v), 0 };
        }
        if (auto d = Is<Dot>(n); d && d->member == B_LEN) return LenTermOf(d->obj);
        if (auto b = Is<Binary>(n); b && (b->op == T_PLUS || b->op == T_MINUS)) {
            auto t = n->exprtype;
            if (!t || t->kind != TY_INT || t->intstorage != IS_I64) return {};
            auto lt = TermOf(b->left), rt = TermOf(b->right);
            if (!lt.ok || !rt.ok) return {};
            if (b->op == T_PLUS && lt.b.kind == BK_ZERO) std::swap(lt, rt);
            if (rt.b.kind != BK_ZERO) return {};
            auto off = b->op == T_PLUS ? SatAdd(lt.off, rt.off) : SatSub(lt.off, rt.off);
            if (off == INF) return {};
            if (lt.b.kind != BK_ZERO && !SmallOff(off)) return {};
            return Term { true, lt.b, off };
        }
        if (auto b = Is<Binary>(n); b && (b->op == T_MOD || b->op == T_BITAND))
            return Derived(n, [&] { return RangedOpTerm(b); });
        if (auto ac = Is<AsCast>(n)) return Derived(n, [&] { return CastTerm(ac); });
        // A spliced-in call body (or a bare block) is its value expression.
        if (auto ib = Is<InlineBlock>(n)) return BlockValueTerm(ib->body, ib->sf);
        if (auto bl = Is<Block>(n)) return BlockValueTerm(bl, nullptr);
        return {};
    }

    // The value a block produces: its trailing expression, or a final return
    // to the inlined function. Statements before it were already walked, so
    // their facts are in place; Derived's generation guard rejects the term
    // if anything was invalidated after the value was computed.
    Term BlockValueTerm(Block *b, SFunction *sf) {
        if (b->tail) return TermOf(b->tail);
        if (b->stmts.empty()) return {};
        auto r = Is<Return>(b->stmts.back());
        if (sf && r && r->target == sf && r->vals.size() == 1) return TermOf(r->vals[0]);
        return {};
    }

    // Memoizes a fact-emitting derivation per node. A repeat visit reuses the
    // recorded term only while no generation has advanced since: afterwards
    // the operands are no longer the ones that produced the value, and
    // re-deriving would attach the old value's bound to new operands.
    map<Node *, pair<Term, int>> derived;

    template<typename F> Term Derived(Node *n, F f) {
        if (mode == M_KILLS) return {};   // Never poison the memo from a havoc walk.
        auto it = derived.find(n);
        if (it != derived.end()) return it->second.second == nextgen ? it->second.first
                                                                    : Term {};
        auto t = f();
        derived[n] = { t, nextgen };
        return t;
    }

    // `a % b` and `a & b` bound their result by b (§6.2): the remainder is
    // below the divisor, and a mask can only pass bits the mask has. Both
    // yield a fresh base carrying those two facts.
    Term RangedOpTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = b->exprtype;
        if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT) return {};
        auto uns = IsUnsigned(t->intstorage);
        auto rt = TermOf(b->right);
        if (!rt.ok) return {};
        if (b->op == T_MOD) {
            // A zero divisor aborts (§6.2), so a completed modulo has b >= 1;
            // signed operands additionally need a nonnegative dividend for
            // the result to be nonnegative (the remainder takes its sign).
            if (!uns) {
                auto lt = TermOf(b->left);
                if (!lt.ok || !Query(Zero(), lt.b, lt.off)) return {};
                if (!Query(Zero(), rt.b, rt.off)) return {};
            }
        } else {
            // The mask bound needs a nonnegative mask; for unsigned operands
            // that also requires the term to be a real (i64-range) value.
            if (!Query(Zero(), rt.b, rt.off)) return {};
        }
        auto m = TmpBase();
        AddFactB(Zero(), m, 0);                                     // 0 <= result.
        AddFactB(m, rt.b, b->op == T_MOD ? SatSub(rt.off, 1) : rt.off);
        return Term { true, m, 0 };
    }

    // A numeric cast whose value is inside the target's range is the
    // identity, so it carries its operand's term across (§6.3). Each side is
    // checked only where the source type does not already guarantee it.
    Term CastTerm(AsCast *ac) {
        if (mode == M_KILLS) return {};
        auto tt = ac->exprtype, st = ac->child->exprtype;
        if (!tt || tt->kind != TY_INT || tt->intstorage == IS_VARINT) return {};
        if (!st || st->kind != TY_INT || st->intstorage == IS_VARINT) return {};
        auto ct = TermOf(ac->child);
        if (!ct.ok) return {};
        // u64's maximum exceeds the i64 range the facts compute in; every u64
        // term the analysis produces is a proven-in-range value, so the
        // window that matters on either side is [0, i64.max].
        auto win = [](IntStorage s) {
            auto r = RangeOf(s);
            if (s == IS_U64) r = { 0, INT64_MAX };
            return r;
        };
        auto [slo, shi] = win(st->intstorage);
        auto [tlo, thi] = win(tt->intstorage);
        if (slo < tlo && !Query(Zero(), ct.b, SatSub(ct.off, tlo))) return {};
        if (shi > thi && !Query(ct.b, Zero(), SatSub(thi, ct.off))) return {};
        return ct;
    }

    // A term usable as a comparison side: the machine comparison then equals
    // the mathematical one. var+const is excluded (the addition itself may
    // have wrapped before the compare); len+small-const is exact.
    static bool CmpAdmissible(const Term &t) {
        if (t.b.kind == BK_VAR) return t.off == 0;
        return true;   // BK_ZERO any; BK_LEN offsets are SmallOff-capped already.
    }

    // ------------------------------------------------------------------
    // Kills.

    enum Mode { M_KILLS, M_RECORD, M_JUDGE };
    Mode mode = M_JUDGE;
    set<int> *ksum = nullptr;          // Killscan summary: bumped place ids...
    set<int> *shsum = nullptr;         // ...those a non-growing bump can hit...
    set<VarDef *> *vksum = nullptr;    // ...and re-bound/killed variables.
    bool anybump = false;
    int loopdepth = 0;          // Inside a loop body or function-value body.

    void BumpVar(VarDef *v, bool bridge = true) {
        anybump = true;
        auto old = VarBase(v);
        flow.vgen[VarId(v)] = ++nextgen;
        if (vksum) vksum->insert(v);
        if (!bridge) return;
        auto it = mono.find(v);
        if (it == mono.end()) return;
        if (it->second > 0) AddFactB(old, VarBase(v), 0);   // old <= new.
        else AddFactB(VarBase(v), old, 0);                  // new <= old.
    }

    void BumpPlace(int pid, int dir) {   // dir: +1 grow, -1 shrink, 0 unknown.
        anybump = true;
        auto old = LenBase(pid);
        flow.pgen[pid] = ++nextgen;
        if (ksum) ksum->insert(pid);
        if (shsum && dir <= 0) shsum->insert(pid);
        if (dir > 0) AddFactB(old, LenBase(pid), 0);
        else if (dir < 0) AddFactB(LenBase(pid), old, 0);
    }

    // A length mutation with an exactly known delta: push (+1), or append of
    // a constant-length source. Only meaningful for a single execution, so
    // kills-mode summaries fall back to the directional bump.
    void ExactLenStep(int pid, int64_t delta) {
        if (mode == M_KILLS) {
            BumpPlace(pid, delta >= 0 ? 1 : -1);
            return;
        }
        anybump = true;
        auto old = LenBase(pid);
        flow.pgen[pid] = ++nextgen;
        if (ksum) ksum->insert(pid);
        auto nw = LenBase(pid);
        AddFactB(nw, old, delta);            // new <= old + delta.
        AddFactB(old, nw, SatSub(0, delta)); // old <= new - delta.
    }

    // The new length equals term t (clear, resize, a fresh array literal).
    void ExactLenIs(int pid, const Term &t) {
        if (mode == M_KILLS || !t.ok) return;
        auto lb = LenBase(pid);
        AddFactB(lb, t.b, t.off);
        AddFactB(t.b, lb, SatSub(0, t.off));
    }

    // Could place P name the same array as a write whose target is the exact
    // place recvpid (>= 0), storage owned by tu (tk == UK_OWNED), or unknown
    // storage (tk == UK_OPAQUE)? chainroot additionally hits everything whose
    // access path starts at that variable (the path itself was overwritten).
    bool AffectedByWrite(int ppid, int recvpid, int tk, VarDef *tu, VarDef *chainroot) {
        auto &P = places[ppid];
        if (P.ultkind == UK_STATIC) return false;
        if (chainroot && P.rootv == chainroot) return true;
        if (recvpid >= 0) {
            if (ppid == recvpid) return true;
            auto &R = places[recvpid];
            if (R.ultkind == UK_OWNED) {
                if (P.ultkind == UK_OWNED && P.ultv == R.ultv)
                    return P.refcrossed || R.refcrossed;   // Distinct plain paths are distinct arrays.
                return P.ultkind == UK_OPAQUE && Reach(R.ultv);
            }
            return Reach(P.rootv) || (P.ultkind == UK_OWNED ? Reach(P.ultv) : true);
        }
        if (tk == UK_OWNED) {
            if (P.rootv == tu) return true;
            if (P.ultkind == UK_OWNED && P.ultv == tu) return true;
            return P.ultkind == UK_OPAQUE && Reach(tu);
        }
        return Reach(P.rootv) || (P.ultkind == UK_OWNED ? Reach(P.ultv) : true);
    }

    // Kills for a grow/shrink builtin; the receiver itself gets the exact
    // delta when one is known, possible aliases the directional bump. Returns
    // the receiver's place id (or -1).
    int GrowShrinkKill(Node *recv, int dir, int64_t exact = INT64_MIN) {
        auto failidx = false;
        auto pid = recv ? PlaceOf(recv, &failidx) : -1;
        // A receiver reached through an element (a[i].f.push(...)) can only
        // be an element-interior array, which no tracked place names.
        if (pid < 0 && failidx) return -1;
        for (size_t i = 0; i < places.size(); i++) {
            if (!places[i].lenmut) continue;
            if (!AffectedByWrite((int)i, pid, UK_OPAQUE, nullptr, nullptr)) continue;
            if ((int)i == pid && exact != INT64_MIN) ExactLenStep(pid, exact);
            else BumpPlace((int)i, dir);
        }
        return pid;
    }

    void StorageWriteKill(int tk, VarDef *tu, VarDef *chainroot) {
        for (size_t i = 0; i < places.size(); i++)
            if (AffectedByWrite((int)i, -1, tk, tu, chainroot)) BumpPlace((int)i, 0);
    }

    void RebindKill(VarDef *root) {
        for (size_t i = 0; i < places.size(); i++)
            if (places[i].rootv == root) BumpPlace((int)i, 0);
    }

    void KillByCall(bool anyrefarg) {
        for (size_t i = 0; i < places.size(); i++) {
            auto &P = places[i];
            if (P.ultkind == UK_STATIC) continue;
            auto hit = Reach(P.rootv) ||
                       (P.ultkind == UK_OWNED ? Reach(P.ultv) : anyrefarg);
            if (hit) BumpPlace((int)i, 0);
        }
        for (auto v : intvars)
            if (v->isglobal || v->captured || addrof.count(v)) BumpVar(v);
    }

    // ------------------------------------------------------------------
    // Variable writes: shifts, sets, and invariant recording.

    bool IsCand(VarDef *v) { return cands.find(v) != cands.end(); }

    void RecordShift(VarDef *v, int64_t c) {
        auto it = cands.find(v);
        if (it == cands.end()) return;
        auto &st = it->second;
        auto [lo, hi] = RangeOf(v->type->intstorage);
        if (c > 0) {
            auto nowrap = Query(VarBase(v), Zero(), SatSub(hi, c));   // v <= hi - c.
            st.wrapfree = st.wrapfree && nowrap;
            st.ge0 = st.ge0 && nowrap;
            for (auto pit = st.le.begin(); pit != st.le.end();)
                if (Query(VarBase(v), LenBase(*pit), -c)) ++pit;      // v <= len - c.
                else pit = st.le.erase(pit);
        } else if (c < 0) {
            auto nowrap = Query(Zero(), VarBase(v), SatSub(c, lo));   // v >= lo - c.
            st.wrapfree = st.wrapfree && nowrap;
            st.ge0 = st.ge0 && Query(Zero(), VarBase(v), c);          // v >= -c.
            if (!nowrap) st.le.clear();
        }
    }

    void RecordSet(VarDef *v, const Term &t, bool isdecl) {
        auto it = cands.find(v);
        if (it == cands.end()) return;
        auto &st = it->second;
        if (isdecl) st.declseen = true;
        st.ge0 = st.ge0 && t.ok && Query(Zero(), t.b, t.off);
        for (auto pit = st.le.begin(); pit != st.le.end();)
            if (t.ok && Query(t.b, LenBase(*pit), SatSub(0, t.off))) ++pit;
            else pit = st.le.erase(pit);
    }

    // Re-assert granted axioms as stored facts so a shift transports them.
    void Materialize(VarDef *v) {
        if (mode != M_JUDGE) return;
        auto vb = VarBase(v);
        if (ge0.count(v)) AddFactB(Zero(), vb, 0);
        auto it = lelen.find(v);
        if (it != lelen.end())
            for (auto pid : it->second) AddFactB(vb, LenBase(pid), 0);
    }

    void ShiftCore(VarDef *v, int64_t c) {
        if (!SmallOff(c)) { BumpVar(v, false); return; }
        auto [lo, hi] = RangeOf(v->type->intstorage);
        auto ok = c > 0 ? Query(VarBase(v), Zero(), SatSub(hi, c))
                        : Query(Zero(), VarBase(v), SatSub(c, lo));
        if (!ok) { BumpVar(v); return; }
        Materialize(v);
        // The base keeps its generation and is reinterpreted as the new value
        // (every fact mentioning it shifts below), so any memoized term built
        // on it must not be reused past this point.
        nextgen++;
        auto vb = VarBase(v);
        for (auto &f : flow.facts) {
            auto l = f.l == vb, r = f.r == vb;
            if (l == r) continue;
            f.c = SatAdd(f.c, l ? c : -c);
        }
    }

    void ShiftWrite(VarDef *v, int64_t c) {
        if (mode == M_RECORD) RecordShift(v, c);
        if (mode == M_KILLS) { BumpVar(v); return; }
        if (c == 0) return;
        ShiftCore(v, c);
    }

    void SetWrite(VarDef *v, Node *rhs, bool isdecl) {
        auto t = mode == M_KILLS ? Term {} : TermOf(rhs);
        if (mode == M_RECORD) {
            if (t.ok && t.b == VarBase(v)) RecordShift(v, t.off);
            else RecordSet(v, t, isdecl);
        }
        if (mode == M_KILLS) { BumpVar(v, false); return; }
        if (t.ok && t.b == VarBase(v)) { ShiftCore(v, t.off); return; }
        BumpVar(v, false);
        if (t.ok) {
            auto nb = VarBase(v);
            AddFactB(nb, t.b, t.off);
            AddFactB(t.b, nb, SatSub(0, t.off));
        }
    }

    void VarKillWrite(VarDef *v) {
        if (mode == M_RECORD) RecordSet(v, Term {}, false);
        BumpVar(v, false);
    }

    static bool ScalarIntVar(VarDef *v) {
        return v && v->type && v->type->kind == TY_INT &&
               v->type->intstorage != IS_U64 && v->type->intstorage != IS_VARINT;
    }

    // ------------------------------------------------------------------
    // Lvalue chains (for kill targeting).

    enum ChainKind { CH_OK, CH_INDEX, CH_FAIL };
    struct Chain {
        ChainKind kind = CH_FAIL;
        VarDef *root = nullptr;
        bool anycross = false, rootonlycross = false;
    };

    Chain ChainOf(Node *n) {
        Chain ch;
        auto midcross = false;
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) {
                ch.root = id->vdef;
                if (!ch.root) return ch;
                auto rootref = ch.root->type && ch.root->type->kind == TY_REF;
                ch.anycross = midcross || rootref;
                ch.rootonlycross = rootref && !midcross;
                ch.kind = CH_OK;
                return ch;
            }
            if (auto d = Is<Dot>(cur); d && d->fieldidx >= 0) {
                if (d->obj->exprtype && d->obj->exprtype->kind == TY_REF) midcross = true;
                cur = d->obj;
                continue;
            }
            if (Is<Index>(cur)) { ch.kind = CH_INDEX; return ch; }
            return ch;
        }
    }

    void PointeeWriteKill(Node *lval, TypeExpr *pt) {
        int tk = UK_OPAQUE;
        VarDef *tu = nullptr;
        if (auto id = Is<Ident>(lval); id && id->vdef) {
            auto [k, u] = UltOf(id->vdef);
            tk = k;
            tu = u;
        }
        if (tk == UK_STATIC) return;   // Static data is never writable.
        auto scalar = pt && (pt->kind == TY_INT || pt->kind == TY_FLT || pt->kind == TY_BOOL);
        if (!scalar) StorageWriteKill(tk, tu, nullptr);
        if (pt && pt->kind == TY_INT) {
            if (tk == UK_OWNED) {
                if (tu->type && tu->type->kind == TY_INT) BumpVar(tu, false);
            } else {
                for (auto v : intvars) if (Reach(v)) BumpVar(v, false);
            }
        }
    }

    // ------------------------------------------------------------------
    // Judging.

    void RecordLine(Line l, bool elided) {
        auto &e = lineout[{ l.fileidx, l.line }];
        if (elided) e.first++;
        else e.second++;
    }

    void JudgeIndex(Index *ix) {
        if (mode != M_JUDGE) return;
        idxtotal++;
        auto lent = LenTermOf(ix->obj);
        auto it = TermOf(ix->idx);
        auto ok = lent.ok && it.ok &&
                  Query(Zero(), it.b, it.off) &&
                  Query(it.b, lent.b, SatSub(SatSub(lent.off, it.off), 1));
        if (ok) { ix->nobc = true; idxelided++; }
        RecordLine(ix->line, ok);
    }

    void JudgeSlice(SliceExpr *se, const Term &lent) {
        sltotal++;
        auto bt = [&](Node *bn, bool fromend, Term dflt) -> Term {
            if (!bn) return dflt;
            auto t = TermOf(bn);
            if (!t.ok) return {};
            if (fromend) {
                if (t.b.kind != BK_ZERO || !lent.ok) return {};
                return Term { true, lent.b, SatSub(lent.off, t.off) };
            }
            return t;
        };
        auto lot = bt(se->lo, se->lo_from_end, Term { true, Zero(), 0 });
        auto hit = bt(se->hi, se->hi_from_end, lent);
        auto ok = lent.ok && lot.ok && hit.ok &&
                  Query(Zero(), lot.b, lot.off) &&
                  Query(lot.b, hit.b, SatSub(hit.off, lot.off)) &&
                  Query(hit.b, lent.b, SatSub(lent.off, hit.off));
        if (ok) { se->nobc = true; slelided++; }
        RecordLine(se->line, ok);
    }

    // ------------------------------------------------------------------
    // Condition facts. Only added when evaluating the condition could not
    // have changed tracked state (the caller checks HasKillEffects), since
    // the comparisons ran against pre-kill values otherwise.

    void CondFacts(Node *c, bool truth) {
        if (mode == M_KILLS) return;
        if (auto u = Is<Unary>(c); u && u->op == T_NOT) { CondFacts(u->child, !truth); return; }
        auto b = Is<Binary>(c);
        if (!b) return;
        if ((b->op == T_ANDAND && truth) || (b->op == T_OROR && !truth)) {
            CondFacts(b->left, truth);
            CondFacts(b->right, truth);
            return;
        }
        auto op = b->op;
        if (op != T_LT && op != T_GT && op != T_LTEQ && op != T_GTEQ && op != T_EQ &&
            op != T_NEQ)
            return;
        auto intok = [](Node *n) {
            auto t = n->exprtype;
            return t && t->kind == TY_INT && t->intstorage != IS_U64 &&
                   t->intstorage != IS_VARINT;
        };
        if (!intok(b->left) || !intok(b->right)) return;
        auto lt = TermOf(b->left), rt = TermOf(b->right);
        if (!lt.ok || !rt.ok || !CmpAdmissible(lt) || !CmpAdmissible(rt)) return;
        if (!truth) {
            switch (op) {
                case T_LT:   op = T_GTEQ; break;
                case T_LTEQ: op = T_GT; break;
                case T_GT:   op = T_LTEQ; break;
                case T_GTEQ: op = T_LT; break;
                case T_EQ:   op = T_NEQ; break;
                default:     op = T_EQ; break;
            }
        }
        auto le = [&](const Term &x, const Term &y, int64_t d) {   // x <= y + d.
            AddFactB(x.b, y.b, SatAdd(SatSub(y.off, x.off), d));
        };
        switch (op) {
            case T_LT:   le(lt, rt, -1); break;
            case T_LTEQ: le(lt, rt, 0); break;
            case T_GT:   le(rt, lt, -1); break;
            case T_GTEQ: le(rt, lt, 0); break;
            case T_EQ:   le(lt, rt, 0); le(rt, lt, 0); break;
            default: break;   // != alone bounds nothing.
        }
    }

    // ------------------------------------------------------------------
    // Prescans.

    void NoteVar(VarDef *v) {
        if (ScalarIntVar(v)) intvars.insert(v);
    }

    void MarkAddr(Node *n) {
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) {
                auto v = id->vdef;
                if (!v) return;
                addrof.insert(v);
                auto [k, u] = UltOf(v);
                if (k == UK_OWNED && u && u != v) addrof.insert(u);
                return;
            }
            if (auto d = Is<Dot>(cur)) { cur = d->obj; continue; }
            if (auto ix = Is<Index>(cur)) { cur = ix->obj; continue; }
            if (auto u2 = Is<Unary>(cur)) { cur = u2->child; continue; }
            return;
        }
    }

    void NotePlace(Node *n) {
        auto pid = PlaceOf(n);
        if (pid >= 0) specpids.insert(pid);
    }

    // Which variable an index/bound expression pivots on, for relating it to
    // the array it indexes when selecting `v <= len(P)` invariant candidates.
    static VarDef *PivotVar(Node *n) {
        if (auto id = Is<Ident>(n)) return id->vdef;
        if (auto b = Is<Binary>(n); b && (b->op == T_PLUS || b->op == T_MINUS)) {
            if (auto v = PivotVar(b->left)) return v;
            return PivotVar(b->right);
        }
        return nullptr;
    }

    void NoteRel(Node *obj, Node *idx) {
        if (!idx) return;
        auto v = PivotVar(idx);
        if (!v) return;
        auto pid = PlaceOf(obj);
        if (pid >= 0) relpids[v].insert(pid);
    }

    void MarkPass(Node *n) {
        if (!n) return;
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) MarkAddr(u->child);
        if (auto fl = Is<ForLoop>(n)) {
            if (fl->byref) MarkAddr(fl->iter);
            NoteVar(fl->vdef);
            NoteVar(fl->idxdef);
        }
        if (auto m = Is<MatchExpr>(n))
            for (auto &arm : m->arms) {
                if (arm.pat.byref) MarkAddr(m->scrutinee);
                NoteVar(arm.binder);
            }
        if (auto id = Is<Ident>(n)) NoteVar(id->vdef);
        if (auto vd = Is<VarDecl>(n)) {
            for (auto d : vd->defs) NoteVar(d);
            if (vd->defs.size() == 1 && vd->inits.size() == 1 && vd->defs[0])
                wdecl.insert(vd->defs[0]);
            else
                for (auto d : vd->defs) if (d) wbad.insert(d);
        }
        if (auto a = Is<Assign>(n)) {
            if (auto id = Is<Ident>(a->lval); id && id->vdef && !a->pointee) {
                auto v = id->vdef;
                auto lit = Is<IntLit>(a->rhs);
                if (a->op == T_ASSIGN) {
                    wkinds[v].setw = true;
                } else if ((a->op == T_PLUSEQ || a->op == T_MINUSEQ) && lit && !lit->uns) {
                    auto c = a->op == T_PLUSEQ ? lit->val : -lit->val;
                    (c >= 0 ? wkinds[v].inc : wkinds[v].dec) = true;
                } else {
                    wbad.insert(v);
                }
            }
        }
        if (auto x = Is<IncDec>(n)) {
            if (auto id = Is<Ident>(x->lval); id && id->vdef &&
                (!id->vdef->type || id->vdef->type->kind != TY_REF))
                (x->op == T_INC ? wkinds[id->vdef].inc : wkinds[id->vdef].dec) = true;
        }
        if (auto ix = Is<Index>(n)) {
            NotePlace(ix->obj);
            NoteRel(ix->obj, ix->idx);
        }
        if (auto se = Is<SliceExpr>(n)) {
            NotePlace(se->obj);
            NoteRel(se->obj, se->lo);
            NoteRel(se->obj, se->hi);
        }
        if (auto d = Is<Dot>(n); d && d->member == B_LEN) NotePlace(d->obj);
        if (auto c = Is<Call>(n)) MarkPass(c->fvbody);
        n->Children([&](Node *ch) { MarkPass(ch); });
    }

    // Breaks that would bind a loop/block whose body is `n` (stops at nested
    // binders; inlined bodies are transparent, over-approximating is fine).
    bool HasBreaks(Node *n) {
        if (!n) return false;
        if (Is<Break>(n)) return true;
        if (auto g = Is<Guard>(n); g && !g->elseb && g->implicitexit == 1) return true;
        if (Is<While>(n) || Is<LoopExpr>(n) || Is<ForLoop>(n) || Is<EarlyBlock>(n))
            return false;
        auto found = false;
        if (auto c = Is<Call>(n)) found = HasBreaks(c->fvbody);
        n->Children([&](Node *ch) { found = found || HasBreaks(ch); });
        return found;
    }

    bool ReturnsForTarget(Node *n, SFunction *sf) {
        if (!n) return false;
        if (auto r = Is<Return>(n)) if (r->target == sf) return true;
        auto found = false;
        if (auto c = Is<Call>(n)) found = ReturnsForTarget(c->fvbody, sf);
        n->Children([&](Node *ch) { found = found || ReturnsForTarget(ch, sf); });
        return found;
    }

    // A break/continue that would cut an iteration of the loop whose body is
    // `n` short, making its per-iteration push count unreliable (returns are
    // fine: they leave the loop for good, and the facts are stated after it).
    bool HasIterationJumps(Node *n) {
        if (!n) return false;
        if (Is<Break>(n) || Is<Continue>(n)) return true;
        if (auto g = Is<Guard>(n); g && !g->elseb && g->implicitexit == 1) return true;
        if (Is<While>(n) || Is<LoopExpr>(n) || Is<ForLoop>(n) || Is<EarlyBlock>(n))
            return false;
        auto found = false;
        if (auto c = Is<Call>(n)) found = HasIterationJumps(c->fvbody);
        n->Children([&](Node *ch) { found = found || HasIterationJumps(ch); });
        return found;
    }

    // Collects the place ids `n` can invalidate into `out`.
    void SummarizeInto(Node *n, set<int> &out) {
        auto savedflow = flow;
        auto savedmode = mode;
        auto savedks = ksum;
        auto savedsh = shsum;
        auto savedvks = vksum;
        ksum = &out;
        shsum = nullptr;
        vksum = nullptr;
        mode = M_KILLS;
        Stmt(n);
        flow = std::move(savedflow);
        mode = savedmode;
        ksum = savedks;
        shsum = savedsh;
        vksum = savedvks;
    }

    // Runs the walk over `n` recording only kill effects into a scratch flow;
    // returns whether anything tracked was changed.
    bool HasKillEffects(Node *n) {
        auto savedflow = flow;
        auto savedmode = mode;
        auto savedany = anybump;
        auto savedks = ksum;
        auto savedsh = shsum;
        auto savedvks = vksum;
        ksum = nullptr;
        shsum = nullptr;
        vksum = nullptr;
        anybump = false;
        mode = M_KILLS;
        Stmt(n);
        auto r = anybump;
        flow = std::move(savedflow);
        mode = savedmode;
        anybump = savedany;
        ksum = savedks;
        shsum = savedsh;
        vksum = savedvks;
        return r;
    }

    // Applies the kill effects of `n` to the current flow (loop-entry havoc).
    void StripKills(Node *n) {
        auto saved = mode;
        mode = M_KILLS;
        Stmt(n);
        mode = saved;
    }

    // ------------------------------------------------------------------
    // The walk. Stmt returns false when the statement provably diverges.

    bool Stmt(Node *n) {
        if (!n) return true;
        if (auto vd = Is<VarDecl>(n)) {
            for (auto i : vd->inits) Val(i);
            if (vd->defs.size() == 1 && vd->inits.size() == 1 && vd->defs[0]) {
                auto v = vd->defs[0];
                auto lt = mode != M_KILLS && v->type && v->type->kind == TY_ARRAY
                              ? FreshLenOf(vd->inits[0]) : Term {};
                if (ScalarIntVar(v)) {
                    SetWrite(v, vd->inits[0], true);
                } else if (loopdepth > 0 && v->type &&
                           (v->type->kind == TY_SLICE || v->type->kind == TY_REF)) {
                    RebindKill(v);   // Loop-repeated redeclaration rebinds.
                } else if (loopdepth > 0 && v->type && v->type->kind != TY_FLT &&
                           v->type->kind != TY_BOOL && v->type->kind != TY_INT) {
                    StorageWriteKill(UK_OWNED, v, v);   // Loop-repeated reconstruction.
                }
                if (lt.ok) {
                    auto pid = PlaceOfVar(v);
                    if (pid >= 0) ExactLenIs(pid, lt);
                }
            } else {
                for (auto d : vd->defs) if (d) VarKillWrite(d);
            }
            return true;
        }
        if (auto a = Is<Assign>(n)) { DoAssign(a); return true; }
        if (auto x = Is<IncDec>(n)) { DoIncDec(x); return true; }
        if (auto r = Is<Return>(n)) {
            for (auto v : r->vals) Val(v);
            return false;
        }
        if (auto b = Is<Break>(n)) {
            if (b->val) Val(b->val);
            return false;
        }
        if (Is<Continue>(n)) return false;
        if (auto g = Is<Guard>(n)) { DoGuard(g); return true; }
        if (Is<FnDecl>(n)) return true;
        return ValCtl(n);
    }

    bool ValCtl(Node *n) {
        if (auto b = Is<Block>(n)) return DoBlock(b);
        if (auto f = Is<IfExpr>(n)) return DoIf(f);
        if (auto m = Is<MatchExpr>(n)) return DoMatch(m);
        if (auto w = Is<While>(n)) { DoWhile(w); return true; }
        if (auto fl = Is<ForLoop>(n)) { DoFor(fl); return true; }
        if (auto lp = Is<LoopExpr>(n)) { DoLoop(lp); return true; }
        if (auto eb = Is<EarlyBlock>(n)) return DoEarly(eb);
        if (auto ib = Is<InlineBlock>(n)) return DoInline(ib);
        if (auto c = Is<Call>(n); c && c->builtin == B_ASSERT) {
            DoCall(c);
            if (!c->args.empty())
                if (auto bl = Is<BoolLit>(c->args[0]); bl && !bl->val) return false;
            return true;
        }
        Val(n);
        return true;
    }

    void Val(Node *n) {
        if (!n) return;
        if (Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) || Is<StrLit>(n) ||
            Is<NullLit>(n) || Is<Ident>(n) || Is<FunVal>(n))
            return;
        if (auto u = Is<Unary>(n)) { Val(u->child); return; }
        if (auto b = Is<Binary>(n)) { DoBinary(b); return; }
        if (auto d = Is<Dot>(n)) { Val(d->obj); return; }
        if (auto c = Is<Call>(n)) { DoCall(c); return; }
        if (auto ix = Is<Index>(n)) {
            Val(ix->obj);
            Val(ix->idx);
            JudgeIndex(ix);
            return;
        }
        if (auto se = Is<SliceExpr>(n)) { DoSlice(se); return; }
        if (auto ac = Is<AsCast>(n)) { Val(ac->child); return; }
        if (auto re = Is<RangeExpr>(n)) {
            Val(re->lo);
            Val(re->hi);
            return;
        }
        if (auto al = Is<ArrayLit>(n)) {
            for (auto e : al->elems) Val(e);
            if (al->fillval) Val(al->fillval);
            if (al->fillcount) Val(al->fillcount);
            if (al->capexpr) Val(al->capexpr);
            return;
        }
        if (auto sl = Is<StructLit>(n)) {
            for (auto &fi : sl->inits) Val(fi.val);
            return;
        }
        if (auto bl = Is<Block>(n)) { DoBlock(bl); return; }
        if (auto f = Is<IfExpr>(n)) { DoIf(f); return; }
        if (auto m = Is<MatchExpr>(n)) { DoMatch(m); return; }
        if (auto w = Is<While>(n)) { DoWhile(w); return; }
        if (auto fl = Is<ForLoop>(n)) { DoFor(fl); return; }
        if (auto lp = Is<LoopExpr>(n)) { DoLoop(lp); return; }
        if (auto eb = Is<EarlyBlock>(n)) { DoEarly(eb); return; }
        if (auto ib = Is<InlineBlock>(n)) { DoInline(ib); return; }
        // Anything else carries no tracked effects.
    }

    void DoBinary(Binary *b) {
        if ((b->op == T_ANDAND || b->op == T_OROR) && mode != M_KILLS) {
            // The right side runs only when the left settled it one way; its
            // facts and effects merge against the short-circuit path.
            Val(b->left);
            auto after = flow;
            CondFacts(b->left, b->op == T_ANDAND);
            Val(b->right);
            flow = Meet(after, flow);
            return;
        }
        Val(b->left);
        Val(b->right);
    }

    void DoCall(Call *c) {
        Node *recv = nullptr;
        if (auto d = Is<Dot>(c->callee)) {
            recv = d->obj;
            Val(recv);
        }
        for (auto a : c->args) Val(a);
        if (c->builtin >= 0) {
            auto rn = recv ? recv : (c->args.empty() ? nullptr : c->args[0]);
            // The first non-receiver argument, in either call spelling.
            auto arg0 = recv ? (c->args.empty() ? nullptr : c->args[0])
                             : (c->args.size() > 1 ? c->args[1] : nullptr);
            switch (c->builtin) {
                case B_PUSH:
                    GrowShrinkKill(rn, 1, 1);
                    break;
                case B_APPEND: {
                    auto st = arg0 ? LenTermOf(arg0) : Term {};
                    GrowShrinkKill(rn, 1, st.ok && st.b.kind == BK_ZERO ? st.off
                                                                        : INT64_MIN);
                    break;
                }
                case B_ALLOC_INDEX: case B_ALLOC_REF:
                    GrowShrinkKill(rn, 1);
                    break;
                case B_POP: {
                    // Exact only when provably non-empty: pop on an empty
                    // array is unchecked and wraps the stored length.
                    auto exact = INT64_MIN;
                    if (mode != M_KILLS && rn) {
                        auto lt = LenTermOf(rn);
                        if (lt.ok && Query(Zero(), lt.b, SatSub(lt.off, 1)))
                            exact = -1;
                    }
                    GrowShrinkKill(rn, -1, exact);
                    break;
                }
                case B_CLEAR: {
                    auto pid = GrowShrinkKill(rn, -1);
                    if (pid >= 0) ExactLenIs(pid, Term { true, Zero(), 0 });
                    break;
                }
                case B_RESIZE: {
                    auto nt = mode == M_KILLS ? Term {} : TermOf(arg0);
                    auto pid = GrowShrinkKill(rn, 0);
                    if (pid >= 0) ExactLenIs(pid, nt);
                    break;
                }
                case B_ASSERT:
                    if (mode != M_KILLS && !c->args.empty() && !HasKillEffects(c->args[0]))
                        CondFacts(c->args[0], true);
                    break;
                default:
                    break;   // len/cap/print/free/queues/threads: no tracked effect.
            }
            return;
        }
        if (c->fvbody) {
            // The function value runs inside the callee, possibly repeatedly
            // and after arbitrary callee effects: analyze it from scratch.
            auto saved = std::move(flow);
            flow = Flow {};
            loopdepth++;
            for (auto p : c->fvparams) if (p) BumpVar(p, false);   // Re-bind per call.
            DoBlock(c->fvbody);
            loopdepth--;
            flow = std::move(saved);
        }
        auto anyref = false;
        auto reft = [](Node *a) { return a->exprtype && a->exprtype->kind == TY_REF; };
        if (recv && reft(recv)) anyref = true;
        for (auto a : c->args) anyref = anyref || reft(a);
        KillByCall(anyref);
    }

    void DoSlice(SliceExpr *se) {
        Val(se->obj);
        // The runtime check compares against a length snapshot taken before
        // the bound expressions run; capture the term at that moment.
        auto lent = mode == M_JUDGE ? LenTermOf(se->obj) : Term {};
        if (se->lo) Val(se->lo);
        if (se->hi) Val(se->hi);
        if (mode == M_JUDGE) JudgeSlice(se, lent);
    }

    void DoAssign(Assign *a) {
        WalkLvalParts(a->lval);
        Val(a->rhs);
        if (a->op == T_DOTASSIGN) {
            auto ch = ChainOf(a->lval);
            if (ch.kind == CH_INDEX) return;   // Element ref slots: no tracked places.
            if (ch.kind == CH_OK) RebindKill(ch.root);
            else StorageWriteKill(UK_OPAQUE, nullptr, nullptr);
            return;
        }
        if (a->pointee) {
            auto lt = a->lval->exprtype;
            PointeeWriteKill(a->lval, lt && lt->kind == TY_REF ? lt->ref->sub : nullptr);
            return;
        }
        if (auto id = Is<Ident>(a->lval)) {
            auto v = id->vdef;
            if (!v) return;
            if (ScalarIntVar(v)) {
                if (a->op == T_ASSIGN) {
                    SetWrite(v, a->rhs, false);
                } else if (a->op == T_PLUSEQ || a->op == T_MINUSEQ) {
                    auto lit = Is<IntLit>(a->rhs);
                    if (lit && !lit->uns && SmallOff(lit->val))
                        ShiftWrite(v, a->op == T_PLUSEQ ? lit->val : -lit->val);
                    else VarKillWrite(v);
                } else {
                    VarKillWrite(v);
                }
                return;
            }
            auto t = v->type;
            auto lt = mode != M_KILLS && t && t->kind == TY_ARRAY && a->op == T_ASSIGN
                          ? FreshLenOf(a->rhs) : Term {};
            if (t && t->kind == TY_SLICE) RebindKill(v);
            else if (t && t->kind != TY_FLT && t->kind != TY_BOOL && t->kind != TY_INT)
                StorageWriteKill(UK_OWNED, v, v);
            if (lt.ok) {
                auto pid = PlaceOfVar(v);
                if (pid >= 0) ExactLenIs(pid, lt);
            }
            return;
        }
        if (Is<Index>(a->lval)) return;   // Element writes cannot change any tracked length.
        auto lt = a->lval->exprtype;
        if (lt && (lt->kind == TY_INT || lt->kind == TY_FLT || lt->kind == TY_BOOL))
            return;   // A scalar field write cannot change any length.
        auto ch = ChainOf(a->lval);
        if (ch.kind == CH_INDEX) return;
        if (ch.kind == CH_FAIL || !ch.root) {
            StorageWriteKill(UK_OPAQUE, nullptr, nullptr);
            return;
        }
        if (!ch.anycross) {
            StorageWriteKill(UK_OWNED, ch.root, ch.root);
        } else if (ch.rootonlycross) {
            auto [k, u] = UltOf(ch.root);
            if (k != UK_STATIC) StorageWriteKill(k, u, ch.root);
            else RebindKill(ch.root);
        } else {
            StorageWriteKill(UK_OPAQUE, nullptr, ch.root);
        }
    }

    void DoIncDec(IncDec *x) {
        WalkLvalParts(x->lval);
        auto lt = x->lval->exprtype;
        if (lt && lt->kind == TY_REF) {
            PointeeWriteKill(x->lval, lt->ref->sub);
            return;
        }
        if (auto id = Is<Ident>(x->lval); id && id->vdef) {
            if (ScalarIntVar(id->vdef)) ShiftWrite(id->vdef, x->op == T_INC ? 1 : -1);
            else VarKillWrite(id->vdef);
        }
        // A field/element integer is not a tracked base.
    }

    void WalkLvalParts(Node *n) {
        if (Is<Ident>(n)) return;
        if (auto d = Is<Dot>(n)) { Val(d->obj); return; }
        if (auto ix = Is<Index>(n)) {
            Val(ix->obj);
            Val(ix->idx);
            JudgeIndex(ix);
            return;
        }
        Val(n);
    }

    bool DoBlock(Block *b) {
        auto fell = true;
        for (auto st : b->stmts) {
            fell = Stmt(st);
            if (!fell) break;
        }
        if (fell && b->tail) Val(b->tail);
        return fell;
    }

    bool DoIf(IfExpr *f) {
        Val(f->cond);
        if (mode == M_KILLS) {
            DoBlock(f->thenb);
            if (f->elseb) Stmt(f->elseb);
            return true;
        }
        auto killfree = !HasKillEffects(f->cond);
        auto base = flow;
        if (killfree) CondFacts(f->cond, true);
        auto fellthen = DoBlock(f->thenb);
        auto thenf = std::move(flow);
        flow = std::move(base);
        if (killfree) CondFacts(f->cond, false);
        auto fellelse = f->elseb ? Stmt(f->elseb) : true;
        if (fellthen && !fellelse) { flow = std::move(thenf); return true; }
        if (!fellthen && fellelse) return true;   // flow is already the else path.
        if (!fellthen && !fellelse) return false;
        flow = Meet(thenf, flow);
        return true;
    }

    bool DoMatch(MatchExpr *m) {
        Val(m->scrutinee);
        if (mode == M_KILLS) {
            for (auto &arm : m->arms) {
                if (arm.binder) BumpVar(arm.binder, false);   // Re-binds per execution.
                Stmt(arm.body);
            }
            return true;
        }
        auto st = TermOf(m->scrutinee);
        auto stt = m->scrutinee->exprtype;
        auto admissible = st.ok && CmpAdmissible(st) && stt && stt->kind == TY_INT &&
                          stt->intstorage != IS_U64;
        auto base = flow;
        Flow acc;
        auto anyfell = false;
        for (auto &arm : m->arms) {
            flow = base;
            if (admissible && (arm.pat.kind == P_INT || arm.pat.kind == P_RANGE)) {
                AddFactB(Zero(), st.b, SatSub(st.off, arm.lo));            // lo <= s.
                AddFactB(st.b, Zero(), SatSub(SatSub(arm.hi, 1), st.off)); // s <= hi-1.
            }
            auto fell = Stmt(arm.body);
            if (fell) {
                if (!anyfell) acc = std::move(flow);
                else acc = Meet(acc, flow);
                anyfell = true;
            }
        }
        if (!anyfell) { flow = std::move(base); return false; }
        flow = std::move(acc);
        return true;
    }

    void DoGuard(Guard *g) {
        Val(g->cond);
        if (mode == M_KILLS) {
            if (g->elseb) DoBlock(g->elseb);
            return;
        }
        auto killfree = !HasKillEffects(g->cond);
        if (g->elseb) {
            auto save = flow;
            if (killfree) CondFacts(g->cond, false);
            DoBlock(g->elseb);   // Must diverge (TC).
            flow = std::move(save);
        }
        if (killfree) CondFacts(g->cond, true);
    }

    void DoWhile(While *w) {
        loopdepth++;
        if (mode == M_KILLS) {
            Val(w->cond);
            DoBlock(w->body);
            loopdepth--;
            return;
        }
        StripKills(w->cond);
        StripKills(w->body);
        Val(w->cond);
        auto exitf = flow;
        auto killfree = !HasKillEffects(w->cond);
        if (killfree) CondFacts(w->cond, true);
        DoBlock(w->body);
        flow = std::move(exitf);
        loopdepth--;
        if (killfree && !HasBreaks(w->body)) CondFacts(w->cond, false);
    }

    void DoFor(ForLoop *fl) {
        if (mode == M_KILLS) {
            Val(fl->iter);
            loopdepth++;
            if (fl->vdef) BumpVar(fl->vdef, false);
            if (fl->idxdef) BumpVar(fl->idxdef, false);
            DoBlock(fl->body);
            loopdepth--;
            return;
        }
        Val(fl->iter);
        // Range/count loops compare the variable against loop-entry
        // snapshots: capture those terms before the body havoc. Array/slice
        // loops re-read the length every iteration: capture after.
        Term lot, hit;
        if (fl->iterkind == IK_RANGE) {
            if (auto r = Is<RangeExpr>(fl->iter)) {
                lot = TermOf(r->lo);
                hit = TermOf(r->hi);
            }
        } else if (fl->iterkind == IK_COUNT) {
            lot = Term { true, Zero(), 0 };
            hit = TermOf(fl->iter);
        }
        // Counted push loops `for _ in n { ...; a.push(x); ...; }`: when no
        // other statement can touch a's length and no break/continue skips an
        // iteration, the loop grows a by exactly its push count per
        // iteration, so afterwards len(a) == len-before + k*n.
        vector<pair<int, int64_t>> pushpids;   // (place, pushes per iteration).
        vector<Term> prelens;
        if ((fl->iterkind == IK_COUNT || fl->iterkind == IK_RANGE) && mode != M_KILLS &&
            !fl->body->tail && !HasIterationJumps(fl->body)) {
            map<int, int64_t> counts;
            set<int> otherbumps;
            for (auto st : fl->body->stmts) {
                if (auto pc = Is<Call>(st); pc && pc->builtin == B_PUSH) {
                    Node *precv = nullptr, *parg = nullptr;
                    if (auto pd = Is<Dot>(pc->callee)) {
                        precv = pd->obj;
                        parg = pc->args.empty() ? nullptr : pc->args[0];
                    } else if (pc->args.size() == 2) {
                        precv = pc->args[0];
                        parg = pc->args[1];
                    }
                    auto pid = precv ? PlaceOf(precv) : -1;
                    if (pid >= 0 && (!parg || !HasKillEffects(parg))) {
                        counts[pid]++;
                        continue;
                    }
                }
                SummarizeInto(st, otherbumps);
            }
            for (auto &[pid, k] : counts)
                if (!otherbumps.count(pid)) {
                    pushpids.push_back({ pid, k });
                    prelens.push_back(Term { true, LenBase(pid), 0 });
                }
        }
        loopdepth++;
        StripKills(fl->body);
        if (fl->iterkind == IK_ARRAY || fl->iterkind == IK_SLICE) {
            lot = Term { true, Zero(), 0 };
            hit = LenTermOf(fl->iter);
        }
        auto exitf = flow;
        auto iv = fl->iterkind == IK_ARRAY || fl->iterkind == IK_SLICE ? fl->idxdef
                                                                       : fl->vdef;
        if (iv) {
            auto vb = VarBase(iv);
            if (lot.ok && CmpAdmissible(lot)) AddFactB(lot.b, vb, SatSub(0, lot.off));
            if (hit.ok && CmpAdmissible(hit)) AddFactB(vb, hit.b, SatSub(hit.off, 1));
        }
        if ((fl->iterkind == IK_RANGE || fl->iterkind == IK_COUNT) && fl->idxdef)
            AddFactB(Zero(), VarBase(fl->idxdef), 0);
        DoBlock(fl->body);
        flow = std::move(exitf);
        loopdepth--;
        if (!pushpids.empty()) {
            Term iters;
            if (fl->iterkind == IK_COUNT) {
                iters = hit;
            } else if (lot.ok && hit.ok) {
                if (lot.b == hit.b) iters = Term { true, Zero(), SatSub(hit.off, lot.off) };
                else if (lot.b.kind == BK_ZERO)
                    iters = Term { true, hit.b, SatSub(hit.off, lot.off) };
            }
            // A negative count runs zero iterations; facts only when >= 0.
            if (iters.ok && CmpAdmissible(iters) && Query(Zero(), iters.b, iters.off)) {
                for (size_t i = 0; i < pushpids.size(); i++) {
                    auto [pid, k] = pushpids[i];
                    auto &pre = prelens[i];
                    auto post = LenBase(pid);
                    if (iters.b.kind == BK_ZERO) {
                        auto total = SatAdd(iters.off, 0);
                        for (auto m = int64_t(1); m < k && total != INF; m++)
                            total = SatAdd(total, iters.off);
                        if (total == INF) continue;
                        AddFactB(post, pre.b, total);              // post == pre + k*n.
                        AddFactB(pre.b, post, SatSub(0, total));
                    } else if (k == 1) {
                        AddFactB(iters.b, post, SatSub(0, iters.off));   // n <= post.
                        if (Query(pre.b, Zero(), 0))                     // pre == 0:
                            AddFactB(post, iters.b, iters.off);          // post == n.
                    }
                }
            }
        }
    }

    void DoLoop(LoopExpr *lp) {
        loopdepth++;
        if (mode == M_KILLS) {
            DoBlock(lp->body);
            loopdepth--;
            return;
        }
        StripKills(lp->body);
        auto exitf = flow;
        DoBlock(lp->body);
        flow = std::move(exitf);
        loopdepth--;
    }

    bool DoEarly(EarlyBlock *eb) {
        if (mode == M_KILLS) {
            DoBlock(eb->body);
            return true;
        }
        if (!HasBreaks(eb->body)) return DoBlock(eb->body);
        auto entry = flow;
        DoBlock(eb->body);
        flow = std::move(entry);
        StripKills(eb->body);
        return true;
    }

    bool DoInline(InlineBlock *ib) {
        if (mode == M_KILLS) {
            DoBlock(ib->body);
            return true;
        }
        // A trailing return-to-this-block is the block's one normal exit and
        // needs no havoc; earlier ones join the end from other states.
        auto &stmts = ib->body->stmts;
        Node *last = stmts.empty() ? nullptr : stmts.back();
        auto lastret = Is<Return>(last) && ((Return *)last)->target == ib->sf;
        auto early = false;
        for (size_t i = 0; i + 1 < stmts.size(); i++)
            early = early || ReturnsForTarget(stmts[i], ib->sf);
        if (last) {
            if (lastret) {
                for (auto v : ((Return *)last)->vals)
                    early = early || ReturnsForTarget(v, ib->sf);
            } else {
                early = early || ReturnsForTarget(last, ib->sf);
            }
        }
        if (ib->body->tail) early = early || ReturnsForTarget(ib->body->tail, ib->sf);
        if (!early) {
            DoBlock(ib->body);
            return true;
        }
        auto entry = flow;
        DoBlock(ib->body);
        flow = std::move(entry);
        StripKills(ib->body);
        return true;
    }

    Flow Meet(const Flow &a, const Flow &b) {
        Flow r;
        for (auto &f : a.facts)
            for (auto &g : b.facts)
                if (f.l == g.l && f.r == g.r) {
                    r.facts.push_back({ f.l, f.r, std::max(f.c, g.c) });
                    break;
                }
        r.vgen = a.vgen;
        for (auto &[k, v] : b.vgen) {
            auto &x = r.vgen[k];
            x = std::max(x, v);
        }
        r.pgen = a.pgen;
        for (auto &[k, v] : b.pgen) {
            auto &x = r.pgen[k];
            x = std::max(x, v);
        }
        return r;
    }

    // ------------------------------------------------------------------
    // Per-specialization driver.

    void RunSpec(FnSpec *sp) {
        addrof.clear();
        intvars.clear();
        wdecl.clear();
        wbad.clear();
        wkinds.clear();
        specpids.clear();
        relpids.clear();
        derived.clear();
        cands.clear();
        ge0.clear();
        lelen.clear();
        mono.clear();
        for (auto &[v, ok] : gge0) if (ok) ge0.insert(v);
        MarkPass(sp->body);
        // Whole-body kill summary: which places and variables any path can
        // invalidate or re-bind.
        set<int> bumped, shrunk;
        set<VarDef *> vbumped;
        {
            auto savedflow = std::move(flow);
            flow = Flow {};
            ksum = &bumped;
            shsum = &shrunk;
            vksum = &vbumped;
            mode = M_KILLS;
            DoBlock(sp->body);
            ksum = nullptr;
            shsum = nullptr;
            vksum = nullptr;
            flow = std::move(savedflow);
        }
        // Invariant candidates: eligible variables against this spec's
        // never-invalidated places.
        auto eligible = [&](VarDef *v) {
            return v && wdecl.count(v) && !wbad.count(v) && !v->captured && !v->isglobal &&
                   !v->isparam && !addrof.count(v) && ScalarIntVar(v);
        };
        for (auto v : wdecl) {
            if (!eligible(v)) continue;
            auto &c = cands[v];
            auto rit = relpids.find(v);
            if (rit != relpids.end())
                for (auto pid : rit->second) {
                    // `v <= len(P)` survives any growth of P, so only a
                    // shrink (or a re-bound access path) rules P out.
                    if (shrunk.count(pid) || vbumped.count(places[pid].rootv)) continue;
                    if (c.le.size() >= 8) break;
                    c.le.push_back(pid);
                }
        }
        if (!cands.empty()) {
            mode = M_RECORD;
            flow = Flow {};
            derived.clear();
            DoBlock(sp->body);
            for (auto &[v, c] : cands) {
                if (!c.declseen) continue;
                if (c.ge0) ge0.insert(v);
                if (!c.le.empty()) lelen[v] = c.le;
                auto &wk = wkinds[v];
                if (c.wrapfree && !wk.setw && (wk.inc != wk.dec))
                    mono[v] = wk.inc ? 1 : -1;
            }
        }
        mode = M_JUDGE;
        flow = Flow {};
        derived.clear();
        DoBlock(sp->body);
    }

    // Seeds gge0 from the globals' initializers, then drops every candidate
    // some write in some live body fails to preserve. The candidate is
    // assumed while checking each write (ordinary induction: the initializer
    // is the base case, every write the step).
    void ValidateGlobalInvariants() {
        for (auto g : ast.globals) {
            if (g->defs.size() != 1 || g->inits.size() != 1) continue;
            auto v = g->defs[0];
            if (!ScalarIntVar(v)) continue;
            auto lit = Is<IntLit>(g->inits[0]);
            gge0[v] = lit && !lit->uns && lit->val >= 0;
        }
        if (gge0.empty()) return;
        // Any address-taking anywhere admits writes this pass cannot see.
        for (auto sp : ast.fnspecs) {
            if (!sp->live || !sp->body) continue;
            addrof.clear();
            intvars.clear();
            wdecl.clear();
            wbad.clear();
            wkinds.clear();
            relpids.clear();
            MarkPass(sp->body);
            for (auto v : addrof) gaddr.insert(v);
        }
        for (auto &[v, ok] : gge0) if (gaddr.count(v)) ok = false;
        for (auto sp : ast.fnspecs) {
            if (!sp->live || !sp->body) continue;
            auto any = false;
            for (auto &[v, ok] : gge0) if (ok) any = true;
            if (!any) return;
            addrof.clear();
            intvars.clear();
            wdecl.clear();
            wbad.clear();
            wkinds.clear();
            relpids.clear();
            derived.clear();
            cands.clear();
            ge0.clear();
            lelen.clear();
            mono.clear();
            MarkPass(sp->body);
            for (auto &[v, ok] : gge0)
                if (ok) {
                    cands[v].declseen = true;
                    ge0.insert(v);   // The inductive hypothesis.
                }
            mode = M_RECORD;
            flow = Flow {};
            DoBlock(sp->body);
            for (auto &[v, ok] : gge0) {
                auto it = cands.find(v);
                if (it != cands.end() && !it->second.ge0) ok = false;
            }
        }
        ge0.clear();
        cands.clear();
    }

    void RunAll() {
        ValidateGlobalInvariants();
        for (auto sp : ast.fnspecs)
            if (sp->live && sp->body) RunSpec(sp);
        // Global initializers and shared field defaults: judged with no
        // surrounding context (defaults are shared across construction sites).
        addrof.clear();
        intvars.clear();
        cands.clear();
        ge0.clear();
        lelen.clear();
        mono.clear();
        mode = M_JUDGE;
        for (auto g : ast.globals)
            for (auto i : g->inits) {
                flow = Flow {};
                Val(i);
            }
        for (auto si : ast.structinsts)
            for (auto d : si->defaults)
                if (d) {
                    flow = Flow {};
                    Val(d);
                }
        for (auto ei : ast.enuminsts)
            for (auto &vd : ei->vdefaults)
                for (auto d : vd)
                    if (d) {
                        flow = Flow {};
                        Val(d);
                    }
    }

    // ------------------------------------------------------------------
    // --bce-test: verify `// bce:elide` and `// bce:keep` source annotations
    // (every check on such a line must have the annotated outcome).

    int VerifyAnnotations() {
        auto fails = 0;
        for (size_t fi = 0; fi < ast.sources.size(); fi++) {
            auto &src = *ast.sources[fi].second;
            auto line = 1;
            size_t pos = 0;
            while (pos <= src.size()) {
                auto eol = src.find('\n', pos);
                auto len = (eol == string::npos ? src.size() : eol) - pos;
                auto sv = string_view(src).substr(pos, len);
                auto want = 0;
                if (sv.find("bce:elide") != string_view::npos) want = 1;
                else if (sv.find("bce:keep") != string_view::npos) want = 2;
                if (want) {
                    auto it = lineout.find({ (int)fi, line });
                    auto el = it == lineout.end() ? 0 : it->second.first;
                    auto kp = it == lineout.end() ? 0 : it->second.second;
                    auto ok = want == 1 ? el > 0 && kp == 0 : kp > 0 && el == 0;
                    if (!ok) {
                        fprintf(stderr,
                                "bce-test: %s:%d: annotated %s, but %d elided / %d kept\n",
                                ast.sources[fi].first.c_str(), line,
                                want == 1 ? "bce:elide" : "bce:keep", el, kp);
                        fails++;
                    }
                }
                if (eol == string::npos) break;
                pos = eol + 1;
                line++;
            }
        }
        return fails;
    }
};

}  // namespace goose
