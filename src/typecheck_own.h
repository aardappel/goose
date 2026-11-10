// Goose compiler — the typechecker's ownership rules (definitions of
// TypeCheck members, typecheck.h): docs/design/ownership.md. An `own` value
// carries an obligation: it is not copied, and exactly one consumer takes it
// before it dies. Nothing here reaches codegen but the two builtins that
// write storage (take, replace) and a move out of a struct variable, which
// leaves its own fields empty (Ident::ownmove).
//
// The obligation is flow state of the variable holding the value
// (VarDef::own), saved, restored and joined with definite assignment
// (FlowState): held, claimed by a `defer`, consumed, or mixed where the
// paths to a point disagree, which is an error at the join. A value no
// variable holds yet (a call's result, a literal) is on a list its statement
// must leave empty (BodyState::ownfresh). A destination whose type holds an
// obligation takes it (CheckValue): a parameter, a result, a field of a
// literal, a declaration. Any other use of an own value borrows it as its
// plain type (FitsAt).
//
// Storage that is no variable of its own -- a field reached through a
// reference, an element, a global -- holds a live obligation or an empty
// value at all times, and gives one up only through take() and replace(),
// which leave it empty or refilled. What the checker knows of it is which
// own fields of a struct are empty (OwnFlow::empty), for the two places that
// ask: the end of a struct variable's scope, and the end of a `trusted fn`,
// where an own handle's claim ends and what it points at must hold nothing.
#pragma once

namespace goose {

inline bool TypeCheck::OwnTracked(TypeExpr *t) {
    if (!t) return false;
    if (t->oq) return true;
    return t->kind == TY_STRUCT && !t->struc->st->isroot && HoldsOwn(t);
}

// The struct whose own fields a variable of type t answers for: the one it
// is, or the one an own reference, or a borrow of one, points at. Null where
// there is none.
inline TypeExpr *TypeCheck::OwnPointee(TypeExpr *t) {
    if (t->kind == TY_REF && t->ref->lenstorage < 0) t = t->ref->sub;
    if (t->kind != TY_STRUCT || t->struc->st->isroot) return nullptr;
    return GetStructInst(t)->holdsown ? t : nullptr;
}

inline void TypeCheck::OwnLeaves(TypeExpr *t, const string &prefix, vector<OwnLeaf> &out) {
    auto inst = GetStructInst(t);
    for (size_t i = 0; i < inst->st->fields.size(); i++) {
        auto &f = inst->st->fields[i];
        if (f.ispad) continue;
        auto ft = inst->ftypes[i];
        auto path = cat(prefix, ".", f.name);
        if (ft->oq) out.push_back({ path, ft });
        else if (OwnPointee(ft) && ft->kind == TY_STRUCT) OwnLeaves(ft, path, out);
    }
}

// The bits of every own field of the struct a variable of type t answers
// for: all of them set is that struct holding nothing.
inline uint32_t TypeCheck::OwnLeafMask(TypeExpr *t) {
    auto st = OwnPointee(t);
    if (!st) return 0;
    vector<OwnLeaf> leaves;
    OwnLeaves(st, {}, leaves);
    if (leaves.size() > MAXOWNLEAVES)
        Error(t->line, cat("struct ", TypeStr(st), " has more than ", (int64_t)MAXOWNLEAVES,
                           " own fields, which the checker does not track; group some in a "
                           "struct stored in a pool of its own"));
    return leaves.size() == MAXOWNLEAVES ? ~0u : (1u << leaves.size()) - 1;
}

// The own fields of the struct of type t that the literal n builds leaves
// empty, as bits (OwnLeaves): one it gives no value, a null or a []. Zero
// for any other expression, of which nothing is known.
inline uint32_t TypeCheck::LitEmptyLeaves(Node *n, TypeExpr *t) {
    auto bit = 0;
    uint32_t mask = 0;
    auto count = [&](TypeExpr *st) {
        vector<OwnLeaf> leaves;
        OwnLeaves(st, {}, leaves);
        return (int)leaves.size();
    };
    auto empty = [&](Node *v) {
        if (!v || Is<NullLit>(v)) return true;
        if (auto al = Is<ArrayLit>(v)) return al->elems.empty() && !al->fillval && !al->capexpr;
        auto c = Is<Call>(v);
        return c && c->builtin == B_DEFAULT;
    };
    auto set = [&](int k) { if (k < (int)MAXOWNLEAVES) mask |= 1u << k; };
    function<void(Node *, TypeExpr *)> walk = [&](Node *v, TypeExpr *st) {
        auto sl = Is<StructLit>(v);
        auto inst = GetStructInst(st);
        for (size_t i = 0; i < inst->st->fields.size(); i++) {
            if (inst->st->fields[i].ispad) continue;
            auto ft = inst->ftypes[i];
            auto fv = sl && sl->sinst == inst ? sl->InitFor((int)i) : nullptr;
            if (ft->oq) {
                if (sl && empty(fv)) set(bit);
                bit++;
            } else if (OwnPointee(ft) && ft->kind == TY_STRUCT) {
                if (sl && Is<StructLit>(fv)) {
                    walk(fv, ft);
                } else {
                    auto n = count(ft);
                    if (sl && empty(fv))
                        for (auto k = 0; k < n; k++) set(bit + k);
                    bit += n;
                }
            }
        }
    };
    if (auto st = OwnPointee(t); st && st == t && Is<StructLit>(n)) walk(n, st);
    return mask;
}

// Whether n is written as a value that owns nothing: a null, a [], a
// default.
inline bool TypeCheck::OwnNothing(Node *n) {
    if (!n) return false;
    if (Is<NullLit>(n)) return true;
    if (auto al = Is<ArrayLit>(n)) return al->elems.empty() && !al->fillval && !al->capexpr;
    auto c = Is<Call>(n);
    return c && c->builtin == B_DEFAULT;
}

inline void TypeCheck::TrackOwn(VarDef *vd) {
    if (!vd->type || vd->isglobal || vd->istemp || vd->copybind) return;
    if (!OwnTracked(vd->type)) {
        if (vd->type->kind == TY_REF && OwnPointee(vd->type)) {
            ownused = true;
            vd->ownborrow = true;
            vd->own = OwnFlow {};
        }
        return;
    }
    ownused = true;
    vd->owned = true;
    vd->own = OwnFlow {};
    // One declared without a value holds nothing until it is assigned.
    if (!vd->assigned) vd->own.state = OS_CONSUMED;
}

// Whether the code being checked is text of a `trusted fn`, a block written
// in one included: where an obligation may begin and end unchecked.
inline bool TypeCheck::InTrusted() {
    auto t = TextFn();
    return t && t->trusted;
}

inline string TypeCheck::OwnWhere(const Node *at) {
    return at && at->line.line ? cat(" at ", Where(at->line)) : string();
}

inline OwnFlow TypeCheck::JoinOwn(const OwnFlow &a, const OwnFlow &b) {
    if (a.state == b.state && a.claim == b.claim) {
        auto r = a;
        r.empty = a.empty & b.empty;
        if (!r.at) r.at = b.at;
        return r;
    }
    // A variable known empty on one path is, beside one that may hold a
    // value, one that may: whatever takes it finds out which. Beside a
    // consumed one, whose bits are stale, it is as good as consumed.
    if (a.state == OS_EMPTY || b.state == OS_EMPTY) {
        auto &other = a.state == OS_EMPTY ? b : a;
        if (other.state != OS_MIXED) return other;
    }
    OwnFlow r;
    r.state = OS_MIXED;
    auto &from = a.state != OS_HELD ? a : b;
    r.at = from.at;
    // One mixed already keeps what made it so (CheckLoopPasses).
    r.claim = a.state == OS_MIXED ? a.claim : b.state == OS_MIXED ? b.claim : 0;
    return r;
}

// A use of an own variable, which must hold its value: every mention but
// the assignment that gives it a new one.
inline void TypeCheck::UseOwn(VarDef *vd, const Node *at) {
    if (!vd->owned || !reachable) return;
    auto &o = vd->own;
    if (o.state == OS_CONSUMED && o.at != at)
        Error(at, cat(vd->name, " was consumed", OwnWhere(o.at), " and holds nothing here: an "
                      "own value belongs to whatever took it"));
    if (o.state == OS_MIXED)
        Error(at, cat(vd->name, " may have been consumed", OwnWhere(o.at),
                      o.claim < 0 ? ", in an earlier iteration of the loop around it"
                                  : ", on another path to here",
                      ": every path must leave an own value held, or consumed"));
}

// vd's value passes to what `at` hands it to.
inline void TypeCheck::ConsumeVar(VarDef *vd, Node *at) {
    if (!reachable) return;
    // A struct's own fields lie in storage a reference may still name: the
    // move leaves them empty, so that nothing reads its claims out of them
    // again. A return's local dies with the move.
    auto moved = [&] {
        if (auto id = Is<Ident>(at); id && !vd->type->oq)
            id->ownmove = !(inreturn && vd->ownerspec == frames.back().spec &&
                            !frames.back().isfunval);
    };
    auto &o = vd->own;
    switch (o.state) {
        case OS_HELD: case OS_EMPTY: break;
        case OS_CONSUMED:
            Error(at, cat(vd->name, " was consumed", OwnWhere(o.at), " already: an own value is "
                          "consumed once"));
        case OS_CLAIMED:
            Error(at, cat(vd->name, " is consumed here and again by the defer", OwnWhere(o.at),
                          ", which releases it when its block ends: keep one of the two"));
        case OS_MIXED:
            UseOwn(vd, at);
    }
    o.state = OS_CONSUMED;
    o.empty = 0;
    o.claim = 0;
    o.at = at;
    moved();
}

// Whether n names storage that is no tracked variable of its own: a field,
// an element, a global, a borrow.
inline bool TypeCheck::OwnPlace(Node *n) {
    while (auto u = Is<Unary>(n)) {
        if (!u->synth) break;
        n = u->child;
    }
    if (auto id = Is<Ident>(n)) return !id->vdef || !id->vdef->owned;
    return Is<Dot>(n) || Is<Index>(n);
}

// The value of n, which holds an obligation, passes to the destination that
// took it: a variable's is consumed, a fresh value's is the destination's
// from here on, and storage gives one up only through take(). `pointee`:
// the value is what the reference n denotes points at, which is storage.
inline void TypeCheck::ConsumeOwn(Node *n, bool pointee) {
    if (!reachable) return;
    ownused = true;
    auto inner = n;
    while (auto u = Is<Unary>(inner)) {
        if (!u->synth) break;
        inner = u->child;
    }
    if (auto id = Is<Ident>(inner); id && id->vdef && id->vdef->owned && !pointee) {
        ConsumeVar(id->vdef, inner);
        return;
    }
    if (pointee) {
        auto what = ExprStr(inner);
        Error(n, cat("what ", what, " refers to holds own values and stays in its storage: a "
                     "read through a reference only borrows it; move it out with take(r), "
                     "where r is a plain reference to it, which leaves it empty"));
    }
    if (OwnPlace(inner)) {
        auto what = ExprStr(inner);
        Error(n, cat(what, " holds an own value in storage, which a plain read only borrows: "
                           "move it out with take(", what, "), which leaves it empty"));
    }
    auto &f = cur.ownfresh;
    f.erase(remove(f.begin(), f.end(), n), f.end());
    if (inner != n) f.erase(remove(f.begin(), f.end(), inner), f.end());
}

// A local array whose elements hold own values, or a root struct with a
// field that is one, must hold none where its scope ends (4.6 of the design
// note): there is no text per element there to consume what they own. The
// one check ownership adds at run time, written as the defer the program
// would write: `defer if xs.len != 0 { abort(...) }`, after the declaration.
inline void TypeCheck::GuardOwnArrays(VarDecl *decl, VarDef *d) {
    auto t = d->type;
    if (!t) return;
    vector<string_view> fields;
    auto whole = t->kind == TY_ARRAY && HoldsOwn(t->arr->sub);
    if (t->kind == TY_STRUCT && t->struc->st->isroot) {
        auto inst = GetStructInst(t);
        for (size_t i = 0; i < inst->st->fields.size(); i++)
            if (auto ft = inst->ftypes[i]; ft && ft->kind == TY_ARRAY && HoldsOwn(ft->arr->sub))
                fields.push_back(inst->st->fields[i].name);
    }
    if (!whole && fields.empty()) return;
    ownused = true;
    if (blockpos.empty()) Error(decl, "internal: an own array declared outside a block");
    auto b = blockpos.back().block;
    auto idx = blockpos.back().idx;
    if (idx >= b->stmts.size() || b->stmts[idx] != decl)
        Error(decl, cat("an array of own-holding elements (", d->name, ") is declared as a "
                        "statement of its own, with one name"));
    // A later pass of a loop, or round of a cycle, checks the same body.
    if (idx + 1 < b->stmts.size())
        if (auto df = Is<Defer>(b->stmts[idx + 1]); df && df->ownguard == d) return;
    auto line = decl->line;
    auto body = ast.New<Block>(line);
    auto check = [&](string_view field) {
        Node *place = ast.New<Ident>(line, d->name);
        if (!field.empty()) place = ast.New<Dot>(line, place, field);
        auto len = ast.New<Dot>(line, place, "len");
        auto cond = ast.New<Binary>(line, T_NEQ, len, ast.New<IntLit>(line, 0));
        auto fail = ast.New<Call>(line, ast.New<Ident>(line, "::abort"));
        fail->args.push_back(ast.New<StrLit>(
            line, cat(d->name, field.empty() ? "" : ".", field, " still holds own values where "
                      "its scope ends: pop each element, or release them all, before it does")));
        fail->implicit = true;
        auto then = ast.New<Block>(line);
        then->stmts.push_back(fail);
        body->stmts.push_back(ast.New<IfExpr>(line, cond, then, nullptr));
    };
    if (whole) check({});
    for (auto f : fields) check(f);
    auto df = ast.New<Defer>(line, body);
    df->ownguard = d;
    b->stmts.insert(b->stmts.begin() + (ptrdiff_t)idx + 1, df);
}

// An own value a call or a literal made, which nothing holds yet.
inline void TypeCheck::NoteOwnFresh(Node *n, const Val &v) {
    // A call's results after the first reach only a declaration binding
    // them all: any other use drops them, and what they own.
    if (auto c = Is<Call>(n); c && reachable && c->rettypes.size() > 1 && n != multidestnode)
        for (size_t i = 1; i < c->rettypes.size(); i++)
            if (HoldsOwn(c->rettypes[i]))
                Error(n, cat("result ", (int64_t)i + 1, " of this call is a ",
                             TypeStr(c->rettypes[i]), ", which is dropped here: bind every "
                             "result (let a, b = ...) and consume it"));
    if (!reachable || !v.type || v.isnull || v.emptyarr || v.lvalue || v.pointee) return;
    if (Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n) || Is<Unary>(n)) return;
    if (!OwnTracked(v.type)) return;
    ownused = true;
    auto &f = cur.ownfresh;
    if (find(f.begin(), f.end(), n) == f.end()) f.push_back(n);
}

// The statement whose check began with `base` values pending has ended:
// one it made and nothing took was dropped.
inline void TypeCheck::NoOwnDropped(size_t base) {
    auto &f = cur.ownfresh;
    if (f.size() <= base) return;
    auto n = f[base];
    f.resize(base);
    if (!reachable) return;
    Error(n, cat("this ", TypeStr(n->exprtype ? n->exprtype : ast.voidtype), " is dropped: an "
                 "own value must be bound with let, passed to an own parameter, stored in an "
                 "own field or returned, and whatever takes it consumes it"));
}

// An exit leaves the variables from vars[base] on: each that carries an
// obligation must hold nothing there, or be claimed by a defer the exit
// runs. `ending`: the trusted fn whose own exit this is, where the claim of
// an own parameter of its ends, provided what it points at holds nothing.
inline void TypeCheck::RequireOwnConsumed(int base, const Node *at, const string &exit,
                                          FnSpec *ending) {
    if (!ownused || !reachable) return;
    for (auto i = (int)vars.size() - 1; i >= base; i--) {
        auto v = vars[i];
        if (!v->owned || !v->assigned) continue;
        auto &o = v->own;
        if (o.state == OS_CONSUMED || o.state == OS_CLAIMED || o.state == OS_EMPTY) continue;
        auto loc = at ? LocOf(at) : LocOf(v->line);
        auto fail = [&](const string &msg) { ErrorIn(loc, msg, Chain(ActiveExpansionTrace())); };
        if (o.state == OS_MIXED)
            fail(cat(v->name, " is consumed on some paths to ", exit, " and still held on "
                     "others", OwnWhere(o.at), ": every path must consume it"));
        auto mask = OwnLeafMask(v->type);
        // A struct whose own fields were all taken holds nothing.
        if (!v->type->oq && mask && o.empty == mask) continue;
        if (ending && v->isparam && v->type->oq && v->ownerspec == ending) {
            // A run's elements have no text each: nothing here could say
            // that each was emptied.
            if (v->type->kind == TY_SLICE && HoldsOwn(v->type->sub))
                fail(cat("trusted fn ", ending->sf->name, " ends the claim of ", v->name,
                         " on a run of ", TypeStr(v->type->sub), ", whose elements hold own "
                         "values: a run of such elements is not supported; keep them in a "
                         "slot_pool, one handle each"));
            if (!mask || o.empty == mask) continue;
            vector<OwnLeaf> leaves;
            OwnLeaves(OwnPointee(v->type), {}, leaves);
            string missing;
            auto nmissing = 0;
            for (size_t k = 0; k < leaves.size(); k++)
                if (!(o.empty & (1u << k))) {
                    Append(missing, missing.empty() ? "" : ", ", leaves[k].path.substr(1));
                    nmissing++;
                }
            fail(cat("trusted fn ", ending->sf->name, " ends the claim of ", v->name, " on a ",
                     TypeStr(OwnPointee(v->type)), " whose own field", nmissing > 1 ? "s " : " ",
                     missing, " may still hold a value: take() each one before ", exit,
                     ", in the block the caller passes"));
        }
        string how;
        if (!v->type->oq && mask) {
            how = cat("take() each own field of it, or pass ", v->name, " on whole");
        } else {
            how = cat("release it, return it, store it, or claim it with a defer that does");
        }
        fail(cat(v->name, " (", TypeStr(v->type), ") is still held at ", exit, ": ", how));
    }
}

// A scope's text ended: its variables die there, and the defers written in
// it have run, so what they claimed is consumed from here on.
inline void TypeCheck::OwnScopeEnd(const Scope &s, int scopeidx) {
    if (!ownused || !reachable) return;
    auto &fr = frames.back();
    auto fnend = s.kind == SK_FN && !fr.isfunval && scopeidx == fr.scopebase;
    RequireOwnConsumed(s.varbase, nullptr,
                       fnend ? "the end of the function" : "the end of its block",
                       fnend && fr.sf && fr.sf->trusted ? fr.spec : nullptr);
    if (s.ownclaims)
        for (auto v : vars)
            if (v->owned && v->own.state == OS_CLAIMED && v->own.claim == s.serial) {
                v->own.state = OS_CONSUMED;
                v->own.claim = 0;
            }
}

// A jump leaves the scopes from `fromscope` on: their defers run, so what
// they claimed is consumed in the flow the jump carries.
inline void TypeCheck::RunOwnClaims(int fromscope) {
    if (!ownused) return;
    for (auto i = fromscope; i < (int)scopes.size(); i++) {
        if (!scopes[i].ownclaims) continue;
        for (auto v : vars)
            if (v->owned && v->own.state == OS_CLAIMED && v->own.claim == scopes[i].serial) {
                v->own.state = OS_CONSUMED;
                v->own.claim = 0;
            }
    }
}

// The paths joining after `construct` at `at` must agree on every own
// variable (Austral's rule): one consumed on some and held on others would
// have no state the code after it could rely on.
inline void TypeCheck::OwnAgree(Node *at, const char *construct) {
    if (!ownused || !reachable) return;
    for (auto v : vars) {
        if (!v->owned || v->own.state != OS_MIXED || v->own.claim < 0) continue;
        Error(at, cat(v->name, " is consumed on one path through this ", construct,
                      OwnWhere(v->own.at), " and still held on another: every path must "
                      "consume it, or none"));
    }
}

inline void TypeCheck::ClearOwnEmpties() {
    for (auto v : vars) if (v->owned || v->ownborrow) v->own.empty = 0;
}

// Code here, or a callee, may store an own value into storage: nothing the
// checker took for empty is known to be any more, and the body's callers
// learn the same (FnSpec::fillsown).
inline void TypeCheck::NoteOwnFill(Node *) {
    if (auto spec = CurRealFrame().spec) spec->fillsown = true;
    ClearOwnEmpties();
}

inline TypeCheck::OwnSnapshot TypeCheck::SaveOwn() {
    OwnSnapshot o;
    if (!ownused) return o;
    for (auto v : vars) if (v->owned || v->ownborrow) o.vars.push_back({ v, v->own });
    o.fresh = cur.ownfresh.size();
    return o;
}

inline void TypeCheck::RestoreOwn(const OwnSnapshot &o) {
    if (!ownused) return;
    for (auto &[v, f] : o.vars) v->own = f;
    if (cur.ownfresh.size() > o.fresh) cur.ownfresh.resize(o.fresh);
}

// The own fields a path names, as bits of the variable that answers for
// them: `c.buf` of a struct variable c, `h.buf` of an own handle h, the
// same through a borrow bound to what h points at (VarDef::ownalias), or
// through any other plain reference, which answers for what the code has
// seen emptied through it (VarDef::ownborrow). The path crosses no other
// reference and no element, whose storage no variable answers for. `whole`:
// the path is the variable itself.
inline bool TypeCheck::OwnPath(Node *n, VarDef *&base, uint32_t &bits, bool &whole) {
    vector<string_view> names;
    while (auto d = Is<Dot>(n)) {
        names.push_back(d->name);
        n = d->obj;
    }
    auto id = Is<Ident>(n);
    if (!id || !id->vdef) return false;
    auto vd = id->vdef;
    if (vd->ownalias) vd = vd->ownalias;
    if (!vd->owned && !vd->ownborrow) return false;
    // A defer's body runs at the exits: what it empties through a borrow
    // says nothing of the code after the defer.
    if (!vd->owned && deferbodies && Depth(vd) <= DeferDepth()) return false;
    base = vd;
    whole = names.empty();
    bits = 0;
    auto st = OwnPointee(vd->type);
    if (whole) {
        bits = OwnLeafMask(vd->type);
        return true;
    }
    if (!st) return false;
    string path;
    for (auto i = names.size(); i-- > 0;) Append(path, ".", names[i]);
    vector<OwnLeaf> leaves;
    OwnLeaves(st, {}, leaves);
    for (size_t k = 0; k < leaves.size() && k < MAXOWNLEAVES; k++) {
        auto &lp = leaves[k].path;
        // The field itself, or a struct of them.
        if (lp == path || (lp.size() > path.size() && lp.compare(0, path.size(), path) == 0 &&
                           lp[path.size()] == '.'))
            bits |= 1u << k;
    }
    return bits != 0;
}

// An assignment writes over storage holding an own value, which would drop
// what is there: a variable that holds nothing takes a new value, a trusted
// fn vouches for the slot it reuses, and anything else is a replace().
inline bool TypeCheck::OwnAssign(Node *at, const LVal &lv, TypeExpr *target) {
    if (!HoldsOwn(target)) return false;
    ownused = true;
    if (lv.var && lv.var->owned) {
        auto &o = lv.var->own;
        auto mask = OwnLeafMask(lv.var->type);
        auto holds = lv.var->assigned && reachable &&
                     (o.state == OS_HELD || o.state == OS_CLAIMED || o.state == OS_MIXED) &&
                     !(!lv.var->type->oq && mask && o.state == OS_HELD && o.empty == mask);
        if (holds && !InTrusted())
            Error(at, cat(lv.var->name, " still holds its ", TypeStr(lv.var->type),
                          o.state == OS_CLAIMED ? ", which a defer releases when its block ends"
                                                : "",
                          ": an assignment would drop it; consume it first, or exchange it with "
                          "replace(", lv.var->name, ", ...)"));
        if (holds) NoteOwnFill(at);
        return true;
    }
    // A field its variable has seen emptied, with nothing since that could
    // have filled it, takes a value as an empty variable does:
    // `c.buf = pool.resize(take(c.buf), n)`.
    VarDef *base = nullptr;
    uint32_t bits = 0;
    auto whole = false;
    auto as = Is<Assign>(at);
    auto emptied = as && OwnPath(as->lval, base, bits, whole) && bits &&
                   (base->own.empty & bits) == bits;
    if (!emptied && !InTrusted()) {
        auto what = ExprStr(as ? as->lval : at);
        Error(at, cat("assigning to ", what, " would drop the ", TypeStr(target), " it holds: "
                      "take() it first, or exchange it with replace(", what, ", ...), which "
                      "returns the old value to consume"));
    }
    // What else the variable has seen emptied stays so: the store went
    // through it, to these fields.
    auto keep = emptied ? base->own.empty & ~bits : 0;
    NoteOwnFill(at);
    if (emptied) base->own.empty = keep;
    return true;
}

// A call whose callee may leave by a `return ... from` or a block's return
// (FnSpec::needs) exits every function on the way to its target: none may
// hold an unclaimed own value across it.
inline void TypeCheck::CallMayPropagate(Node *at, FnSpec *spec, string_view name) {
    if (!ownused || !reachable || !spec || spec->needs.empty()) return;
    auto base = (int)vars.size();
    for (auto t : spec->needs)
        for (auto i = (int)frames.size() - 1; i >= 1; i--)
            if (!frames[i].isfunval && frames[i].spec == t) {
                base = min(base, frames[i].varbase);
                break;
            }
    // The frame making the call is left too, whatever the target.
    base = min(base, frames[RealFrameIndex()].varbase);
    RequireOwnConsumed(base, at,
                       cat("this call of ", name, ", which may return past here (a `return ... "
                           "from`, or a block's return)"),
                       nullptr);
}

// take(place): the own value a place holds, leaving the place empty (§4.6
// of the design note): an optional reference's null, a slice's empty run,
// each own field of a struct.
inline Val TypeCheck::CheckTake(Call *c, vector<Node *> &args) {
    ownused = true;
    auto lv = CheckLValue(args[0]);
    auto t = lv.var ? lv.var->type : lv.type;
    // A plain reference names the place it points at.
    if (IsPlainRef(t) && !t->oq) {
        DerefLValue(lv, args[0]);
        t = lv.type;
    }
    if (!HoldsOwn(t))
        Error(c, cat("take moves an own value out of the place holding it, and ",
                     ExprStr(args[0]), " is a ", TypeStr(t), ", which holds none"));
    if (!OwnTracked(t))
        Error(c, cat("take moves one own value, and ", ExprStr(args[0]), " is a ", TypeStr(t),
                     ": its elements leave one at a time, by pop"));
    if (!OwnEmptyable(t))
        Error(c, cat("take leaves its place empty, and a ", TypeStr(t), " has no empty value: "
                     "only an optional reference (own T?), a slice (own T[:]) and a struct of "
                     "those do",
                     lv.var && lv.var->owned ? cat("; ", lv.var->name, " is a variable, which "
                                                   "moves by being passed on") : string()));
    VarDef *base = nullptr;
    uint32_t bits = 0;
    auto whole = false;
    auto tracked = OwnPath(args[0], base, bits, whole);
    if (tracked && whole && base == lv.var && base->owned) {
        // The variable's own value: it is left empty, which it then reads as.
        UseOwn(base, args[0]);
        if (base->own.state == OS_CLAIMED)
            Error(c, cat(base->name, " is released by the defer", OwnWhere(base->own.at),
                         ": taking it as well would release it twice"));
        base->own.state = OS_EMPTY;
        base->own.at = args[0];
        base->own.empty = OwnLeafMask(base->type);
    } else if (tracked) {
        if (deferbodies && Depth(base) <= DeferDepth())
            Error(c, cat("a defer claims a whole own variable, not a field of one: take ",
                         ExprStr(args[0]), " into a variable of its own, and release that"));
        if (!base->owned && !lv.writable)
            Error(c, cat("cannot take ", ExprStr(args[0]), " through a non-writable value (const, "
                         "or a read-only instantiation, §9.5): take writes the empty value back"));
        base->own.empty |= bits;
    } else if (!lv.writable) {
        Error(c, cat("cannot take ", ExprStr(args[0]), " through a non-writable value (const, "
                     "or a read-only instantiation, §9.5): take writes the empty value back"));
    }
    args[0]->exprtype = t;
    // A relative link gives the reference it stands for.
    auto vt = IsRelRef(t) ? LoadType(t) : t;
    c->rettypes.push_back(vt);
    Val v;
    v.type = vt;
    if (IsRefOrSlice(t)) {
        // The value points where the place's did.
        auto rv = ContainerRead(lv);
        if (lv.var && IsRefOrSlice(lv.var->type)) rv.SetProv(RefProvOf(lv.var));
        v.SetProv(rv);
    } else {
        v.Set(TempRoot(), false);
        if (HoldsPlainRef(t)) {
            auto rv = lv.var ? Val {} : ContainerRead(lv);
            v.contents = lv.var ? lv.var->contents : rv.contents;
            v.holderset = true;
        }
    }
    return v;
}

// replace(place, v): stores v in a place holding an own value and gives
// back what was there, for the caller to consume.
inline Val TypeCheck::CheckReplace(Call *c, vector<Node *> &args) {
    ownused = true;
    auto lv = CheckLValue(args[0]);
    auto t = lv.var ? lv.var->type : lv.type;
    if (IsPlainRef(t) && !t->oq) {
        DerefLValue(lv, args[0]);
        t = lv.type;
    }
    if (!HoldsOwn(t))
        Error(c, cat("replace exchanges the own value a place holds, and ", ExprStr(args[0]),
                     " is a ", TypeStr(t), ", which holds none: assign it"));
    if (!OwnTracked(t))
        Error(c, cat("replace exchanges one own value, and ", ExprStr(args[0]), " is a ",
                     TypeStr(t), ": its elements enter and leave one at a time, by push and pop"));
    VarDef *base = nullptr;
    uint32_t bits = 0;
    auto whole = false;
    auto tracked = OwnPath(args[0], base, bits, whole);
    if (tracked && whole && base == lv.var && base->owned) {
        UseOwn(base, args[0]);
        if (base->own.state == OS_CLAIMED)
            Error(c, cat(base->name, " is released by the defer", OwnWhere(base->own.at),
                         ": the value replace returns would be released as well"));
        base->own = OwnFlow {};
    } else if (!lv.writable && !(lv.var && lv.var->owned)) {
        Error(c, cat("cannot replace ", ExprStr(args[0]), " through a non-writable value "
                     "(const, or a read-only instantiation, §9.5)"));
    }
    args[0]->exprtype = t;
    auto vt = IsRelRef(t) ? LoadType(t) : t;
    Val old;
    old.type = vt;
    if (IsRefOrSlice(t)) {
        auto rv = ContainerRead(lv);
        if (lv.var && IsRefOrSlice(lv.var->type)) rv.SetProv(RefProvOf(lv.var));
        old.SetProv(rv);
    } else {
        old.Set(TempRoot(), false);
        if (HoldsPlainRef(t)) {
            auto rv = lv.var ? Val {} : ContainerRead(lv);
            old.contents = lv.var ? lv.var->contents : rv.contents;
            old.holderset = true;
        }
    }
    Val nv;
    {
        // The new value is stored where the old one lay.
        DestScope ds(*this, Dest(lv, lv.var && IsRefOrSlice(t), lv.reached));
        SlotScope ss(*this, true);
        nv = CheckValue(args[1], t);
    }
    // A variable now points where either value did.
    if (lv.var && IsRefOrSlice(t) && !lv.var->isglobal) AddBinding(lv.var, nv);
    auto keep = tracked ? base->own.empty & ~bits : 0;
    NoteOwnFill(c);
    if (tracked) base->own.empty = keep;
    c->rettypes.push_back(vt);
    return old;
}

}  // namespace goose
