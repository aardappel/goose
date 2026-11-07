// Goose compiler — the typechecker's `par for` (definitions of TypeCheck
// members, typecheck.h): what the body of a loop whose iterations may run on
// several threads at once may touch (docs/design/child_threads.md §4.3-§4.6).
//
// The body is a block of the enclosing function, checked as a `for`'s is,
// with every variable outside it -- the enclosing function's, those of the
// functions it is nested in, and the globals -- viewed read-only (R1): their
// values, and every reference or slice read out of them, carry no writable
// provenance (ParView), so the writability rule of §9.5 rejects a write
// through them wherever it is made, in a callee too, whose specialization
// takes its arguments' writability. What one iteration owns is writable
// (R2): the element `par for &x` binds, a[k] for the iteration's index k,
// and a.chunk(k, n) or a.chunk_at(bounds, k) of the loop's count or bounds,
// each of an array of flat elements in one known storage (ParPart), which
// the rest of the body may then not reach otherwise (FinishParFor). An
// integer of the enclosing function accumulated by one operation and not
// otherwise used is a reduction, kept per chunk and combined in chunk order
// at the join (§4.6). Neither the body nor anything it calls may print,
// use a queue, a thread or a pool, or exit (R3), nor leave the body but by
// ending an iteration (R4). A function the body calls is specialized for it
// (FnSpec::inpar), and so held to these rules, globals read-only included.
#pragma once

namespace goose {

// Whether the body of the innermost par for being checked sees vd from
// outside: a global, or a variable declared before the loop, of the function
// the loop is in or of one that function is nested in. Every variable the
// body declares, and every one a function checked for it declares, is deeper
// than the loop's own scope (VarDef::depth counts the scopes on the
// compile-time call path).
inline bool TypeCheck::ParCaptured(const VarDef *vd) {
    return InPar() && vd && (vd->isglobal || vd->depth <= Par().depth);
}

// Whether vd is a binder that tells the iterations of a par for whose body is
// being checked apart (ParCtx::ordinal, ordinal2, shifted): a[k] and
// a.chunk(k, n) are the iteration's own by it (§4.3 R2), so nothing in the
// body, or a function it calls, may write it, through a reference either.
inline bool TypeCheck::ParBinder(const VarDef *vd) {
    for (auto i = (int)parctx.size() - 1; vd && i >= 0 && parctx[i]; i--) {
        auto &c = *parctx[i];
        if (vd == c.ordinal || vd == c.ordinal2 || vd == c.shifted) return true;
    }
    return false;
}

// The par fors whose bodies are being checked that see vd from outside,
// innermost first, up to a body checked for none (a null entry). A par for
// in another's body sees what the outer one sees from outside, and more.
template<typename F> void TypeCheck::EachParSeeing(const VarDef *vd, F f) {
    for (auto i = (int)parctx.size() - 1; i >= 0 && parctx[i]; i--)
        if (vd && (vd->isglobal || vd->depth <= parctx[i]->depth)) f(*parctx[i]);
}

// Whether a par for around the innermost one sees vd from outside: no
// iteration of the inner one owns a part of it, since the outer one's
// iterations all run the inner loop.
inline bool TypeCheck::ParCapturedAround(const VarDef *vd) {
    auto n = 0;
    EachParSeeing(vd, [&](ParCtx &) { n++; });
    return n > 1;
}

// The frame whose specialization has x as a parameter's class, or a
// component of one (ClassParam), innermost first; -1 where none has.
inline int TypeCheck::ParClassFrame(VarDef *x) {
    for (auto i = (int)frames.size() - 1; i >= 0; i--) {
        auto sp = frames[i].spec;
        if (!sp || frames[i].isfunval) continue;
        auto view = false;
        if (ClassParam(sp, x, view) >= 0) return i;
    }
    return -1;
}

// Whether a reference or slice with alternative a, in a par for's body or a
// function checked for it, may lead to storage the body sees read-only
// (§4.3 R1), and so carries no writable provenance, however writable the
// slot it was read out of: a variable from outside the body or a global
// (ParCaptured); anything outside its scope, for a bound, which outside
// static data's includes every global; a class of the loop's function or of
// one around it, which stands for its callers' storage; and a class of a
// function checked for the body, unless every call reaching it passed
// storage of the body's own (VarDef::parlocal), of which a component of the
// class is part. Static data is read-only by its type.
inline bool TypeCheck::ParReadOnlyAlt(const RootAlt &a) {
    if (!InPar()) return false;
    auto x = a.root;
    if (!x) return !a.exact;
    if (IsClassRoot(x)) {
        if (ParClassFrame(x) <= Par().frame) return true;
        auto base = x;
        while (base->componentowner) base = base->componentowner;
        return !base->parlocal;
    }
    return !a.exact || ParCaptured(x);
}

// owner.field, d, of a root struct (§3.9) in a par for's body, lv the path
// to the field: where the identifier names the owner and the path reaches
// one component of it alone, that is all the name reaches there, which
// FinishParFor judges in the owner's place -- a use of the owner reaches
// every component.
inline void TypeCheck::NoteParComponent(Dot *d, const LVal &lv) {
    auto id = Is<Ident>(d->obj);
    if (!id || !id->vdef || !lv.Exact()) return;
    auto c = lv.Root();
    // But a component holding a reference leads where that points, which
    // the owner's contents say (ParRootsOf of the owner), not the component.
    if (c && c->componentowner == id->vdef && c->type && !IsRefOrSlice(c->type) &&
        !HoldsPlainRef(c->type))
        parcomponents[id] = c;
}

// Whether everything a value with roots r leads to is storage of the par
// for's body, each place named exactly, through the references held there
// too, which a callee handed it may read out and follow (RootArg::parlocal).
// A class's own summary says what leads on from it.
inline bool TypeCheck::ParLocalReach(const Roots &r) {
    if (!InPar()) return false;
    Roots all = r;
    for (size_t i = 0; i < all.alts.size(); i++) {
        auto a = all.alts[i];
        if (ParReadOnlyAlt(a)) return false;
        auto x = a.root;
        if (!x || IsClassRoot(x) || !x->type) continue;
        if (IsRefOrSlice(x->type)) all.Add(x->ref.AsRoots());
        else if (HoldsPlainRef(x->type)) all.Add(x->contents);
    }
    return true;
}

// A name in the body (or in a function checked for it) that reads or writes
// vd, a variable from outside it: FinishParFor judges each, for every par
// for whose body the name is in that sees vd from outside.
inline void TypeCheck::NoteParUse(Node *at, VarDef *vd) {
    auto seen = false;
    EachParSeeing(vd, [&](ParCtx &) { seen = true; });
    if (!seen) return;
    ParNote n;
    n.kind = PN_USE;
    n.at = at;
    n.var = vd;
    ParApply(n);
}

// What a par for's body does that its loop judges (ParNote), to the par
// fors on the stack, and recorded with every specialization being checked
// for them, whose reuse does it again (ParReplay).
inline void TypeCheck::ParApply(const ParNote &n) {
    switch (n.kind) {
        case PN_USE:
            EachParSeeing(n.var, [&](ParCtx &ctx) { ctx.uses.push_back({ n.at, n.var }); });
            break;
        case PN_REDUCE:
            ParReduce(n.at, n.var, n.op, n.name);
            break;
        case PN_PART:
            if (InPar() && Par().loop == n.loop) Par().parts.push_back(n.part);
            break;
        case PN_PARTUSE:
            if (InPar() && Par().loop == n.loop) Par().partuses.insert(n.at);
            break;
        case PN_LENGTH:
            EachParSeeing(n.var, [&](ParCtx &ctx) { ctx.partuses.insert(n.at); });
            break;
    }
    ParRecord(n);
}

// Every specialization being checked above the outermost par for on the
// stack, and checked for it, records n: a call that later reuses it in a
// par for's body, whose loop no check of it saw, replays n there. But not
// where n's variable is the specialization's own, declared in its frame or
// one above (VarDef::depth past the frame's scopebase): no loop of a caller
// sees that from outside, whatever its depth now -- a loop in the callee's
// own body, which saw it, is checked again with the body.
inline void TypeCheck::ParRecord(const ParNote &n) {
    auto base = -1;
    for (auto i = (int)parctx.size() - 1; i >= 0 && parctx[i]; i--) base = parctx[i]->frame;
    if (base < 0) return;
    for (auto i = (int)frames.size() - 1; i > base; i--) {
        auto &f = frames[i];
        if (f.isfunval || !f.spec || !f.spec->inpar) continue;
        if (n.var && !n.var->isglobal && n.var->depth > f.scopebase) continue;
        auto &notes = parnotes[f.spec];
        auto known = false;
        for (auto &m : notes)
            known = known ||
                    (m.kind == n.kind && m.at == n.at && m.var == n.var && m.loop == n.loop);
        if (!known) notes.push_back(n);
    }
}

// A call in a par for's body reusing spec, which a check made in another
// loop's body or before: what that check found the body and its callees do
// is done again here, so this loop judges it too. A specialization in
// progress records what it does as its check goes on.
inline void TypeCheck::ParReplay(FnSpec *spec) {
    if (spec->inprogress) return;
    auto it = parnotes.find(spec);
    if (it == parnotes.end()) return;
    auto notes = it->second;   // Replaying records into the checks in progress.
    for (auto &n : notes) ParApply(n);
}

// An update of vd, a reduction by `cls` (T_PLUSEQ for + and -, T_OREQ,
// T_ANDEQ or T_XOREQ) of every par for around it that sees it from outside:
// the iterations of an outer one each run an inner one, whose chunks add
// into the outer chunk's own total. One operation per reduction (§4.6).
inline void TypeCheck::ParReduce(Node *at, VarDef *vd, TType cls, Node *name) {
    auto opname = [](TType k) {
        return k == T_PLUSEQ ? "+= and -=" : k == T_OREQ ? "|=" : k == T_ANDEQ ? "&=" : "^=";
    };
    EachParSeeing(vd, [&](ParCtx &ctx) {
        ctx.redtargets.insert(name);
        for (auto &[v, o, first] : ctx.reductions) {
            if (v != vd) continue;
            if (o != cls)
                Error(at, cat(vd->name, " is a reduction of this par for by ", opname(o),
                              " (at ", Where(first->line), "), and cannot be updated by ",
                              opname(cls), " too: a reduction has one operation "
                              "(docs/design/child_threads.md §4.6)"));
            return;
        }
        ctx.reductions.push_back({ vd, cls, at });
    });
}

// `a.len` or `a.cap` of the builtin property, at `at`, the name of a: the
// length, which no iteration of a par for can change, read beside the parts
// of a the iterations write (FinishParFor).
inline void TypeCheck::ParLengthUse(Node *at, VarDef *vd) {
    ParNote n;
    n.kind = PN_LENGTH;
    n.at = at;
    n.var = vd;
    ParApply(n);
}

// Whether x.len or x.cap, x the variable vd, reads a header no iteration of
// a par for can change: the variable's own -- a slice's, or an array's,
// which the body sees read-only -- or the static length of a fixed-size
// array a reference leads to. The header of any other array a reference
// leads to lies where the reference points, maybe in an element an
// iteration owns and may resize (a row's cells, say), so reading it is a
// use of the reference like any other.
inline bool TypeCheck::ParLengthExempt(VarDef *vd) {
    auto t = vd->type;
    if (!t) return false;
    if (t->kind != TY_REF) return true;
    auto p = t->ref->sub;
    return p && p->kind == TY_ARRAY && p->arr->akind == A_FIXED;
}

// A reference or slice of type t read out of storage in a par for's body,
// which may point at r: what it reads or writes there may be a part of
// another iteration, or a reduction (FinishParFor), for every par for whose
// body this is.
inline void TypeCheck::ParReadBack(const Roots &r, TypeExpr *t) {
    auto at = cur.nodepath.empty() ? nullptr : cur.nodepath.back().node;
    for (auto i = (int)parctx.size() - 1; i >= 0 && parctx[i]; i--) {
        auto &ctx = *parctx[i];
        ctx.readbacks.push_back({ r, t, at ? at : ctx.loop });
    }
}

// The loop's header, checked: what chunk(k, n) and chunk_at(bounds, k) of
// the iteration's index must take to be the iteration's own run. `par for k
// in n` and `par for k in 0..n` run k over the n runs of chunk(k, n), and
// `par for k in b.len - 1` over the runs between b's offsets.
inline void TypeCheck::StartParFor(ForLoop *x, ParCtx &ctx) {
    ctx.loop = x;
    if (auto r = Is<RangeExpr>(x->iter)) {
        if (auto lo = Is<IntLit>(r->lo); lo && !lo->uns && lo->val == 0) ctx.count = r->hi;
    } else if (x->iterkind == IK_COUNT) {
        ctx.count = x->iter;
    }
    if (auto b = Is<Binary>(ctx.count); b && b->op == T_MINUS) {
        auto one = Is<IntLit>(b->right);
        auto len = Is<Dot>(b->left);
        if (one && !one->uns && one->val == 1 && len && len->name == "len" &&
            len->member == B_LEN && Is<Ident>(len->obj))
            ctx.bounds = len->obj;
    }
}

// One pass over the body (CheckLoopPasses), in the loop's scope, its binders
// made: where the variables outside the body end, and the iteration's index.
inline void TypeCheck::ParPass(ForLoop *x, ParCtx &ctx) {
    ctx.depth = (int)scopes.size() - 1;
    ctx.scope = (int)scopes.size() - 1;
    ctx.frame = (int)frames.size() - 1;
    ctx.ordinal = ctx.ordinal2 = ctx.shifted = nullptr;
    if (x->iterkind == IK_RANGE || x->iterkind == IK_COUNT) {
        auto r = Is<RangeExpr>(x->iter);
        auto lo = r ? Is<IntLit>(r->lo) : nullptr;
        if (!r || (lo && !lo->uns && lo->val == 0)) ctx.ordinal = x->vdef;
        else ctx.shifted = x->vdef;
        ctx.ordinal2 = x->idxdef;
    } else {
        ctx.ordinal = x->idxdef;
    }
}

// Whether two expressions are the same partition's: the loop's count and a
// chunk's, or its bounds and a chunk_at's. `a` may be unchecked yet, so a
// name is looked up where it is.
inline bool TypeCheck::SameParExpr(Node *a, Node *b) {
    if (!a || !b) return false;
    if (auto x = Is<IntLit>(a)) {
        auto y = Is<IntLit>(b);
        return y && x->val == y->val && x->uns == y->uns;
    }
    if (auto x = Is<Ident>(a)) {
        auto y = Is<Ident>(b);
        if (!y || !x->expansionerror.empty() || !y->expansionerror.empty()) return false;
        auto xv = x->vdef ? x->vdef : LookupVar(x->name, x->ns);
        auto yv = y->vdef ? y->vdef : LookupVar(y->name, y->ns);
        return xv && xv == yv;
    }
    if (auto x = Is<Dot>(a)) {
        auto y = Is<Dot>(b);
        return y && x->name == y->name && SameParExpr(x->obj, y->obj);
    }
    if (auto x = Is<Binary>(a)) {
        auto y = Is<Binary>(b);
        return y && x->op == y->op && SameParExpr(x->left, y->left) &&
               SameParExpr(x->right, y->right);
    }
    return false;
}

// The pools the relative references a value of type t holds point into
// (§3.9), and those of what a reference or slice it holds leads to: a `T&<w
// in pool>` points into the global pool, wherever it is held. One measured
// from where it lies points within the storage that holds it.
inline void TypeCheck::ParRelPools(TypeExpr *t, vector<VarDef *> &out,
                                   vector<TypeExpr *> &seen) {
    if (!t) return;
    for (auto s : seen) if (s == t) return;
    seen.push_back(t);
    switch (t->kind) {
        case TY_REF:
            if (t->ref->lenstorage >= 0 && t->ref->pool &&
                find(out.begin(), out.end(), t->ref->pool) == out.end())
                out.push_back(t->ref->pool);
            ParRelPools(t->ref->sub, out, seen);
            return;
        case TY_SLICE: ParRelPools(t->sub, out, seen); return;
        case TY_ARRAY: ParRelPools(t->arr->sub, out, seen); return;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            EachField(t, [&](TypeExpr *ft) { ParRelPools(ft, out, seen); });
            return;
        default: return;
    }
}

// Whether a value of type t holds, or leads to, a reference into a pool.
inline bool TypeCheck::ParHoldsPoolRef(TypeExpr *t) {
    vector<VarDef *> pools;
    vector<TypeExpr *> seen;
    ParRelPools(t, pools, seen);
    return !pools.empty();
}

// Where vd's value may reach: its own storage and what the references it
// holds point at, or, for a reference or slice, where it points.
inline Roots TypeCheck::ParRootsOf(VarDef *vd) {
    Roots r;
    // Where a reference or slice variable points: its bindings, but for a
    // global `var` one, which any function may bind, checked after the body
    // or not -- a later call reusing the body's check must stand on all it
    // can be given (GlobalVarRead).
    auto pointsat = [&](VarDef *x) {
        return BoundAnywhere(x) ? GlobalVarRead(x).AsRoots() : x->ref.AsRoots();
    };
    if (vd->type && IsRefOrSlice(vd->type)) {
        r.Add(pointsat(vd));
    } else {
        r.Set(vd, true);
        if (vd->type && HoldsPlainRef(vd->type)) r.Add(vd->contents);
    }
    // The pools its relative references, and those of what it leads to,
    // point into, which a callee handed it may read through them (§3.9).
    auto addpools = [&](TypeExpr *t) {
        vector<VarDef *> pools;
        vector<TypeExpr *> seen;
        ParRelPools(t, pools, seen);
        for (auto p : pools) {
            RootAlt a;
            a.root = p;
            a.exact = true;
            r.Add(a);
        }
    };
    addpools(vd->type);
    // And where the references held in that storage lead, and so on: a
    // callee handed vd may read them out and follow them. A slot holding a
    // reference or slice -- a variable a reference to a slice names, say --
    // leads where that points; a holder, where what it holds does; and a
    // parameter's class, where the view of its caller's slot or holder does.
    for (size_t i = 0; i < r.alts.size(); i++) {
        auto x = r.alts[i].root;
        if (!x || x == vd) continue;
        if (x->type) addpools(x->type);
        if (x->type && IsRefOrSlice(x->type)) {
            r.Add(pointsat(x));
            continue;
        }
        if (!x->type || HoldsPlainRef(x->type)) r.Add(x->contents);
        if (!x->type && x->heldslice) r.Add(x->heldslice->ref.AsRoots());
        if (!x->type && x->heldholder) r.Add(x->heldholder->contents);
    }
    return r;
}

// A part an iteration owns (§4.4): of vd, lying in `root`, cut as `form`
// takes it with `key`, at `at`, where `name` names vd.
inline void TypeCheck::AddParPart(VarDef *vd, VarDef *root, ParForm form, Node *key, Node *at,
                                  Node *name, TypeExpr *elem, VarDef *by) {
    ParNote n;
    n.kind = PN_PART;
    n.part = { vd, root, form, key, at, by, elem };
    n.loop = Par().loop;
    n.at = at;
    n.var = vd;
    ParApply(n);
    if (!name) return;
    ParNote u;
    u.kind = PN_PARTUSE;
    u.at = name;
    u.var = vd;
    u.loop = Par().loop;
    ParApply(u);
}

// a[k], a a variable outside the body and k a binder of the loop that tells
// the iterations apart: the element the iteration owns (§4.3 R2), whose path
// is as writable as a is outside the body. `owned` is a, `by` the binder
// where it is not the iteration's ordinal (ParPart::by).
inline bool TypeCheck::ParIndexPart(Index *ix, VarDef *&owned, VarDef *&by) {
    if (!InPar()) return false;
    auto id = Is<Ident>(ix->obj);
    auto kid = Is<Ident>(ix->idx);
    if (!id || !kid || !id->expansionerror.empty() || !kid->expansionerror.empty()) return false;
    auto vd = LookupVar(id->name, id->ns);
    if (!vd || vd->isglobal || !ParCaptured(vd) || ParCapturedAround(vd)) return false;
    auto kd = LookupVar(kid->name, kid->ns);
    if (!kd || (!ParOrdinal(kd) && kd != Par().shifted)) return false;
    owned = vd;
    by = ParOrdinal(kd) ? nullptr : kd;
    return true;
}

// a.chunk(k, n) of the loop's count n, or a.chunk_at(bounds, k) of the
// loop's bounds, k the iteration's index and a a variable outside the body:
// the run the iteration owns (§4.4).
inline bool TypeCheck::ParChunkPart(Call *c, const BuiltinDef &d, vector<Node *> &args,
                                    VarDef *&owned) {
    if (!InPar() || args.size() != 3) return false;
    auto id = Is<Ident>(args[0]);
    if (!id || !id->expansionerror.empty()) return false;
    auto vd = LookupVar(id->name, id->ns);
    if (!vd || vd->isglobal || !ParCaptured(vd) || ParCapturedAround(vd)) return false;
    auto kid = Is<Ident>(d.kind == B_CHUNK ? args[1] : args[2]);
    if (!kid || !kid->expansionerror.empty() || !ParOrdinal(LookupVar(kid->name, kid->ns)))
        return false;
    auto key = d.kind == B_CHUNK ? args[2] : args[1];
    if (!SameParExpr(key, d.kind == B_CHUNK ? Par().count : Par().bounds)) return false;
    (void)c;
    owned = vd;
    return true;
}

// a.chunk_at(bounds, k) in a par for's body, bounds the loop's and k its
// index, checked: the fork checks the bounds whole before any iteration runs
// (§4.4), whichever runs the iterations take -- against a's length where a
// is a variable from outside the body, whether the iterations own its runs
// or only read them, and as non-decreasing from 0 where a is anything else.
inline void TypeCheck::ParChunkCheck(vector<Node *> &args) {
    if (!InPar() || args.size() != 3) return;
    auto kid = Is<Ident>(args[2]);
    auto bid = Is<Ident>(args[1]);
    if (!kid || !bid || !kid->vdef || !bid->vdef || !ParOrdinal(kid->vdef)) return;
    if (!SameParExpr(args[1], Par().bounds)) return;
    auto id = Is<Ident>(args[0]);
    auto a = id && id->vdef && ParCaptured(id->vdef) ? id->vdef : nullptr;
    auto &cc = Par().chunkchecks;
    for (auto &[x, y] : cc)
        if (x == a && y == bid->vdef) return;
    cc.push_back({ a, bid->vdef });
}

[[noreturn]] inline void TypeCheck::ParWriteError(Node *at, VarDef *vd, const char *what) {
    if (ParBinder(vd))
        Error(at, cat(what, " ", vd->name, " in a par for: ", vd->name, " tells the "
                      "iterations apart, each owning a[", vd->name, "] or a.chunk(", vd->name,
                      ", n) by it, so nothing may change it (docs/design/child_threads.md "
                      "§4.3 R2)"));
    if (vd->isglobal)
        Error(at, cat(what, " ", vd->name, " in a par for: a global is read-only in a par for's "
                      "body and in every function it calls, since every iteration may run at "
                      "once (docs/design/child_threads.md §4.3 R1, R3)"));
    Error(at, cat(what, " ", vd->name, " in a par for: ", vd->name, " is a variable of the "
                  "enclosing function, which every iteration sees and may run beside the "
                  "others, so the body may only read it (docs/design/child_threads.md §4.3 R1); "
                  "write what one iteration owns instead -- the element `par for &x` binds, "
                  "a[k] for the loop's index k, a.chunk(k, n) of the loop's count -- or, for "
                  "an integer accumulated by one operation, make it a reduction (x += ...)"));
}

// A write through a reference read out of a slot at `at` in a par for's
// body, or a function checked for it, which ReadBackLVal leaves read-only
// though the slot's type does not: the reference may point at storage the
// body sees read-only (ParReadOnlyAlt).
[[noreturn]] inline void TypeCheck::ParLoadedWriteError(Node *at, const char *what) {
    Error(at, cat(what, " this reference in a par for: it is read out of storage, and may "
                  "point at a variable from outside the body or at a global, which every "
                  "iteration sees and may run beside the others, so the body may only read "
                  "it (docs/design/child_threads.md §4.3 R1); a copy of a holder from outside "
                  "the body holds what that holder does, and a function the body hands one "
                  "is checked as the body is"));
}

// `x op= v`, `x++`, `x--` or `x = v` (op T_ASSIGN), `x .= r` (T_DOTASSIGN),
// on x, a variable from outside the body as a whole: a reduction of an
// integer local by an associative operation (§4.6), or no write at all.
inline void TypeCheck::ParAssign(Node *at, LVal &lv, TType op) {
    auto vd = lv.var;
    auto t = vd->type;
    auto reducing = op == T_PLUSEQ || op == T_MINUSEQ || op == T_OREQ || op == T_ANDEQ ||
                    op == T_XOREQ || op == T_INC || op == T_DEC;
    if (vd->isglobal || !reducing)
        ParWriteError(at, vd, op == T_DOTASSIGN ? "cannot rebind" : "cannot assign");
    if (t && t->kind == TY_FLT)
        Error(at, cat("cannot accumulate ", vd->name, " in a par for: a float is no reduction, "
                      "since its sum would depend on how the iterations are cut into chunks "
                      "(docs/design/child_threads.md §4.6); write each iteration's part into an "
                      "element (partial[k] = acc) and add them up after the loop, in the order "
                      "you choose"));
    if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT || t->ix)
        ParWriteError(at, vd, "cannot update");
    ParNote n;
    n.kind = PN_REDUCE;
    n.at = at;
    n.var = vd;
    n.op = op == T_MINUSEQ || op == T_INC || op == T_DEC ? T_PLUSEQ : op;
    n.name = Is<Assign>(at) ? Is<Assign>(at)->lval : Is<IncDec>(at) ? Is<IncDec>(at)->lval : at;
    ParApply(n);
}

// A builtin (`builtin`), or a function (`callee`), that a par for's body or
// a function checked for it calls: none whose effect is other than the
// writes the iteration owns (§4.3 R3), since the order of those effects
// would show how the iterations were scheduled.
inline void TypeCheck::ParEffect(Call *c, int builtin, SFunction *callee) {
    string what;
    switch (builtin) {
        case B_PRINT: case B_EXIT: case B_QPUT: case B_QGET: case B_QPOLL: case B_QLIMIT:
        case B_THREAD_SPAWN: case B_THREAD_SPAWN_CONTAINED: case B_THREAD_WAIT:
        case B_THREAD_POOL: case B_APPLY: case B_APPLY_CONTAINED: case B_POOL_CLOSE:
            what = BuiltinByKind(builtin).name;
            break;
        default: break;
    }
    // The standard library's input and output, its clocks and environment,
    // and its raw memory access are C whose effects are the world's.
    if (callee && callee->isextern && ast.StdlibFile(callee->line.fileidx)) {
        auto &file = ast.sources[callee->line.fileidx].first;
        auto ends = [&](const char *s) {
            auto n = strlen(s);
            return file.size() >= n && file.compare(file.size() - n, n, s) == 0 &&
                   (file.size() == n || file[file.size() - n - 1] == '/' ||
                    file[file.size() - n - 1] == '\\');
        };
        if (ends("os.goose") || ends("ffi.goose")) what = string(callee->qname);
    }
    if (what.empty()) return;
    auto direct = (int)frames.size() - 1 == Par().frame || frames.back().isfunval;
    Error(c, cat(what, " in a par for", direct ? "" : "'s callee", ": an iteration's only effect "
                 "may be its writes to what it owns, and the order of anything else -- output, "
                 "a queue, a thread or pool, exit -- would show how the iterations were "
                 "scheduled (docs/design/child_threads.md §4.3 R3); do it before or after the "
                 "loop, or run the program with GOOSE_THREADS=1 to debug"));
}

// A return (`what`), from the function of frame `targetframe`, in a par
// for's body or a function checked for it: none may leave the body (§4.3
// R4), whose iterations end only by running to its end or a continue.
inline void TypeCheck::ParExit(Node *at, int targetframe, const char *what) {
    if (!InPar() || targetframe > Par().frame) return;
    Error(at, cat(what, " out of a par for: an iteration ends by running to the end of the "
                  "body or by continue, and nothing leaves the loop early, since the "
                  "other iterations may be running (docs/design/child_threads.md §4.3 R4)"));
}

// A call in a par for's body, or in a function checked for it, to a function
// whose check returned out of the functions enclosing every call of it
// (FnSpec::needs), which a reused check does not do again.
inline void TypeCheck::ParCallNeeds(Node *at, FnSpec *callee) {
    if (!InPar() || !callee) return;
    for (auto t : callee->needs) {
        auto f = FrameOfSpec(t);
        if (f >= 0 && f <= Par().frame)
            Error(at, cat("this call returns from ", t->sf->name, " out of a par for: an "
                          "iteration ends by running to the end of the body or by continue "
                          "(docs/design/child_threads.md §4.3 R4)"));
    }
}

// The specialization whose parameter's class r is, on the compile-time path,
// and whether its calls so far all gave that parameter, or its view, one root
// exactly, an array other than every other argument's (RootArg::exact,
// parconcrete): only then may another such class of it be other storage,
// which the calls yet to come may still deny (ResolveParConflicts). Null
// where no frame's specialization has r as a class.
inline FnSpec *TypeCheck::ClassSpecOf(VarDef *r, bool &distinct) {
    distinct = false;
    for (auto i = (int)frames.size() - 1; i >= 0; i--) {
        auto sp = frames[i].spec;
        if (!sp || frames[i].isfunval) continue;
        auto view = false;
        auto p = ClassParam(sp, r, view);
        if (p < 0) continue;
        distinct = view ? p < (int)sp->views.size() && sp->views[p].exact &&
                              sp->views[p].parconcrete
                        : p < (int)sp->roots.size() && sp->roots[p].exact &&
                              sp->roots[p].parconcrete;
        return sp;
    }
    return nullptr;
}

// Whether a value with alternative `a` may lead to storage `root`, a part's
// or a reduction's, in the par for of ctx (MayAliasRoots, for a par for). A
// variable's own storage is no other's, nor are two components of one root
// struct, and what the loop's own activation made is no caller's; but a
// parameter's class stands for what callers handed the activation, which
// may be any of their storage: a global's, a variable's of a function
// around this one, another class's. Two classes of one specialization are
// other storage only where every call reaching it gives them arrays apart,
// which only the last of those calls settles: AL_DEFER, with the
// specialization in `defer`. A bound, an inexact alternative, may be
// anything outside its scope.
inline TypeCheck::Alias TypeCheck::ParMayAlias(const RootAlt &a, VarDef *root, ParCtx &ctx,
                                               FnSpec *&defer) {
    defer = nullptr;
    auto r = a.root;
    if (r == root) return AL_YES;
    // A root struct's owner holds its components' storage (§3.9): what
    // reaches the owner reaches every one of them.
    if (r && root && (ProjectedFrom(root, r) || ProjectedFrom(r, root))) return AL_YES;
    // Static data, and a const global's storage, are read-only, and no
    // writable part lies in either.
    if (!r || !root || (r->isglobal && r->type && r->type->cq)) return AL_NO;
    if (!a.exact) return WithinDepth(root, r) ? AL_YES : AL_NO;
    if (r->componentowner && r->componentowner == root->componentowner &&
        r->componentfield != root->componentfield && TypeEq(r->componentbase, root->componentbase))
        return AL_NO;
    auto spec = frames[ctx.frame].spec;
    auto own = [&](VarDef *v) {
        return IsTemp(v) || (!IsClassRoot(v) && !v->isglobal && v->ownerspec == spec);
    };
    if (own(root) || own(r)) return AL_NO;
    auto rc = IsClassRoot(root), ac = IsClassRoot(r);
    if (!rc && !ac) return AL_NO;
    if (rc && ac) {
        bool rd = false, ad = false;
        auto rs = ClassSpecOf(root, rd);
        auto as = ClassSpecOf(r, ad);
        if (!rs || rs != as || !rd || !ad) return AL_YES;
        defer = rs;
        return AL_DEFER;
    }
    return AL_YES;
}

// Whether runs of elements of types a and b, in one storage, may overlap:
// where they are of one type, where one holds the other by value -- the
// cells of a row beside the rows -- and where either is bytes, which a byte
// view cuts out of anything. Elements of two types neither of which holds
// the other lie apart, as two fields' arrays do.
inline bool TypeCheck::ParElemsOverlap(TypeExpr *a, TypeExpr *b) {
    return !a || !b || MayHoldValueOf(a, b) || MayHoldValueOf(b, a) || IsU8(a) || IsU8(b);
}

// Whether storage of type t holds a run of elements of type e, which a part
// of type e is: an array of them, or of what holds one, by value. Any array
// may be a run of bytes, which a byte view (bytes_of) cuts out of it.
inline bool TypeCheck::ParHoldsRun(TypeExpr *t, TypeExpr *e) {
    if (!t || !e) return true;
    if (t->kind == TY_ARRAY)
        return IsU8(e) || MaySameType(t->arr->sub, e) || ParHoldsRun(t->arr->sub, e);
    return AnyField(t, [&](TypeExpr *ft) { return ParHoldsRun(ft, e); });
}

// Whether a reference or slice of pointee type p may lead to a part's
// storage, a run of elements of type e: point at an element of it or at
// what holds the run; view it as bytes, as a byte view (bytes_of) may any
// array's elements; or point at references, which lead anywhere
// (ParRootsOf takes their roots).
inline bool TypeCheck::ParPointeeReaches(TypeExpr *p, TypeExpr *e) {
    return !p || !e || IsU8(p) || IsRefOrSlice(p) || HoldsPlainRef(p) || ParHoldsPoolRef(p) ||
           MayHoldValueOf(e, p) || ParHoldsRun(p, e);
}

// Whether what a use of vd reaches may be a part's storage of elements of
// type e: where a reference or slice of it points (ParPointeeReaches); vd's
// own storage, where that holds such a run; and anything, where vd holds
// references (ParRootsOf takes their roots).
inline bool TypeCheck::ParUseReaches(VarDef *vd, TypeExpr *e) {
    auto t = vd->type;
    if (!t || !e) return true;
    if (IsRefOrSlice(t) && !IsRelRef(t)) return ParPointeeReaches(PointeeOf(t), e);
    return HoldsPlainRef(t) || ParHoldsPoolRef(t) || ParHoldsRun(t, e);
}

// Whether a value of type t may lead to rv, an integer variable (a
// reduction's), other than by naming it: as a reference that may point at
// it, or by references it holds or points at (ParRootsOf takes their roots).
// A slice views an array's elements, never a variable of its own, and a
// byte view the elements of an array too.
inline bool TypeCheck::ParReachesVar(TypeExpr *t, VarDef *rv) {
    if (!t || !rv->type) return true;
    if (IsRefOrSlice(t)) {
        auto p = PointeeOf(t);
        if (!p || IsRefOrSlice(p) || HoldsPlainRef(p)) return true;
        return t->kind == TY_REF && MayHoldValueOf(rv->type, p);
    }
    return HoldsPlainRef(t);
}

// The body is checked: its parts must each be cut one way and reached no
// other way, and its reductions used no other way (§4.4, §4.6). What
// codegen needs of them goes on the loop.
inline void TypeCheck::FinishParFor(ForLoop *x, ParCtx &ctx) {
    // The binder a chunk is taken with, as written: chunk(k, n), chunk_at(b, k).
    auto index = [&](const ParPart &p) -> string {
        auto c = Is<Call>(p.at);
        auto an = c ? c->ArgNodes() : vector<Node *>();
        auto i = p.form == PF_CHUNK ? 1u : 2u;
        return i < an.size() ? ExprStr(an[i]) : string("k");
    };
    auto formname = [&](const ParPart &p) -> string {
        auto of = p.var ? string(p.var->name) : string("the sequence");
        switch (p.form) {
            case PF_ELEMENT:
                return p.by ? cat(of, "[", p.by->name, "]")
                            : cat("the element of ", of, " the iteration's index names");
            case PF_CHUNK: return cat(of, ".chunk(", index(p), ", ", ExprStr(p.key), ")");
            case PF_CHUNK_AT: return cat(of, ".chunk_at(", ExprStr(p.key), ", ", index(p), ")");
        }
        return "";
    };
    // A use or a part that may reach storage x, where y lies: an error where
    // the two may be one storage, and where only the call sites can tell
    // (two classes of one specialization), one ResolveParConflicts reports
    // unless every call reaching it passed them arrays apart.
    auto clash = [&](Alias al, FnSpec *sp, VarDef *xr, VarDef *yr, Node *at, auto msg) {
        if (al == AL_YES) Error(at, msg());
        if (al == AL_DEFER && pardeferred.insert({ at, sp, xr, yr }).second)
            parconflicts.push_back({ at, sp, xr, yr, msg() });
    };
    auto redmsg = [&](string what, VarDef *rv, Node *first) {
        return cat(what, " may point at ", rv->name, ", a reduction of this par for (",
                   Where(first->line), "), so the body may not read it through ",
                   "that: each chunk of iterations keeps a total of its own, which the loop "
                   "combines once they all end (docs/design/child_threads.md §4.6)");
    };
    for (size_t i = 0; i < ctx.parts.size(); i++) {
        auto &b = ctx.parts[i];
        for (size_t j = 0; j < i; j++) {
            auto &a = ctx.parts[j];
            if (a.at == b.at) continue;
            // One view of one storage, cut one way: by the same element
            // index, or into the same runs.
            if (a.root == b.root && a.var && a.var == b.var) {
                auto same = a.form == b.form &&
                            (a.form == PF_ELEMENT ? a.by == b.by : SameParExpr(a.key, b.key));
                if (!same)
                    Error(b.at, cat("a par for's iterations own ", formname(b), " here and ",
                                    formname(a), " at ", Where(a.at->line), ", which are cut "
                                    "differently and so overlap between iterations "
                                    "(docs/design/child_threads.md §4.4)"));
                continue;
            }
            // Two views that may be of one storage may start at different
            // elements of it, whatever their cuts.
            if (!ParElemsOverlap(a.elem, b.elem)) continue;
            RootAlt ra;
            ra.root = b.root;
            ra.exact = true;
            FnSpec *sp = nullptr;
            auto al = ParMayAlias(ra, a.root, ctx, sp);
            clash(al, sp, b.root, a.root, b.at, [&] {
                return cat("a par for's iterations own ", formname(b), " here and ",
                           formname(a), " at ", Where(a.at->line), ", which may be views of "
                           "one array from different starts, and so overlap between "
                           "iterations (docs/design/child_threads.md §4.4)");
            });
        }
    }
    for (auto &[at, vd] : ctx.uses) {
        if (ctx.partuses.count(at)) continue;
        // A name of a root struct's owner whose path reaches one component
        // alone uses that component (NoteParComponent).
        auto pc = parcomponents.find(at);
        auto reach = pc != parcomponents.end() && pc->second->componentowner == vd ? pc->second
                                                                                  : vd;
        auto roots = ParRootsOf(reach);
        for (auto &p : ctx.parts) {
            auto mine = vd == p.var;
            auto msg = [&] {
                return cat(vd->name, " is ", mine ? "" : "or may point into storage ",
                           "partitioned by this par for, each iteration owning ", formname(p),
                           " (", Where(p.at->line), "), so no iteration may read or write ",
                           mine ? "it" : "that storage", " any other way: another iteration "
                           "may be writing what this one reads "
                           "(docs/design/child_threads.md §4.4)",
                           vd->isglobal && !mine
                               ? cat("; a global that is only read can be declared const, "
                                     "which no part lies in")
                               : string());
            };
            if (mine) Error(at, msg());
            auto reaches = ParUseReaches(reach, p.elem);
            for (auto &a : roots.alts) {
                FnSpec *sp = nullptr;
                auto al = a.root == p.root ? AL_YES
                          : reaches        ? ParMayAlias(a, p.root, ctx, sp)
                                           : AL_NO;
                clash(al, sp, a.root, p.root, at, msg);
            }
        }
        for (auto &[rv, op, first] : ctx.reductions) {
            if (rv == vd) {
                if (!ctx.redtargets.count(at))
                    Error(at, cat(vd->name, " is a reduction of this par for (",
                                  Where(first->line), "), so the body may only accumulate it, "
                                  "never read it or write it otherwise: each chunk of "
                                  "iterations keeps a total of its own, which the loop "
                                  "combines once they all end "
                                  "(docs/design/child_threads.md §4.6)"));
                continue;
            }
            // Nor read it through a reference or what holds one.
            if (!ParReachesVar(reach->type, rv)) continue;
            for (auto &a : roots.alts) {
                FnSpec *sp = nullptr;
                auto al = ParMayAlias(a, rv, ctx, sp);
                clash(al, sp, a.root, rv, at, [&] {
                    return redmsg(string(vd->name), rv, first);
                });
            }
        }
    }
    // Each iteration of a loop over a sequence reads its own element of it:
    // a part that may be another view of the sequence's storage, from
    // another start, may be another iteration's element. The element `par
    // for &x` binds, and a[k] of the sequence's own variable by the loop's
    // index, are the iteration's own.
    if (ctx.hasseq)
        for (auto &p : ctx.parts) {
            if (p.at == x) continue;
            if (p.var && p.var == ctx.seqvar && p.form == PF_ELEMENT && !p.by) continue;
            if (!ParElemsOverlap(p.elem, ctx.seqelem)) continue;
            for (auto &a : ctx.seqroots.alts) {
                FnSpec *sp = nullptr;
                auto al = ParMayAlias(a, p.root, ctx, sp);
                clash(al, sp, a.root, p.root, p.at, [&] {
                    return cat("a par for's iterations each read their element of the sequence "
                               "and own ", formname(p), " here, which may be a view of the "
                               "sequence's storage from another start, so that one iteration "
                               "writes what another reads (docs/design/child_threads.md §4.4)");
                });
            }
        }
    // A reference or slice read out of storage in the body may lead to the
    // storage of a part, or to a reduction, whatever the variable whose
    // storage it was read out of.
    for (auto &rb : ctx.readbacks) {
        auto pointee = rb.type && IsRefOrSlice(rb.type) ? PointeeOf(rb.type) : nullptr;
        for (auto &p : ctx.parts) {
            if (!ParPointeeReaches(pointee, p.elem)) continue;
            for (auto &a : rb.roots.alts) {
                FnSpec *sp = nullptr;
                auto al = ParMayAlias(a, p.root, ctx, sp);
                clash(al, sp, a.root, p.root, rb.at, [&] {
                    return cat("a reference or slice read out of storage here may point into "
                               "storage partitioned by this par for, each iteration owning ",
                               formname(p), " (", Where(p.at->line), "), so no iteration may "
                               "read or write it this way: another iteration may be writing "
                               "what this one reads (docs/design/child_threads.md §4.4)");
                });
            }
        }
        for (auto &[rv, op, first] : ctx.reductions) {
            if (!ParReachesVar(rb.type, rv)) continue;
            for (auto &a : rb.roots.alts) {
                FnSpec *sp = nullptr;
                auto al = ParMayAlias(a, rv, ctx, sp);
                clash(al, sp, a.root, rv, rb.at, [&] {
                    return redmsg("a reference or slice read out of storage here", rv, first);
                });
            }
        }
    }
    // Added to what an earlier check of the same loop found (a recursive
    // cycle's round, say), whose functions this check reused: what they
    // reduce or partition they did not check again.
    for (auto &[rv, op, first] : ctx.reductions) {
        auto known = false;
        for (auto &[v, o] : x->reductions) known = known || v == rv;
        if (!known) x->reductions.push_back({ rv, op });
    }
    auto checkbounds = [&](VarDef *a, VarDef *b) {
        auto known = false;
        for (auto &[av, bv] : x->chunkats) known = known || (av == a && bv == b);
        if (!known) x->chunkats.push_back({ a, b });
    };
    for (auto &p : ctx.parts) {
        if (p.form != PF_CHUNK_AT || !p.var) continue;
        auto b = Is<Ident>(p.key);
        if (b && b->vdef) checkbounds(p.var, b->vdef);
    }
    for (auto &[a, b] : ctx.chunkchecks) checkbounds(a, b);
}

}  // namespace goose
