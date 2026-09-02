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
// delta on the previous length, a counted loop whose body pushes a fixed
// number of times per iteration, and a slice binding, whose length is the
// difference of its bounds (`src[lo..lo + W]` is W long, so the row-slice
// idiom needs no assert). Values likewise: `a % b` and `a & b` land in
// [0, b], and a cast whose value the facts already place in the target's
// range carries its operand's term across — together these prove the
// reduce-a-hash-into-a-table idiom without any guard in the source.
//
// Products and two-variable sums fall outside a difference domain, so they
// are handled by intervals instead: where both operands have constant bounds,
// `a * b` takes the extremes of the four corner products and `a ± b` the sum
// of the two ranges, on a one-shot base. That is what proves a row-major
// index — `y * W + x` against a `W * H` length — and the interval is only
// stated when it fits the expression's own width, since §6.2 arithmetic wraps
// there in release builds.
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
    // §10.4: a data stack reserves at most 2^48 bytes, so no value is larger
    // and no array longer than that. Guaranteed rather than assumed, and the
    // 15 bits of headroom below i64 are what make size arithmetic (i + 1,
    // len - 1, len + len) provably free of overflow.
    static constexpr int64_t LENMAX = int64_t(1) << 48;
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
        auto d = Dist(l, r);
        return d != INF && d <= c;
    }

    // The smallest c for which `l <= r + c` is provable, INF when nothing is.
    int64_t Dist(Base l, Base r) {
        if (l == r) return 0;
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
        return dist[idx(r)];
    }

    // ------------------------------------------------------------------
    // Constant intervals. The difference domain relates two bases at a time,
    // so it cannot name `a * b` or `a + b` at all; what it can do is read off
    // each operand's constant bounds and combine those, which is enough for
    // the row-major index shapes (`y * W + x` against a `W * H` length).

    struct Ival { bool ok = false; int64_t lo = 0, hi = 0; };

    // The tightest constant bounds the facts prove for a term's value. Both
    // ends must be finite: an interval open at either end says nothing a
    // product or a sum could use, and every bound produced here stays inside
    // CCAP, so the arithmetic below cannot leave the range the facts and the
    // machine agree on.
    Ival BoundsOf(const Term &t) {
        if (!t.ok) return {};
        if (t.b.kind == BK_ZERO) return Ival { true, t.off, t.off };
        auto up = Dist(t.b, Zero());   // b <= up.
        if (up == INF) return {};
        auto dn = Dist(Zero(), t.b);   // 0 <= b + dn, i.e. b >= -dn.
        if (dn == INF) return {};
        auto hi = SatAdd(up, t.off), lo = SatAdd(SatSub(0, dn), t.off);
        if (hi == INF || lo == INF || hi > CCAP || lo < -CCAP) return {};
        if (lo > hi) return {};   // Contradictory facts prove nothing usable.
        return Ival { true, lo, hi };
    }

    // Exact i64 product, refused when the result would leave the fact range.
    static bool MulIn(int64_t a, int64_t b, int64_t &r) {
        if (a >= CCAP || a <= -CCAP || b >= CCAP || b <= -CCAP) return false;
        if (a == 0 || b == 0) { r = 0; return true; }
        auto aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
        if (aa > CCAP / bb) return false;
        r = a * b;
        return true;
    }

    // A one-shot base carrying [lo, hi], for a value the operation computes
    // but the domain cannot relate to anything else. The interval is only
    // stated when the machine's own arithmetic agrees with it: §6.2 wraps at
    // the expression's width in release builds, so a result the facts place
    // outside that width is not the value the program computed.
    Term IvalTerm(TypeExpr *t, int64_t lo, int64_t hi) {
        if (!t || t->kind != TY_INT || t->intstorage == IS_U64 ||
            t->intstorage == IS_VARINT)
            return {};
        auto [tlo, thi] = RangeOf(t->intstorage);
        if (lo < tlo || hi > thi) return {};
        auto m = TmpBase();
        AddFactB(Zero(), m, SatSub(0, lo));   // lo <= m.
        AddFactB(m, Zero(), hi);              // m <= hi.
        return Term { true, m, 0 };
    }

    // `a * b` from the operands' constant bounds: the four corner products
    // bracket the result whatever the signs, so a negative counter is handled
    // by the same rule as a nonnegative one.
    Term MulTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = b->exprtype;
        if (!t || t->kind != TY_INT || t->intstorage == IS_U64 ||
            t->intstorage == IS_VARINT)
            return {};
        auto lt = TermOf(b->left), rt = TermOf(b->right);
        if (!lt.ok || !rt.ok) return {};
        auto li = BoundsOf(lt), ri = BoundsOf(rt);
        if (!li.ok || !ri.ok) return {};
        int64_t c[4];
        if (!MulIn(li.lo, ri.lo, c[0]) || !MulIn(li.lo, ri.hi, c[1]) ||
            !MulIn(li.hi, ri.lo, c[2]) || !MulIn(li.hi, ri.hi, c[3]))
            return {};
        auto lo = c[0], hi = c[0];
        for (auto i = 1; i < 4; i++) {
            lo = std::min(lo, c[i]);
            hi = std::max(hi, c[i]);
        }
        return IvalTerm(b->exprtype, lo, hi);
    }

    // `a + b` / `a - b` where neither side is a constant, so the difference
    // domain has no term for it: the intervals add. Both ends came out of
    // BoundsOf inside CCAP, so the sum cannot overflow the i64 the facts are
    // computed in.
    Term IvalSumTerm(Binary *b, const Term &lt, const Term &rt) {
        if (mode == M_KILLS) return {};
        auto li = BoundsOf(lt), ri = BoundsOf(rt);
        if (!li.ok || !ri.ok) return {};
        auto minus = b->op == T_MINUS;
        auto lo = minus ? li.lo - ri.hi : li.lo + ri.lo;
        auto hi = minus ? li.hi - ri.lo : li.hi + ri.hi;
        if (lo < -CCAP || hi > CCAP) return {};
        return IvalTerm(b->exprtype, lo, hi);
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
            if (!t || t->kind != TY_INT || t->intstorage == IS_U64 ||
                t->intstorage == IS_VARINT)
                return {};
            auto lt = TermOf(b->left), rt = TermOf(b->right);
            if (!lt.ok || !rt.ok) return {};
            if (b->op == T_PLUS && lt.b.kind == BK_ZERO) std::swap(lt, rt);
            // Two moving operands: no difference term, but the intervals add.
            if (rt.b.kind != BK_ZERO)
                return Derived(n, [&] { return IvalSumTerm(b, lt, rt); });
            if (t->intstorage != IS_I64) return {};
            auto off = b->op == T_PLUS ? SatAdd(lt.off, rt.off) : SatSub(lt.off, rt.off);
            if (off == INF) return {};
            if (lt.b.kind != BK_ZERO && !SmallOff(off)) return {};
            return Term { true, lt.b, off };
        }
        if (auto b = Is<Binary>(n); b && b->op == T_MUL)
            return Derived(n, [&] { return MulTerm(b); });
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

    // `a % b` and `a & b` bound their result by b (§6.2): a Euclidean
    // remainder lies in [0, |b|) whatever the dividend's sign, and a mask can
    // only pass bits the mask has. Both yield a fresh base carrying the
    // resulting facts.
    Term RangedOpTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = b->exprtype;
        if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT) return {};
        auto rt = TermOf(b->right);
        if (!rt.ok) return {};
        auto m = TmpBase();
        AddFactB(Zero(), m, 0);   // 0 <= result, unconditionally.
        if (b->op == T_MOD) {
            // A zero divisor aborts (§6.2), so a completed modulo has b != 0.
            // The upper bound is |b|, which is a term only when the divisor
            // is provably nonnegative or is a negative constant.
            if (Query(Zero(), rt.b, rt.off)) AddFactB(m, rt.b, SatSub(rt.off, 1));
            else if (rt.b.kind == BK_ZERO && rt.off < 0)
                AddFactB(m, Zero(), SatSub(SatSub(0, rt.off), 1));
        } else if (Query(Zero(), rt.b, rt.off)) {
            AddFactB(m, rt.b, rt.off);
        }
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

    // One bound of a slice expression, as a term in the state at the moment
    // that bound is evaluated: absent means 0 (lower) or the length snapshot
    // (upper), and a from-end bound is that snapshot minus the term.
    Term SliceBound(Node *bn, bool fromend, const Term &lent, Term dflt) {
        if (!bn) return dflt;
        auto t = TermOf(bn);
        if (!t.ok) return {};
        if (!fromend) return t;
        if (t.b.kind != BK_ZERO || !lent.ok) return {};
        return Term { true, lent.b, SatSub(lent.off, t.off) };
    }

    // A completed `p[lo..hi]` has length exactly hi - lo: the check aborts
    // unless 0 <= lo <= hi <= len, so on any path that continues the
    // difference is the new length, and a slice's length never moves
    // afterwards (growing p leaves the slice's own header alone). The domain
    // can name that difference when both bounds sit on one base -- the
    // `src[lo..lo + W]` row idiom, a constant length -- or when the lower
    // bound is a constant, leaving a length offset from the upper bound's.
    static Term SliceLenTerm(const Term &lot, const Term &hit) {
        if (!lot.ok || !hit.ok) return {};
        auto d = SatSub(hit.off, lot.off);
        if (!SmallOff(d)) return {};
        // A constant difference below zero is a slice that always aborts;
        // stating it as a length would only contradict `0 <= len`.
        if (lot.b == hit.b) return d < 0 ? Term {} : Term { true, Zero(), d };
        if (lot.b.kind == BK_ZERO) return Term { true, hit.b, d };
        return {};
    }

    // The length of the value the last walked SliceExpr produced, consumed by
    // the declaration or assignment that binds it.
    Term slicelen;

    void JudgeSlice(SliceExpr *se, const Term &lent, const Term &lot, const Term &hit) {
        sltotal++;
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

    // ------------------------------------------------------------------
    // The two walks. Both dispatch to the per-node overrides at the end of
    // this file; a null child is simply nothing to analyze.

    // Analyzes `n` in its program position; false when control provably does
    // not continue past it. Value positions discard that (an expression only
    // fails to complete by diverging, which the enclosing statement sees).
    bool Walk(Node *n) { return n ? n->BceWalk(*this) : true; }

    void Mark(Node *n) { if (n) n->BceMark(*this); }

    // The parts of an assignment target that are themselves evaluated: the
    // location is written rather than read, but an index into it is computed
    // and bounds-checked like any other.
    void WalkLvalParts(Node *n) {
        if (Is<Ident>(n)) return;
        if (auto d = Is<Dot>(n)) { Walk(d->obj); return; }
        if (auto ix = Is<Index>(n)) {
            Walk(ix->obj);
            Walk(ix->idx);
            JudgeIndex(ix);
            return;
        }
        Walk(n);
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
        Walk(n);
        flow = std::move(savedflow);
        mode = savedmode;
        ksum = savedks;
        shsum = savedsh;
        vksum = savedvks;
    }

    // Reference variables `n` indexes, whose pointee is an array with a
    // length worth reading once (a fixed array's is a constant).
    void RefIndexed(Node *n, vector<VarDef *> &out) {
        if (!n) return;
        if (auto ix = Is<Index>(n))
            if (auto id = Is<Ident>(ix->obj); id && id->vdef && id->vdef->type) {
                auto t = id->vdef->type;
                auto s = t->kind == TY_REF ? t->ref->sub : nullptr;
                if (s && s->kind == TY_ARRAY && s->arr->akind != A_FIXED &&
                    std::find(out.begin(), out.end(), id->vdef) == out.end())
                    out.push_back(id->vdef);
            }
        // As in the prescan: `trailing` is a template, fvbody is what runs.
        if (auto c = Is<Call>(n)) {
            RefIndexed(c->callee, out);
            for (auto a : c->args) RefIndexed(a, out);
            RefIndexed(c->fvbody, out);
            return;
        }
        n->Children([&](Node *ch) { RefIndexed(ch, out); });
    }

    // Which of the reference variables a loop indexes keep the same array,
    // base and length throughout: an array behind a reference has both halves
    // of its view in memory the C backend must reload after every byte store
    // (§6.5 also makes growth during iteration legal), and only a grow, a
    // shrink, a whole-value write or a call that can reach the array changes
    // them -- exactly what the kill summary reports. Codegen reads the view of
    // each variable named here once, before the loop.
    void LoopViewRefs(Node *body, Node *cond, vector<VarDef *> &out) {
        out.clear();
        if (mode != M_JUDGE) return;
        vector<VarDef *> vars;
        RefIndexed(body, vars);
        RefIndexed(cond, vars);
        // The summary only reports kills for places that already exist, so
        // name them all before walking.
        vector<pair<VarDef *, int>> named;
        for (auto v : vars) {
            auto pid = PlaceOfVar(v);
            if (pid >= 0) named.push_back({ v, pid });
        }
        if (named.empty()) return;
        set<int> kills;
        SummarizeInto(body, kills);
        SummarizeInto(cond, kills);
        for (auto &[v, pid] : named) if (!kills.count(pid)) out.push_back(v);
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
        Walk(n);
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
        Walk(n);
        mode = saved;
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
        Mark(sp->body);
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
            Walk(sp->body);
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
            Walk(sp->body);
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
        Walk(sp->body);
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
            Mark(sp->body);
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
            Mark(sp->body);
            for (auto &[v, ok] : gge0)
                if (ok) {
                    cands[v].declseen = true;
                    ge0.insert(v);   // The inductive hypothesis.
                }
            mode = M_RECORD;
            flow = Flow {};
            Walk(sp->body);
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
                Walk(i);
            }
        for (auto si : ast.structinsts)
            for (auto d : si->defaults)
                if (d) {
                    flow = Flow {};
                    Walk(d);
                }
        for (auto ei : ast.enuminsts)
            for (auto &vd : ei->vdefaults)
                for (auto d : vd)
                    if (d) {
                        flow = Flow {};
                        Walk(d);
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

// ---------------------------------------------------------------------------
// BceMark: the prescan, one override per node kind that contributes to it.
// The default recurses over Children, which is all a node whose only role is
// to hold subexpressions needs.

inline void Node::BceMark(BCE &b) { Children([&](Node *ch) { b.Mark(ch); }); }

inline void Ident::BceMark(BCE &b) { b.NoteVar(vdef); }

inline void Unary::BceMark(BCE &b) {
    if (op == T_BITAND) b.MarkAddr(child);
    Node::BceMark(b);
}

inline void Dot::BceMark(BCE &b) {
    if (member == B_LEN) b.NotePlace(obj);
    Node::BceMark(b);
}

inline void Call::BceMark(BCE &b) {
    // `trailing` is an unchecked template; the instance that runs is fvbody.
    b.Mark(callee);
    for (auto a : args) b.Mark(a);
    b.Mark(fvbody);
}

inline void Index::BceMark(BCE &b) {
    b.NotePlace(obj);
    b.NoteRel(obj, idx);
    Node::BceMark(b);
}

inline void SliceExpr::BceMark(BCE &b) {
    b.NotePlace(obj);
    b.NoteRel(obj, lo);
    b.NoteRel(obj, hi);
    Node::BceMark(b);
}

inline void MatchExpr::BceMark(BCE &b) {
    for (auto &arm : arms) {
        if (arm.pat.byref) b.MarkAddr(scrutinee);
        b.NoteVar(arm.binder);
    }
    Node::BceMark(b);
}

inline void ForLoop::BceMark(BCE &b) {
    if (byref) b.MarkAddr(iter);
    b.NoteVar(vdef);
    b.NoteVar(idxdef);
    Node::BceMark(b);
}

inline void VarDecl::BceMark(BCE &b) {
    for (auto d : defs) b.NoteVar(d);
    if (defs.size() == 1 && inits.size() == 1 && defs[0]) b.wdecl.insert(defs[0]);
    else for (auto d : defs) if (d) b.wbad.insert(d);
    Node::BceMark(b);
}

inline void Assign::BceMark(BCE &b) {
    if (auto id = Is<Ident>(lval); id && id->vdef && !pointee) {
        auto v = id->vdef;
        auto lit = Is<IntLit>(rhs);
        if (op == T_ASSIGN) {
            b.wkinds[v].setw = true;
        } else if ((op == T_PLUSEQ || op == T_MINUSEQ) && lit && !lit->uns) {
            auto c = op == T_PLUSEQ ? lit->val : -lit->val;
            (c >= 0 ? b.wkinds[v].inc : b.wkinds[v].dec) = true;
        } else {
            b.wbad.insert(v);
        }
    }
    Node::BceMark(b);
}

inline void IncDec::BceMark(BCE &b) {
    if (auto id = Is<Ident>(lval); id && id->vdef &&
        (!id->vdef->type || id->vdef->type->kind != TY_REF))
        (op == T_INC ? b.wkinds[id->vdef].inc : b.wkinds[id->vdef].dec) = true;
    Node::BceMark(b);
}

// ---------------------------------------------------------------------------
// BceWalk: the analysis proper, one override per node kind that carries facts,
// kills, or a bounds check. The default evaluates the children left to right
// and falls through, which is exactly right for every operand-holding node.

inline bool Node::BceWalk(BCE &b) {
    Children([&](Node *ch) { b.Walk(ch); });
    return true;
}

// A function-value template is never analyzed: the checked instance reached
// through its call site is (Call::BceWalk).
inline bool FunVal::BceWalk(BCE &) { return true; }

inline bool Binary::BceWalk(BCE &b) {
    if ((op == T_ANDAND || op == T_OROR) && b.mode != BCE::M_KILLS) {
        // The right side runs only when the left settled it one way, so its
        // facts and effects merge against the short-circuit path.
        b.Walk(left);
        auto after = b.flow;
        b.CondFacts(left, op == T_ANDAND);
        b.Walk(right);
        b.flow = b.Meet(after, b.flow);
        return true;
    }
    b.Walk(left);
    b.Walk(right);
    return true;
}

inline bool Index::BceWalk(BCE &b) {
    b.Walk(obj);
    b.Walk(idx);
    b.JudgeIndex(this);
    return true;
}

inline bool SliceExpr::BceWalk(BCE &b) {
    auto kills = b.mode == BCE::M_KILLS;
    b.Walk(obj);
    // The runtime check compares against a length snapshot taken before the
    // bound expressions run, and each bound against the state it is evaluated
    // in; capture all three at those moments.
    auto lent = kills ? BCE::Term {} : b.LenTermOf(obj);
    b.slicelen = BCE::Term {};
    b.Walk(lo);
    auto lot = kills ? BCE::Term {}
                     : b.SliceBound(lo, lo_from_end, lent,
                                    BCE::Term { true, BCE::Zero(), 0 });
    auto gen = b.nextgen;
    b.Walk(hi);
    auto hit = kills ? BCE::Term {} : b.SliceBound(hi, hi_from_end, lent, lent);
    // Evaluating the upper bound may have re-pinned what the lower one names,
    // in which case the captured term no longer denotes the value compared.
    if (b.nextgen != gen) lot = BCE::Term {};
    if (!kills) b.slicelen = BCE::SliceLenTerm(lot, hit);
    if (b.mode == BCE::M_JUDGE) b.JudgeSlice(this, lent, lot, hit);
    return true;
}

inline bool Call::BceWalk(BCE &b) {
    Node *recv = nullptr;
    if (auto d = Is<Dot>(callee)) {
        recv = d->obj;
        b.Walk(recv);
    }
    for (auto a : args) b.Walk(a);
    if (builtin >= 0) {
        auto rn = recv ? recv : (args.empty() ? nullptr : args[0]);
        // The first non-receiver argument, in either call spelling.
        auto arg0 = recv ? (args.empty() ? nullptr : args[0])
                         : (args.size() > 1 ? args[1] : nullptr);
        switch (builtin) {
            case B_PUSH:
                b.GrowShrinkKill(rn, 1, 1);
                break;
            case B_APPEND: {
                auto st = arg0 ? b.LenTermOf(arg0) : BCE::Term {};
                b.GrowShrinkKill(rn, 1, st.ok && st.b.kind == BCE::BK_ZERO ? st.off
                                                                          : INT64_MIN);
                break;
            }
            case B_ALLOC_INDEX: case B_ALLOC_REF:
                b.GrowShrinkKill(rn, 1);
                break;
            case B_POP: {
                // A pop that returns proves its own precondition: popping an
                // empty array aborts (§9.3), so the length was at least one
                // and the new one is exactly one less.
                auto lt = b.mode == BCE::M_KILLS || !rn ? BCE::Term {} : b.LenTermOf(rn);
                b.GrowShrinkKill(rn, -1, -1);
                if (lt.ok && lt.b.kind != BCE::BK_ZERO)
                    b.AddFactB(BCE::Zero(), lt.b, lt.off - 1);
                break;
            }
            case B_CLEAR: {
                auto pid = b.GrowShrinkKill(rn, -1);
                if (pid >= 0) b.ExactLenIs(pid, BCE::Term { true, BCE::Zero(), 0 });
                break;
            }
            case B_RESIZE: {
                auto nt = b.mode == BCE::M_KILLS ? BCE::Term {} : b.TermOf(arg0);
                auto pid = b.GrowShrinkKill(rn, 0);
                if (pid >= 0) b.ExactLenIs(pid, nt);
                break;
            }
            case B_ASSERT:
                if (b.mode != BCE::M_KILLS && !args.empty() && !b.HasKillEffects(args[0]))
                    b.CondFacts(args[0], true);
                // A statically false assertion ends the path.
                if (!args.empty())
                    if (auto bl = Is<BoolLit>(args[0]); bl && !bl->val) return false;
                break;
            default:
                break;   // len/cap/print/free/queues/threads: no tracked effect.
        }
        return true;
    }
    if (fvbody) {
        // The function value runs inside the callee, possibly repeatedly and
        // after arbitrary callee effects: analyze it from scratch.
        auto saved = std::move(b.flow);
        b.flow = BCE::Flow {};
        b.loopdepth++;
        for (auto p : fvparams) if (p) b.BumpVar(p, false);   // Re-bound per call.
        b.Walk(fvbody);
        b.loopdepth--;
        b.flow = std::move(saved);
    }
    auto anyref = false;
    auto reft = [](Node *a) { return a->exprtype && a->exprtype->kind == TY_REF; };
    if (recv && reft(recv)) anyref = true;
    for (auto a : args) anyref = anyref || reft(a);
    b.KillByCall(anyref);
    return true;
}

inline bool Block::BceWalk(BCE &b) {
    auto fell = true;
    for (auto st : stmts) {
        fell = b.Walk(st);
        if (!fell) break;
    }
    if (fell) b.Walk(tail);
    return fell;
}

inline bool IfExpr::BceWalk(BCE &b) {
    b.Walk(cond);
    if (b.mode == BCE::M_KILLS) {
        b.Walk(thenb);
        b.Walk(elseb);
        return true;
    }
    auto killfree = !b.HasKillEffects(cond);
    auto base = b.flow;
    if (killfree) b.CondFacts(cond, true);
    auto fellthen = b.Walk(thenb);
    auto thenf = std::move(b.flow);
    b.flow = std::move(base);
    if (killfree) b.CondFacts(cond, false);
    auto fellelse = b.Walk(elseb);
    if (fellthen && !fellelse) { b.flow = std::move(thenf); return true; }
    if (!fellthen && fellelse) return true;   // b.flow is already the else path.
    if (!fellthen && !fellelse) return false;
    b.flow = b.Meet(thenf, b.flow);
    return true;
}

inline bool MatchExpr::BceWalk(BCE &b) {
    b.Walk(scrutinee);
    if (b.mode == BCE::M_KILLS) {
        for (auto &arm : arms) {
            if (arm.binder) b.BumpVar(arm.binder, false);   // Re-bound per execution.
            b.Walk(arm.body);
        }
        return true;
    }
    auto st = b.TermOf(scrutinee);
    auto stt = scrutinee->exprtype;
    auto admissible = st.ok && BCE::CmpAdmissible(st) && stt && stt->kind == TY_INT &&
                      stt->intstorage != IS_U64;
    auto base = b.flow;
    BCE::Flow acc;
    auto anyfell = false;
    for (auto &arm : arms) {
        b.flow = base;
        if (admissible && (arm.pat.kind == P_INT || arm.pat.kind == P_RANGE)) {
            b.AddFactB(BCE::Zero(), st.b, BCE::SatSub(st.off, arm.lo));
            b.AddFactB(st.b, BCE::Zero(), BCE::SatSub(BCE::SatSub(arm.hi, 1), st.off));
        }
        if (b.Walk(arm.body)) {
            if (!anyfell) acc = std::move(b.flow);
            else acc = b.Meet(acc, b.flow);
            anyfell = true;
        }
    }
    if (!anyfell) { b.flow = std::move(base); return false; }
    b.flow = std::move(acc);
    return true;
}

inline bool EarlyBlock::BceWalk(BCE &b) {
    if (b.mode == BCE::M_KILLS) {
        b.Walk(body);
        return true;
    }
    if (!b.HasBreaks(body)) return b.Walk(body);
    auto entry = b.flow;
    b.Walk(body);
    b.flow = std::move(entry);
    b.StripKills(body);
    return true;
}

inline bool While::BceWalk(BCE &b) {
    b.loopdepth++;
    if (b.mode == BCE::M_KILLS) {
        b.Walk(cond);
        b.Walk(body);
        b.loopdepth--;
        return true;
    }
    b.LoopViewRefs(body, cond, hoistrefs);
    b.StripKills(cond);
    b.StripKills(body);
    b.Walk(cond);
    auto exitf = b.flow;
    auto killfree = !b.HasKillEffects(cond);
    if (killfree) b.CondFacts(cond, true);
    b.Walk(body);
    b.flow = std::move(exitf);
    b.loopdepth--;
    if (killfree && !b.HasBreaks(body)) b.CondFacts(cond, false);
    return true;
}

inline bool LoopExpr::BceWalk(BCE &b) {
    b.loopdepth++;
    if (b.mode == BCE::M_KILLS) {
        b.Walk(body);
        b.loopdepth--;
        return true;
    }
    b.LoopViewRefs(body, nullptr, hoistrefs);
    b.StripKills(body);
    auto exitf = b.flow;
    b.Walk(body);
    b.flow = std::move(exitf);
    b.loopdepth--;
    return true;
}

inline bool ForLoop::BceWalk(BCE &b) {
    if (b.mode == BCE::M_KILLS) {
        b.Walk(iter);
        b.loopdepth++;
        if (vdef) b.BumpVar(vdef, false);
        if (idxdef) b.BumpVar(idxdef, false);
        b.Walk(body);
        b.loopdepth--;
        return true;
    }
    b.Walk(iter);
    // Range and count loops compare the variable against loop-entry snapshots,
    // so capture those terms before the body havoc; array and slice loops
    // re-read the length every iteration, so capture after.
    BCE::Term lot, hit;
    if (iterkind == IK_RANGE) {
        if (auto r = Is<RangeExpr>(iter)) {
            lot = b.TermOf(r->lo);
            hit = b.TermOf(r->hi);
        }
    } else if (iterkind == IK_COUNT) {
        lot = BCE::Term { true, BCE::Zero(), 0 };
        hit = b.TermOf(iter);
    }
    // Counted push loops `for _ in n { ...; a.push(x); ...; }`: when no other
    // statement can touch a's length and no break or continue skips an
    // iteration, the loop grows a by its push count per iteration, so
    // afterwards len(a) == len-before + k*n.
    vector<pair<int, int64_t>> pushpids;
    vector<BCE::Term> prelens;
    if ((iterkind == IK_COUNT || iterkind == IK_RANGE) && !body->tail &&
        !b.HasIterationJumps(body)) {
        map<int, int64_t> counts;
        set<int> otherbumps;
        for (auto st : body->stmts) {
            if (auto pc = Is<Call>(st); pc && pc->builtin == B_PUSH) {
                Node *precv = nullptr, *parg = nullptr;
                if (auto pd = Is<Dot>(pc->callee)) {
                    precv = pd->obj;
                    parg = pc->args.empty() ? nullptr : pc->args[0];
                } else if (pc->args.size() == 2) {
                    precv = pc->args[0];
                    parg = pc->args[1];
                }
                auto pid = precv ? b.PlaceOf(precv) : -1;
                if (pid >= 0 && (!parg || !b.HasKillEffects(parg))) {
                    counts[pid]++;
                    continue;
                }
            }
            b.SummarizeInto(st, otherbumps);
        }
        for (auto &[pid, k] : counts)
            if (!otherbumps.count(pid)) {
                pushpids.push_back({ pid, k });
                prelens.push_back(BCE::Term { true, b.LenBase(pid), 0 });
            }
    }
    // Growth during iteration is legal (§6.5), so the length is re-read every
    // iteration unless nothing in the body can change it -- which is what the
    // kill summary answers, calls included.
    if (b.mode == BCE::M_JUDGE && (iterkind == IK_ARRAY || iterkind == IK_SLICE)) {
        auto pid = b.PlaceOf(iter);
        if (pid >= 0) {
            set<int> kills;
            b.SummarizeInto(body, kills);
            fixedlen = !kills.count(pid);
        }
    }
    b.loopdepth++;
    b.LoopViewRefs(body, nullptr, hoistrefs);
    b.StripKills(body);
    if (iterkind == IK_ARRAY || iterkind == IK_SLICE) {
        lot = BCE::Term { true, BCE::Zero(), 0 };
        hit = b.LenTermOf(iter);
    }
    auto exitf = b.flow;
    auto iv = iterkind == IK_ARRAY || iterkind == IK_SLICE ? idxdef : vdef;
    if (iv) {
        auto vb = b.VarBase(iv);
        if (lot.ok && BCE::CmpAdmissible(lot))
            b.AddFactB(lot.b, vb, BCE::SatSub(0, lot.off));
        if (hit.ok && BCE::CmpAdmissible(hit))
            b.AddFactB(vb, hit.b, BCE::SatSub(hit.off, 1));
    }
    if ((iterkind == IK_RANGE || iterkind == IK_COUNT) && idxdef)
        b.AddFactB(BCE::Zero(), b.VarBase(idxdef), 0);
    b.Walk(body);
    b.flow = std::move(exitf);
    b.loopdepth--;
    if (!pushpids.empty()) {
        BCE::Term iters;
        if (iterkind == IK_COUNT) {
            iters = hit;
        } else if (lot.ok && hit.ok) {
            if (lot.b == hit.b)
                iters = BCE::Term { true, BCE::Zero(), BCE::SatSub(hit.off, lot.off) };
            else if (lot.b.kind == BCE::BK_ZERO)
                iters = BCE::Term { true, hit.b, BCE::SatSub(hit.off, lot.off) };
        }
        // A negative count runs zero iterations; facts only when it is >= 0.
        if (iters.ok && BCE::CmpAdmissible(iters) &&
            b.Query(BCE::Zero(), iters.b, iters.off)) {
            for (size_t i = 0; i < pushpids.size(); i++) {
                auto [pid, k] = pushpids[i];
                auto &pre = prelens[i];
                auto post = b.LenBase(pid);
                if (iters.b.kind == BCE::BK_ZERO) {
                    auto total = iters.off;
                    for (auto m = int64_t(1); m < k && total != BCE::INF; m++)
                        total = BCE::SatAdd(total, iters.off);
                    if (total == BCE::INF) continue;
                    b.AddFactB(post, pre.b, total);              // post == pre + k*n.
                    b.AddFactB(pre.b, post, BCE::SatSub(0, total));
                } else if (k == 1) {
                    b.AddFactB(iters.b, post, BCE::SatSub(0, iters.off));   // n <= post.
                    if (b.Query(pre.b, BCE::Zero(), 0))                     // pre == 0:
                        b.AddFactB(post, iters.b, iters.off);               // post == n.
                }
            }
        }
    }
    return true;
}

inline bool Guard::BceWalk(BCE &b) {
    b.Walk(cond);
    if (b.mode == BCE::M_KILLS) {
        b.Walk(elseb);
        return true;
    }
    auto killfree = !b.HasKillEffects(cond);
    if (elseb) {
        auto save = b.flow;
        if (killfree) b.CondFacts(cond, false);
        b.Walk(elseb);   // Must diverge (TC).
        b.flow = std::move(save);
    }
    if (killfree) b.CondFacts(cond, true);
    return true;
}

inline bool InlineBlock::BceWalk(BCE &b) {
    if (b.mode == BCE::M_KILLS) {
        b.Walk(body);
        return true;
    }
    // A trailing return to this block is its one normal exit and needs no
    // havoc; earlier ones join the end from other states.
    auto &stmts = body->stmts;
    Node *last = stmts.empty() ? nullptr : stmts.back();
    auto lastret = Is<Return>(last) && ((Return *)last)->target == sf;
    auto early = false;
    for (size_t i = 0; i + 1 < stmts.size(); i++)
        early = early || b.ReturnsForTarget(stmts[i], sf);
    if (last) {
        if (lastret) {
            for (auto v : ((Return *)last)->vals)
                early = early || b.ReturnsForTarget(v, sf);
        } else {
            early = early || b.ReturnsForTarget(last, sf);
        }
    }
    if (body->tail) early = early || b.ReturnsForTarget(body->tail, sf);
    if (!early) {
        b.Walk(body);
        return true;
    }
    auto entry = b.flow;
    b.Walk(body);
    b.flow = std::move(entry);
    b.StripKills(body);
    return true;
}

inline bool Return::BceWalk(BCE &b) {
    for (auto v : vals) b.Walk(v);
    return false;
}

inline bool Break::BceWalk(BCE &b) {
    b.Walk(val);
    return false;
}

inline bool Continue::BceWalk(BCE &) { return false; }

inline bool VarDecl::BceWalk(BCE &b) {
    for (auto i : inits) b.Walk(i);
    if (defs.size() == 1 && inits.size() == 1 && defs[0]) {
        auto v = defs[0];
        auto lt = b.mode != BCE::M_KILLS && v->type && v->type->kind == TY_ARRAY
                      ? b.FreshLenOf(inits[0]) : BCE::Term {};
        // A slice binding takes the length its bounds just stated.
        if (b.mode != BCE::M_KILLS && v->type && v->type->kind == TY_SLICE &&
            Is<SliceExpr>(inits[0]))
            lt = b.slicelen;
        if (BCE::ScalarIntVar(v)) {
            b.SetWrite(v, inits[0], true);
        } else if (b.loopdepth > 0 && v->type &&
                   (v->type->kind == TY_SLICE || v->type->kind == TY_REF)) {
            b.RebindKill(v);   // Loop-repeated redeclaration rebinds.
        } else if (b.loopdepth > 0 && v->type && v->type->kind != TY_FLT &&
                   v->type->kind != TY_BOOL && v->type->kind != TY_INT) {
            b.StorageWriteKill(BCE::UK_OWNED, v, v);   // Loop-repeated reconstruction.
        }
        if (lt.ok) {
            auto pid = b.PlaceOfVar(v);
            if (pid >= 0) b.ExactLenIs(pid, lt);
        }
    } else {
        for (auto d : defs) if (d) b.VarKillWrite(d);
    }
    return true;
}

inline bool Assign::BceWalk(BCE &b) {
    b.WalkLvalParts(lval);
    b.Walk(rhs);
    if (op == T_DOTASSIGN) {
        auto ch = b.ChainOf(lval);
        if (ch.kind == BCE::CH_INDEX) return true;   // Element ref slots: no tracked places.
        if (ch.kind == BCE::CH_OK) b.RebindKill(ch.root);
        else b.StorageWriteKill(BCE::UK_OPAQUE, nullptr, nullptr);
        return true;
    }
    if (pointee) {
        auto pt = lval->exprtype;
        b.PointeeWriteKill(lval, pt && pt->kind == TY_REF ? pt->ref->sub : nullptr);
        return true;
    }
    if (auto id = Is<Ident>(lval)) {
        auto v = id->vdef;
        if (!v) return true;
        if (BCE::ScalarIntVar(v)) {
            if (op == T_ASSIGN) {
                b.SetWrite(v, rhs, false);
            } else if (op == T_PLUSEQ || op == T_MINUSEQ) {
                auto lit = Is<IntLit>(rhs);
                if (lit && !lit->uns && BCE::SmallOff(lit->val))
                    b.ShiftWrite(v, op == T_PLUSEQ ? lit->val : -lit->val);
                else b.VarKillWrite(v);
            } else {
                b.VarKillWrite(v);
            }
            return true;
        }
        auto t = v->type;
        auto fresh = b.mode != BCE::M_KILLS && t && t->kind == TY_ARRAY && op == T_ASSIGN
                         ? b.FreshLenOf(rhs) : BCE::Term {};
        if (b.mode != BCE::M_KILLS && t && t->kind == TY_SLICE && op == T_ASSIGN &&
            Is<SliceExpr>(rhs))
            fresh = b.slicelen;
        if (t && t->kind == TY_SLICE) b.RebindKill(v);
        else if (t && t->kind != TY_FLT && t->kind != TY_BOOL && t->kind != TY_INT)
            b.StorageWriteKill(BCE::UK_OWNED, v, v);
        if (fresh.ok) {
            auto pid = b.PlaceOfVar(v);
            if (pid >= 0) b.ExactLenIs(pid, fresh);
        }
        return true;
    }
    if (Is<Index>(lval)) return true;   // Element writes cannot change any tracked length.
    auto lt = lval->exprtype;
    if (lt && (lt->kind == TY_INT || lt->kind == TY_FLT || lt->kind == TY_BOOL))
        return true;                    // A scalar field write cannot change a length.
    auto ch = b.ChainOf(lval);
    if (ch.kind == BCE::CH_INDEX) return true;
    if (ch.kind == BCE::CH_FAIL || !ch.root) {
        b.StorageWriteKill(BCE::UK_OPAQUE, nullptr, nullptr);
        return true;
    }
    if (!ch.anycross) {
        b.StorageWriteKill(BCE::UK_OWNED, ch.root, ch.root);
    } else if (ch.rootonlycross) {
        auto [k, u] = b.UltOf(ch.root);
        if (k != BCE::UK_STATIC) b.StorageWriteKill(k, u, ch.root);
        else b.RebindKill(ch.root);
    } else {
        b.StorageWriteKill(BCE::UK_OPAQUE, nullptr, ch.root);
    }
    return true;
}

inline bool IncDec::BceWalk(BCE &b) {
    b.WalkLvalParts(lval);
    auto lt = lval->exprtype;
    if (lt && lt->kind == TY_REF) {
        b.PointeeWriteKill(lval, lt->ref->sub);
        return true;
    }
    if (auto id = Is<Ident>(lval); id && id->vdef) {
        if (BCE::ScalarIntVar(id->vdef)) b.ShiftWrite(id->vdef, op == T_INC ? 1 : -1);
        else b.VarKillWrite(id->vdef);
    }
    // A field or element integer is not a tracked base.
    return true;
}

}  // namespace goose
