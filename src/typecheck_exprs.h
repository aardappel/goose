// Goose compiler — the typechecker's expressions (definitions of TypeCheck
// members, typecheck.h): lvalue paths, values with reference transparency
// (§3.8) and the implicit conversions (§6.3, §3.10), the store rule (§9.2),
// constant folding and operand unification (§6.1), and struct and variant
// literals (§4.2).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Lvalue paths: names, fields, elements, optionally through references.

inline TypeCheck::LVal TypeCheck::CheckLValue(Node *n) {
    NodeScope ns(*this, n);
    // The value the path denotes, recorded as its Check would (Ident::Check,
    // ContainerRead): what the operands evaluated before a later point of
    // the statement hold (HeldOperands).
    auto record = [&](const LVal &lv) {
        Val v;
        v.type = LoadType(lv.type);
        v.SetProv(lv);
        RecordVal(n, v);
    };
    if (auto id = Is<Ident>(n)) {
        auto vd = LookupVar(id->name, id->ns, n);
        if (!vd) {
            if (auto kind = ScopeNameKind(id->name))
                Error(n, cat(id->name, " names a ", kind, ", not a variable"));
            Error(n, cat("unknown variable: ", id->name));
        }
        // A local's first assignment may construct it, but a global is
        // constructed only by its own initializer (§11.1): before that has
        // run, no path may start at it, whether it reads or writes.
        if (vd->isglobal) RequireAssigned(vd, n);
        id->vdef = vd;
        LVal lv;
        lv.type = vd->narrowed ? vd->narrowed : vd->type;
        lv.var = vd;
        lv.Set(vd, true);
        lv.byteview = vd->contentbyteview;
        // Contents are writable unless the type says const or the binding
        // is a copy (§9.5). Whether the variable itself is assigned is up
        // to its binding alone (`let`, a copy: WholeWritable, §4.4).
        lv.writable = !vd->copybind && !(vd->type && vd->type->cq);
        lv.letbound = !vd->isvar;
        lv.letname = vd->name;
        lv.copyof = vd->copybind ? vd : nullptr;
        lv.reusable = vd->reusable;
        n->exprtype = vd->type;
        if (IsRefOrSlice(lv.type)) {
            Val v;
            v.type = LoadType(lv.type);
            v.SetProv(RefProvOf(vd));
            RecordVal(n, v);
        } else {
            record(lv);
        }
        return lv;
    }
    if (auto d = Is<Dot>(n)) {
        auto lv = LValueBase(d->obj);
        DerefLValue(lv, d->obj);
        ResolveMemberLValue(lv, d);
        n->exprtype = lv.type;
        RecordVal(n, ContainerRead(lv));
        return lv;
    }
    if (auto ix = Is<Index>(n)) {
        auto lv = LValueBase(ix->obj);
        DerefLValue(lv, ix->obj);
        SliceProvenance(lv, ix->obj);
        TypeExpr *elem;
        if (lv.type->kind == TY_SLICE) {
            elem = lv.type->sub;
        } else {
            if (lv.type->kind != TY_ARRAY)
                Error(n, cat("cannot index a value of type ", TypeStr(lv.type)));
            RequireComplete(lv.type, n->line);
            elem = lv.type->arr->sub;
        }
        if (ClassOf(elem) != SC_FIXED)
            Error(n, cat(lv.type->kind == TY_SLICE ? "slices" : "arrays",
                         " of variable-size elements cannot be indexed, only iterated; for "
                         "random access, keep your own array of references to them (§3.3)"));
        CheckIntAny(ix->idx);
        if (lv.type->cq) lv.writable = false;   // An element of a const value.
        lv.letbound = false;
        lv.type = elem;
        lv.var = nullptr;
        lv.fromstorage = true;
        lv.isslot = true;
        lv.isvarint = elem->kind == TY_INT && elem->intstorage == IS_VARINT;
        n->exprtype = lv.type;
        RecordVal(n, ContainerRead(lv));
        return lv;
    }
    Error(n, "not an assignable location");
}

// The base of a path: itself a path, or any other expression (a call
// result, a string literal, ...) whose value is then addressed. A null
// root means static data; a temporary has a root of its own (TempRoot).
// `cmpview`: the path is the slice `==` compares an operand as (§4.5),
// which ends with the comparison, so it may view any temporary.
inline TypeCheck::LVal TypeCheck::LValueBase(Node *n, bool cmpview) {
    if (Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n)) return CheckLValue(n);
    auto v = CheckV(n, nullptr);
    NoUntypedEmptyArray(v, n, "an index, slice or field of it");
    if (!cmpview) NoTemporaryLiteral(n, v.type);
    n->exprtype = v.type;
    LVal lv;
    lv.type = v.type;
    lv.SetProv(v);
    // A value result is materialized in its own temporary storage. References
    // and slices instead retain the (possibly inexact) owner they borrow.
    lv.intemp = TempContents(v, lv.contents);
    if (lv.intemp) lv.Set(v.Root(), true);
    return lv;
}

// An array literal of variable-size elements is a T[] (§3.3), and such a
// value comes into existence only where a construction context builds it
// (§4.2). A statement temporary would have to hold it for a slice or
// reference to view, where a fixed one is a plain C temporary.
inline void TypeCheck::NoTemporaryLiteral(Node *n, TypeExpr *t) {
    if (!Is<ArrayLit>(n) || ClassOf(t) == SC_FIXED) return;
    Error(n, cat("an array literal of variable-size elements is a ", TypeStr(t),
                 ", built only in a construction context (§4.2), never as a temporary "
                 "for a slice or reference to view; bind it to a variable first"));
}

// Crossing a reference in a path (auto-deref, §3.8): the storage owner
// becomes the reference's root, writability its provenance, and the rest of
// the path lies in its pointee (Prov::reached).
inline void TypeCheck::DerefLValue(LVal &lv, Node *at) {
    if (lv.type->kind != TY_REF) return;
    if (lv.type->ref->optional)
        Error(at, "optional value must be narrowed (if/guard/assert) before use");
    // Reading a reference variable requires it to have a value.
    if (lv.var) {
        RequireAssigned(lv.var, at);
        lv.SetProv(RefProvOf(lv.var));
    } else if (lv.fromstorage) {
        // Crossing a reference the path read out of a container: it is a
        // read-back, so where it points is re-derived, and it is as writable
        // as its slot says rather than the path to the slot (§9.5). A
        // relative one loads as an ordinary reference into the same pool,
        // so it keeps the container's root.
        ReadBackLVal(lv);
    }
    lv.type = lv.type->ref->sub;
    lv.reached = LoadType(lv.type);
    lv.var = nullptr;
    lv.letbound = false;
    lv.throughref = true;
    // The pointee may be a whole grow-shrink array, or a variable holding a
    // view into one (RootAlt::slotread), and is none of the views a class's
    // storage holds (RootAlt::classread).
    lv.isslot = false;
    lv.ClearReads();
    if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT) lv.isvarint = true;
}

// A global is unassigned until the driver reaches its declaration, which
// is when its initializer runs (§11.1).
inline void TypeCheck::RequireAssigned(VarDef *vd, Node *at) {
    if (vd->assigned) return;
    auto order = vd->isglobal
                     ? cat(": globals initialize in declaration order, imported files first "
                           "(§11.1), so ", vd->name, " has no value until its initializer at ",
                           Where(vd->line), " has run")
                     : string();
    Error(at, cat("variable ", vd->name, " may be used before it is assigned", order));
}

// Accessing through a slice variable: writes and roots follow the slice
// value's provenance, not the variable's own var-ness. What is indexed or
// sliced lies in the elements it views (Prov::reached).
inline void TypeCheck::SliceProvenance(LVal &lv, Node *at) {
    if (lv.type->kind != TY_SLICE) return;
    if (!lv.var) {
        if (lv.fromstorage) ReadBackLVal(lv);
        else if (lv.throughref) lv.SetProv(SlotView(lv, lv.type));
        lv.reached = lv.type->sub;
        return;
    }
    RequireAssigned(lv.var, at);
    lv.SetProv(RefProvOf(lv.var));
    lv.reached = lv.type->sub;
    lv.var = nullptr;
}

// A location whose own type is a reference or slice: the value loaded out
// of it is a read-back, so its root is re-derived (§9.5). Out of a field or
// an element it is as writable as the slot says (SlotLoadWritable); a slice
// loaded through a reference is no more writable than the reference.
inline void TypeCheck::ReadBackLVal(LVal &lv) {
    if (!IsRefOrSlice(lv.type)) return;
    // A byte view can point at any typed storage. Its owner cannot be
    // recovered by enumerating u8 containers. Global slots may have been
    // filled by functions whose stores have not yet been checked. That
    // matters to a grow-only array's shrink (§5.1), whose byte views are
    // stored like any other view; a grow-shrink array's shrink passes over
    // a slot read, byte view or not (§5.2).
    lv.byteview = lv.byteview || lv.Any([&](const RootAlt &a) {
        return a.root && (a.root->contentbyteview ||
                          (a.root->isglobal && lv.type->cq && IsU8(PointeeOf(lv.type))));
    });
    // What was stored into a slot passed the store rule (§5.2) with its own
    // provenance; the container's says nothing about it.
    auto slotread = lv.isslot && SlotReadable(lv.type);
    auto rb = ReadBackRoot(lv.type, lv, lv.byteview, lv.intemp ? &lv.contents : nullptr,
                           slotread, true);
    lv.TakeAlts(rb);
    for (auto &a : lv.alts) a.slotread = slotread;
    lv.freshview = false;
    lv.reached = nullptr;
    lv.intemp = false;
    lv.writable = lv.isslot ? SlotLoadWritable(lv.type, lv.writable)
                            : lv.writable && !lv.type->cq;
}

// How writable a reference or slice loaded out of a field or an element of
// type t is, the path to the slot being as writable as `path` (§9.5): as the
// slot's type says, whatever the path, since a read-only one is stored only
// in a `const` slot, and `const` is shallow. But a self-relative reference
// points within the value or array its slot lies in (§3.9), which is no more
// writable through it than along the path.
inline bool TypeCheck::SlotLoadWritable(TypeExpr *t, bool path) {
    auto selfrel = t->kind == TY_REF && t->ref->lenstorage >= 0 && !t->ref->pool;
    return !t->cq && (path || !selfrel);
}

// The value a field or element location yields: the load, which for a
// reference or slice is a read-back (ReadBackLVal).
inline Val TypeCheck::ContainerRead(LVal lv) {
    Val v;
    if (lv.type->kind == TY_SLICE) {
        v.slot = lv;
        v.hasslot = true;
    }
    ReadBackLVal(lv);
    v.type = LoadType(lv.type);
    v.SetProv(lv);
    if (!IsRefOrSlice(v.type) && HoldsPlainRef(v.type)) {
        // What a holder read out of a container points at is bounded by
        // the container: everything stored into it had to outlive it. Out
        // of a temporary, it points where the temporary's contents do. Out
        // of a field or an element, every reference it holds was stored
        // there, so none points into a grow-shrink array (RootAlt::slotread).
        if (lv.intemp) {
            v.contents = lv.contents.roots;
        } else {
            v.contents = Bounds(lv);
            for (auto &a : v.contents.alts) a.slotread = lv.isslot;
        }
        v.holderset = true;
        v.holderfrom = lv.intemp ? lv.contents.from : HolderSource(lv);
        MarkClassCopy(v);
    }
    // The contents of a slot of a const type are read-only, as a variable's
    // are (§9.5), where its value is bound by reference, taken as a slice or
    // grown; the slot itself is assigned as a whole along its path (§4.4).
    if (!IsRefOrSlice(lv.type) && lv.type->cq) v.writable = false;
    v.lvalue = v.type->kind != TY_REF;
    // A varint is written only at construction (§3.6): a reference to one,
    // which binds as a varint& (StorageType), is read-only, as `&` makes it.
    if (IsVarintT(lv.type)) {
        v.isvarint = true;
        v.writable = false;
    }
    return v;
}

// The operands of n, in evaluation order (§2), each with how n consumes it
// once its later operands have run. A call consumes its receiver and
// arguments at the call; print, str and format render each argument just
// before the next runs (EmitFormatInto, EmitStr), so an earlier argument is
// nothing afterwards, and only the one being rendered is held (HeldOperands).
// A condition and a scrutinee are read before the construct's parts run, so
// they hold nothing over them: a match binder is a variable in scope, which
// the scans see. A `for` walks its sequence in place, so that is held while
// the body runs (HoldForSequence).
template<typename F> void TypeCheck::ForOperands(Node *n, F f) {
    if (auto c = Is<Call>(n)) {
        auto builtin = c->builtin >= 0;
        auto member = builtin && (BuiltinByKind(c->builtin).flags & BF_MEMBER);
        auto render = builtin && (c->builtin == B_PRINT || c->builtin == B_STR ||
                                  c->builtin == B_FORMAT);
        auto first = true;
        auto kind = [&]() {
            auto k = member && first ? HK_RECEIVER : render ? HK_NONE : HK_VALUE;
            first = false;
            return k;
        };
        if (auto d = Is<Dot>(c->callee)) f(d->obj, kind());
        for (auto a : c->args) f(a, kind());
        return;
    }
    if (auto ix = Is<Index>(n)) {
        f(ix->obj, HK_ELEMS);
        f(ix->idx, HK_VALUE);
        return;
    }
    if (auto se = Is<SliceExpr>(n)) {
        f(se->obj, HK_ELEMS);
        if (se->lo) f(se->lo, HK_VALUE);
        if (se->hi) f(se->hi, HK_VALUE);
        return;
    }
    if (auto b = Is<Binary>(n)) {
        f(b->left, HK_VIEW);
        f(b->right, HK_VALUE);
        return;
    }
    if (auto a = Is<Assign>(n)) {
        f(a->lval, HK_LOCATION);
        f(a->rhs, HK_VALUE);
        return;
    }
    if (auto al = Is<ArrayLit>(n)) {
        for (auto e : al->elems) f(e, HK_VALUE);
        if (al->fillval) f(al->fillval, HK_VALUE);
        if (al->fillcount) f(al->fillcount, HK_VALUE);
        if (al->capexpr) f(al->capexpr, HK_VALUE);
        return;
    }
    if (auto sl = Is<StructLit>(n)) {
        for (auto &fi : sl->inits) f(fi.val, HK_VALUE);
        return;
    }
    if (auto r = Is<Return>(n)) {
        for (auto v : r->vals) f(v, HK_VALUE);
        return;
    }
    if (auto vd = Is<VarDecl>(n)) {
        for (auto i : vd->inits) f(i, HK_VALUE);
        return;
    }
    if (Is<Unary>(n) || Is<AsCast>(n) || Is<Dot>(n) || Is<IncDec>(n) || Is<Break>(n)) {
        n->Children([&](Node *ch) { f(ch, HK_VALUE); });
        return;
    }
    // A range's bounds, like a count, are read once, before the first
    // iteration: they hold nothing.
    if (auto fl = Is<ForLoop>(n)) {
        if (!Is<RangeExpr>(fl->iter)) f(fl->iter, HK_SEQUENCE);
        return;
    }
    // Conditions, scrutinees, blocks and bodies: nothing held.
}

inline int TypeCheck::OperandIndex(Node *parent, Node *child) {
    auto idx = -1, i = 0;
    ForOperands(parent, [&](Node *ch, HoldKind) {
        if (ch == child) idx = i;
        i++;
    });
    return idx;
}

// Enters n, the operand its parent is at, or a node checked on its own (a
// statement, a path the checker walks itself); the same node entered again
// on the way, as a path is by its node's Check, stays one entry.
inline bool TypeCheck::Descend(Node *n) {
    if (!cur.nodepath.empty() && cur.nodepath.back().node == n) return false;
    auto idx = cur.nodepath.empty() ? -1 : OperandIndex(cur.nodepath.back().node, n);
    if (idx >= 0) cur.nodepath.back().pos = idx;
    cur.nodepath.push_back({ n, idx, 0, false, (int)frames.size() - 1 });
    return true;
}

// Leaves the innermost node: as an operand it has been evaluated, so its
// parent stands past it. An error's unwinding leaves a path a body's check
// replaced (CheckSpecBody) as it is.
inline void TypeCheck::Ascend(Node *n) {
    if (cur.nodepath.empty() || cur.nodepath.back().node != n) return;
    auto e = cur.nodepath.back();
    cur.nodepath.pop_back();
    if (e.idx >= 0 && !cur.nodepath.empty()) cur.nodepath.back().pos = e.idx + 1;
}

// What the operand n of `parent`, whose value is v, holds live: its value
// where it is a reference or slice; an array's elements where the parent
// views them (HK_VIEW), or indexes or slices them (HK_ELEMS, the elements the
// path leads to, a temporary's exactly); the references a holder holds,
// each as a reference to its pointee; a builtin's receiver as a reference
// to the array it is, or its elements where the builtin reads them
// (to_bytes, a slice); an assignment's location as recorded (CheckAssign).
template<typename F>
void TypeCheck::HoldAs(Node *n, const Val &v, HoldKind kind, Node *parent, const char *render,
                       F f) {
    if (!v.type || kind == HK_NONE) return;
    auto t = v.type;
    auto hold = [&](Val hv, bool location = false, bool elems = false) {
        f(Held { n, std::move(hv), location, render, elems });
    };
    switch (kind) {
        case HK_LOCATION:
            hold(v, true);
            return;
        case HK_ELEMS: {
            auto d = IsPlainRef(t) ? DecayRef(v) : v;
            TypeExpr *elem = d.type->kind == TY_ARRAY ? d.type->arr->sub
                             : d.type->kind == TY_SLICE ? d.type->sub : nullptr;
            if (!elem) return;
            if (IsTemp(d.Root())) d.Set(d.Root(), true);
            d.type = ast.SliceOf(elem, n->line);
            hold(d, false, true);
            return;
        }
        case HK_RECEIVER: {
            auto c = (Call *)parent;
            auto rt = IsPlainRef(t) ? t->ref->sub : t;
            TypeExpr *elem = rt->kind == TY_ARRAY ? rt->arr->sub
                             : rt->kind == TY_SLICE ? rt->sub : nullptr;
            auto held = v;
            auto elems = c->builtin == B_TO_BYTES || rt->kind == TY_SLICE;
            if (elems) {
                if (!elem) return;
                held.type = ast.SliceOf(elem, n->line);
            } else if (held.type->kind != TY_REF) {
                held.type = ast.RefTo(rt, n->line);
            }
            if (IsTemp(held.Root()) && t->kind != TY_REF && t->kind != TY_SLICE)
                held.Set(held.Root(), true);
            hold(held, false, elems);
            return;
        }
        case HK_SEQUENCE:
            HoldForSequence((ForLoop *)parent, v, f);
            return;
        default: break;
    }
    // HK_VALUE and HK_VIEW: values constructed in argument, literal and
    // result slots own their copies; references, slices and reference fields
    // still retain borrows, and an array operand viewed as a sequence its
    // elements until the operation ends.
    if (IsRefOrSlice(t)) {
        hold(v);
        return;
    }
    if (kind == HK_VIEW && t->kind == TY_ARRAY && ClassOf(t) != SC_FIXED) {
        auto view = v;
        view.type = ast.SliceOf(t->arr->sub, n->line);
        hold(view, false, true);
    }
    if (HoldsPlainRef(t)) {
        vector<TypeExpr *> pointees;
        RefPointees(t, pointees);
        auto held = v;
        held.TakeAlts(ContentsOf(v));
        for (auto pt : pointees) {
            held.type = ast.RefTo(pt, n->line);
            hold(held);
        }
    }
}

// What a `for` over an array or slice, whose sequence has the value v, reads
// on every iteration while its body runs (§6.5). Codegen spells the path to
// the sequence into the loop (ForLoop::CgStmt, GenLoc): each reference or
// slice the path loads out of a field or element is loaded again, from where
// it lies, and the sequence is walked in place, as a view of its elements --
// unless it is a variable, which the loop names and the scans see, or a
// resizable array, whose length the loop reads again and whose elements
// never move. A reference to a slice is held as it is: the slice it refers
// to is read again too (HeldRefsMayPointInto).
template<typename F> void TypeCheck::HoldForSequence(ForLoop *fl, const Val &v, F f) {
    if (fl->iterkind != IK_ARRAY && fl->iterkind != IK_SLICE) return;
    auto hold = [&](Node *n, Val hv, bool reread) {
        // A temporary is storage of its own, which nothing names to shrink.
        for (auto &a : hv.alts)
            if (IsTemp(a.root)) a.exact = true;
        f(Held { .node = n, .v = std::move(hv), .loop = fl, .reread = reread });
    };
    // `&` of a path is that path.
    auto unwrap = [](Node *n) {
        auto u = Is<Unary>(n);
        if (u && u->op == T_BITAND && (Is<Ident>(u->child) || Is<Dot>(u->child) ||
                                       Is<Index>(u->child)) &&
            u->child->exprtype && u->child->exprtype->kind != TY_REF)
            return u->child;
        return n;
    };
    auto path = unwrap(fl->iter);
    if (!Is<Ident>(path)) {
        auto seq = v;
        if (IsPlainRef(seq.type) && seq.type->ref->sub->kind != TY_SLICE) seq = DecayRef(seq);
        auto st = seq.type;
        if (st->kind != TY_ARRAY) {
            hold(fl->iter, seq, false);
        } else if (ClassOf(st) != SC_RESIZABLE) {
            seq.type = ast.SliceOf(st->arr->sub, fl->iter->line);
            hold(fl->iter, seq, false);
        }
    }
    for (auto n = path;;) {
        Node *obj;
        if (auto d = Is<Dot>(n); d && !d->variantconst) obj = d->obj;
        else if (auto ix = Is<Index>(n)) obj = ix->obj;
        else break;
        // The slot lies in what obj is, or leads to.
        auto it = nodevals.find(obj);
        if (n->exprtype && IsRefOrSlice(n->exprtype) && it != nodevals.end()) {
            auto slot = DecayRef(it->second);
            slot.byteview = false;
            slot.type = ast.SliceOf(LoadType(n->exprtype), n->line);
            hold(n, slot, true);
        }
        n = unwrap(obj);
    }
}

// Every value evaluated before the point being checked that is still live:
// the operands each node on the path has evaluated so far (its parent
// consumes them after this point), as their parents hold them. A call at
// the point itself, applying its callee's effects, has consumed its own
// operands: what the callee does to them its parameters' pairs judge
// (§5.1, NoteLiveViews). The exception is the argument print, str or format
// is rendering (`cur.renderarg`): its views, where a struct, enum or array
// argument lies (`cur.renderwhere`), and the parts the rendering is walking
// in place (`cur.renderwalks`), which an overload of a part runs in the
// middle of rendering.
template<typename F> void TypeCheck::HeldOperands(F f) {
    for (auto &e : cur.nodepath) {
        auto own = e.discovering || (&e == &cur.nodepath.back() && Is<Call>(e.node));
        auto i = 0;
        ForOperands(e.node, [&](Node *ch, HoldKind kind) {
            auto at = i++;
            if (at >= e.pos) return;
            auto it = nodevals.find(ch);
            if (it == nodevals.end()) return;
            if (ch == cur.renderarg && cur.rendering) {
                HoldAs(ch, it->second, HK_VIEW, e.node, cur.rendering, f);
                if (ch == cur.renderwhere) {
                    auto where = it->second;
                    where.type = ast.RefTo(where.type, ch->line);
                    f(Held { ch, std::move(where), false, cur.rendering });
                }
                for (auto &w : cur.renderwalks) {
                    auto walked = w;
                    walked.type = ast.RefTo(w.type, ch->line);
                    f(Held { .node = ch, .v = std::move(walked), .render = cur.rendering,
                             .inplace = true });
                }
                return;
            }
            if (own) return;
            HoldAs(ch, it->second, kind, e.node, nullptr, f);
        });
    }
}

// Every part of the statement that runs after the point being checked: the
// operands each node on the path has not evaluated yet, the one being
// checked excluded, and where the point is in the head of a construct, the
// parts the head decides between. Each comes with the frame it is checked in.
template<typename F> void TypeCheck::LaterOperands(F f) {
    for (size_t k = 0; k < cur.nodepath.size(); k++) {
        auto &e = cur.nodepath[k];
        auto last = k + 1 == cur.nodepath.size();
        auto i = 0;
        ForOperands(e.node, [&](Node *ch, HoldKind) {
            auto at = i++;
            if (at > e.pos || (last && at == e.pos)) f(ch, e.frame);
        });
        if (!last) AfterHead(e.node, cur.nodepath[k + 1].node, [&](Node *p) { f(p, e.frame); });
    }
}

// What construct n runs after its head -- an if's condition, a match's
// scrutinee, a for's sequence, a while's condition -- where `next` is the
// node being checked inside it: while that is the head, every part the head
// leads to; while it is one of those parts, nothing more of n, since the
// other branches do not run.
template<typename F> void TypeCheck::AfterHead(Node *n, Node *next, F f) {
    if (auto fi = Is<IfExpr>(n)) {
        if (next != fi->cond) return;
        f(fi->thenb);
        if (fi->elseb) f(fi->elseb);
    } else if (auto m = Is<MatchExpr>(n)) {
        if (next != m->scrutinee) return;
        for (auto &arm : m->arms) f(arm.body);
    } else if (auto fl = Is<ForLoop>(n)) {
        auto r = Is<RangeExpr>(fl->iter);
        if (r && next == r->lo) f(r->hi);
        if (next == fl->iter || (r && (next == r->lo || next == r->hi))) f(fl->body);
    } else if (auto w = Is<While>(n)) {
        if (next == w->cond) f(w->body);
    }
}

// The root bounding the references inside a holder value: what was
// derived for it, else the value's own root (a temporary's outlives nothing).
inline VarDef *TypeCheck::HolderRootOf(const Val &v) { return ContentsOf(v).Root(); }

// Field / builtin-property resolution on an lvalue path.
inline void TypeCheck::ResolveMemberLValue(LVal &lv, Dot *d) {
    auto t = lv.type;
    // Steps the path into the named field of a field run; a frame
    // object's resizable tail has a header of its own to address (C.2).
    auto step = [&](const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                    bool inframe) {
        for (auto i = 0; i < (int)fields.size(); i++) {
            auto &f = fields[i];
            if (f.ispad || f.name != d->name) continue;
            d->fieldidx = i;
            if (lv.type->cq) lv.writable = false;   // A field of a const value.
            lv.letbound = f.isconst;
            lv.letname = f.name;
            lv.type = ftypes[i];
            lv.var = nullptr;
            lv.fromstorage = true;
            lv.isslot = true;
            lv.fotail = inframe && ClassOf(lv.type) == SC_RESIZABLE;
            lv.isvarint = lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT;
            return true;
        }
        return false;
    };
    if (t->kind == TY_STRUCT) {
        auto inst = GetStructInst(t);
        if (step(inst->st->fields, inst->ftypes, FieldsInFrame(d))) return;
        Error(d, cat("struct ", inst->st->name, " has no field ", d->name));
    }
    if (t->kind == TY_VARIANT) {
        auto inst = GetEnumInst(t->var->adt);
        auto vi = inst->en->VariantIndex(t->var->variant);
        if (step(t->var->variant->fields, inst->vftypes[vi], false)) return;
        Error(d, cat("variant ", inst->en->name, ".", t->var->variant->name,
                     " has no field ", d->name));
    }
    Error(d, cat("no field access on a value of type ", TypeStr(t)));
}

// ------------------------------------------------------------------
// Values: the per-node dispatch plus the implicit-conversion rules.

// References are transparent: load the pointee unless the destination
// wants the reference itself. Optionals never decay (narrow first).
inline Val TypeCheck::DecayRef(Val v) {
    if (!IsPlainRef(v.type)) return v;
    Val r;
    r.type = LoadType(v.type->ref->sub);
    r.pointee = true;
    // A slice is the one its slot holds, as writable as that is, and the
    // slot is where the reference points (Val::slot). Any other pointee
    // lies where the reference points, as writable as the reference, as a
    // path crossing it is (DerefLValue, §9.5): a format hook taking it by
    // reference is handed it there (§3.7), and a slice a construct joins an
    // array into views it there (§6.4). But a varint, written only at
    // construction (§3.6), loads as the i64 it decodes to, which no hook
    // may write. A compound pointee value keeps the container info,
    // harmless, though crossing the reference drops where the reference was
    // read out of (RootAlt::slotread, classread). A holder holds what was
    // stored where the reference points, as one read out of a container
    // does (ContainerRead).
    if (r.type->kind == TY_SLICE) {
        auto sv = SlotView(v, r.type);
        r.TakeAlts(sv);
        r.writable = sv.writable;
        r.byteview = sv.byteview;
        r.freshview = sv.freshview;
        r.slot = v;
        r.hasslot = true;
    } else {
        r.TakeAlts(v);
        r.ClearReads();
        r.writable = v.writable && !IsVarintT(v.type->ref->sub);
        r.byteview = v.byteview && HoldsPlainRef(r.type);
        if (HoldsPlainRef(r.type)) {
            r.contents = Bounds(r);
            r.holderset = true;
            r.holderfrom = HolderSource(r);
            MarkClassCopy(r);
        }
    }
    r.isvarint = IsVarintT(v.type->ref->sub);
    return r;
}

// Does dt consume a reference value as-is (so no decay before fitting)?
inline bool TypeCheck::KeepsRef(const Val &v, TypeExpr *dt) {
    if (!IsPlainRef(v.type)) return true;  // Nothing to decay.
    if (dt->kind == TY_REF) return true;   // Binding (plain/optional/relative).
    // A whole (pointee) array meeting a slice of its element type (§3.10).
    if (dt->kind == TY_SLICE && v.type->ref->sub->kind == TY_ARRAY) return true;
    return false;
}

// An lvalue of a reference destination's pointee type binds by reference
// (§4.1): the node becomes `&node`, as if written, so every later pass
// sees an ordinary reference argument. `writes`: something may write
// through the reference, which is not so of an identity comparison's,
// index_of's, or one bound to a `const T&`.
inline Node *TypeCheck::AutoRef(Node *n, Val &v, bool writes) {
    if (!Referenceable(n, v)) NoResizableRef(n);
    if (auto id = Is<Ident>(n); id && id->vdef && writes && v.writable)
        NoteWritableRef(id->vdef, n);
    auto u = ast.New<Unary>(n->line, T_BITAND, n);
    u->synth = true;
    SlotRoots(v);
    v.type = ast.RefTo(ast.PlainOf(StorageType(v)), n->line);
    v.type->cq = !v.writable;   // The reference carries a const value's qualifier.
    v.lvalue = false;
    u->exprtype = v.type;
    return u;
}

// A slice lvalue bound by reference: the reference names the slot, so it is
// rooted where the slot lies, as `&` of it is (§3.8), not where the slice
// points (Val::slot), and is as writable as `&` of it: a callee re-points no
// slot of a by-value binder, of a `const` value or of one reached through a
// `const T&` (§9.5). A field's or an element's slice is writable unless its
// type says `const`, which a load through the reference sees (SlotView), so
// there the path alone says.
inline void TypeCheck::SlotRoots(Val &v) {
    if (!v.hasslot || v.type->kind != TY_SLICE) return;
    v.held = v;
    v.hasheld = true;
    v.TakeAlts(v.slot);
    v.writable = v.slot.writable;
    v.hasslot = false;
    for (auto &a : v.alts)
        if (auto sv = SliceVarOf(a.root)) sv->slotref = true;
}

// The type of the storage lvalue v denotes, or each branch of a construct v
// does (Val::storagebranches), which a reference to it points at: its
// value's, but for varint storage, whose value is the i64 it decodes to
// (§3.6).
inline TypeExpr *TypeCheck::StorageType(const Val &v) {
    return (v.lvalue || v.storagebranches) && v.isvarint ? ast.inttypes[IS_VARINT] : v.type;
}

inline bool TypeCheck::BindsRef(const Val &v, TypeExpr *dt) {
    return dt->kind == TY_REF && v.lvalue && v.type->kind != TY_REF && !v.isnull &&
           TypeEq(StorageType(v), dt->ref->sub);
}

inline bool TypeCheck::IsNonFixedLValue(const Val &v) {
    return v.lvalue && !IsRefOrSlice(v.type) && ClassOf(v.type) != SC_FIXED;
}

// A reference to a non-fixed value denotes a non-fixed lvalue (§3.8). A
// varint pointee loads as the i64 it decodes to, a fixed-size value.
inline bool TypeCheck::IsNonFixedRef(const Val &v) {
    return IsPlainRef(v.type) && ClassOf(LoadType(v.type->ref->sub)) != SC_FIXED;
}

// Whether a resizable-valued path has a header of its own to reference
// (C.2): a variable, or the tail of a frame object.
inline bool TypeCheck::Referenceable(Node *n, const Val &v) {
    if (ClassOf(v.type) != SC_RESIZABLE) return true;
    if (Is<Ident>(n)) return true;
    auto d = Is<Dot>(n);
    return d && FieldsInFrame(d);
}

// Whether the struct d steps into is a frame object held as one (C.2): a
// variable, a reference's pointee, a temporary, or the tail of a frame
// object held so in turn. As the tail of a value that is not a frame object
// it is bytes of that value, and its own tail has no header either.
inline bool TypeCheck::FieldsInFrame(Dot *d) {
    auto ot = d->obj->exprtype;
    if (!ot) return false;
    auto pointee = ot->kind == TY_REF;
    if (pointee) ot = ot->ref->sub;
    if (ot->kind != TY_STRUCT || !GetStructInst(ot)->frameobj) return false;
    auto od = Is<Dot>(d->obj);
    return pointee || !od || FieldsInFrame(od);
}

// A reference to a resizable value is a reference to its header (C.2), and
// only a variable and a frame object's tail have one of their own.
[[noreturn]] inline void TypeCheck::NoResizableRef(Node *at) {
    Error(at, "cannot reference a resizable value nested in a variable-size prefix "
              "or an ADT payload; reference the owning variable instead");
}

inline bool TypeCheck::UserRefOf(Node *n) {
    auto u = Is<Unary>(n);
    return u && u->op == T_BITAND && !u->synth;
}

// A non-fixed value reaches a value destination only as an rvalue or an
// explicit copy (§4.1): an lvalue, or a reference to one, is never copied
// implicitly. A function's own local is moved by `return`, or as its body's
// final expression.
inline bool TypeCheck::ImplicitCopy(const Val &v, Node *n, TypeExpr *dt) {
    if (!reachable) return false;
    if (IsRefOrSlice(dt) || dt->kind == TY_VOID) return false;
    if (ClassOf(dt) == SC_FIXED) return false;
    auto src = IsPlainRef(v.type) ? v.type->ref->sub : v.type;
    // A varint is read as the i64 it decodes to (§3.6), like any scalar.
    if (ClassOf(src) == SC_FIXED || IsVarintT(src)) return false;
    if (!v.lvalue && !IsPlainRef(v.type)) return false;
    if (inreturn && v.lvalue && IsOwnLocal(n)) return false;
    return true;
}

inline void TypeCheck::ImplicitCopyError(Node *n) {
    auto u = Is<Unary>(n);
    auto what = ExprStr(u && u->op == T_BITAND ? u->child : n);
    Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1): pass copy(",
                 what, ") for a copy, or bind it by reference"));
}

// A branch `&x` of a fixed-size x whose construct copies it (§4.1).
inline void TypeCheck::RefCopyWarning(Node *n) {
    auto what = ExprStr(Is<Unary>(n)->child);
    Warn(n, cat("redundant &: the construct's value is a copy of ", what, " either way (§4.1); "
                "a reference-typed binding binds ", what, " without it"));
}

// A branch's value v whose construct has no destination type: the
// construct's value is a copy of it, of type dt, which non-fixed storage, or
// a reference to it, reaches only through copy(x), as at a value destination
// (§4.1). On an argument's path (argpath) a reference parameter may yet bind
// the branch by reference instead, where it is storage of any size class or
// a reference, and on a construct's first check of its branches (joinpath)
// they may yet join as a slice; `out`, the value the branch gives its
// construct, notes both for that argument or construct.
inline void TypeCheck::CheckBranchCopy(const Val &v, Node *n, TypeExpr *dt, Val &out) {
    out.storagebranches = v.storagebranches || (v.lvalue && Referenceable(n, v)) ||
                          IsPlainRef(v.type);
    out.implicitcopy = v.implicitcopy;
    out.refcopies = v.refcopies;
    if (!ImplicitCopy(v, n, dt)) return;
    if (argpath != n && joinpath != n) ImplicitCopyError(n);
    if (!out.implicitcopy) out.implicitcopy = n;
}

// A local of the function being checked, which its return moves (§4.1).
inline bool TypeCheck::IsOwnLocal(Node *n) {
    auto id = Is<Ident>(n);
    return id && id->vdef && !id->vdef->isglobal && id->vdef->ownerspec == frames.back().spec;
}

// A value that tspec's result type is inferred from (§7.1) is taken as an
// un-annotated `let` takes its initializer (§3.8, §4.1): an explicit `&x`
// keeps its reference, a reference to a non-fixed value stays that
// reference, and a non-fixed lvalue is returned by reference, except the
// function's own local, which the return moves. Storage that does not
// outlive the function cannot be returned by reference, and a non-fixed
// value in it is not copied implicitly either.
inline Val TypeCheck::CheckInferredResult(Node *&n, FnSpec *tspec) {
    if (auto u = Is<Unary>(n); UserRefOf(n)) {
        auto v = CheckV(n, nullptr);
        n->exprtype = v.type;
        if (IsNonFixedRef(v) && !IsOwnLocal(u->child))
            Warn(n, cat("redundant &: ", ExprStr(u->child),
                        " is returned by reference without it (§4.1)"));
        return v;
    }
    auto v = CheckValue(n, nullptr, false, false, true);
    if (IsUntypedEmptyArray(v.type))
        Error(n, "cannot infer the element type of []: an omitted result type is taken from "
                 "the value returned (§7.1); declare the function's result type");
    if (IsNonFixedLValue(v) && !IsOwnLocal(n)) n = AutoRef(n, v);
    // All of a multi-value call's results are forwarded, as they are.
    if (auto c = Is<Call>(n); c && c->rettypes.size() > 1) return v;
    if (reachable && IsNonFixedRef(v)) {
        auto dies = v.Any([&](const RootAlt &a) {
            return (a.root && a.root->ownerspec == tspec) || IsTemp(a.root);
        });
        if (dies) {
            auto what = ExprStr(n);
            Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1), and "
                         "its storage does not outlive this function: return copy(", what,
                         ")"));
        }
    }
    return v;
}

// The whole of array-valued n as a slice: `n[..]`, synthesized for an
// equality between array kinds (§4.5).
inline Node *TypeCheck::WholeSlice(Node *n) {
    auto se = ast.New<SliceExpr>(n->line, n);
    se->cmpview = true;
    return se;
}

// A value meeting a destination of type `expected` (null or void: none).
// Argument position (`callsite`) leaves the redundant-& warning to the call's
// own resolution, where an explicit & may have picked the overload. A
// variable whose type is `inferred` from the value is no destination type
// either, but binds a reference to a non-fixed value rather than copying the
// pointee (§3.8, §4.1). `branchcopy`: n is a branch's value whose construct
// has no destination type, which copies it (CheckBranchCopy), and `expected`
// at most the type an earlier break gave the construct.
inline Val TypeCheck::CheckValue(Node *&n, TypeExpr *expected, bool callsite, bool branchcopy,
                                 bool inferred) {
    auto orig = n;
    auto v = CheckV(n, expected);
    // An argument's casts are its call's to judge (JudgeCallCasts).
    if (!callsite) JudgeCastAt(orig, expected && expected->kind != TY_VOID ? expected : nullptr, v);
    // A nominal default is an ordinary construction at this destination,
    // including relative fields; do not turn it into a copied call result.
    if (auto c = Is<Call>(n); c && c->builtin == B_DEFAULT &&
        v.type->kind != TY_ARRAY && Is<StructLit>(c->defaultinit))
        n = c->defaultinit;
    auto dt = expected && expected->kind != TY_VOID ? expected : DecayRef(v).type;
    // An argument's parameter may bind the branch by reference instead, which
    // makes the & redundant another way: the argument's check against it
    // says which (argpath, RefCopyWarnings).
    auto refcopy = branchcopy && UserRefOf(n) && IsPlainRef(v.type) && !KeepsRef(v, dt) &&
                   ClassOf(dt) == SC_FIXED;
    if (refcopy && argpath != n) RefCopyWarning(n);
    Val copied;
    if (!expected || expected->kind == TY_VOID) {
        if (branchcopy) CheckBranchCopy(v, n, dt, copied);
        if (!inferred || !IsNonFixedRef(v)) v = DecayRef(v);
    } else {
        if (!callsite && expected->kind == TY_REF && UserRefOf(n))
            Warn(n, cat("redundant &: ", ExprStr(Is<Unary>(n)->child),
                        " binds by reference here without it (§4.1)"));
        if (BindsRef(v, expected)) n = AutoRef(n, v, !expected->cq);
        if (branchcopy) CheckBranchCopy(v, n, expected, copied);
        else RequireCopyable(v, n, expected);
        if (!KeepsRef(v, expected)) v = DecayRef(v);
        auto from = LoadType(v.type);
        auto litfloat = v.litfloat;
        MustFit(v, n, expected);
        NoRelRefCopy(n, expected);
        if (reachable && expected->kind == TY_FLT) {
            if (IsIntT(from)) ToFloat(n, from, expected);
            else if (litfloat && !TypeEq(from, expected)) RetypeFlex(n, expected);
        }
    }
    if (refcopy && argpath == n) copied.refcopies.push_back(n);
    v.storagebranches = copied.storagebranches;
    v.implicitcopy = copied.implicitcopy;
    v.refcopies = std::move(copied.refcopies);
    n->exprtype = v.type;
    RecordVal(n, v);
    return v;
}

// The same, with `d` as the destination the value constructs into.
inline Val TypeCheck::CheckValueAt(Node *&n, TypeExpr *expected, Dest d, bool callsite) {
    DestScope ds(*this, d);
    return CheckValue(n, expected, callsite);
}

// An operand of an operator: always the pointee.
inline Val TypeCheck::Operand(Node *n) {
    auto v = DecayRef(CheckV(n, nullptr));
    n->exprtype = v.type;
    return v;
}

inline void TypeCheck::MustFit(Val &v, Node *n, TypeExpr *dt) {
    auto vt = DecayRef(v).type;
    if (vt->kind == TY_ARRAY && vt->arr->akind == A_FIXED)
        CheckArrayCount(n, dt, ArraySize(vt->arr));
    if (!reachable) return;  // A diverging operand fits anything.
    fitfail.clear();
    fitnode = n;
    auto from = LoadType(v.type);
    if (FitsAt(v, dt)) {
        if (IsIntT(dt) && !TypeEq(from, dt)) {
            RelyOnNamed(v, n);
            if (v.flexint && dt->intstorage != IS_VARINT) RetypeFlexInt(n, dt);
        } else if (IsF32(dt) && from->kind == TY_FLT && !IsF32(from)) {
            RelyOnNamed(v, n);
        }
    } else {
        if (!fitfail.empty()) Error(n, fitfail);
        auto got = dt->kind == TY_REF ? StorageType(v) : v.type;
        string hint;
        if (v.type->kind == TY_INT && dt->kind == TY_INT)
            hint = " (narrowing and sign changes require an explicit `as`)";
        else if (v.type->kind == TY_FLT && dt->kind == TY_INT)
            hint = " (a float converts to an integer only with an explicit `as`)";
        else if (v.type->kind == TY_FLT && !IsF32(v.type) && IsF32(dt)) {
            hint = " (f64 to f32 requires an explicit `as f32` to round the value)";
            // Context refines only unconstrained floating generic calls.
            // A typed argument, explicit type argument or overload boundary
            // may still have committed this result to f64 (§7.7).
            if (auto c = Is<Call>(n); c && c->spec && !c->spec->bindings.empty())
                Append(hint, "; this generic call has committed to f64; choose f32 before "
                             "a typed argument, explicit type argument or overload commits "
                             "the computation");
        }
        Error(n, cat("expected a value of type ", TypeStr(dt), ", got ", TypeStr(got), hint));
    }
}

// The implicit adaptations legal at construction/assignment sites (§6.3,
// §3.1, §3.7, §3.10). On success v.type becomes dt. Also the enforcement
// point of the store rule (§9.2): a reference/slice stored into storage
// owned by curdst must be rooted at least as shallow (call-site argument
// slots pass curdst null: parameters always die before their arguments'
// roots).
inline bool TypeCheck::FitsAt(Val &v, TypeExpr *dt) {
    auto t = v.type;
    // An lvalue at a reference destination is the reference to it (§4.1),
    // a `const T&` where the lvalue is read-only (§9.5).
    if (BindsRef(v, dt)) {
        SlotRoots(v);
        t = v.type = ast.RefTo(ast.PlainOf(StorageType(v)), dt->line);
        v.type->cq = !v.writable;
        v.lvalue = false;
    }
    // The null literal fits any optional (plain or relative).
    if (v.isnull) {
        if (dt->kind == TY_REF && dt->ref->optional) { v.type = dt; return true; }
        fitfail = cat("null is only a value of optional types, not ", TypeStr(dt));
        return false;
    }
    // An array where a slice of its element type is expected is the whole
    // array as a slice (§3.10), where it is stored, and through a reference
    // where the reference points: a `const` one where the array is read-only,
    // which the rules below then judge as any slice (§9.2, §9.5).
    if (dt->kind == TY_SLICE) {
        auto at = IsPlainRef(t) && t->ref->sub->kind == TY_ARRAY ? t->ref->sub : t;
        if (at->kind == TY_ARRAY && TypeEq(at->arr->sub, dt->sub)) {
            if (at != t) v.ClearReads();   // As DerefLValue.
            t = v.type = ast.SliceOf(at->arr->sub, dt->line);
            t->cq = !v.writable;
            v.lvalue = false;
            v.reusable = 0;   // A view of a pool is not the pool, as a[..] is not.
        }
    }
    // The store rule (§9.2) applies to a reference or slice, and to a value
    // holding references or slices by value (a struct with a slice field),
    // whose contents are bounded by its holder root.
    // Constness (§9.5): a read-only reference or slice lands in a slot only
    // if the slot's type says `const`, which is what a later read of the
    // slot then sees; a parameter or result takes either and is read-only
    // in that instantiation.
    if (IsRefOrSlice(dt) && IsRefOrSlice(t) && !v.writable && !dt->cq && constslot) {
        fitfail = cat("storing a read-only ", dt->kind == TY_SLICE ? "slice" : "reference",
                      " of type ", TypeStr(t),
                      t->cq ? "" : " (read-only in this instantiation)",
                      " in a slot of type ", TypeStr(dt), " (§9.5); declare the slot const");
        return false;
    }
    auto holder = !IsRefOrSlice(dt) && !IsRefOrSlice(t) && HoldsPlainRef(dt);
    // Argument slots pass no destination (parameters die before their
    // arguments' roots); an element or field being constructed does.
    if (((IsRefOrSlice(dt) && IsRefOrSlice(t)) || holder) && !curdst.roots.None()) {
        const Roots &roots = holder ? ContentsOf(v) : v.AsRoots();
        // An inexact destination root only bounds the storage the slot is
        // in: that may be any storage there or further out that can hold
        // what the destination's path reached (Dest::reached), which holds
        // the slot by value, so its owner holds one too (ShrinkTargets). What
        // can hold that can hold the slot, so of the storage the slot's type
        // admits, this leaves out only what cannot be its owner. Each place
        // the value may point must outlive each (§9.2).
        auto reached = curdst.reached ? curdst.reached : dt;
        auto dsts = ShrinkTargets(curdst.roots, reached, curdst.slot);
        for (auto &a : roots.alts) {
            for (auto &d : dsts) {
                if (Depth(a.root) <= Depth(d.root)) continue;
                fitfail = cat("storing a reference rooted at ",
                              a.root ? a.root->name : string_view("static data"),
                              ", which does not outlive the destination");
                if (!curdst.roots.Exact())
                    Append(fitfail, ": it is reached through a reference that may point into ",
                           TargetStr(d));
                Append(fitfail, " (§9.2)");
                return false;
            }
        }
        // Binding a global reference or slice variable stores into a global
        // (§5.2).
        if (!curdst.varbind || curdst.roots.None() || curdst.roots.Root()->isglobal) {
            auto reach = false;
            if (auto gs = StoredIntoGrowShrink(v, roots, t, holder, &reach)) {
                fitfail = NeverStoredError(gs, MayPointWording(roots, gs), reach);
                return false;
            }
        }
        // Rebinding one of this activation's own variables is not a store
        // that could outlive it: what the variable is bound to came from
        // an activation that outlives this one, as the first binding did.
        auto spec = CurRealFrame().spec;
        auto ownvar = curdst.varbind && curdst.roots.Exact() &&
                      curdst.roots.Root()->ownerspec == spec;
        if (!CycleStorable(roots) && spec && !ownvar && (spec->incycle || spec->sf->isrec)) {
            // A threaded parameter class may be stored while it stays
            // threaded, and only where every activation's store lands in
            // the same storage: a parameter's class the slot may be in must
            // stay threaded too. A function the cycle's rounds found to be
            // in it by a call back into the cycle (FnSpec::joinedat) is in
            // it from its first statement.
            auto joined = !spec->sf->isrec && spec->joinedat.line > 0
                              ? cat(spec->sf->name, " calls into its recursive cycle at ",
                                    Where(spec->joinedat))
                              : string();
            for (auto &a : roots.alts) {
                if (CycleStorable(a.root) || ThreadStorable(a.root)) continue;
                fitfail = CycleStoreError(a.root, joined);
                return false;
            }
            for (auto &d : dsts) {
                if (ThreadedChain(d.root)) continue;
                fitfail = CycleStoreError(d.root, joined);
                return false;
            }
        }
        // Each storage the slot may be in holds the value from here on; a
        // reference or slice variable itself holds it as its binding, which
        // for a global is a store all the same (CheckGlobalShrinks).
        if (!curdst.varbind) {
            for (auto &d : dsts) {
                if (holder) {
                    // A literal's fields were each recorded as they were
                    // stored; a whole-value event for it would only be a
                    // looser copy.
                    if (!Is<StructLit>(fitnode) && !Is<ArrayLit>(fitnode))
                        RecordStore(d.root, roots, v.byteview, nullptr, v.holderfrom, reached,
                                    d.bound);
                } else {
                    RecordStore(d.root, roots, v.byteview, PointeeOf(t), nullptr, reached,
                                d.bound, curdst.slot,
                                t->kind == TY_REF && PointeeOf(t)->kind == TY_SLICE);
                    if (curdst.slot)
                        StoreIntoSlot(fitnode, d.root, d.bound, dt, v,
                                      curdst.roots.Exact()
                                          ? " through a reference"
                                          : " through a reference that may name it");
                }
            }
        } else if (curdst.roots.Exact() && curdst.roots.Root()->isglobal) {
            NoteGlobalBinding(curdst.roots.Root(), roots, v.byteview, PointeeOf(t),
                              fitnode ? fitnode->line : Line {});
        }
    }
    // A value other than a reference or slice is stored whole, so whether
    // its contents are read-only is for the slot's type to say (§9.5):
    // `const` is added implicitly, and a copy of a const value is a fresh
    // one. A reference's or slice's own qualifier is about its pointee.
    if (IsRefOrSlice(dt) ? TypeEq(t, dt) : TopConstEq(t, dt)) { v.type = dt; return true; }
    switch (dt->kind) {
        case TY_INT:
            if (t->kind != TY_INT) return false;
            // A literal parameter adapts to any integer type; whether the
            // literal fits is each call site's question (§7.7).
            if (v.unsized) {
                RecordLitAdapt(v, dt, fitnode ? fitnode->line : Line {});
                v.type = dt;
                return true;
            }
            if (v.flexint && dt->intstorage != IS_VARINT) {
                if (FlexFits(v, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                fitfail = ConstsNoFit(v, dt);
                return false;
            }
            // A constant adapts to any integer type its value fits, and a
            // construct of constants to any type they all fit (§6.4).
            if (v.ck == CK_INT || (v.litint && dt->intstorage != IS_VARINT)) {
                if (v.litint ? ConstsFit(v, dt->intstorage)
                             : FitsIntStorage(v.ival, v.uns, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                fitfail = ConstsNoFit(v, dt);
                return false;
            }
            if (dt->intstorage == IS_VARINT) {
                // varint stores hold the full i64 value range: every
                // integer type embeds except u64 (§3.6).
                if (t->intstorage != IS_U64 && t->intstorage != IS_VARINT) {
                    v.type = dt;
                    return true;
                }
                return false;
            }
            if (ImplicitInt(t->intstorage, dt->intstorage)) {
                v.type = dt;
                return true;
            }
            return false;
        case TY_FLT:
            // Every integer converts to either float type (§6.3).
            if (t->kind == TY_INT) {
                IntToFloat(v, dt);
                return true;
            }
            if (t->kind != TY_FLT) return false;
            // A float taking its type from its literals adapts to f32; f32
            // widens to f64.
            if (IsF32(dt)) { if (!LitFloat(v)) return false; }
            else if (!IsF32(t)) return false;
            if (v.unsized) RecordLitAdapt(v, dt, fitnode ? fitnode->line : Line {});
            v.type = dt;
            return true;
        case TY_ARRAY: {
            // Construction of an array from another array/slice of the
            // same element type (copies, §3.7/§4.2). Fixed destinations
            // need a statically known length, so only [] adapts.
            TypeExpr *selem = nullptr;
            if (t->kind == TY_ARRAY) selem = t->arr->sub;
            else if (t->kind == TY_SLICE) selem = t->sub;
            else return false;
            if (v.emptyarr) {
                if (dt->arr->akind == A_FIXED && ArraySize(dt->arr) != 0) return false;
                v.type = dt;
                return true;
            }
            if (dt->arr->akind == A_FIXED) return false;
            if (!TypeEq(selem, dt->arr->sub)) return false;
            v.type = dt;
            return true;
        }
        case TY_SLICE:
            // The same slice type but for constness: adding `const` is
            // implicit, and dropping it was rejected above for a slot and
            // is the read-only instantiation everywhere else (§9.5).
            if (t->kind == TY_SLICE && TypeEq(t->sub, dt->sub)) {
                v.type = dt;
                return true;
            }
            return false;
        case TY_REF: {
            if (t->kind != TY_REF) return false;
            if (!TypeEq(t->ref->sub, dt->ref->sub)) return false;
            if (t->ref->lenstorage >= 0) return false;  // Values are never relative.
            if (dt->ref->lenstorage >= 0) {
                // Storing an ordinary reference into a relative reference
                // location (§3.9). A self-relative one needs both ends in
                // the same root array; an `in pool` one needs the value in
                // the pool, and takes the destination wherever it is.
                if (t->ref->optional && !dt->ref->optional) return false;
                // Null stores as the sentinel offset, which means the same
                // in every location, so an optional relative slot takes a
                // null wherever the slot is: a linked structure's sentinel
                // end does not force plain links. A null has no roots
                // (NullLit, RefProvOf), or static data exactly
                // (default<T>(), a parameter given one), where nothing else
                // a writable reference is given points (§9.5), nor one to a
                // type no literal supplies; a read-only u8 one may point
                // into a string literal. As one alternative of a merged
                // value it names no array (§9.2): the others say where the
                // value points.
                auto nullalt = [&](const RootAlt &a) {
                    return !a.root && a.exact && t->ref->optional &&
                           (v.writable || !StaticCanContain(t->ref->sub));
                };
                Roots links;
                for (auto &a : v.alts) if (!nullalt(a)) links.Add(a);
                // A reference that points nowhere yet (RefProvOf), or a
                // location reached through one, is read again once it does.
                if (links.None() || (!dt->ref->pool && curdst.unknown)) {
                    v.type = dt;
                    return true;
                }
                // The target must be the *same* array, so a root that only
                // bounds the pointee's lifetime will not do (§9.5).
                auto want = dt->ref->pool ? dt->ref->pool : curdst.roots.Root();
                auto have = dt->ref->pool ? PoolOf(links.Root()) : links.Root();
                if (!want || (!dt->ref->pool && !curdst.roots.Exact()) || !links.Exact() ||
                    have != want) {
                    auto why = links.Exact() ? string() : ReadBackWhy(links);
                    auto vroot = links.Root() ? links.Root()->name : string_view("static data");
                    fitfail = cat(dt->ref->pool
                                      ? cat("a relative reference in ", want->name,
                                            " must point into ", want->name, " (§3.9); ")
                                      : string("a relative reference must point within the "
                                               "same root as its location (§3.9); "),
                                  !why.empty() ? why
                                  : !links.Exact()
                                      ? cat("this reference's root is not known exactly, "
                                            "only that it outlives ", vroot)
                                  : cat("this reference is rooted at ", vroot));
                    return false;
                }
                v.type = dt;
                return true;
            }
            // T& widens to T?.
            if (dt->ref->optional && !t->ref->optional) { v.type = dt; return true; }
            // The same reference type but for constness (as for slices).
            if (dt->ref->optional == t->ref->optional) { v.type = dt; return true; }
            return false;
        }
        case TY_ENUM: {
            // Mode adaptation copies at construction (fixed <-> variable);
            // a variant value constructs its enum (the tag is static).
            if (t->kind == TY_VARIANT) {
                auto adt = t->var->adt;
                if (adt->enu->en != dt->enu->en) return false;
                if (!TypeArgsEq(adt->enu->args, dt->enu->args)) return false;
            } else if (t->kind == TY_ENUM) {
                if (t->enu->en != dt->enu->en) return false;
                if (!TypeArgsEq(t->enu->args, dt->enu->args)) return false;
            } else {
                return false;
            }
            if (!dt->enu->varmode && !GetEnumInst(dt)->allfixed) return false;
            v.type = dt;
            return true;
        }
        default: return false;
    }
}

inline string TypeCheck::ConstStr(const Val &v) {
    return v.uns ? cat((uint64_t)v.ival) : cat(v.ival);
}

// Conditions: bool, or an optional (§3.8 truthiness + narrowing). Plain
// references decay (a bool& condition reads its pointee); an already
// narrowed optional stays a valid (trivially true) test.
inline Val TypeCheck::CheckCond(Node *n) {
    FlagScope rs(inreturn, false);
    auto v = CheckV(n, nullptr);
    auto id = Is<Ident>(n);
    auto narrowedopt = id && id->vdef && IsOptional(id->vdef->type) && id->vdef->narrowed;
    if (!narrowedopt) v = DecayRef(v);
    n->exprtype = v.type;
    if (!narrowedopt && v.type->kind != TY_BOOL && !IsOptional(v.type))
        Error(n, cat("condition must be bool or an optional, got ", TypeStr(v.type)));
    return v;
}

// Unifies two branch values (literal/f32/root adaptations); null = branch
// diverged (bottom). Errors when both produce values of unrelated types
// and a value is wanted.
inline TypeExpr *TypeCheck::UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue) {
    if (!a) return b;
    if (!b) return a;
    if (TypeEq(a, b)) return a;
    if (TopConstEq(a, b)) return a->cq ? a : b;   // Read-only in one branch: in both.
    if (a->kind == TY_INT && b->kind == TY_INT) {
        if (ImplicitInt(a->intstorage, b->intstorage)) return b;
        if (ImplicitInt(b->intstorage, a->intstorage)) return a;
    }
    if (a->kind == TY_FLT && b->kind == TY_FLT) return ast.flttypes[FS_F64];
    // An integer branch converts to the other's float type (§6.3).
    if (a->kind == TY_INT && b->kind == TY_FLT) return b;
    if (a->kind == TY_FLT && b->kind == TY_INT) return a;
    if (!wantvalue) return ast.voidtype;
    Error(at, cat("branches have mismatched types: ", TypeStr(a), " vs ", TypeStr(b)));
}

inline Val TypeCheck::VoidVal() {
    Val v;
    v.type = ast.voidtype;
    return v;
}

// &lvalue: reference creation (§3.8) with its restrictions. On a location
// that itself holds a reference (a reference variable or field), yields
// the stored reference — there are no references to references.
inline Val TypeCheck::CheckRefOf(Unary *x) {
    auto lv = CheckLValue(x->child);
    if (lv.var) RequireAssigned(lv.var, x);
    // A resizable value has a header of its own only as a whole variable or
    // as a frame object's tail (C.2).
    if (!lv.var && !lv.fotail && lv.type->kind != TY_REF &&
        ClassOf(lv.type) == SC_RESIZABLE)
        NoResizableRef(x);
    if (lv.type->kind == TY_REF) {
        // Out of a container, the stored reference is a read-back (§9.5).
        if (!lv.var) return ContainerRead(lv);
        Val v;
        v.type = LoadType(lv.type);  // Relative refs load as plain (§3.9).
        v.SetProv(RefProvOf(lv.var));
        return v;
    }
    Val v;
    v.type = ast.RefTo(ast.PlainOf(lv.type), x->line);
    v.SetProv(lv);
    // A field or an element of a const type is as read-only through a
    // reference as a variable of one (§9.5); a slice's qualifier is about
    // its elements, not the slot the reference names.
    v.writable = lv.writable && !lv.isvarint && (lv.type->kind == TY_SLICE || !lv.type->cq);
    // Where the variable is out of sight -- behind a parameter's class, or
    // read back -- a load through the reference has only its writability to
    // go by (SlotView): a slice variable's is no more than its binding's, as
    // when it is bound by reference (AutoRef), whatever its type says.
    if (lv.var && lv.type->kind == TY_SLICE)
        v.writable = v.writable && RefProvOf(lv.var).writable;
    v.type->cq = !v.writable;   // `&x` of a const value is a `const T&` (§9.5).
    if (lv.var && v.writable) NoteWritableRef(lv.var, x);
    if (auto sv = SliceVarOf(lv.var)) sv->slotref = true;
    return v;
}

// A writable reference to d is made at `at` (§4.1: `&d`, or d bound by
// reference). A `let` is written through one as a `var` is (§4.4), before
// or after anything that relies on its value: the marks this, RelyOnNonneg,
// ResizesToMark and RelyOnConstant leave do not follow the flow, so neither
// a loop's later pass, a cycle's later round nor a nested function checked
// only once can get past them, and whichever comes second is an error (but
// for a resize, which is merely unbalanced after the reference).
inline void TypeCheck::NoteWritableRef(VarDef *d, Node *at) {
    if (!d->refwrite) d->refwrite = at;
    if (d->nonneguse)
        Error(at, cat(d->name, " is bound to a writable reference here, through which it may "
                      "become negative, while the comparison with a u64 at ",
                      Where(d->nonneguse->line), " relies on its value being non-negative "
                      "(§4.4, §6.1); convert the signed side with `as` there"));
    if (d->markuse)
        Error(at, cat(d->name, " is bound to a writable reference here, through which it may "
                      "change, while the resize at ", Where(d->markuse->line), " relies on it "
                      "still holding the length it was bound to (§4.4, §5.2); bind the "
                      "reference to a copy of it, or make ", d->name, " a var"));
    if (d->constuse)
        Error(at, cat(d->name, " is bound to a writable reference here, through which it may "
                      "change, while the ", d->constwhat, " at ", Where(d->constuse->line),
                      " relies on it keeping its initializer's value, as a named constant "
                      "(§4.4, §11.1); bind the reference to a copy of it, or declare ",
                      d->name, " const"));
}

// A comparison with a u64 at `at` relies on v being non-negative (§6.1),
// and so on the `let`s it was read from keeping their initializers'
// values. Returns one bound to a writable reference (NoteWritableRef),
// which may not have.
inline VarDef *TypeCheck::RelyOnNonneg(const Val &v, Node *at) {
    for (auto d = v.nonnegfrom; d; d = d->nonnegfrom) {
        if (d->refwrite) return d;
        if (!d->nonneguse) d->nonneguse = at;
    }
    return nullptr;
}

// A compile-time size, fill count or match pattern takes the named constant
// d at its initializer's value (§11.1), directly or through the initializer
// of the one it names, so relies on d keeping that value, which a writable
// reference to d could change (§4.4). As with a comparison relying on a
// `let` (RelyOnNonneg), whichever comes second is an error.
inline void TypeCheck::RelyOnConstant(ConstUse &use, VarDef *d) {
    if (d->refwrite)
        Error(use.at, cat("the ", use.what, " relies on ", use.named->name,
                          " keeping its initializer's value",
                          d != use.named ? cat(", which rests on ", d->name, "'s") : string(),
                          ", but a writable reference bound to ", d->name, " at ",
                          Where(d->refwrite->line), " may change it (§4.4, §11.1); bind that "
                          "reference to a copy of it, or declare ", d->name, " const"));
    if (!d->constuse) {
        d->constuse = use.at;
        d->constwhat = use.what;
    }
}

// A constant read from a named constant (Val::constfrom) adapts at `at` to
// a type other than its own, as a literal would (§3.1): that relies on the
// named constants it was computed from keeping their initializers' values.
// A trial relies on nothing.
inline void TypeCheck::RelyOnNamed(const Val &v, Node *at) {
    if (unifytrial || !v.constfrom) return;
    ConstUse use { at, "use as a literal", v.constfrom };
    for (auto d = v.constfrom; d; d = d->constfrom) RelyOnConstant(use, d);
}

// Folds a constant binary op at the width and signedness of out.type
// (FoldIntOp, ast.h, which the optimizer folds with too). Operand values fit
// out.type (the unify rules ensured it). A zero divisor aborts at run time,
// which a constant expression need not wait for; one a named constant gives
// is left to run time, as a division naming it is in a `for` count (§6.5).
inline void TypeCheck::FoldInt(TType op, Val &l, Val &r, Val &out, Node *at) {
    if (l.ck != CK_INT || r.ck != CK_INT) return;
    auto s = out.type->intstorage;
    if (s == IS_VARINT) return;
    if ((op == T_DIV || op == T_MOD) && !r.ival) {
        if (r.constfrom) return;
        Error(at, "constant division by zero");
    }
    int64_t res;
    if (!FoldIntOp(op, l.ival, r.ival, s, res)) return;
    out.ck = CK_INT;
    out.ival = res;
    out.uns = s == IS_U64 && res < 0;
    // One chain carries on; the other operand's named constants are relied
    // on here.
    if (l.constfrom && r.constfrom) RelyOnNamed(r, at);
    out.constfrom = l.constfrom ? l.constfrom : r.constfrom;
}

// The operand/result type of a binary numeric operator: equal types
// stand; a constant adapts to the other operand's type; otherwise the
// operand that implicitly widens into the other picks the wider type
// (§6.1). Returns null for non-numeric pairs, and where a trial finds no
// common type (unifytrial).
inline TypeExpr *TypeCheck::UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt,
                                         TypeExpr *rt, bool cmp) {
    auto fail = [&](const string &msg) -> TypeExpr * {
        if (!unifytrial) Error(at, msg);
        return nullptr;
    };
    if (IsIntT(lt) && IsIntT(rt)) {
        if (TypeEq(lt, rt)) return lt;
        // A construct of integer constants (Val::litint) adapts as the
        // constants would.
        auto lconst = lv.ck == CK_INT || lv.litint || lv.flexint,
             rconst = rv.ck == CK_INT || rv.litint || rv.flexint;
        // A literal parameter adapts to a typed operand, as a constant
        // does; meeting a constant, it stays at its own type (§7.7).
        if (lv.unsized && !rv.unsized && !rconst) {
            RecordLitAdapt(lv, rt, at->line);
            return rt;
        }
        if (rv.unsized && !lv.unsized && !lconst) {
            RecordLitAdapt(rv, lt, at->line);
            return lt;
        }
        if (lconst && rconst) {
            if (lv.uns || rv.uns) {
                auto neg = [](const Val &x) {
                    return (x.litint || x.flexint) ? x.litlo < 0 : !x.uns && x.ival < 0;
                };
                if (neg(lv) || neg(rv))
                    return fail("constant operands have no common type (one is above "
                                "i64.max, the other negative)");
                return ast.inttypes[IS_U64];
            }
            return ast.inttypes[IS_I64];
        }
        auto fits = [&](const Val &c, TypeExpr *t) {
            return c.flexint ? FlexFits(c, t->intstorage)
                   : c.litint ? ConstsFit(c, t->intstorage)
                              : FitsIntStorage(c.ival, c.uns, t->intstorage);
        };
        if (lconst) {
            if (!fits(lv, rt)) return fail(ConstsNoFit(lv, rt));
            RelyOnNamed(lv, at);
            return rt;
        }
        if (rconst) {
            if (!fits(rv, lt)) return fail(ConstsNoFit(rv, lt));
            RelyOnNamed(rv, at);
            return lt;
        }
        if (ImplicitInt(lt->intstorage, rt->intstorage)) return rt;
        if (ImplicitInt(rt->intstorage, lt->intstorage)) return lt;
        // §6.1: a comparison produces bool, so it has no result type to
        // pick and the mathematical answer across signedness is never in
        // doubt. u64 is the one unsigned type with no signed supertype;
        // it may meet a signed operand the compiler knows is
        // non-negative, and the compare is then a single unsigned one.
        // Without that knowledge the conversion could change the value,
        // so the cast has to be written (and thought about).
        if (cmp) {
            auto isu64 = [](TypeExpr *t) { return t->intstorage == IS_U64; };
            if (isu64(lt) != isu64(rt)) {
                auto &sv = isu64(lt) ? rv : lv;
                auto st = isu64(lt) ? rt : lt;
                if (!IsUnsigned(st->intstorage)) {
                    if (sv.nonneg) {
                        // A trial relies on nothing: where a writable reference may
                        // make the signed side negative, it finds no common type.
                        if (unifytrial) {
                            for (auto d = sv.nonnegfrom; d; d = d->nonnegfrom)
                                if (d->refwrite) return nullptr;
                            return ast.inttypes[IS_U64];
                        }
                        if (auto w = RelyOnNonneg(sv, at)) {
                            auto d = sv.nonnegfrom;
                            Error(at, cat("comparing ", TypeStr(lt), " with ", TypeStr(rt),
                                          " relies on ", d->name, " being non-negative",
                                          w != d ? cat(", which rests on ", w->name) : string(),
                                          ", but a writable reference bound to ", w->name,
                                          " at ", Where(w->refwrite->line), " may make it "
                                          "negative (§4.4, §6.1); convert it with `as`"));
                        }
                        return ast.inttypes[IS_U64];
                    }
                    return fail(cat("comparing ", TypeStr(lt), " with ", TypeStr(rt),
                                    " needs the signed side to be known non-negative "
                                    "(a literal, .len/.cap, or a `let` bound to one); "
                                    "convert it with `as` otherwise"));
                }
            }
        }
        return fail(cat("operands of ", TName(op), " have no common type: ", TypeStr(lt),
                        " and ", TypeStr(rt), " (convert one with `as`)"));
    }
    if (lt->kind == TY_FLT && rt->kind == TY_FLT) {
        if (TypeEq(lt, rt)) return lt;
        // One side is f32, the other f64: an f64 taking its type from its
        // literals adapts to the typed side, otherwise f32 widens (§6.3).
        auto &wide = IsF32(lt) ? rv : lv;
        auto narrow = IsF32(lt) ? lt : rt;
        if (LitFloat(wide)) {
            RecordLitAdapt(wide, narrow, at->line);
            RelyOnNamed(wide, at);
            return narrow;
        }
        return ast.flttypes[FS_F64];
    }
    // An integer operand converts to the float operand's type (§6.3).
    if (lt->kind == TY_FLT && IsIntT(rt)) return lt;
    if (IsIntT(lt) && rt->kind == TY_FLT) return rt;
    return nullptr;
}

// Re-types an operand to its numeric type ct: adapted constants and
// implicitly widened operands emit at ct downstream, an integer converts to
// a float ct in a node of its own, and a float of literals and integers is
// computed at ct throughout. Binary unification and scalar broadcasting use
// the same conversion.
inline void TypeCheck::RetypeOperand(Node *&n, Val &v, TypeExpr *ct) {
    if (auto t = LoadType(v.type); ct->kind == TY_FLT && IsIntT(t)) {
        ToFloat(n, t, ct);
    } else {
        if (v.litfloat && !TypeEq(v.type, ct)) RetypeFlex(n, ct);
        else if (v.flexint && !TypeEq(v.type, ct)) RetypeFlexInt(n, ct);
        else if (v.litint && !TypeEq(v.type, ct)) RetypeBranches(n, ct);
        n->exprtype = ct;
    }
    RetypeVal(v, ct);
}

inline void TypeCheck::RetypeOperands(Node *&left, Node *&right, Val &lv, Val &rv,
                                      TypeExpr *ct) {
    RetypeOperand(left, lv, ct);
    RetypeOperand(right, rv, ct);
}

// An operand's value as the unified type ct gives it (RetypeOperands).
inline void TypeCheck::RetypeVal(Val &v, TypeExpr *ct) {
    if (ct->kind == TY_FLT && IsIntT(LoadType(v.type))) IntToFloat(v, ct);
    else v.type = ct;
}

// The value of the numeric operation b, `lv op rv` (§6.1, §6.2): the
// operands' common type `ct`, which their nodes take, and the result's type
// and constant. A `trial` changes no node and reports no error: it gives a
// value of no type where b would be one.
inline Val TypeCheck::NumericBinary(Binary *b, Val lv, Val rv, TypeExpr *&ct, bool trial) {
    auto op = b->op;
    auto lt = LoadType(lv.type), rt = LoadType(rv.type);
    FlagScope ut(unifytrial, unifytrial || trial);
    FlagScope lr(litrecord, litrecord && !trial);
    ct = nullptr;
    auto fail = [&](const string &msg) {
        if (!trial) Error(b, msg);
        return Val {};
    };
    auto retype = [&]() {
        if (trial) {
            RetypeVal(lv, ct);
            RetypeVal(rv, ct);
        } else {
            RetypeOperands(b->left, b->right, lv, rv, ct);
        }
    };
    // A constant division by zero is FoldInt's error.
    auto fold = [&](Val &v) {
        if (trial && lv.ck == CK_INT && rv.ck == CK_INT && (op == T_DIV || op == T_MOD) &&
            !rv.ival && !rv.constfrom)
            return false;
        FoldInt(op, lv, rv, v, b);
        return true;
    };
    // A shift of a constant by a count that is no constant, and integer
    // operations whose operands are constants and such values, are no
    // constants that adapt (Val::flexint).
    auto flexint = [&](Val &v, bool shift) {
        int64_t llo, lhi, rlo, rhi;
        if (v.ck == CK_INT || !FlexConsts(lv, llo, lhi)) return;
        if (shift) {
            v.flexint = true;
            v.litlo = llo;
            v.lithi = lhi;
        } else if ((lv.flexint || rv.flexint) && FlexConsts(rv, rlo, rhi)) {
            v.flexint = true;
            v.litlo = std::min(llo, rlo);
            v.lithi = std::max(lhi, rhi);
        }
        if (!trial) b->flexint = v.flexint;
    };
    Val v;
    switch (op) {
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:
            ct = UnifyNumeric(b, op, lv, rv, lt, rt, true);
            if (!ct)
                return fail(cat("ordering comparison requires numeric operands, got ",
                                TypeStr(lt), " and ", TypeStr(rt)));
            retype();
            v.type = ast.booltype;
            return v;
        case T_EQ: case T_NEQ:
            ct = UnifyNumeric(b, op, lv, rv, lt, rt, true);
            if (!ct) return Val {};
            retype();
            v.type = ast.booltype;
            return v;
        case T_SHL: case T_SHR:
            // Shifts: the left operand's type is the result type; the count
            // may be any integer type and is masked to the width (§6.2).
            if (!IsIntT(lt) || !IsIntT(rt))
                return fail(cat("shift requires integer operands, got ", TypeStr(lt), " and ",
                                TypeStr(rt)));
            ct = v.type = lv.ck == CK_INT ? ast.inttypes[lv.uns ? IS_U64 : IS_I64] : lt;
            if (!trial) b->left->exprtype = v.type;
            lv.type = v.type;
            if (!fold(v)) return Val {};
            flexint(v, true);
            return v;
        case T_BITAND: case T_BITOR: case T_XOR:
            if (!IsIntT(lt) || !IsIntT(rt))
                return fail(cat("bitwise operator requires integer operands, got ", TypeStr(lt),
                                " and ", TypeStr(rt)));
            ct = UnifyNumeric(b, op, lv, rv, lt, rt);
            if (!ct) return Val {};
            retype();
            v.type = ct;
            if (!fold(v)) return Val {};
            flexint(v, false);
            return v;
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            ct = UnifyNumeric(b, op, lv, rv, lt, rt);
            if (!ct) return Val {};
            // Where no operand has a float type of its own, the result
            // takes the type its destination or other operand has, as a
            // float literal does (Val::litfloat).
            auto literal = [&](const Val &x, TypeExpr *t) { return IsIntT(t) || LitFloat(x); };
            auto flex = ct->kind == TY_FLT && literal(lv, lt) && literal(rv, rt);
            retype();
            v.type = ct;
            if (ct->kind == TY_INT) {
                if (!fold(v)) return Val {};
                flexint(v, false);
            } else if (lv.ck == CK_FLT && rv.ck == CK_FLT && op != T_MOD) {
                // % (fmod) is left to the runtime.
                auto x = lv.fval, y = rv.fval;
                if (IsF32(ct)) { x = (float)x; y = (float)y; }
                v.ck = CK_FLT;
                switch (op) {
                    case T_PLUS:  v.fval = x + y; break;
                    case T_MINUS: v.fval = x - y; break;
                    case T_MUL:   v.fval = x * y; break;
                    default:      v.fval = y != 0 ? x / y : 0; break;
                }
                if (IsF32(ct)) v.fval = (double)(float)v.fval;
            }
            v.litfloat = flex && v.ck != CK_FLT;
            if (!trial) b->litfloat = v.litfloat;
            if (flex) {
                // One chain carries on, as FoldInt's does.
                if (lv.constfrom && rv.constfrom) RelyOnNamed(rv, b);
                v.constfrom = lv.constfrom ? lv.constfrom : rv.constfrom;
            }
            return v;
        }
        default:
            assert(false);
            return Val {};
    }
}

// The value of the unary operation u, `-v` or `~v` (§6.2), as NumericBinary's
// is of a binary one: a trial changes no node, reports no error, and gives a
// value of no type where u would be one.
inline Val TypeCheck::NumericUnary(Unary *u, const Val &cv, bool trial) {
    auto v = cv;
    if (v.notconst && u->op == T_MINUS) {
        v.flexint = v.notconst = false;
        v.ck = CK_INT;
        v.ival = ~v.litlo;
    }
    auto t = LoadType(v.type);
    auto fail = [&](const string &msg) {
        if (!trial) Error(u, msg);
        return Val {};
    };
    Val r;
    switch (u->op) {
        case T_MINUS:
            if (IsIntT(t)) {
                // -(i64.min) leaves i64, so like a signed op that overflows
                // (FoldIntOp) it is no constant: a debug build aborts on it
                // at run time.
                if (v.ck == CK_INT && (v.uns || v.ival != INT64_MIN)) {
                    // -(2^63) is exactly i64.min; any other u64-range value
                    // cannot be negated.
                    if (v.uns && v.ival != INT64_MIN)
                        return fail("negated constant too large for i64");
                    r.type = ast.inttypes[IS_I64];
                    r.ck = CK_INT;
                    r.ival = v.uns ? INT64_MIN : -v.ival;
                    r.constfrom = v.constfrom;
                    if (!trial) u->child->exprtype = r.type;
                    return r;
                }
                if (IsUnsigned(t->intstorage))
                    return fail(cat("cannot negate a value of unsigned type ", TypeStr(t),
                                    " (convert with `as`)"));
                r.type = t;
            } else if (t->kind == TY_FLT) {
                r.type = t;
                if (v.ck == CK_FLT) {
                    r.ck = CK_FLT;
                    r.fval = -v.fval;
                } else {
                    r.litfloat = LitFloat(v);
                }
                r.constfrom = v.constfrom;
                if (!trial) u->litfloat = r.litfloat;
            } else {
                return fail(cat("cannot negate a value of type ", TypeStr(t)));
            }
            return r;
        case T_BITNOT:
            if (!IsIntT(t)) return fail(cat("~ requires an integer, got ", TypeStr(t)));
            // `~c` of a constant c >= 0 depends on the width it is computed
            // at (`~4` is 251 as a u8), so it adapts as `~(1 << k)` does
            // rather than folding to i64's -5.
            if (v.ck == CK_INT && !v.uns && v.ival >= 0) {
                r.type = ast.inttypes[IS_I64];
                r.flexint = r.notconst = true;
                r.litlo = r.lithi = v.ival;
                r.constfrom = v.constfrom;
                if (!trial) {
                    u->child->exprtype = r.type;
                    u->flexint = true;
                }
                return r;
            }
            if (v.ck == CK_INT) {
                r.type = ast.inttypes[v.uns ? IS_U64 : IS_I64];
                r.ck = CK_INT;
                r.ival = ~v.ival;
                r.uns = v.uns && r.ival < 0;
                r.constfrom = v.constfrom;
                if (!trial) u->child->exprtype = r.type;
                return r;
            }
            r.type = t;
            if (v.flexint) {
                r.flexint = true;
                r.litlo = v.litlo;
                r.lithi = v.lithi;
            }
            if (!trial) u->flexint = r.flexint;
            return r;
        default:
            assert(false);
            return Val {};
    }
}

// An integer value converted to the float type ft (§6.3): a constant stays
// one, now of the float's value; a literal parameter is one only where it
// is an integer.
inline void TypeCheck::IntToFloat(Val &v, TypeExpr *ft) {
    if (v.ck == CK_INT) {
        v.ck = CK_FLT;
        v.fval = v.uns ? (double)(uint64_t)v.ival : (double)v.ival;
        if (IsF32(ft)) v.fval = v.uns ? (double)(float)(uint64_t)v.ival : (double)(float)v.ival;
        v.uns = false;
    }
    v.unsized = false;
    v.unsizedparam = nullptr;
    v.litint = false;
    v.flexint = false;
    v.nonneg = false;
    v.lvalue = false;
    v.type = ft;
}

// An integer converted to a float (§6.3) is a node of its own, an implicit
// cast, which a later check of the node retargets rather than wraps again:
// the conversion then survives whatever the optimizer makes of the integer
// expression, such as an inlined call or a block reduced to its value.
// `from` is the integer's type.
inline void TypeCheck::ToFloat(Node *&n, TypeExpr *from, TypeExpr *ft) {
    auto a = Is<AsCast>(n);
    if (!a || !a->implicit) {
        n->exprtype = from;
        a = ast.New<AsCast>(n->line, n, ft, false);
        a->implicit = true;
        n = a;
    }
    a->type = a->totype = ft;
    a->exprtype = ft;
}

// A float of literals and integers (Val::litfloat) takes the float type t
// its destination or other operand has: each node computing it, down to its
// literals and the conversions of its integers, and a construct's branches
// (RetypeBranches). A constant part keeps its own nodes, which the optimizer
// folds at full precision before the result rounds to t, as a constant at t
// is anywhere.
inline void TypeCheck::RetypeFlex(Node *&n, TypeExpr *t) {
    n->exprtype = t;
    if (auto b = Is<Binary>(n); b && b->litfloat) {
        RetypeFlex(b->left, t);
        RetypeFlex(b->right, t);
    } else if (auto u = Is<Unary>(n); u && u->litfloat) {
        RetypeFlex(u->child, t);
    } else if (auto a = Is<AsCast>(n); a && a->implicit) {
        a->type = a->totype = t;
    } else {
        RetypeBranches(n, t);
    }
}

// An integer computed from constants through a shift (Val::flexint) takes
// the integer type t its destination or other operand has: each node
// computing it, a shift's left operand but not its count, down to its
// constants and constructs of constants.
inline void TypeCheck::RetypeFlexInt(Node *n, TypeExpr *t) {
    n->exprtype = t;
    if (auto b = Is<Binary>(n); b && b->flexint) {
        RetypeFlexInt(b->left, t);
        if (b->op != T_SHL && b->op != T_SHR) RetypeFlexInt(b->right, t);
    } else if (auto u = Is<Unary>(n); u && u->flexint) {
        RetypeFlexInt(u->child, t);
    } else {
        RetypeBranches(n, t);
    }
}

// ------------------------------------------------------------------
// Redundant casts (§6.3). Where an explicit cast's operand converts to its
// type implicitly (Converts), the checker follows the value the node would
// have without the cast (CastAlt::alt) up the expression, and each consumer
// judges it: a typed destination, an operator, a call's resolution, a cast
// around it; or, where the difference is one its own consumer can still
// tell, follows its own value on. Code whose types differ between
// specializations (InGenericCode) is not judged.

// Whether the code being checked is generic -- in a function with type
// parameters or untyped ones, in a function value's body, or in a function
// or default declared in one -- so that the types around a cast may differ
// from one specialization to the next (§7.7).
inline bool TypeCheck::InGenericCode() {
    auto generic = [](const SFunction *sf) {
        if (!sf) return false;
        if (!sf->generics.empty()) return true;
        for (auto &p : sf->params) if (!p.type) return true;
        return false;
    };
    for (auto i = (int)frames.size() - 1; i >= 0;) {
        auto &fr = frames[i];
        if (fr.isfunval && !fr.isdefault) return true;
        if (generic(fr.sf) || generic(fr.defaultfn)) return true;
        for (auto sp = fr.lexspec; sp; sp = sp->lexparent)
            if (!sp->bindings.empty() || generic(sp->sf)) return true;
        // A default names only what its declaration's top level does.
        if (fr.isdefault || fr.lexframe < 0 || fr.lexframe >= i) return false;
        i = fr.lexframe;
    }
    return false;
}

// Whether the numeric value v converts to the numeric type t implicitly
// (§6.3): FitsAt's rules for numbers, recording nothing. A literal parameter
// only widens: whether its literal fits a narrower type is each call site's
// question, which the cast answers at run time.
inline bool TypeCheck::Converts(const Val &v, TypeExpr *t) {
    auto vt = LoadType(v.type);
    if (t->kind == TY_INT) {
        if (vt->kind != TY_INT) return false;
        if (vt->intstorage == t->intstorage) return true;
        if (v.ck == CK_INT || (v.litint && t->intstorage != IS_VARINT))
            return v.litint ? ConstsFit(v, t->intstorage)
                            : FitsIntStorage(v.ival, v.uns, t->intstorage);
        if (t->intstorage == IS_VARINT) return vt->intstorage != IS_U64;
        return ImplicitInt(vt->intstorage, t->intstorage);
    }
    if (t->kind != TY_FLT) return false;
    if (vt->kind == TY_INT) return true;
    if (vt->kind != TY_FLT) return false;
    return vt->fltstorage == t->fltstorage || (IsF32(t) ? LitFloat(v) : IsF32(vt));
}

// Whether two numeric values are the same to whatever consumes them: of one
// type, the same constant or none, and adapting alike (§6.3).
inline bool TypeCheck::SameNumVal(const Val &a, const Val &b) {
    if (!a.type || !b.type || !SameNum(LoadType(a.type), LoadType(b.type))) return false;
    if (a.ck != b.ck || a.litfloat != b.litfloat || a.litint != b.litint ||
        a.flexint != b.flexint || a.unsized != b.unsized || a.notconst != b.notconst)
        return false;
    if (a.notconst && a.litlo != b.litlo) return false;
    if (a.ck == CK_INT && (a.ival != b.ival || a.uns != b.uns)) return false;
    if (a.ck == CK_FLT && a.fval != b.fval) return false;
    return !a.litint || (a.litlo == b.litlo && a.lithi == b.lithi);
}

// Whether the operand of the cast a follows, without the cast, reaches type
// `at` as it now does through it: computed alike -- a float of literals or a
// construct of constants at the type the cast gives it, and a construct that
// a destination gives its type (`typed`) at the one it has -- and converted
// to the same value, an integer rounding to the cast's float type only where
// that is `at`.
inline bool TypeCheck::SameReach(const CastAlt &a, TypeExpr *at, bool typed) {
    auto &cv = a.operand;
    auto st = LoadType(cv.type), tt = a.cast->totype;
    if ((cv.litfloat && cv.ck == CK_NONE) || cv.litint) return SameNum(st, tt) && SameNum(tt, at);
    if (typed && IsBranchConstruct(a.cast->child) && !SameNum(st, at)) return false;
    auto rounds = IsIntT(st) ? tt->kind == TY_FLT : !SameNum(st, tt);
    return !rounds || SameNum(tt, at);
}

// Whether a followed value reaches type `at` as the cast's value does: the
// cast's own operand as SameReach says, a float of literals the deletion
// leaves where it settles.
inline bool TypeCheck::Reaches(const CastAlt &a, TypeExpr *at, bool typed) {
    return a.pending ? !a.settle || SameNum(at, a.settle) : SameReach(a, at, typed);
}

// The explicit cast x of cv, whose value is v (AsCast::Check). A cast the
// checker followed to the operand is judged by what x makes of it: where x
// may go as well, that verdict holds only while x stays. x is followed where
// its operand converts to its type implicitly, outside generic code.
inline void TypeCheck::NoteCast(AsCast *x, const Val &raw, const Val &cv, const Val &v) {
    auto removable = !InGenericCode() && Converts(cv, x->totype);
    if (auto it = castalts.find(x->child); it != castalts.end()) {
        auto a = it->second;
        // x's value is of its type whatever it converts. Its operand is
        // checked with no type, so a float of literals it leaves computes at
        // f64.
        auto same = a.pending ? !a.settle || !IsF32(a.settle) : SameReach(a, x->totype, false);
        CastVerdict(a, same ? CastReason(a, a.reach ? a.reach : x->totype, !a.pending) : "",
                    removable ? x->Origin() : nullptr);
    }
    if (!removable) return;
    CastAlt a;
    a.cast = x;
    a.operand = a.alt = cv;
    a.raw = raw;
    castalts[x] = a;
    if (!SameNumVal(cv, v)) casttyped.insert(x);
}

// n's value without a followed cast is tv where it is v, and n computed the
// operand at `at`. The same value makes the cast redundant. A float of
// literals where v is a plain float computes at the type it settles at: n's
// consumer judges that, as it judges a value that differs from v in no more
// than how it adapts (a constant). Anything else keeps the cast.
inline void TypeCheck::FollowCast(Node *n, const CastAlt &a, const Val &tv, const Val &v,
                                  TypeExpr *at) {
    if (!tv.type) return CastVerdict(a, "");
    auto next = a;
    next.alt = tv;
    next.pending = true;
    if (tv.litfloat && !v.litfloat && v.type->kind == TY_FLT) {
        // It has to settle at the type v computes at: the cast's, or that of
        // the float of literals its own operand is.
        auto &cv = a.operand;
        auto settle = a.pending                        ? a.settle
                      : cv.litfloat && cv.ck == CK_NONE ? LoadType(cv.type)
                                                        : a.cast->totype;
        if (!settle || !SameNum(settle, LoadType(v.type)) || !Reaches(a, settle, false))
            return CastVerdict(a, "");
        next.settle = settle;
        next.reach = a.reach ? a.reach : settle;
    } else {
        if (!Reaches(a, at, false)) return CastVerdict(a, "");
        if (SameNumVal(tv, v)) return CastVerdict(a, CastReason(a, a.reach ? a.reach : at));
        if (!SameNum(LoadType(tv.type), LoadType(v.type)) || tv.litfloat || v.litfloat ||
            tv.litint || v.litint || tv.flexint || v.flexint)
            return CastVerdict(a, "");
        next.settle = nullptr;
        next.reach = a.reach ? a.reach : at;
    }
    castalts[n] = next;
    casttyped.insert(n);
}

// b's operands' followed casts, judged by b's value without each: the value
// of NumericBinary's trial (FollowCast), or for a comparison the type it
// compares at. Where both operands' values would differ without their
// casts (casttyped), each deletion is judged with the other's as well: both
// may go where b's value stays the same all three ways; where it does not
// survive both, one may go, the right one where it can, the left operand
// keeping the type as the reader meets it first.
inline void TypeCheck::JudgeBinaryCasts(Binary *b, Node *l, Node *r, const Val &lv,
                                        const Val &rv, const Val &v, TypeExpr *ct) {
    auto cmp = v.type->kind == TY_BOOL;
    auto same = [&](const Val &tv, TypeExpr *tct) {
        return tv.type && (cmp ? SameNum(tct, ct) : SameNumVal(tv, v));
    };
    CastAlt alts[2];
    Val tvs[2];
    TypeExpr *tcts[2] = { nullptr, nullptr };
    bool has[2], alone[2] = { false, false };
    for (auto s = 0; s < 2; s++) {
        auto it = castalts.find(s ? r : l);
        has[s] = it != castalts.end();
        if (!has[s]) continue;
        alts[s] = it->second;
        tvs[s] = NumericBinary(b, s ? lv : alts[s].alt, s ? alts[s].alt : rv, tcts[s], true);
        alone[s] = same(tvs[s], tcts[s]) && Reaches(alts[s], tcts[s], false);
    }
    auto both = casttyped.count(l) && casttyped.count(r);
    auto together = false;
    if (both) {
        TypeExpr *tct = nullptr;
        auto tv = NumericBinary(b, alts[0].alt, alts[1].alt, tct, true);
        together = same(tv, tct) && Reaches(alts[0], tct, false) && Reaches(alts[1], tct, false);
    }
    for (auto s = 0; s < 2; s++) {
        if (!has[s]) continue;
        auto &a = alts[s];
        if (!cmp && !both) {
            FollowCast(b, a, tvs[s], v, tcts[s]);
            continue;
        }
        auto redundant = alone[s] && (!both || together || (s ? true : !alone[1]));
        CastVerdict(a, redundant ? CastReason(a, a.reach ? a.reach : tcts[s]) : "");
    }
}

// u's operand's followed cast, judged by u's value without it, as an
// operator's are (JudgeBinaryCasts).
inline void TypeCheck::JudgeUnaryCast(Unary *u, Node *c, const Val &r) {
    auto it = castalts.find(c);
    if (it == castalts.end()) return;
    auto a = it->second;
    auto tr = NumericUnary(u, a.alt, true);
    FollowCast(u, a, tr, r, tr.type ? LoadType(tr.type) : nullptr);
}

// The value of n meets its destination: of type dt, or of none, where n's
// own type is what it binds (an inferred `let`, a branch of a construct with
// no type, a rendered argument); v is n's value as checked. A followed cast
// is redundant where the value without it converts to dt as n's does, or
// with no type is the same. An argument of a builtin its call takes for
// want of a function of its name is not judged: a function might take it
// without the cast.
inline void TypeCheck::JudgeCastAt(Node *n, TypeExpr *dt, const Val &v) {
    auto it = castalts.find(n);
    if (it == castalts.end()) return;
    auto a = it->second;
    auto &tv = a.alt;
    auto same = false;
    if (builtinfallback.count(n)) {
        same = false;
    } else if (!dt) {
        dt = LoadType(v.type);
        same = SameNumVal(tv, v);
    } else {
        same = (dt->kind == TY_INT || dt->kind == TY_FLT) && Converts(tv, dt) &&
               Reaches(a, dt, true);
    }
    CastVerdict(a, same ? CastReason(a, a.reach ? a.reach : dt) : "");
}

// Why deleting the cast a follows would leave the program the same, where
// its operand reaches type `reach` without it; `bycast`: through a cast
// around it.
inline string TypeCheck::CastReason(const CastAlt &a, TypeExpr *reach, bool bycast) {
    auto &cv = a.operand;
    auto st = LoadType(cv.type), tt = a.cast->totype;
    auto what = ExprStr(a.cast->child);
    auto an = [&](TypeExpr *t) {
        auto s = TypeStr(t);
        return cat(s[0] == 'i' || s[0] == 'f' ? "an " : "a ", s);
    };
    auto literal = cv.ck != CK_NONE || cv.litint || cv.litfloat;
    if (!literal && SameNum(st, tt)) return cat(what, " is already ", TypeStr(tt));
    if (bycast) return cat("the cast to ", TypeStr(reach), " around it converts ", what, " as well");
    if (!literal && SameNum(st, reach)) return cat(what, " is already ", TypeStr(reach), " here");
    if (cv.ck == CK_INT && IsIntT(reach))
        return cat(cv.constfrom ? cat(what, " is ", ConstStr(cv), ", which") : ConstStr(cv),
                   " fits ", TypeStr(reach), " here");
    if (cv.ck != CK_NONE || cv.litfloat) return cat(what, " is ", an(reach), " here");
    if (cv.litint)
        return cat("its constants ", cv.litlo, " to ", cv.lithi, " fit ", TypeStr(reach), " here");
    return cat(an(st), " converts to ", TypeStr(reach), " implicitly here");
}

// A check's verdict on the cast a follows (its clone in this
// specialization): redundant for `reason`, or with none not. A verdict
// reached at the cast `dependson` holds only while that one stays. It waits
// as a warning does while the check may be repeated (Warn), and
// ReportRedundantCasts weighs every check's.
inline void TypeCheck::CastVerdict(const CastAlt &a, const string &reason,
                                   const AsCast *dependson) {
    if (quiet) return;
    Warning w;
    w.cast = a.cast->Origin();
    w.redundant = !reason.empty();
    if (w.redundant) {
        w.text = cat("redundant `as", a.cast->unchecked ? "! " : " ", TypeStr(a.cast->totype),
                     "`: ", reason);
        w.dependson = dependson;
    }
    if (WarningsHeld()) cur.pendingwarnings.push_back(std::move(w));
    else castverdicts.push_back(std::move(w));
}

// Once the whole program is checked, a cast warns, in source order, where
// every verdict on it found it redundant and no cast a verdict was reached at
// warns itself.
inline void TypeCheck::ReportRedundantCasts() {
    FlushWarnings();
    struct Tally {
        size_t first = SIZE_MAX;
        bool needed = false;
        vector<const AsCast *> deps;
    };
    unordered_map<const AsCast *, Tally> tally;
    for (size_t i = 0; i < castverdicts.size(); i++) {
        auto &w = castverdicts[i];
        auto &t = tally[w.cast];
        if (!w.redundant) {
            t.needed = true;
            continue;
        }
        if (w.dependson) t.deps.push_back(w.dependson);
        if (t.first == SIZE_MAX) t.first = i;
    }
    unordered_map<const AsCast *, bool> warns;
    function<bool(const AsCast *)> warn = [&](const AsCast *x) {
        if (auto it = warns.find(x); it != warns.end()) return it->second;
        auto it = tally.find(x);
        auto w = it != tally.end() && !it->second.needed && it->second.first != SIZE_MAX;
        if (w)
            for (auto d : it->second.deps) w = w && !warn(d);
        return warns[x] = w;
    };
    vector<size_t> out;
    for (auto &[x, t] : tally) if (warn(x)) out.push_back(t.first);
    auto key = [&](size_t i) {
        auto l = castverdicts[i].cast->line;
        return tuple(l.fileidx, l.line, i);
    };
    sort(out.begin(), out.end(), [&](size_t i, size_t j) { return key(i) < key(j); });
    for (auto i : out)
        fputs(cat(Where(castverdicts[i].cast->line), ": warning: ", castverdicts[i].text, "\n")
                  .c_str(),
              stderr);
}

// A name in a compile-time size, capacity or fill count, or in an integer
// match pattern, checked in the scope it is written in. It resolves as any
// name does (§11.1): a local of the name -- a parameter, or a variable around
// a nested function or a block --, a type parameter or a nested function
// hides the global of the name and is no constant (§3.3, §8.1).
// ConstIntValue cannot tell, and takes the global: a size is evaluated
// wherever its type is compared, in another scope as often as not. In a
// parameter default, which names what a top-level declaration would, a name
// its scope makes something else is an error as well (DefaultScopeName).
inline void TypeCheck::ConstName(Ident *id, const char *what) {
    if (auto vd = LookupVar(id->name, id->ns); vd && !vd->isglobal)
        Error(id, cat(what, " ", id->name, " names a local variable, not a constant"));
    if (auto kind = ScopeNameKind(id->name))
        Error(id, cat(what, " ", id->name, " names a ", kind, ", not a constant"));
    DefaultScopeName(id->name, id, false);
}

inline void TypeCheck::ConstNames(Node *n, const char *what) {
    ConstExprNames(n, what, [&](Ident *id, const char *w) { ConstName(id, w); });
}

// The sizes and capacities of a type written in the scope being checked: an
// annotation, a type argument, a literal's type, or a nested function's or
// a trailing block's signature. A type that comes from elsewhere -- a
// field's, one a type parameter is bound to, an alias's -- was checked where
// it is written.
inline void TypeCheck::ConstNamesIn(TypeExpr *t) {
    SizeNames(t, [&](Ident *id, const char *w) { ConstName(id, w); });
}

// A size in the parameter or result types of a function or a block names
// none of its parameters or type parameters: there the name means the
// parameter, which holds an argument, or the type parameter, which stands
// for a type, no constant, and hides the global of the name (§3.3, §11.1).
inline void TypeCheck::SignatureNames(const vector<Param> &params,
                                      const vector<TypeExpr *> &rets,
                                      const vector<GenericParam> &generics) {
    auto check = [&](TypeExpr *t) {
        SizeNames(t, [&](Ident *id, const char *what) {
            for (auto &p : params)
                if (p.name == id->name)
                    Error(id, cat(what, " ", id->name, " names a parameter, not a constant"));
        });
        TypeParamSizes(t, generics);
    };
    for (auto &p : params)
        if (p.type) check(p.type);
    for (auto t : rets) check(t);
}

// A size in t naming one of the type parameters in `generics`: a function's
// in its signature, a generic struct's or enum's in its fields' types.
inline void TypeCheck::TypeParamSizes(TypeExpr *t, const vector<GenericParam> &generics) {
    SizeNames(t, [&](Ident *id, const char *what) {
        for (auto &g : generics)
            if (g.name == id->name)
                Error(id, cat(what, " ", id->name, " names a type parameter, not a constant"));
    });
}

inline int64_t TypeCheck::FillCount(Node *n) {
    ConstNames(n, "array fill count");
    return ConstIntOrError(n, "array fill count");
}

// Array extents and fill counts obey the same integer types as expressions
// (§3.3, §6.1–6.2). Keep known values separate from literal adaptability:
// a named u8 constant is known here, but N + 1 still computes at u8 width.
// This runs even while type declarations are validated, before globals have
// been checked, so resolve their initializers without evaluating runtime code
// or attaching caller-local bindings to the shared expression nodes. A name
// is the global of the name; where the expression is written, a local of
// the name is an error (ConstName).
inline bool TypeCheck::ConstIntValue(Node *n, Val &v, bool &literal,
                                     set<VarDecl *> &visiting, ConstUse *use) {
    if (auto i = Is<IntLit>(n)) {
        v.type = ast.inttypes[i->uns ? IS_U64 : IS_I64];
        v.ck = CK_INT;
        v.ival = i->val;
        v.uns = i->uns;
        literal = true;
        return true;
    }
    if (auto id = Is<Ident>(n)) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (!g || g->isvar || g->inits.size() != 1) return false;
        if (!visiting.insert(g).second)
            Error(n, cat("cycle in constant initializer: ", id->name));
        if (use) {
            // The first global on the way is one the use names itself.
            if (visiting.size() == 1) use->named = g->defs[0];
            RelyOnConstant(*use, g->defs[0]);
        }
        auto ok = ConstIntValue(g->inits[0], v, literal, visiting, use);
        visiting.erase(g);
        if (!ok) return false;
        if (g->type) {
            auto t = g->type;
            if (!IsIntT(t)) return false;
            if (!FitsIntStorage(v.ival, v.uns, t->intstorage))
                Error(n, cat("constant ", ConstStr(v), " does not fit ", TypeStr(t)));
            v.type = ast.PlainOf(t);
            v.uns = t->intstorage == IS_U64 && v.ival < 0;
        }
        literal = false;
        return true;
    }
    if (auto u = Is<Unary>(n)) {
        if (u->op != T_MINUS && u->op != T_BITNOT) return false;
        if (!ConstIntValue(u->child, v, literal, visiting, use)) return false;
        auto s = v.type->intstorage;
        if (u->op == T_MINUS) {
            if (v.uns && (!literal || v.ival != INT64_MIN))
                Error(n, "negated constant too large for i64");
            if (!literal && IsUnsigned(s))
                Error(n, cat("cannot negate a value of unsigned type ", TypeStr(v.type)));
            if (!v.uns && v.ival == INT64_MIN)
                Error(n, "signed overflow in constant expression");
            v.ival = (int64_t)(0u - (uint64_t)v.ival);
            if (literal) v.type = ast.inttypes[IS_I64];
            v.uns = false;
        } else {
            v.ival = ~v.ival;
            auto bits = IntBits(s);
            if (IsUnsigned(s) && bits < 64)
                v.ival = (int64_t)((uint64_t)v.ival & ((1ull << bits) - 1));
            v.uns = s == IS_U64 && v.ival < 0;
        }
        if (!FitsIntStorage(v.ival, v.uns, v.type->intstorage))
            Error(n, "signed overflow in constant expression");
        return true;
    }
    if (auto b = Is<Binary>(n)) {
        switch (b->op) {
            case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD:
            case T_BITAND: case T_BITOR: case T_XOR: case T_SHL: case T_SHR: break;
            default: return false;
        }
        Val l, r;
        bool llit, rlit;
        if (!ConstIntValue(b->left, l, llit, visiting, use) ||
            !ConstIntValue(b->right, r, rlit, visiting, use)) return false;
        if (b->op == T_SHL || b->op == T_SHR) {
            v.type = l.type;
        } else {
            auto lf = l, rf = r;
            if (!llit) lf.ck = CK_NONE;
            if (!rlit) rf.ck = CK_NONE;
            v.type = UnifyNumeric(n, b->op, lf, rf, l.type, r.type);
        }
        if (b->op == T_DIV && !IsUnsigned(v.type->intstorage) &&
            l.ival == INT64_MIN && r.ival == -1)
            Error(n, "constant division overflow");
        FoldInt(b->op, l, r, v, n);
        if (v.ck != CK_INT) Error(n, "signed overflow in constant expression");
        literal = llit && rlit;
        return true;
    }
    return false;
}

// All scalar leaves integers, or all floats; only structs and fixed
// arrays compose; the value must be fixed-size (a constructed result).
inline bool TypeCheck::ElementwiseOK(TypeExpr *t) {
    int isint = -1;
    function<bool(TypeExpr *)> rec = [&](TypeExpr *t2) -> bool {
        switch (t2->kind) {
            case TY_INT:
                if (t2->intstorage == IS_VARINT) return false;
                if (isint == 0) return false;
                isint = 1;
                return true;
            case TY_FLT:
                if (isint == 1) return false;
                isint = 0;
                return true;
            case TY_STRUCT: {
                auto inst = GetStructInst(t2);
                for (size_t i = 0; i < inst->ftypes.size(); i++)
                    if (inst->ftypes[i] && !rec(inst->ftypes[i])) return false;
                return true;
            }
            case TY_ARRAY:
                return t2->arr->akind == A_FIXED && rec(t2->arr->sub);
            default:
                return false;
        }
    };
    return (t->kind == TY_STRUCT || t->kind == TY_ARRAY) && rec(t);
}

// Scaling keeps the aggregate's nominal type, so every leaf must accept
// the same scalar type. Mixed-width aggregates still have their ordinary
// aggregate/aggregate arithmetic, but no implicit broadcast conversion.
inline TypeExpr *TypeCheck::ElementwiseScalarType(TypeExpr *t) {
    if (!ElementwiseOK(t)) return nullptr;
    TypeExpr *leaf = nullptr;
    function<bool(TypeExpr *)> rec = [&](TypeExpr *tt) {
        if (tt->kind == TY_STRUCT) {
            for (auto ft : GetStructInst(tt)->ftypes)
                if (ft && !rec(ft)) return false;
            return true;
        }
        if (tt->kind == TY_ARRAY) return rec(tt->arr->sub);
        if (!leaf) leaf = LoadType(tt);
        return TopConstEq(leaf, tt);
    };
    return rec(t) ? leaf : nullptr;
}

inline Val TypeCheck::CheckVariantConst(Dot *d, SEnum *en) {
    if (!en->generics.empty())
        Error(d, cat("generic enum ", en->name, " needs type arguments to name a variant"));
    auto t = ast.EnumOf(en, {}, false, d->line);
    auto inst = GetEnumInst(t);
    auto found = en->FindVariant(d->name);
    if (!found) Error(d, cat("enum ", en->name, " has no variant named ", d->name));
    if (found->has_payload)
        Error(d, cat("variant ", en->name, ".", d->name,
                     " has a payload; construct it with ", en->name, ".", d->name, " { ... }"));
    d->variantconst = found;
    d->einst = inst;
    if (!inst->allfixed || inst->selfrel) t->enu->varmode = true;
    Val v;
    v.type = t;
    return v;
}

// ------------------------------------------------------------------
// Struct and variant literals (§4.2). The per-node entry is
// StructLit::Check in typecheck_nodes.h.

// `selft` is the type of the value this literal constructs (the enum type
// for a variant literal in fixed enum mode), which is what `self` names.
inline TypeCheck::LitDeep TypeCheck::CheckInits(StructLit *sl, vector<Field> &fields,
                                               vector<TypeExpr *> &ftypes,
                                               string_view what, TypeExpr *selft) {
    LitDeep deep;
    auto named = !sl->inits.empty() && !sl->inits[0].name.empty();
    vector<bool> got(fields.size(), false);
    auto pos = 0;
    for (auto &fi : sl->inits) {
        auto idx = -1;
        if (named) {
            for (auto i = 0; i < (int)fields.size(); i++)
                if (!fields[i].ispad && fields[i].name == fi.name) { idx = i; break; }
            if (idx < 0) Error(fi.val, cat(what, " has no field ", fi.name));
            if (got[idx]) Error(fi.val, cat("duplicate initializer for field ", fi.name));
            // Declaration order is required (§4.2): values construct
            // front-to-back, so out-of-order names would obfuscate either
            // evaluation order or cost.
            for (auto i = idx + 1; i < (int)fields.size(); i++)
                if (got[i]) {
                    // Include every supplied field, not just the prefix
                    // seen so far. Omitted defaults and padding need not
                    // be added when the user puts the names in order.
                    string order;
                    for (auto &field : fields) {
                        if (field.ispad) continue;
                        for (auto &init : sl->inits) {
                            if (init.name != field.name) continue;
                            Append(order, order.empty() ? "" : ", ", field.name);
                            break;
                        }
                    }
                    Error(fi.val, cat("field initializers must follow declaration "
                                      "order: ", fi.name, " comes before ", fields[i].name,
                                      "; supplied fields in declaration order: ", order,
                                      "; fields are evaluated and constructed front-to-back"));
                }
        } else {
            while (pos < (int)fields.size() && fields[pos].ispad) pos++;
            if (pos >= (int)fields.size())
                Error(fi.val, cat("too many initializers for ", what));
            idx = pos++;
        }
        got[idx] = true;
        sl->fieldindices.push_back(idx);
    }
    vector<FieldInit> ordered(fields.size());
    for (size_t i = 0; i < sl->inits.size(); i++) ordered[sl->fieldindices[i]] = sl->inits[i];
    sl->inits.clear();
    sl->fieldindices.clear();
    for (auto i = 0; i < (int)fields.size(); i++) {
        if (fields[i].ispad) continue;
        auto fi = ordered[i];
        fi.name = fields[i].name;
        if (!fi.val) {
            if (fields[i].defaultval) {
                fi.val = fields[i].defaultval->Clone(ast);
                fi.fromdefault = true;
            } else if (IsOptional(ftypes[i])) continue;
            else if (sl->defaultall) {
                string why;
                if (!HasDefault(ftypes[i], why))
                    Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                                  " (it has no declared default, and ", why, ")"));
                fi.val = DefaultValue(ftypes[i], sl->line);
            }
            else Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                               " (it has no default)"));
        }
        sl->inits.push_back(fi);
        sl->fieldindices.push_back(i);
    }
    // Every initializer is in place before any is checked: each is an
    // operand of the literal, the ones before it held and the ones after it
    // still to run (HeldOperands, LaterOperands).
    for (size_t k = 0; k < sl->inits.size(); k++) {
        auto &fi = sl->inits[k];
        auto ft = ftypes[sl->fieldindices[k]];
        if (Is<SelfRef>(fi.val)) CheckSelfInit(fi.val, ft, selft);
        else {
            SlotScope ss(*this, true);
            auto fv = fi.fromdefault
                          ? CheckDefaultInit(fi.val, ft, selft, sl, fields[sl->fieldindices[k]])
                          : CheckValue(fi.val, ft);
            NoteLitElem(deep, fi.val, fv, ft);
        }
    }
    return deep;
}

// `self` in a field initializer: the field must hold a non-optional
// relative reference to the very value being constructed (§3.9), which is
// the one reference to it that exists before the value does. Optional
// relative references are excluded because offset 0 is their null.
inline void TypeCheck::CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft) {
    if (ft->kind != TY_REF || ft->ref->lenstorage < 0)
        Error(n, cat("self initializes relative-reference fields (T&<u32> and friends), "
                     "not ", TypeStr(ft)));
    if (ft->ref->optional)
        Error(n, cat("self cannot initialize the optional relative reference ",
                     TypeStr(ft), ": offset 0 is its null (§3.9)"));
    if (!TypeEq(ft->ref->sub, selft))
        Error(n, cat("self here is a value of type ", TypeStr(selft), ", which does not "
                     "fit a field of type ", TypeStr(ft)));
    // An `in pool` self is the value's own offset in the pool, so unlike a
    // self-relative one it only means anything where the literal is being
    // built: inside that pool.
    if (ft->ref->pool && (!curdst.roots.Exact() || PoolOf(curdst.roots.Root()) != ft->ref->pool))
        Error(n, cat("self initializes ", TypeStr(ft), " only in a literal being built "
                     "inside ", ft->ref->pool->name, " (a push, an append, an alloc, or an "
                     "element store), since it stores the value's own offset in it (§3.9)"));
    // A resizable pointee needs a header the offset cannot carry; the root
    // rule keeps every other relative reference away from one, but a
    // self-reference satisfies that rule by construction.
    if (ClassOf(selft) == SC_RESIZABLE)
        Error(n, cat("self cannot be stored relative: ", TypeStr(selft),
                     " is resizable, and a relative reference is an offset alone (§3.9)"));
    n->exprtype = ft;
}

// ------------------------------------------------------------------
// Statements.

inline void TypeCheck::CheckStmts(Block *b) {
    for (size_t i = 0; i < b->stmts.size(); i++) {
        blockpos.back().idx = i;
        CheckStmt(b->stmts[i]);
    }
    blockpos.back().idx = b->stmts.size();
}

// Whether the code under n names `name`: a variable use, or a call of a
// nested function whose body does. Syntactic, so a shadowing declaration
// counts too, which only errs on the safe side.
inline bool TypeCheck::MentionsName(Node *n, string_view name, set<SFunction *> &seen) {
    if (!n) return false;
    if (auto id = Is<Ident>(n); id && id->name == name) return true;
    if (auto fd = Is<FnDecl>(n)) {
        if (fd->sf->body && seen.insert(fd->sf).second &&
            MentionsName(fd->sf->body, name, seen))
            return true;
    }
    if (auto c = Is<Call>(n)) {
        string_view callee;
        if (auto id = Is<Ident>(c->callee)) callee = id->name;
        else if (auto d = Is<Dot>(c->callee)) callee = d->name;
        if (!callee.empty()) {
            for (auto &[si, sf] : localfns)
                if (sf->name == callee && sf->body && seen.insert(sf).second &&
                    MentionsName(sf->body, name, seen))
                    return true;
        }
    }
    auto hit = false;
    n->Children([&](Node *ch) { hit = hit || MentionsName(ch, name, seen); });
    return hit;
}

// Whether variable v can be read again after the shrink being checked
// (§5.1): in the rest of its statement, later in an open block at or
// inside v's scope, or anywhere in a loop that contains this point and
// that v was declared outside of, whose next iteration runs the earlier
// part of the body again; so too anywhere in the body of a function value
// being run that v was declared outside of, which the function running it
// may call again. Only code of v's own frame, or of one nested in it, can
// name v: the same name in another frame -- the function running a function
// value's body, a callee checked inside its caller's check -- is another
// variable.
inline bool TypeCheck::UsedAfter(VarDef *v) {
    set<SFunction *> seen;
    auto vframe = FrameOfScope(Depth(v) - 1);
    auto mentions = [&](Node *n, int fi) {
        return NamesFrame(fi, vframe) && MentionsName(n, v->name, seen);
    };
    auto later = false;
    LaterOperands([&](Node *n, int fi) { later = later || mentions(n, fi); });
    if (later) return true;
    for (auto i = 0; i < (int)scopes.size(); i++) {
        if (scopes[i].kind != SK_LOOP) continue;
        // A `for` binding is rebound by the loop itself at every iteration.
        if (auto fl = Is<ForLoop>(scopes[i].node); fl && (fl->vdef == v || fl->idxdef == v))
            return true;
        if (Depth(v) <= i && scopes[i].node && mentions(scopes[i].node, FrameOfScope(i)))
            return true;
    }
    for (auto &bp : blockpos) {
        if (bp.scopeidx < Depth(v) - 1) continue;
        auto fi = FrameOfScope(bp.scopeidx);
        auto &fr = frames[fi];
        // A function value's body, whole.
        if (fr.isfunval && !fr.isdefault && bp.scopeidx == fr.scopebase &&
            Depth(v) - 1 < bp.scopeidx && mentions(bp.block, fi))
            return true;
        for (auto i = bp.idx + 1; i < bp.block->stmts.size(); i++)
            if (mentions(bp.block->stmts[i], fi)) return true;
        if (bp.idx < bp.block->stmts.size() && bp.block->tail && mentions(bp.block->tail, fi))
            return true;
    }
    return false;
}

inline void TypeCheck::CheckStmt(Node *n) {
    // A statement inside a returned value's block is no part of that value:
    // the function's locals outlive it.
    FlagScope rs(inreturn, false);
    NodeScope ns(*this, n);
    if (auto vd = Is<VarDecl>(n)) { CheckVarDecl(vd, false); return; }
    if (auto a = Is<Assign>(n)) { CheckAssign(a); return; }
    if (auto x = Is<IncDec>(n)) { CheckIncDec(x); return; }
    if (auto fd = Is<FnDecl>(n)) { DeclareLocalFn(fd); return; }
    CheckStmtExpr(n);
}

}  // namespace goose
