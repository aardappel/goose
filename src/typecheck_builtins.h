// Goose compiler — the typechecker's builtins (definitions of TypeCheck
// members, typecheck.h): the builtin functions and array members (§3.3,
// §3.7, §5.4, §11.2), text rendering through user `format` overloads, and
// the shrink rules of §5.1 and §5.2.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

// An argument of print/str/format (§3.7): every value type has a text
// form -- scalars and bool as text, u8 arrays and slices as their bytes
// (quoted inside an aggregate), other arrays as [a, b], structs and
// variants as their positional literal, references as their pointee,
// null as null. A user overload fn format(out: u8[>..]&, v: T) renders a
// T instead wherever one occurs; its specialization is recorded on the
// call for codegen.
inline void TypeCheck::CheckPrintable(Call *c, const char *what, Node *a) {
    auto av = CheckValue(a, nullptr);
    vector<TypeExpr *> seen;
    CheckRenderable(c, what, av.type, a, seen);
}

inline void TypeCheck::CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at,
                                       vector<TypeExpr *> &seen) {
    for (auto s : seen) if (TypeEq(s, t)) return;   // Recursion through references.
    seen.push_back(t);
    if (UserFormat(c, t)) return;
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: return;
        case TY_ARRAY: CheckRenderable(c, what, t->arr->sub, at, seen); return;
        case TY_SLICE: CheckRenderable(c, what, t->sub, at, seen); return;
        case TY_REF: CheckRenderable(c, what, t->ref->sub, at, seen); return;
        case TY_STRUCT: {
            auto si = GetStructInst(t);
            for (auto ft : si->ftypes) if (ft) CheckRenderable(c, what, ft, at, seen);
            return;
        }
        case TY_ENUM: {
            auto ei = GetEnumInst(t);
            for (auto &vf : ei->vftypes)
                for (auto ft : vf) if (ft) CheckRenderable(c, what, ft, at, seen);
            return;
        }
        case TY_VARIANT: {
            auto ei = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(ei->en, t->var->variant);
            for (auto ft : ei->vftypes[vi]) if (ft) CheckRenderable(c, what, ft, at, seen);
            return;
        }
        default:
            Error(at, cat(what, " cannot render a value of type ", TypeStr(t)));
    }
}

// The user's `format` overload for t, instantiated for a builder rooted
// anywhere and a T by value or by reference (the two parameter shapes
// such an overload takes), once per print call. The overloads tried are
// those of the type's own namespace, then the global ones: rendering
// follows the type, not the namespace of whoever prints it
// (docs/design/namespaces.md).
inline FnSpec *TypeCheck::UserFormat(Call *c, TypeExpr *t) {
    for (auto &fs : c->fmtspecs) if (TypeEq(fs.first, t)) return fs.second;
    auto tns = NominalNs(t);
    if (auto sp = UserFormatIn(c, t, tns)) return sp;
    return tns.empty() ? nullptr : UserFormatIn(c, t, {});
}

inline FnSpec *TypeCheck::UserFormatIn(Call *c, TypeExpr *t, string_view ns) {
    auto n = ast.FindNS(ns);
    if (!n) return nullptr;
    auto fit = n->functionmap.find("format");
    if (fit == n->functionmap.end()) return nullptr;
    for (auto sf : fit->second) {
        if (sf->params.size() != 2 || !sf->params[0].type || !sf->params[1].type ||
            !sf->generics.empty() || sf->isnested)
            continue;
        auto pt1 = Subst(sf->params[1].type);
        auto p1 = IsPlainRef(pt1) ? pt1->ref->sub : pt1;
        if (!TypeEq(p1, t)) continue;
        auto pt0 = Subst(sf->params[0].type);
        if (!IsPlainRef(pt0) || !IsArrayKind(pt0->ref->sub, A_GROW) ||
            !IsU8(pt0->ref->sub->arr->sub))
            continue;
        vector<Val> argvals(2);
        argvals[0].type = pt0;
        argvals[0].root = temproot;
        argvals[0].rootexact = true;
        argvals[0].writable = true;
        argvals[1].type = pt1;
        argvals[1].root = temproot;
        argvals[1].rootexact = true;
        argvals[1].writable = true;
        MatchInfo mi;
        mi.sf = sf;
        mi.env = nullptr;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why)) continue;
        auto sp = GetOrCreateSpec(mi, argvals, c);
        c->fmtspecs.push_back({ t, sp });
        return sp;
    }
    return nullptr;
}

// A shrink (`pop`, `resize` down, `clear`) of a grow-only array (§5.1).
// Everything below the stack top belongs to the array's elements for as
// long as it lives, so handing part of the region back is safe exactly
// when nothing can still point into it: the call stands on its own (a
// statement, an initializer, or the right-hand side of an assignment to a
// variable), so no reference taken earlier in the same expression outlives
// it; no reference or slice variable in scope points into the array; and
// no container in scope had a reference into it stored, which every store
// the checker has seen is on record for (storeevents). The receiver is
// named directly, or through a reference variable or parameter, in which
// case the array behind the reference is what shrinks.
inline void TypeCheck::CheckGrowShrink(Node *at, bool standalone, const char *op, Node *recv,
                                       TypeExpr *rtype) {
    auto id = Is<Ident>(recv);
    auto vd = id ? id->vdef : nullptr;
    auto at_type = rtype->kind == TY_REF ? rtype->ref->sub : rtype;
    if (!vd || at_type->kind != TY_ARRAY)
        Error(at, cat(op, " on a grow-only array names the array's variable, or a "
                      "reference to it, not an element of another value (§5.1)"));
    // Through a reference variable or parameter: the array it points at.
    if (vd->type && vd->type->kind == TY_REF) vd = CanonRoot(RefRootOf(vd));
    if (!vd || vd == temproot)
        Error(at, cat(op, " through a reference whose array is not known (§5.1)"));
    GrowOnlyShrinkAt(at, standalone, op, vd);
}

// A grow-only array shrinks wherever nothing can still point into it: a
// local of this function, a caller's array reached through a reference
// parameter, a global, or an enclosing function's local. Everything in
// scope is scanned; a shrink through a parameter or of a global is also
// recorded for the callers, whose own scopes are scanned at the call.
inline void TypeCheck::GrowOnlyShrinkAt(Node *c, bool standalone, const string &op, VarDef *vd) {
    if (!frames.back().spec)
        Error(c, cat("cannot ", op, " ", vd->name, " in a global initializer (§5.1)"));
    if (vd->reusable)
        Error(c, cat("cannot ", op, " reusable pool ", vd->name,
                     ": its slots stay live for the freelist (§5.4)"));
    if (!standalone)
        Error(c, cat("cannot ", op, " ", vd->name,
                     " inside a larger expression: a reference taken earlier in it may "
                     "still be live, so bind the result first (§5.1)"));
    if (invalue)
        Error(c, cat("cannot ", op, " ", vd->name,
                     " inside a value-producing expression: references taken earlier in "
                     "it may still be live (§5.1)"));
    for (auto v : vars) {
        if (v == vd || !v->type) continue;
        auto t = v->type;
        if (t->kind == TY_REF || t->kind == TY_SLICE) {
            // A recorded root is exact only while the variable keeps its
            // first binding: a `var` may since have been rebound to any
            // root at the same depth, and one not bound yet can still
            // commit to this array further down a loop body. A pointee
            // the array cannot contain by value rules the variable out,
            // and so does a reference to a whole resizable value, which
            // is the path to an array rather than a pointer into one.
            if (t->kind == TY_REF && ClassOf(t->ref->sub) == SC_RESIZABLE) continue;
            auto of = PointeeOf(t);
            // A bytes_of view is over the element region itself, so it
            // survives this filter however unrelated its pointee looks.
            if (!v->ref.byteview && of && vd->type && !CanContain(LoadType(vd->type), of))
                continue;
            auto root = RefRootOf(v);
            auto holds = root == vd || (v->isvar && Depth(root) == Depth(vd)) ||
                         (!v->refrootknown && Depth(v) >= Depth(vd));
            if (!holds || !UsedAfter(v)) continue;
        } else {
            // Any other value holds references only where a store put
            // them, and every store this function can see is on record
            // (§9.2); a type without a plain reference or slice in it
            // (flat, or linked by relative references only) has no room
            // for one.
            if (!HoldsPlainRef(t)) continue;
            Line where;
            if (!HolderMayPointInto(v, vd, 0, &where) || !UsedAfter(v)) continue;
            Error(c, cat("cannot ", op, " ", vd->name, " while ", v->name,
                         " is still used: a reference into it was stored there at ",
                         Where(where), " (§5.1)"));
        }
        Error(c, cat("cannot ", op, " ", vd->name, " while ", v->name,
                     " is still used: it may hold a reference or slice into it (§5.1)"));
    }
    if (vd->isglobal) {
        // What other globals hold cannot be enumerated from here: any one
        // whose type can hold a reference to something this array can
        // contain counts as holding one.
        for (auto g : ast.globals) {
            for (auto gd : g->defs) {
                if (gd == vd || !gd->type || !HoldsPlainRef(gd->type)) continue;
                vector<TypeExpr *> ps;
                RefPointees(gd->type, ps);
                for (auto pt : ps)
                    if (CanContain(LoadType(vd->type), pt))
                        Error(c, cat("cannot ", op, " ", vd->name, ": global ", gd->name,
                                     " may hold a reference into it (§5.1)"));
            }
        }
    }
    if (vd->isglobal || !vd->type) NoteShrink(vd);
    // Inside a loop, a store later in the body reaches this shrink on the
    // next iteration: those are checked when the outermost loop ends.
    auto loopscope = -1;
    for (auto i = frames.back().scopebase; i < (int)scopes.size(); i++)
        if (scopes[i].kind == SK_LOOP) { loopscope = i; break; }
    if (loopscope >= 0) {
        PendingShrink ps;
        ps.at = c;
        ps.op = op;
        ps.vd = vd;
        ps.eventstart = storeevents.size();
        ps.loopscope = loopscope;
        // A holder declared inside the loop is fresh every iteration; only
        // one declared outside it carries a store to the next.
        for (auto v : vars)
            if (v != vd && v->type && v->type->kind != TY_REF && v->type->kind != TY_SLICE &&
                HoldsPlainRef(v->type) && Depth(v) <= loopscope)
                ps.holders.push_back(v);
        pendingshrinks.push_back(ps);
    }
}

// A field or element of a literal that is a reference, slice or holder:
// its root joins the literal's.
inline void TypeCheck::NoteLitElem(const Val &v, TypeExpr *t) {
    if (!t) return;
    auto isrs = t->kind == TY_REF || t->kind == TY_SLICE;
    if (!isrs && !HoldsPlainRef(t)) return;
    if (v.isnull) return;
    auto r = CanonRoot(isrs ? v.root : HolderRootOf(v));
    auto exact = isrs ? v.rootexact : v.holderset && v.holderexact;
    if (!litdeep.set || Depth(r) > Depth(litdeep.root)) {
        litdeep.exact = exact && (!litdeep.set || litdeep.root == r);
        litdeep.root = r;
    } else if (litdeep.root != r) {
        litdeep.exact = false;
    }
    litdeep.set = true;
}

inline void TypeCheck::HolderFromLit(Val &v) {
    if (!v.type || !HoldsPlainRef(v.type)) return;
    v.holderset = true;
    v.holderroot = litdeep.set ? litdeep.root : nullptr;
    v.holderexact = litdeep.set && litdeep.exact;
}

inline void TypeCheck::RecordStore(VarDef *container, const Val &v, TypeExpr *pointee,
                                   bool varbind, VarDef *src) {
    if (!container || varbind) return;
    StoreEvent e;
    e.container = container;
    e.root = CanonRoot(v.root);
    e.src = src == container ? nullptr : src;
    // A reference read back out of a container inexactly (§9.5) points
    // at whatever was stored into that container: its stores are the
    // precise answer, where a bound would implicate every sibling.
    if (!e.src && !v.rootexact && v.rootfrom && CanonRoot(v.rootfrom) != container)
        e.src = CanonRoot(v.rootfrom);
    e.exact = v.rootexact;
    e.pointee = pointee;
    if (fitnode) e.at = fitnode->line;
    storeevents.push_back(e);
    // A store into a caller's storage (through a reference parameter's
    // class root) is the caller's to know: kept on the specialization for
    // its call sites to map back.
    if (!container->type && !container->isglobal)
        if (auto spec = CurRealFrame().spec) spec->classevents.push_back(e);
    // The container's contents: the deepest root stored into it so far.
    if (container->type && container->type->kind != TY_REF && container->type->kind != TY_SLICE) {
        if (!container->contentset || Depth(e.root) > Depth(container->contentroot)) {
            container->contentexact = e.exact && (!container->contentset ||
                                                  container->contentroot == e.root);
            container->contentroot = e.root;
        } else if (container->contentroot != e.root) {
            container->contentexact = false;
        }
        container->contentset = true;
    }
}

// What the callee stored into the caller's containers, as the caller's
// own events: a store through reference parameter p into something
// rooted at parameter q becomes a store into argument p's root of a value
// rooted at argument q's. A callee still being checked (a back edge) may
// have stored any reference argument into any container argument.
inline void TypeCheck::ApplyCalleeStores(FnSpec *spec, vector<Val> &argvals, Node *at) {
    auto argroot = [&](size_t q) -> pair<VarDef *, bool> {
        auto pt = spec->argtypes[q];
        auto ph = pt->kind != TY_REF && pt->kind != TY_SLICE;
        return { CanonRoot(ph ? HolderRootOf(argvals[q]) : argvals[q].root),
                 ph ? argvals[q].holderset && argvals[q].holderexact : argvals[q].rootexact };
    };
    auto paramof = [&](VarDef *cr) -> int {
        for (size_t p = 0; p < spec->params.size() && p < argvals.size(); p++)
            if (cr && spec->params[p]->ref.root == cr) return (int)p;
        return -1;
    };
    auto push = [&](VarDef *container, VarDef *r, bool exact, TypeExpr *pointee, VarDef *src) {
        if (!container) return;
        StoreEvent e;
        e.container = container;
        e.root = r;
        e.src = src == container ? nullptr : src;
        e.exact = exact;
        e.pointee = pointee;
        e.at = at->line;
        storeevents.push_back(e);
        if (!container->type && !container->isglobal)
            if (auto cur = CurRealFrame().spec) cur->classevents.push_back(e);
    };
    // A class root of the callee, as seen from here: the argument's root.
    auto mapped = [&](VarDef *cr, bool &exact) -> VarDef * {
        auto q = paramof(cr);
        if (q < 0) return cr;
        auto [qr, qe] = argroot(q);
        exact = exact && qe;
        return qr;
    };
    if (spec->inprogress) {
        for (size_t p = 0; p < spec->argtypes.size() && p < argvals.size(); p++) {
            auto pt = spec->argtypes[p];
            if (pt->kind != TY_REF || !HoldsPlainRef(pt->ref->sub)) continue;
            for (size_t q = 0; q < spec->argtypes.size() && q < argvals.size(); q++) {
                auto qt = spec->argtypes[q];
                if (qt->kind != TY_REF && qt->kind != TY_SLICE && !HoldsPlainRef(qt)) continue;
                auto [r, exact] = argroot(q);
                push(CanonRoot(argvals[p].root), r, false,
                     qt->kind == TY_REF || qt->kind == TY_SLICE ? PointeeOf(qt) : nullptr,
                     nullptr);
                (void)exact;
            }
        }
        return;
    }
    // A function value's body, checked inside the callee, stores values
    // rooted at the callee's parameters into its own lexical containers:
    // those roots are this call's arguments.
    for (auto i = spec->eventstart; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        auto exact = e.exact;
        e.root = mapped(e.root, exact);
        e.src = mapped(e.src, exact);
        e.exact = exact;
    }
    for (auto &e : spec->classevents) {
        auto p = paramof(e.container);
        if (p < 0) continue;
        auto pt = spec->argtypes[p];
        if (pt->kind != TY_REF) continue;   // A by-value parameter is the callee's own copy.
        auto exact = e.exact;
        auto r = mapped(e.root, exact);
        auto src = mapped(e.src, exact);
        push(CanonRoot(argvals[p].root), r, exact, e.pointee, src);
    }
}

// Whether a store into `holder`, from event `from` on, may have put a
// reference into `arr` there: one rooted at it exactly, or one bounded by
// a root the array outlives whose pointee the array's elements can hold.
inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, size_t from, Line *where) {
    set<VarDef *> seen;
    return HolderMayPointInto(holder, arr, from, where, seen);
}

inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, size_t from, Line *where,
                                          set<VarDef *> &seen) {
    if (!seen.insert(holder).second) return false;
    for (auto i = from; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        if (e.container != holder) continue;
        auto hit = false;
        if (e.src && e.src->isglobal) {
            // A global's stores may come from functions not checked yet, so
            // its type decides: any reference it can hold to something the
            // array can contain.
            vector<TypeExpr *> ps;
            if (e.src->type) RefPointees(e.src->type, ps);
            for (auto pt : ps) hit |= CanContain(LoadType(arr->type), pt);
        } else if (e.src) {
            // A copy of another container's contents: whatever that one
            // holds, from its own first event on.
            hit = HolderMayPointInto(e.src, arr, 0, where, seen);
        } else if (e.exact) {
            hit = e.root == arr;
        } else if (e.root) {
            hit = Depth(arr) <= Depth(e.root) &&
                  (!e.pointee || CanContain(LoadType(arr->type), e.pointee));
        }
        if (hit) { if (!e.src) *where = e.at; return true; }
    }
    return false;
}

inline void TypeCheck::ResolvePendingShrinks(int scopeidx) {
    for (size_t i = 0; i < pendingshrinks.size();) {
        auto &ps = pendingshrinks[i];
        if (ps.loopscope < scopeidx) { i++; continue; }
        for (auto h : ps.holders) {
            Line where;
            if (!HolderMayPointInto(h, ps.vd, ps.eventstart, &where)) continue;
            Error(ps.at, cat("cannot ", ps.op, " ", ps.vd->name, " while ", h->name,
                             " is in scope: a reference into it is stored there at ",
                             Where(where), ", which the next iteration reaches (§5.1)"));
        }
        pendingshrinks.erase(pendingshrinks.begin() + (long)i);
    }
}

// The pointee types of the plain references and slices a value of type t
// can hold: what a reference into some other array would be a reference
// to. Relative references point into their own root or a named pool.
inline void TypeCheck::RefPointees(TypeExpr *t, vector<TypeExpr *> &out) {
    switch (t->kind) {
        case TY_REF:
            if (t->ref->lenstorage < 0) out.push_back(LoadType(t->ref->sub));
            return;
        case TY_SLICE: out.push_back(t->sub); return;
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            for (auto ft : inst->ftypes) if (ft) RefPointees(ft, out);
            return;
        }
        case TY_ENUM: {
            auto inst = GetEnumInst(t);
            for (auto &vf : inst->vftypes) for (auto ft : vf) if (ft) RefPointees(ft, out);
            return;
        }
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
            for (auto ft : inst->vftypes[vi]) if (ft) RefPointees(ft, out);
            return;
        }
        case TY_ARRAY: RefPointees(t->arr->sub, out); return;
        default: return;
    }
}

// Whether root r is (or stands for a call-site root that is) a grow-only
// array: the receiver of a §5.1 shrink rather than a §5.2 one.
inline bool TypeCheck::IsGrowOnlyRootVar(VarDef *r) {
    auto v = r;
    while (v && !v->type && v->classfrom) v = v->classfrom;
    return v && v->type && IsArrayKind(v->type, A_GROW);
}

// The globals and parameters a function's body textually shrinks: what a
// call into a cycle still being checked is taken to shrink (§5.1).
inline void TypeCheck::SyntacticShrinks(SFunction *sf) {
    if (sf->shrinkscanned || !sf->body) return;
    sf->shrinkscanned = true;
    auto note = [&](Node *recv) {
        auto id = Is<Ident>(recv);
        if (!id) return;
        for (size_t i = 0; i < sf->params.size(); i++)
            if (sf->params[i].name == id->name) { sf->shrinkparamidx.push_back((int)i); return; }
        if (ast.LookupGlobal(id->name, id->ns)) sf->shrinkglobalnames.push_back(id->name);
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto c = Is<Call>(n)) {
            if (auto d = Is<Dot>(c->callee); d && (d->name == "pop" || d->name == "resize" ||
                                                    d->name == "clear"))
                note(d->obj);
        }
        if (auto a = Is<Assign>(n); a && a->op == T_ASSIGN) note(a->lval);
        n->Children([&](Node *ch) { walk(ch); });
    };
    walk(sf->body);
}

// A shrink of the grow-shrink array rooted at root (§5.2): no variable in
// scope may refer into it. Such references are held only by variables
// (they cannot be stored), so the scan is exact, up to a `var` reference
// the same-depth rebinding rule could have retargeted into it.
inline void TypeCheck::CheckShrinkHolders(Node *at, const string &op, VarDef *root,
                                          const string &what) {
    VisibleVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        if (v->type->kind != TY_REF && v->type->kind != TY_SLICE) return;
        // A reference to the whole array (or the value holding it) is the
        // path to it, not something a shrink invalidates.
        if (v->type->kind == TY_REF && ContainsGrowShrink(v->type->ref->sub)) return;
        // Nor is one whose pointee the array's elements cannot contain: a
        // slice of text rooted at a dictionary keyed by slices points at
        // the text, whatever else it might be rebound to.
        // A bytes_of view is over the element region itself, so the
        // pointee-type filter would dismiss exactly the case it is for.
        if (!v->ref.byteview && !GrowShrinkCanHold(root, PointeeOf(v->type))) return;
        auto r = RefRootOf(v);
        auto holds = r == root || (v->isvar && Depth(r) == Depth(root)) ||
                     (!v->refrootknown && Depth(v) >= Depth(root));
        if (!holds || !UsedAfter(v)) return;
        Error(at, cat("cannot ", op, " while ", v->name, " (bound at ", Where(v->line),
                      ") is still used: it may refer into ", what, " (§5.2)"));
    });
}

// Records a shrink for the callers' sake (§5.2): of a global, on the
// specialization being checked; through a parameter class, on the
// specialization that owns those parameters (a function value's body may
// shrink through its enclosing function's).
inline void TypeCheck::NoteShrink(VarDef *root) {
    if (root->isglobal) {
        if (auto spec = CurRealFrame().spec) spec->shrinkglobals.insert(root);
        return;
    }
    if (root->type) return;  // A local: its owner sees every shrink directly.
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        auto spec = frames[fi].spec;
        if (!spec) continue;
        auto found = false;
        for (size_t i = 0; i < spec->params.size(); i++) {
            if (RefRootOf(spec->params[i]) != root) continue;
            spec->shrinkparams.insert((int)i);
            found = true;
        }
        if (found) return;
    }
}

inline void TypeCheck::ShrinkGrowShrink(Node *at, const string &op, VarDef *root,
                                        const string &what) {
    if (!root) return;
    CheckShrinkHolders(at, op, root, what);
    NoteShrink(root);
}

// The callee's shrinks of grow-shrink arrays (§5.2) are the caller's:
// nothing in scope may refer into an argument it shrinks through or a
// global it shrinks, and both are recorded for the caller's own callers.
// A back edge's summary is incomplete, so it counts as shrinking every
// grow-shrink array it can reach.
inline void TypeCheck::ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                          string_view name) {
    auto pending = spec->inprogress;
    // A grow-only root takes the §5.1 scan (variables and recorded
    // stores), a grow-shrink one the §5.2 scan (variables only).
    auto shrink = [&](VarDef *root, const string &what) {
        if (IsGrowOnlyRootVar(root)) GrowOnlyShrinkAt(at, true, what, root);
        else ShrinkGrowShrink(at, cat(what, " ", root->name), root, string(root->name));
    };
    if (pending) SyntacticShrinks(spec->sf);
    ApplyCalleeStores(spec, argvals, at);
    for (size_t i = 0; i < argvals.size() && i < spec->argtypes.size(); i++) {
        auto pt = spec->argtypes[i];
        auto root = CanonRoot(argvals[i].root);
        if (!root) continue;
        bool shrinks;
        if (!pending) {
            shrinks = spec->shrinkparams.count((int)i) > 0;
        } else if (IsGrowOnlyRootVar(root)) {
            // A back edge's summary is incomplete; a grow-only argument
            // counts as shrunk where the callee textually shrinks it.
            shrinks = false;
            for (auto pi : spec->sf->shrinkparamidx) shrinks |= pi == (int)i;
        } else {
            shrinks = pt->kind == TY_REF && ContainsGrowShrink(pt->ref->sub);
        }
        if (shrinks) shrink(root, cat("call ", name, ", which shrinks"));
    }
    if (pending) {
        // Every grow-shrink global, and every grow-only global some function
        // still being checked textually shrinks.
        for (auto g : ast.globals) {
            for (auto vd : g->defs) {
                if (!vd->type) continue;
                if (ContainsGrowShrink(vd->type)) {
                    ShrinkGrowShrink(at, cat("call ", name, ", which may shrink ", vd->name),
                                     vd, string(vd->name));
                } else if (IsArrayKind(vd->type, A_GROW)) {
                    auto textual = false;
                    for (auto &fr : frames) {
                        if (!fr.spec || !fr.spec->inprogress) continue;
                        SyntacticShrinks(fr.spec->sf);
                        for (auto gn : fr.spec->sf->shrinkglobalnames) textual |= gn == vd->name;
                    }
                    if (textual)
                        GrowOnlyShrinkAt(at, true, cat("call ", name, ", which may shrink ",
                                                       vd->name), vd);
                }
            }
        }
    } else {
        for (auto vd : spec->shrinkglobals)
            shrink(vd, cat("call ", name, ", which shrinks"));
    }
}

// Element construction targets the array's storage (relative references
// in the element must derive from the same root, §3.9).
inline void TypeCheck::ElemArg(Node *&n, TypeExpr *elem, Val &rv) {
    SlotScope ss(*this, true);
    CheckValueAt(n, elem, Dest { rv.root, rv.rootexact }, true);
}

// ------------------------------------------------------------------
// Calling a function value F(a): the body is cloned and checked inline
// in the lexical environment it was written in (§7.6).

inline Val TypeCheck::CheckFunValCall(Call *c, const FnValBind &fb) {
    if (c->trailing)
        Error(c, "a function value call cannot itself take a trailing block");
    if (fb.named) {
        vector<SFunction *> cands = { fb.named };
        Node *nopre = nullptr;
        return ResolveCall(c, cands, fb.env, fb.named->name, nullptr, nopre);
    }
    auto fv = fb.fv;
    vector<Val> argvals;
    for (auto a : c->args) {
        auto v = CheckV(a, nullptr);
        a->exprtype = v.type;
        argvals.push_back(v);
    }
    vector<Param> params;
    if (fv->explicit_params) {
        params = fv->params;
        if (params.size() != argvals.size())
            Error(c, cat("this function value takes ", (int64_t)params.size(),
                         " argument(s), ", (int64_t)argvals.size(), " given"));
    } else if (argvals.size() == 1) {
        Param p;
        p.name = "it";
        params.push_back(p);
    } else if (!argvals.empty()) {
        Error(c, "a block with multiple arguments needs named parameters (x, y => ...)");
    }
    // Parameter types: annotations resolve in the defining environment.
    vector<TypeExpr *> ptypes;
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].type) {
            auto t = SubstEnv(params[i].type, fb.env);
            ValidateType(t, c->line, VT_PARAM);
            ptypes.push_back(t);
        } else {
            auto nt = NaturalType(argvals[i]);
            if (!nt || nt->kind == TY_VOID || nt == fntype)
                Error(c->args[i], "cannot infer a type for this argument");
            ptypes.push_back(nt);
        }
    }
    {
        DestScope ds(*this, Dest {});
        for (size_t i = 0; i < ptypes.size(); i++) CheckArg(c->args[i], ptypes[i]);
    }
    // Check the body inline, with lookups chaining to the definer.
    Frame f;
    f.sf = fb.env ? fb.env->sf : CurRealFrame().sf;
    f.spec = CurRealFrame().spec;
    f.lexspec = fb.env;
    f.lexframe = fb.env ? FrameOfSpec(fb.env) : 0;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = c->line;
    f.isfunval = true;
    frames.push_back(f);
    PushScope(SK_FN);
    c->fvparams.clear();
    for (size_t i = 0; i < params.size(); i++) {
        auto vd = NewVar(params[i].name, ptypes[i], c->line, params[i].isvar);
        vd->assigned = true;
        if (ptypes[i]->kind == TY_REF || ptypes[i]->kind == TY_SLICE) {
            BindRefProvenance(vd, argvals[i]);
            if (ptypes[i]->cq) vd->ref.writable = false;
        }
        // A literal parameter handed to the block stays one inside it.
        if (argvals[i].unsized && !params[i].type && !params[i].isvar) {
            vd->unsized = true;
            vd->unsizedorigin = argvals[i].unsizedparam;
        }
        c->fvparams.push_back(vd);
    }
    c->fvtarget = fb.env ? fb.env->sf : nullptr;
    c->fvbody = (Block *)fv->body->Clone(ast);
    ValueRegion vr(*this, true);   // The body runs inside this call's expression.
    BlockScope bs(*this, c->fvbody);
    CheckStmts(c->fvbody);
    Val v = VoidVal();
    if (auto tail = c->fvbody->tail) {
        auto fi = Is<IfExpr>(tail);
        if ((fi && !fi->elseb) || Is<Guard>(tail)) CheckStmtExpr(tail);
        else v = CheckValue(c->fvbody->tail, nullptr);
    }
    c->fvbody->exprtype = v.type;
    PopScope();
    frames.pop_back();
    return v;
}

}  // namespace goose
