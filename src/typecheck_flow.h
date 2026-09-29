// Goose compiler — the typechecker's flow (definitions of TypeCheck members,
// typecheck.h): scopes, variables and their bindings, flow state (definite
// assignment and optional narrowing, merged at joins), the control
// constructs (§6.4, §6.5, §8.1), and statements (§4.4, §3.8).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Scopes, variables, and flow state (definite assignment + optional
// narrowing, merged at control-flow joins).

inline void TypeCheck::PushScope(int kind, Node *node) {
    Scope s;
    s.kind = kind;
    s.serial = ++scopeserial;
    s.varbase = (int)vars.size();
    s.fnbase = (int)localfns.size();
    s.node = node;
    scopes.push_back(s);
}

inline void TypeCheck::PopScope() {
    auto &s = scopes.back();
    // A `var x = []` that nothing ever pushed into has no type to give
    // codegen; the scope ending is the last chance to say so.
    for (auto i = s.varbase; i < (int)vars.size(); i++)
        if (IsPendingArray(vars[i]->type))
            Error(vars[i]->line, cat("the element type of ", vars[i]->name,
                                     " was never determined: nothing was pushed or "
                                     "appended into it, and no array was assigned to it"));
    vars.resize(s.varbase);
    localfns.resize(s.fnbase);
    scopes.pop_back();
}

// A grow-shrink array anywhere in a value of type t: the array itself, or
// the tail of a struct (elements are never resizable, §3.3).
inline bool TypeCheck::ContainsGrowShrink(TypeExpr *t) {
    switch (t->kind) {
        case TY_ARRAY: return t->arr->akind == A_GROWSHRINK;
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            for (auto ft : inst->ftypes)
                if (ft && ContainsGrowShrink(ft)) return true;
            return false;
        }
        default: return false;
    }
}

// The resizable array a value of type t holds: t itself, or the tail of a
// struct or of a variable ADT's payload (§3.4); null if there is none.
inline TypeExpr *TypeCheck::ResizableArrayIn(TypeExpr *t) {
    if (ClassOf(t) != SC_RESIZABLE) return nullptr;
    if (t->kind == TY_ARRAY) return t;
    TypeExpr *arr = nullptr;
    AnyField(t, [&](TypeExpr *ft) { return (arr = ResizableArrayIn(ft)) != nullptr; });
    return arr;
}

// Whether references rooted at r may point into a grow-shrink array
// (§5.2): r holds one, or stands for a call-site root that does.
inline bool TypeCheck::IsGrowShrinkRoot(VarDef *r) {
    return r && (r->growshrink || (r->type && ContainsGrowShrink(r->type)));
}

// Whether a grow-shrink array inside a value of type t can hold an `of`
// by value: the storage a reference to `of` rooted at that value could
// point into and a shrink could then reuse.
inline bool TypeCheck::GrowShrinkContains(TypeExpr *t, TypeExpr *of) {
    switch (t->kind) {
        case TY_ARRAY:
            if (t->arr->akind == A_GROWSHRINK) return CanContain(t->arr->sub, of);
            return GrowShrinkContains(t->arr->sub, of);
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            for (auto ft : inst->ftypes)
                if (ft && GrowShrinkContains(ft, of)) return true;
            return false;
        }
        default: return false;
    }
}

// Whether a reference to `of` rooted at r may point into a grow-shrink
// array (§5.2): r holds one, or stands for a caller's root that does, and
// that array's elements can contain an `of`. A slice key read back out of
// a dictionary's slots points into text the caller keeps, never into the
// slots themselves, so it is not the kind of reference the rule is about.
inline bool TypeCheck::GrowShrinkCanHold(VarDef *r, TypeExpr *of) {
    if (!IsGrowShrinkRoot(r)) return false;
    if (!of) return true;
    auto v = r;
    // A class whose grow-shrink array is not its own root's (VarDef::gsvia)
    // is storage this frame cannot see: assume it can hold anything.
    if (!v->type && v->gsvia) return true;
    while (v && !v->type && v->classfrom) v = v->classfrom;
    if (!v || !v->type) return true;   // Storage this frame cannot see: assume it can.
    return GrowShrinkContains(v->type, of);
}

// Whether the reference or slice v of type t rooted at root, or for a holder
// any reference it holds, may point into a grow-shrink array by what the
// root holds: what §5.2 keeps out of every field, element and global.
inline bool TypeCheck::IntoGrowShrink(const Prov &v, VarDef *root, TypeExpr *t, bool holder) {
    vector<TypeExpr *> pointees;
    if (holder) RefPointees(t, pointees); else pointees.push_back(PointeeOf(t));
    auto intogs = v.byteview && IsGrowShrinkRoot(root) && MayBeViewed(root);
    for (auto pt : pointees) intogs |= GrowShrinkCanHold(root, pt);
    return intogs;
}

// A grow-shrink array a reference or slice of type t with provenance p may
// point into, or null: one held by any root the value may have -- a
// branch's, a rebind's, a call's -- or, for a reference to a slice, one
// that slice may point into.
inline VarDef *TypeCheck::GrowShrinkTaint(const Prov &p, TypeExpr *t) {
    if (!t || !IsRefOrSlice(t)) return nullptr;
    for (auto &a : p.alts)
        if (IntoGrowShrink(p, a.root, t, false)) return a.root;
    if (t->kind == TY_REF && t->ref->sub->kind == TY_SLICE)
        return GrowShrinkTaint(SlotView(p, t->ref->sub), t->ref->sub);
    return nullptr;
}

// What the store rule (§5.2) checks v, pointing at `roots`, against: a
// grow-shrink array it may point into, or for a holder one a reference it
// holds may. A holder's references were each checked where they were stored.
inline VarDef *TypeCheck::StoredIntoGrowShrink(const Val &v, const Roots &roots, TypeExpr *t,
                                               bool holder) {
    if (holder) {
        for (auto &a : roots.alts)
            if (IntoGrowShrink(v, a.root, t, true)) return a.root;
        return nullptr;
    }
    auto p = Prov(v);
    p.TakeAlts(roots);
    return GrowShrinkTaint(p, t);
}

// Why a reference rooted at root, or a holder of one, is never stored: it
// may point into a grow-shrink array (§5.2), or it is a back edge's result,
// which may point anywhere, as may a parameter whose class stands for one.
// `may`: root is not the reference's own but what it may point into as well
// (Prov::intogs), a parameter's class or the parameter where the argument
// may.
inline string TypeCheck::NeverStoredError(VarDef *root, bool may) {
    auto from = root;
    while (from && !from->type && from->classfrom) from = from->classfrom;
    auto stored = ": such a reference lives in a variable, is passed down or returned, and "
                  "is never stored (§5.2)";
    if (may && (!root->type || IsRefOrSlice(root->type)))
        return cat("storing a reference that may point into a grow-shrink array, as ",
                   root->name, " may", stored);
    return cat("storing a reference ", may ? "that may point " : "", "into ", root->name,
               ", which holds a grow-shrink array", stored);
}

// Whether a byte view could ever cover this root's storage: bytes_of views
// only image-safe elements, so a container of slices or references (a list
// of strings, say) is never viewed, whatever its element type looks like. A
// parameter class root has no type: it stands for every call site its
// specialization serves, not only the one recorded in classfrom, so it may
// always be viewed.
inline bool TypeCheck::MayBeViewed(VarDef *r) {
    if (!r || !r->type || IsRefOrSlice(r->type)) return true;
    return Viewable(LoadType(r->type));
}

inline bool TypeCheck::Viewable(TypeExpr *t) {
    if (t->kind == TY_ARRAY) {
        string why;
        return ImageSafe(t->arr->sub, why) || Viewable(t->arr->sub);
    }
    return AnyField(t, [&](TypeExpr *ft) { return Viewable(ft); });
}

// Whether the reference or slice variable v may point into the array at
// `root`, which a shrink of that array is checked against (§5.1, §5.2): it
// is bound there; or a root of its only bounds the pointee's lifetime, at
// or below that depth; or a binding its record does not show may have put
// it there (RefMayRetarget).
inline bool TypeCheck::RefMayPointInto(VarDef *v, VarDef *root) {
    if (v->refrootknown)
        for (auto &a : v->ref.alts)
            if (a.root == root || (!a.exact && Depth(a.root) >= Depth(root))) return true;
    return RefMayRetarget(v, root);
}

// Whether a binding of v that its record does not show may point into the
// array at `root`: v is a `var` bound at that depth, which a same-depth
// rebind could since have retargeted (§9.2), or it is not bound yet and may
// commit to any array at its depth or outside -- unless this is a discovery
// pass of a loop, whose next pass sees the binding (CheckLoopPasses).
inline bool TypeCheck::RefMayRetarget(VarDef *v, VarDef *root) {
    if (!v->refrootknown) return !UnboundIsBottom() && Depth(v) >= Depth(root);
    return v->isvar && Depth(v->ref.Root()) == Depth(root);
}

// Whether a reference or slice of type t loaded out of a field, an element
// or a global is a slot read (Prov::slotread): anything but a relative
// reference, which points within the array that holds it, a grow-shrink one
// included.
inline bool TypeCheck::SlotReadable(TypeExpr *t) {
    return t->kind == TY_SLICE || (t->kind == TY_REF && t->ref->lenstorage < 0);
}

// Whether the references a reference's pointee holds -- the slice in the
// slot a `T[:]&` names, those in the holder an `S&` names -- may point into
// what a shrink at root frees (§5.1, §5.2), which the pointee's own type
// says nothing about. v is the reference variable, if the reference is one,
// and p its provenance.
//
// A reference to a holder is rooted at the holder, whose store record says
// what it holds, as it does for the holder itself. One to a slice is rooted
// where the slice points, or, taken with an explicit `&`, at the slice
// variable, whose own binding says where (SlotView). The slot may since have
// been rebound, a slice variable at its root's depth, and a slice into a
// grow-only array stored into it through a reference, with anything at that
// depth or outside it; there, as behind a `var` reference, an inexact one or
// a parameter's class of an explicit `&s`, the root only bounds the array.
inline bool TypeCheck::HeldRefsMayPointInto(VarDef *v, const Prov &p, TypeExpr *t,
                                            VarDef *root, TypeExpr *bound, bool growonly) {
    auto slotof = [](VarDef *r) { return r && r->type && IsRefOrSlice(r->type) ? r : nullptr; };
    auto byteview = p.byteview || p.Any([&](const RootAlt &a) {
        auto slot = slotof(a.root);
        return (a.root && a.root->contentbyteview) || (slot && slot->ref.byteview);
    });
    vector<TypeExpr *> pointees;
    RefPointees(t->ref->sub, pointees);
    auto freed = false;
    for (auto pt : pointees)
        freed = freed || ShrinkMayFree(root, bound, growonly, pt, byteview && IsU8(pt));
    if (!freed) return false;
    if (v && !v->refrootknown) return !UnboundIsBottom() && Depth(v) >= Depth(root);
    for (auto &a : p.alts) {
        auto r = a.root;
        auto slot = slotof(r);
        if (r == root) return true;
        if (growonly && t->ref->sub->kind != TY_SLICE && a.exact && r && r->type &&
            !r->isglobal && !slot && !(v && v->isvar)) {
            auto arrtype = bound ? bound : root->type ? LoadType(root->type) : nullptr;
            Line where;
            if (HolderMayPointInto(r, root, arrtype, LiveEventBase(r), &where)) return true;
            continue;
        }
        if (growonly || !a.exact || (r && !r->type && r->viewslot)) {
            if (Depth(r) >= Depth(root)) return true;
            continue;
        }
        if (slot) {
            if (RefMayPointInto(slot, root) || (v && v->isvar && Depth(slot) >= Depth(root)))
                return true;
            continue;
        }
        // A parameter's class is one array in the body; only rebinding the
        // parameter moves it.
        if (r && !r->type && !IsTemp(r)) {
            if (v && v->isvar && Depth(r) == Depth(root)) return true;
            continue;
        }
        if (Depth(r) == Depth(root)) return true;
    }
    return false;
}

// The slice a load through a reference to one sees. Bound by reference
// (§4.1), the reference is rooted where that slice points already. Taken
// with an explicit `&` (§3.8), it is rooted where the slice variable, field
// or element lives: a variable's own binding says where its slice points,
// and the stores into a container only bound it, as the caller's variable
// behind a parameter's class of an explicit `&s` does, and as an inexact
// root bounds the slot. Only a slice variable's binding says whether its
// slice is a slot read (Prov::slotread); what the reference was does not.
inline Prov TypeCheck::SlotView(const Prov &p, TypeExpr *slice) {
    Prov out = p;
    out.alts.clear();
    for (auto &a : p.alts) {
        auto r = a.root;
        if (!r || !a.exact) {
            out.Add({ r, a.exact, a.from, false });
            continue;
        }
        if (r->type && IsRefOrSlice(r->type)) {
            auto v = RefProvOf(r);
            out.Add(v);
            out.writable = out.writable && v.writable;
            continue;
        }
        if (!r->type && r->viewslot) {
            out.Add({ r, false, r, false });
            out.byteview = out.byteview || r->contentbyteview;
            continue;
        }
        if (!r->type || !CanContain(r->type, slice)) {
            out.Add({ r, true, nullptr, false });
            continue;
        }
        LVal lv;
        lv.type = slice;
        lv.SetProv(p);
        lv.Set(r, true);
        ReadBackLVal(lv);
        out.Add(lv);
        out.byteview = out.byteview || lv.byteview;
    }
    return out;
}

// Binds a reference variable to where p points.
inline void TypeCheck::BindProv(VarDef *vd, const Prov &p) {
    vd->ref = p;
    vd->ref.reached = nullptr;
    vd->refrootknown = true;
    NoteFact(vd);
}

// The first non-null binding of a reference variable fixes its provenance.
// Null commits the variable to nothing. A value that points nowhere yet
// (Roots::unknown) binds the variable to nowhere yet: its reads are that,
// and its next binding is its first.
inline void TypeCheck::BindRefProvenance(VarDef *vd, const Val &v) {
    if (v.isnull) return;
    if (v.None()) {
        if (vd->refrootknown && vd->ref.Unknown()) return;   // As it was.
        vd->ref.SetUnknown();
        vd->refrootknown = true;
        NoteFact(vd);
        return;
    }
    BindProv(vd, v);
}

// A global `var` of reference or slice type, but for a self-relative one,
// which holds only null (§3.9): any function may bind it.
inline bool TypeCheck::BoundAnywhere(VarDef *vd) {
    auto t = vd->type;
    return vd->isglobal && vd->isvar && t && IsRefOrSlice(t) &&
           !(t->kind == TY_REF && t->ref->lenstorage >= 0 && !t->ref->pool);
}

// Where such a global points, as a read of it in a function's body sees it.
// A function checked after the body may bind it, and the body's check serves
// every later call, so that is not the bindings checked so far but all it
// can be given: the read-back rule's answer for a global container (§9.5),
// since only globals and static data outlive a global. That is each global
// whose storage can hold the pointee, exact where there is one, or the pool
// a relative one names, but for one the store rule keeps every binding of a
// global from (§5.2, FitsAt and CheckBindingRoot): a grow-shrink array that
// can hold the pointee. Like a slot's contents, it is a slot read.
inline Prov TypeCheck::GlobalVarRead(VarDef *vd) {
    auto t = vd->type;
    Prov p = vd->ref;
    Roots self;
    self.Set(vd, true);
    p.TakeAlts(ReadBackRoot(t, self, p.byteview));
    std::erase_if(p.alts, [&](const RootAlt &a) { return IntoGrowShrink(p, a.root, t, false); });
    for (auto &a : p.alts) a.slotread = SlotReadable(t);
    return p;
}

// Where a reference variable's value points, as a read of it sees it: the
// roots it is committed to, and its provenance bits, but for a global `var`
// read in a function's body (GlobalVarRead). A global initializer runs once,
// where the bindings checked so far are all that can have been made. Before
// any binding a variable points nowhere yet in a discovery pass of a loop,
// which a later pass revisits with the binding a rebind further down the
// body gives it (CheckLoopPasses). Otherwise an optional bound only to null
// so far holds null where every binding that can come before the read has
// been checked: in the body declaring it (a global's is the initializers),
// and for a global `let`, which only its initializer binds. It has no roots
// there, as the literal has none (§9.5). A local one's read in a nested
// function's or a function value's body is the read-back rule's answer,
// whatever can hold the pointee type at its own depth or outside. Anything
// else is the temp sentinel.
inline Prov TypeCheck::RefProvOf(VarDef *vd) {
    if (BoundAnywhere(vd) && CurRealFrame().spec) return GlobalVarRead(vd);
    Prov p = vd->ref;
    if (!vd->refrootknown) {
        if (UnboundIsBottom()) {
            p.SetUnknown();
            return p;
        }
        auto optional = vd->type && vd->type->kind == TY_REF && vd->type->ref->optional;
        auto seen = (vd->isglobal && !vd->isvar) || vd->ownerspec == CurRealFrame().spec;
        if (optional && seen) return p;
        p.Set(temproot, false);
    }
    // A global is storage like a field: no binding puts a reference into a
    // grow-shrink array there (FitsAt, CheckBindingRoot).
    if (vd->isglobal && vd->type && SlotReadable(vd->type))
        for (auto &a : p.alts) a.slotread = true;
    if (!vd->refrootknown && vd->type && vd->type->kind == TY_REF && vd->type->ref->optional) {
        auto cands = RootCandidates(LoadType(vd->type->ref->sub), Depth(vd), false,
                                    !vd->type->cq);
        if (cands.Any([](const RootAlt &a) { return a.root != nullptr; })) p.alts = cands.alts;
    }
    return p;
}

inline VarDef *TypeCheck::ResetLocal(VarDef *previous) {
    if (!previous) return ast.NewVarDef();
    // Cached nested specializations capture this identity. Recompute its
    // checking state, but retain the capture discovered on an earlier pass,
    // and the marks no pass may lose (a nested function checked once set
    // them for every pass).
    auto captured = previous->captured;
    auto nonneguse = previous->nonneguse, markuse = previous->markuse;
    auto refwrite = previous->refwrite;
    *previous = VarDef {};
    previous->captured = captured;
    previous->nonneguse = nonneguse;
    previous->markuse = markuse;
    previous->refwrite = refwrite;
    return previous;
}

inline VarDef *TypeCheck::NewVar(string_view name, TypeExpr *type, Line l, bool isvar,
                                VarDef *previous) {
    auto vd = ResetLocal(previous);
    vd->name = name;
    vd->type = type;
    vd->line = l;
    vd->isvar = isvar;
    vd->depth = CurDepth();
    vd->ownerspec = frames.back().spec;
    vars.push_back(vd);
    return vd;
}

// Name lookup: current frame's scopes innermost-out, then what the body sees
// outside them (free variables of nested fns / function values, §7.5), then
// globals, in the namespace order of docs/design/namespaces.md. A nested
// function called after the scope declaring one of those ended cannot name
// it, which `use`, the node naming it, reports; what the body finds of it
// there is part of the body's key (NoteEnvRead).
inline VarDef *TypeCheck::LookupVar(string_view name, string_view ns, Node *use) {
    auto top = (int)frames.size() - 1;
    for (auto i = (int)vars.size() - 1; i >= frames[top].varbase; i--)
        if (vars[i]->name == name) return vars[i];
    VarDef *found = nullptr;
    ForOuterVars(top, [&](VarDef *v, int i, int fi) {
        if (v->name != name) return false;
        if (use && !InScope(v, i)) {
            auto fn = frames[fi].sf->name;
            Error(use, cat(fn, " names ", v->name, " (bound at ", Where(v->line),
                           "), whose scope has ended where ", fn, " is called (§7.5)"));
        }
        v->captured = true;
        found = v;
        return true;
    });
    if (found) {
        if (use) NoteEnvRead(found);
        return found;
    }
    if (use) DefaultScopeName(name, use, false);
    auto g = ast.LookupGlobal(name, ns);
    return g && !g->defs.empty() ? g->defs[0] : nullptr;
}

// A parameter default names what a top-level declaration would (§7.1). The
// scope it is written in makes some names something else: a parameter of
// its function, a type parameter bound to a function value, and for a
// nested function a variable or nested function around the declaration.
// Such a name in a default is an error, not the global or function of that
// name. `fnonly`: a function is looked up (a member call), which no
// variable of the name would be.
inline void TypeCheck::DefaultScopeName(string_view name, Node *at, bool fnonly) {
    if (name.find("::") != string_view::npos) return;
    auto fi = (int)frames.size() - 1;
    while (fi >= 0 && !frames[fi].decl && !frames[fi].defaultfn) fi = frames[fi].lexframe;
    if (fi < 0 || !frames[fi].defaultfn) return;
    auto &fr = frames[fi];
    auto sf = fr.defaultfn;
    auto error = [&](const string &what) {
        Error(at, cat("the default of parameter ", sf->params[fr.defaultparam].name, " of ",
                      sf->qname, " names ", what, ", which a default cannot: it names what a "
                      "top-level declaration can (§7.1)"));
    };
    if (!fnonly) {
        for (auto &p : sf->params)
            if (p.name == name) error(cat("parameter ", name));
        for (auto &g : sf->generics) {
            auto istype = false;
            for (auto &[n, t] : fr.lexspec->bindings) istype = istype || n == g.name;
            if (g.name == name && !istype) error(cat("function value ", name));
        }
    }
    if (!fr.defaultsite) return;
    if (!fnonly)
        for (auto [v, i] : fr.defaultsite->vars)
            if (v->name == name) error(cat(name, ", a local where ", sf->name, " is declared"));
    for (auto [f, env] : fr.defaultsite->fns)
        if (f->name == name) error(cat(name, ", a nested function"));
}

// Whether the scope a nested function is declared in has ended: its value
// left it, and it is called outside.
inline bool TypeCheck::ScopeEnded(const DeclSite &d) {
    return d.scope >= (int)scopes.size() || scopes[d.scope].serial != d.serial;
}

// A nested function: visible from here to the end of the scope, checked when
// called and specialized per caller (§7.5). Its body names what is in scope
// here, whatever the call, and may call every function declared in the blocks
// around it, so that nested functions call each other in either order (the
// latest declared at or before this point wins, then the first after it).
inline void TypeCheck::DeclareLocalFn(FnDecl *fd) {
    auto top = (int)frames.size() - 1;
    auto &fr = frames[top];
    auto &site = declsites.emplace_back();
    site.scope = (int)scopes.size() - 1;
    site.serial = scopes.back().serial;
    for (auto i = (int)vars.size() - 1; i >= fr.varbase; i--) site.vars.push_back({ vars[i], i });
    ForOuterVars(top, [&](VarDef *v, int i, int) {
        site.vars.push_back({ v, i });
        return false;
    });
    for (auto bp = blockpos.rbegin(); bp != blockpos.rend() && bp->scopeidx >= fr.scopebase; ++bp) {
        auto &stmts = bp->block->stmts;
        auto at = std::min((int)bp->idx, (int)stmts.size() - 1);
        for (auto i = at; i >= 0; i--)
            if (auto d = Is<FnDecl>(stmts[i])) site.fns.push_back({ d->sf, fr.lexspec });
        for (auto i = at + 1; i < (int)stmts.size(); i++)
            if (auto d = Is<FnDecl>(stmts[i])) site.fns.push_back({ d->sf, fr.lexspec });
    }
    ForOuterFns(top, [&](SFunction *sf, FnSpec *env) {
        site.fns.push_back({ sf, env });
        return false;
    });
    declsiteof[{ fr.lexspec, fd->sf }] = &site;
    localfns.push_back({ (int)scopes.size() - 1, fd->sf });
}

// The namespace of the function being checked (a function value's is its
// definer's): where names the checker looks up without a node of their own
// resolve first.
inline string_view TypeCheck::CurNs() {
    for (auto i = (int)frames.size() - 1; i >= 0; i--)
        if (frames[i].sf) return frames[i].sf->ns;
    return {};
}

// The frame whose vars the above may address next: used to find a spec's
// frame index for lexparent chains. A function value's body environment
// (FnSpec::isfunval) is the frame checking the body.
inline int TypeCheck::FrameOfSpec(FnSpec *sp) {
    for (auto i = (int)frames.size() - 1; i >= 0; i--)
        if (frames[i].isfunval ? frames[i].lexspec == sp : frames[i].spec == sp) return i;
    return -1;
}

// The frame a body whose lexical parent is `env` looks names up in next. A
// function declared in a function value's body can be called after that
// body's check has ended (yielded as its value), and then continues in the
// nearest environment around the body still being checked, as one a
// finished block declared does.
inline int TypeCheck::LexFrame(FnSpec *env) {
    auto fi = FrameOfSpec(env);
    while (fi < 0 && env && env->isfunval) fi = FrameOfSpec(env = env->lexparent);
    return fi;
}

// The frame scope index s belongs to: the innermost one whose scopes start
// at or below it.
inline int TypeCheck::FrameOfScope(int s) {
    for (auto fi = (int)frames.size() - 1; fi > 0; fi--)
        if (frames[fi].scopebase <= s) return fi;
    return 0;
}

// Whether code checked in frame fi can name the variables of frame target:
// fi is that frame, or nested in it lexically, as the bodies of the nested
// functions declared and the function values written there are (§7.5, §7.6).
inline bool TypeCheck::NamesFrame(int fi, int target) {
    for (; fi >= 0; fi = frames[fi].lexframe)
        if (fi == target) return true;
    return false;
}

// The specialization of the named function a lexical environment is in
// (null at globals): a function value's body is in the one it was written in.
inline FnSpec *TypeCheck::NamedSpec(FnSpec *env) {
    while (env && env->isfunval) env = env->lexparent;
    return env;
}

// The locals a function nested in `env` can reach outside its own body:
// those of each lexical parent, a function value's body holding the ones
// its frame declares.
inline vector<VarDef *> TypeCheck::LexicalLocals(FnSpec *env) {
    vector<VarDef *> out;
    for (; env; env = env->lexparent) {
        if (!env->isfunval) {
            for (auto vd : vars) if (vd->ownerspec == env) out.push_back(vd);
            continue;
        }
        auto fi = FrameOfSpec(env);
        if (fi < 0) continue;
        auto end = fi + 1 < (int)frames.size() ? frames[fi + 1].varbase : (int)vars.size();
        for (auto i = frames[fi].varbase; i < end; i++) out.push_back(vars[i]);
    }
    return out;
}

inline SFunction *TypeCheck::LookupLocalFn(string_view name) {
    FnSpec *env;
    return LookupLocalFnEnv(name, env);
}

inline vector<int> TypeCheck::NamedFrames(int fi, FnSpec *spec) {
    vector<int> chain;
    auto add = [&](int k) {
        for (; k >= 0; k = frames[k].lexframe) {
            if (find(chain.begin(), chain.end(), k) != chain.end()) break;
            chain.push_back(k);
            if (k == 0) break;
        }
    };
    auto envs = [&](FnSpec *sp) {
        if (!sp) return;
        for (auto &fv : sp->fnvals) add(LexFrame(fv.second.env));
    };
    add(fi);
    envs(spec);
    for (size_t k = 0; k < chain.size(); k++) envs(frames[chain[k]].spec);
    if (find(chain.begin(), chain.end(), 0) == chain.end()) chain.push_back(0);
    sort(chain.begin(), chain.end());
    return chain;
}

inline TypeCheck::FlowState TypeCheck::SaveFlow() {
    FlowState f;
    EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
        f.idx.push_back(i);
        f.st.push_back({ vars[i]->assigned, vars[i]->narrowed });
    });
    // Globals' narrowing participates too (assignment in branches).
    for (auto g : ast.globals)
        for (auto v : g->defs) f.globals.push_back({ v, v->narrowed });
    f.reachable = reachable;
    return f;
}

inline void TypeCheck::RestoreFlow(const FlowState &f) {
    for (size_t k = 0; k < f.idx.size(); k++) {
        if (f.idx[k] >= (int)vars.size()) break;
        vars[f.idx[k]]->assigned = f.st[k].first;
        vars[f.idx[k]]->narrowed = f.st[k].second;
    }
    for (auto [v, narrowed] : f.globals) v->narrowed = narrowed;
    reachable = f.reachable;
}

inline TypeCheck::FlowState TypeCheck::JoinFlow(const FlowState &a, const FlowState &b) {
    auto now = SaveFlow();
    MergeFlow(a, b);
    auto joined = SaveFlow();
    RestoreFlow(now);
    return joined;
}

inline bool TypeCheck::SameFlow(const FlowState &a, const FlowState &b) {
    if (a.reachable != b.reachable || a.idx != b.idx || a.st != b.st) return false;
    for (size_t i = 0; i < a.globals.size(); i++)
        if (a.globals[i].second != b.globals[i].second) return false;
    return true;
}

// Joins two branch end states into the current state: a fact holds after
// the join iff it holds in every reachable branch. A variable neither
// state has -- declared in a branch and still in scope -- holds nothing.
inline void TypeCheck::MergeFlow(const FlowState &a, const FlowState &b) {
    auto join = [&](VarDef *v, pair<bool, TypeExpr *> aa, pair<bool, TypeExpr *> bb) {
        v->assigned = (a.reachable ? aa.first : true) && (b.reachable ? bb.first : true);
        TypeExpr *n = nullptr;
        if (!a.reachable) n = bb.second;
        else if (!b.reachable) n = aa.second;
        else if (aa.second && bb.second) n = aa.second;
        v->narrowed = n;
    };
    const pair<bool, TypeExpr *> none { false, nullptr };
    size_t p = 0, q = 0;
    auto last = -1;
    while (p < a.idx.size() || q < b.idx.size()) {
        auto ia = p < a.idx.size() ? a.idx[p] : INT32_MAX;
        auto ib = q < b.idx.size() ? b.idx[q] : INT32_MAX;
        auto i = min(ia, ib);
        auto aa = ia == i ? a.st[p++] : none;
        auto bb = ib == i ? b.st[q++] : none;
        if (i >= (int)vars.size()) break;
        join(vars[i], aa, bb);
        last = i;
    }
    for (auto i = max(last + 1, frames.back().varbase); i < (int)vars.size(); i++)
        join(vars[i], none, none);
    for (size_t i = 0; i < a.globals.size(); i++) {
        auto [v, an] = a.globals[i];
        auto bn = b.globals[i].second;
        v->narrowed = !a.reachable ? bn : !b.reachable ? an
                      : an && bn ? an : nullptr;
    }
    reachable = a.reachable || b.reachable;
}

// Optional narrowing (§3.8): a bare optional variable as a condition, and
// the obvious compositions. `sense` = the region where cond is true.
inline void TypeCheck::NarrowCond(Node *cond, bool sense) {
    if (auto id = Is<Ident>(cond)) {
        if (!id->vdef) return;
        auto t = id->vdef->type;
        if (t && t->kind == TY_REF && t->ref->optional && sense && !id->vdef->narrowed)
            id->vdef->narrowed = NarrowedRef(t, cond->line);
        return;
    }
    if (auto u = Is<Unary>(cond)) {
        if (u->op == T_NOT) NarrowCond(u->child, !sense);
        return;
    }
    if (auto b = Is<Binary>(cond)) {
        if ((b->op == T_ANDAND && sense) || (b->op == T_OROR && !sense)) {
            NarrowCond(b->left, sense);
            for (auto v : b->rightkills) v->narrowed = nullptr;
            NarrowCond(b->right, sense);
            return;
        }
        // o != null narrows where true; o == null narrows where false.
        if (b->op == T_EQ || b->op == T_NEQ) {
            auto other = Is<NullLit>(b->left) ? b->right
                                              : Is<NullLit>(b->right) ? b->left : nullptr;
            if (other) NarrowCond(other, b->op == T_NEQ ? sense : !sense);
        }
        return;
    }
}

// A holder value bound to a new variable: the variable's contents are the
// value's, and the binding is a store like any other for the shrink rules,
// made where the initializer is.
inline void TypeCheck::NoteHolderBinding(VarDef *d, const Val &v, Node *at) {
    auto saved = fitnode;
    fitnode = at;
    RecordStore(d, ContentsOf(v), v.byteview, nullptr, v.holderfrom);
    fitnode = saved;
}

// ------------------------------------------------------------------
// Small type constructors and views.

// What an optional reference type narrows to (§3.8): a plain reference to
// the same pointee under the same qualifier.
inline TypeExpr *TypeCheck::NarrowedRef(TypeExpr *t, Line l) {
    auto r = ast.RefTo(t->ref->sub, l);
    r->cq = t->cq;
    return r;
}

// Merges the values of two branches (for roots: the deeper — i.e. more
// conservative — root wins; writability must hold in both). A call merges
// the roots its callee's returns give the same way (CallResult).
inline Val TypeCheck::MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at,
                                bool wantvalue) {
    if (!areach) return b;
    if (!breach) return a;
    Val v;
    // Branches that are all integer constants are one of them, a constant
    // of no committed type yet (Val::litint); floats of literals and the
    // integers beside them a float of literals (Val::litfloat). Either kind
    // of branch beside one of a type of its own adapts to that type, as it
    // would at any typed destination (§3.1, §6.3, §6.4): an integer constant
    // to an integer type it fits, a float of literals to a float's width.
    // The branches' nodes take the type the construct settles on
    // (RetypeBranches).
    auto isint = [&](const Val &x) { return x.type && IsIntT(LoadType(x.type)); };
    auto intlit = [&](const Val &x) {
        return x.unsized || x.ck == CK_INT || x.litint;
    };
    auto fltlit = [&](const Val &x) { return LitFloat(x) || isint(x); };
    auto adapt = [&](const Val &c, const Val &o) {
        if (!o.type || !c.type || TypeEq(c.type, o.type)) return false;
        if (o.type->kind == TY_FLT) return LitFloat(c) && !LitFloat(o);
        if (o.type->kind != TY_INT || !isint(c) || intlit(o)) return false;
        if (c.unsized) {
            // A literal parameter adapts to the other branch like a
            // constant (§7.7).
            RecordLitAdapt(c, o.type, at->line);
            return true;
        }
        return ConstsFit(c, o.type->intstorage);
    };
    int64_t alo, ahi, blo, bhi;
    if (IntConsts(a, alo, ahi) && IntConsts(b, blo, bhi)) {
        v.type = TypeEq(a.type, b.type) ? a.type : ast.inttypes[IS_I64];
        v.litint = true;
        v.litlo = std::min(alo, blo);
        v.lithi = std::max(ahi, bhi);
        v.nonneg = v.litlo >= 0;
    } else if (fltlit(a) && fltlit(b) && (LitFloat(a) || LitFloat(b))) {
        v.type = !LitFloat(a) ? b.type : !LitFloat(b) ? a.type
                                        : UnifyBranch(a.type, b.type, at, wantvalue);
        v.litfloat = true;
    } else if (adapt(a, b)) {
        v.type = b.type;
    } else if (adapt(b, a)) {
        v.type = a.type;
    } else {
        v.type = UnifyBranch(a.type, b.type, at, wantvalue);
    }
    // A holder value from either branch: what either's contents may point at.
    if (a.holderset || b.holderset) {
        v.holderset = true;
        v.contents = ContentsOf(a);
        v.contents.Add(ContentsOf(b));
    }
    v.isnull = a.isnull && b.isnull;   // Both null: still a null, which names no root.
    v.storagebranches = a.storagebranches && b.storagebranches;
    v.implicitcopy = a.implicitcopy ? a.implicitcopy : b.implicitcopy;
    // Wherever either branch's value may point, the merged one may (§9.2).
    v.TakeAlts(a);
    v.Add(b);
    v.writable = a.writable && b.writable;
    v.reusable = a.reusable & b.reusable;
    v.byteview = a.byteview || b.byteview;
    return v;
}

// Merges two branches' values as MergeVals does, but for a construct whose
// branches are checked a first time with no destination type (`onjoin`,
// CheckJoin): there arrays and slices of one element type join as a slice
// of it (§6.4) -- a slice and an array, or anything and a value joined so
// already -- and so do arrays of two different types, which stay an error
// unless a slice joins them too (joinhasslice). The joined value is only
// its type: the construct checks its branches again as values of it, each
// array a whole-array slice of itself, and they point where that says.
inline Val TypeCheck::JoinBranches(const Val &a, bool areach, const Val &b, bool breach,
                                   Node *at, bool wantvalue, bool onjoin) {
    if (!areach) return b;
    if (!breach) return a;
    auto elem = [](const Val &x) -> TypeExpr * {
        if (!x.type) return nullptr;
        if (x.type->kind == TY_SLICE) return x.type->sub;
        if (x.type->kind == TY_ARRAY) return x.type->arr->sub;
        return nullptr;
    };
    auto isslice = [](const Val &x) { return x.type->kind == TY_SLICE && !x.joinslice; };
    auto ea = elem(a), eb = elem(b);
    auto joins = onjoin && wantvalue && ea && eb && TypeEq(ea, eb);
    // Two slices, and two arrays of one type, are as they are.
    if (joins && !a.joinslice && !b.joinslice && isslice(a) == isslice(b) &&
        (isslice(a) || TypeEq(a.type, b.type)))
        joins = false;
    if (!joins) {
        // Arrays joined alone, meeting what joins nothing: they were the
        // first branches not to agree.
        NoArrayJoin(a);
        NoArrayJoin(b);
        return MergeVals(a, areach, b, breach, at, wantvalue);
    }
    Val v;
    v.type = ast.SliceOf(ea, at->line);
    // Read-only where a branch is: a const slice, or an array a view of
    // which would be (§9.5).
    auto readonly = [](const Val &x) {
        return x.type->kind == TY_SLICE ? x.type->cq : !x.writable;
    };
    v.type->cq = readonly(a) || readonly(b);
    v.joinslice = true;
    v.joinhasslice = a.joinhasslice || b.joinhasslice || isslice(a) || isslice(b);
    if (a.joinslice || b.joinslice) {
        auto &j = a.joinslice && !a.joinhasslice ? a : b;
        v.joinat = j.joinat;
        v.joina = j.joina;
        v.joinb = j.joinb;
    } else {
        v.joinat = at;
        v.joina = a.type;
        v.joinb = b.type;
    }
    return v;
}

// Whether a reference rooted at r may be stored inside a recursive cycle
// (§7.8): it points into static data, a global, a pool handed to the cycle,
// or a local of an enclosing function outside it, which all outlive every
// activation.
inline bool TypeCheck::CycleStorable(VarDef *r) {
    auto spec = CurRealFrame().spec;
    return !r || r->isglobal || r->poolclass ||
           (r->ownerspec && r->ownerspec != spec && !r->ownerspec->incycle &&
            !r->ownerspec->sf->isrec);
}

// The same of a value that may point at any of several places: each must be.
inline bool TypeCheck::CycleStorable(const Roots &r) {
    return r.All([&](const RootAlt &a) { return CycleStorable(a.root); });
}

// A branch's value outlives the scopes the branch opened: whatever receives
// the construct's value -- a call's argument, which is no store, included --
// gets it after they end. So a reference or slice it is, or one it holds,
// must not point into a variable declared in them or a temporary made there
// (§9.2). `depth` is the scope the construct is in, whose own statement's
// temporaries outlive the value.
inline void TypeCheck::CheckBranchRoot(const Val &v, int depth, Node *at, const char *construct) {
    auto t = v.type;
    if (!t || v.isnull) return;
    auto isrs = IsRefOrSlice(t);
    if (!isrs && !HoldsPlainRef(t)) return;
    const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
    for (auto &a : roots.alts) {
        auto root = a.root;
        // The sentinel stands for a root not known yet, as for a binding.
        if (!root || root == temproot || Depth(root) <= depth + (IsTemp(root) ? 1 : 0))
            continue;
        auto what = !isrs ? "holds references" : t->kind == TY_SLICE ? "is a slice"
                                                                       : "is a reference";
        if (IsTemp(root))
            Error(at, cat("the ", construct, "'s value ", what, " rooted at a temporary, which "
                          "does not outlive it (§9.2): a temporary lasts until the end of its "
                          "statement, or of the block whose final expression made it"));
        Error(at, cat("the ", construct, "'s value ", what, " rooted at ", root->name,
                      ", which does not outlive it (§9.2)"));
    }
}

// A by-value result codegen builds in temporary storage of its own (§9.2):
// the value of an if, match, block, loop or bare { }, which is a copy of
// what the branch taken produced even where that branch names a variable,
// that of a function value's call, and an array default<T>() fills. Like
// a call's result, a view of it is rooted at the temporary, not at the
// storage it was copied from (a variable, or a temporary of a scope the
// construct has left), it names no storage to bind by reference, and it is
// read-only: a write would change the copy and nothing else. What it holds
// still points where the source's contents do. A reference or slice is the
// reference itself.
inline Val TypeCheck::TempCopy(Val v) {
    if (!v.type || v.isnull || IsRefOrSlice(v.type) || v.type->kind == TY_VOID ||
        v.type->kind == TY_FN)
        return v;
    if (!v.holderset && HoldsPlainRef(v.type)) {
        // What the source holds is bounded by its storage, as a container
        // read's is (ContainerRead).
        auto src = v.Root();
        v.contents = v;
        v.contents.Weaken();
        v.holderfrom = IsTemp(src) ? nullptr : src;
        v.holderset = true;
    }
    v.Set(TempRoot(), false);
    v.reached = nullptr;
    v.lvalue = false;
    v.writable = false;
    return v;
}

// A construct whose value settled on the numeric type t (§6.3, §6.4): each
// branch's value takes it, and each break's that gives the construct one,
// down through the constructs nested as branches.
inline void TypeCheck::RetypeBranches(Node *x, TypeExpr *t) {
    auto block = [&](Block *b) {
        if (!b || !b->tail || !b->exprtype || b->exprtype->kind == TY_VOID) return;
        b->exprtype = t;
        RetypeBranch(b->tail, t);
    };
    auto breaks = [&](vector<Break *> &bs) { for (auto br : bs) RetypeBranch(br->val, t); };
    if (auto b = Is<Block>(x)) {
        block(b);
    } else if (auto e = Is<EarlyBlock>(x)) {
        block(e->body);
        breaks(e->breaks);
    } else if (auto l = Is<LoopExpr>(x)) {
        breaks(l->breaks);
    } else if (auto i = Is<IfExpr>(x)) {
        block(i->thenb);
        if (i->elseb) RetypeBranch(i->elseb, t);
    } else if (auto m = Is<MatchExpr>(x)) {
        for (auto &arm : m->arms) RetypeBranch(arm.body, t);
    }
}

// One branch's value, of a construct now of type t: an integer converts to a
// float t in a node of its own (ToFloat); an integer constant, which a type
// wider than its own receives as it is, takes any other t; and a float of
// literals takes an f32 t in every node computing it (RetypeFlex).
inline void TypeCheck::RetypeBranch(Node *&n, TypeExpr *t) {
    if (!n || !n->exprtype || n->exprtype->kind == TY_VOID) return;   // It diverges.
    if (Is<Block>(n) || Is<EarlyBlock>(n) || Is<IfExpr>(n) || Is<MatchExpr>(n) ||
        Is<LoopExpr>(n)) {
        n->exprtype = t;
        RetypeBranches(n, t);
        return;
    }
    auto nt = LoadType(n->exprtype);
    if (TypeEq(nt, t)) return;
    if (IsIntT(nt) && t->kind == TY_FLT) ToFloat(n, nt, t);
    else if (IsIntT(nt) && IsIntT(t) && !ImplicitInt(nt->intstorage, t->intstorage))
        n->exprtype = t;
    else if (nt->kind == TY_FLT && IsF32(t)) RetypeFlex(n, t);
}

// A control construct's value (§6.4), which `check` checks at a
// destination type. With none, a first check takes its branches as they
// are, the construct on joinpath; where they join as a slice there
// (JoinBranches) -- a string literal in one, an array in another -- a
// second checks them as values of that slice type, each array a
// whole-array slice of itself rather than the copy the construct's value
// otherwise is (TempCopy). The first check's warnings, and the copies of
// non-fixed storage it would report (CheckBranchCopy), wait until it is
// known to stand, and the second replaces what it logged for the
// construction checks (growlog). A construct already on the path is a
// branch of the one whose first check it is part of, which settles its
// value.
template<typename F> Val TypeCheck::CheckJoin(Node *x, TypeExpr *expected, F check) {
    if ((expected && expected->kind != TY_VOID) || joinpath == x) return check(expected);
    auto entry = SaveFlow();
    auto warnbase = cur.pendingwarnings.size();
    auto growbase = cur.growlog.size();
    Val v;
    {
        struct Probe {
            TypeCheck &tc;
            JoinPathScope js;
            Probe(TypeCheck &t, Node *n) : tc(t), js(t, n) { tc.cur.joinprobes++; }
            ~Probe() { tc.cur.joinprobes--; }
        } probe(*this, x);
        v = check(expected);
    }
    // Arrays of different types alone: an argument's are its call's to
    // judge against the parameter (argpath).
    if (v.joinslice && !v.joinhasslice) {
        if (argpath != x) NoArrayJoin(v);
        if (!WarningsHeld()) FlushWarnings();
        return v;
    }
    if (v.joinslice) {
        RestoreFlow(entry);
        cur.pendingwarnings.resize(warnbase);
        // The second check logs the growths and shrinks in the branches
        // again, for the values under construction around the construct.
        cur.growlog.resize(growbase);
        // The construct is no typed slot and constructs into no storage of
        // its own: what receives its value judges it as a slice's.
        DestScope ds(*this, Dest {});
        SlotScope ss(*this, false);
        return check(v.type);
    }
    if (!WarningsHeld()) FlushWarnings();
    // An argument's is left to its check against its parameter (argpath).
    if (v.implicitcopy && argpath != x) ImplicitCopyError(v.implicitcopy);
    // Numeric branches join as one type (MergeVals), which each branch's
    // value, checked with none, now takes.
    if (v.type && (IsIntT(v.type) || v.type->kind == TY_FLT)) RetypeBranches(x, v.type);
    return TempCopy(v);
}

inline Val TypeCheck::CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue) {
    auto onpath = argpath == x;
    auto onjoin = joinpath == x;
    CheckCond(x->cond);
    auto entry = SaveFlow();
    NarrowCond(x->cond, true);
    Val tv;
    {
        PathScope ps(*this, onpath ? x->thenb : nullptr);
        JoinPathScope js(*this, onjoin ? x->thenb : nullptr);
        tv = CheckBlockVal(x->thenb, expected, wantvalue, SK_PLAIN);
    }
    auto aflow = SaveFlow();
    RestoreFlow(entry);
    Val ev = VoidVal();
    NarrowCond(x->cond, false);
    PathScope ps(*this, onpath ? x->elseb : nullptr);
    JoinPathScope js(*this, onjoin ? x->elseb : nullptr);
    if (auto ei = Is<IfExpr>(x->elseb)) {
        // On the node path, whose later parts are what its own condition
        // decides between (AfterHead).
        NodeScope ns(*this, ei);
        ev = CheckIf(ei, expected, wantvalue);
    } else if (x->elseb) {
        ev = CheckBlockVal((Block *)x->elseb, expected, wantvalue, SK_PLAIN);
    } else if (wantvalue) {
        Error(x, "an if used as a value requires an else branch");
    }
    // A nested else-if is checked here rather than through its own Check,
    // so its type is settled here too, void where it never produces.
    if (x->elseb) x->elseb->exprtype = ev.type ? ev.type : ast.voidtype;
    auto bflow = SaveFlow();
    RestoreFlow(entry);
    MergeFlow(aflow, bflow);
    if (!wantvalue) return VoidVal();
    auto v = JoinBranches(tv, aflow.reachable, ev, bflow.reachable, x, wantvalue, onjoin);
    // On joinpath the construct that started it makes the copy (CheckJoin).
    return onjoin ? v : TempCopy(v);
}

inline Val TypeCheck::CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue, int scopekind,
                                    Node *scopenode) {
    auto onpath = argpath == b;
    auto onjoin = joinpath == b;
    PushScope(scopekind, scopenode);
    BlockScope bs(*this, b);
    CheckStmts(b);
    Val v = VoidVal();
    if (b->tail) {
        if (wantvalue) {
            PathScope ps(*this, onpath ? b->tail : nullptr);
            JoinPathScope js(*this, onjoin ? b->tail : nullptr);
            v = CheckValue(b->tail, expected, false, CopiesBranch(expected));
        } else {
            CheckStmtExpr(b->tail);
        }
    } else if (wantvalue && reachable && expected && expected->kind != TY_VOID) {
        Error(b, "block used as a value must end in an expression");
    }
    if (!reachable) v.type = nullptr;  // Bottom: the block never produces.
    PopScope();
    CheckBranchRoot(v, CurDepth(), b->tail, "block");
    b->exprtype = v.type ? v.type : ast.voidtype;
    return v;
}

inline Val TypeCheck::CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue) {
    auto onpath = argpath == m;
    auto onjoin = joinpath == m;
    Val sv;
    {
        FlagScope rs(inreturn, false);
        sv = CheckV(m->scrutinee, nullptr);
    }
    // A reference to an integer reads as its pointee (§3.8); one to an ADT
    // is kept, its tag and payload read where the value lies.
    if (IsPlainRef(sv.type) && IsIntT(LoadType(sv.type->ref->sub))) sv = DecayRef(sv);
    m->scrutinee->exprtype = sv.type;
    auto st = sv.type;
    TypeExpr *enumtype = nullptr;
    if (st->kind == TY_REF) {
        if (st->ref->optional)
            Error(m, "optional value must be narrowed (if/guard/assert), not matched");
        if (st->ref->sub->kind == TY_ENUM) enumtype = st->ref->sub;
    } else if (st->kind == TY_ENUM) {
        enumtype = st;
    }
    auto entry = SaveFlow();
    Val result;
    auto resultreach = false;
    auto first = true;
    FlowState acc;
    auto DoArm = [&](MatchArm &arm, VarDef *binder) {
        RestoreFlow(entry);
        PushScope(SK_PLAIN);
        if (binder) vars.push_back(binder);
        Val av;
        if (wantvalue) {
            PathScope ps(*this, onpath ? arm.body : nullptr);
            JoinPathScope js(*this, onjoin ? arm.body : nullptr);
            av = CheckValue(arm.body, expected, false, CopiesBranch(expected));
        } else {
            CheckStmtExpr(arm.body);
        }
        auto aflow = SaveFlow();
        if (!reachable) av.type = nullptr;
        PopScope();
        CheckBranchRoot(av, CurDepth(), arm.body, "match arm");
        if (first) {
            result = av;
            resultreach = aflow.reachable;
            acc = aflow;
            first = false;
        } else {
            result = JoinBranches(result, resultreach, av, aflow.reachable, m, wantvalue, onjoin);
            resultreach = resultreach || aflow.reachable;
            // Accumulate the join of all arms' flow.
            auto save = SaveFlow();
            MergeFlow(acc, aflow);
            acc = SaveFlow();
            RestoreFlow(save);
        }
    };
    for (size_t i = 0; i + 1 < m->arms.size(); i++)
        if (m->arms[i].pat.kind == P_WILDCARD)
            Error(m->arms[i + 1].body, "_ must be the last match arm");
    if (enumtype) {
        auto inst = GetEnumInst(enumtype);
        auto en = inst->en;
        vector<bool> covered(en->variants.size(), false);
        auto haswild = false;
        for (auto &arm : m->arms) {
            if (arm.pat.kind == P_WILDCARD) {
                haswild = true;
                DoArm(arm, nullptr);
                continue;
            }
            arm.variants.clear();
            for (auto &pi : arm.pat.items) {
                auto id = Is<Ident>(pi.lo);
                if (!id || pi.hi || id->name.find("::") != string_view::npos)
                    Error(arm.body, "ADT match arms are variant names (or _)");
                auto found = en->FindVariant(id->name);
                if (!found)
                    Error(arm.body, cat("enum ", en->name, " has no variant named ", id->name));
                auto vi = en->VariantIndex(found);
                if (covered[vi]) {
                    auto &vs = arm.variants;
                    Error(arm.body, find(vs.begin(), vs.end(), found) != vs.end()
                                        ? cat("variant ", id->name, " is listed twice")
                                        : cat("variant ", id->name, " is already matched by "
                                              "an earlier arm"));
                }
                covered[vi] = true;
                arm.variants.push_back(found);
            }
            // Only a lone variant has a binder (the parser's rule).
            auto found = arm.variants[0];
            VarDef *binder = nullptr;
            if (!arm.pat.binder.empty()) {
                if (!found->has_payload)
                    Error(arm.body, cat("variant ", found->name, " has no payload to bind"));
                auto vt = ast.VariantTypeOf(enumtype, found, m->line);
                // Packed resizable ADTs have one owning header, but no
                // persistent header for a payload view (C.2). Do not let
                // the backend manufacture a plain pointer or a stale copy.
                if (ClassOf(vt) == SC_RESIZABLE)
                    Error(arm.body, "binding a resizable ADT payload is not supported by "
                                    "the C backend yet; match its tag without a payload "
                                    "binder, or use a standalone resizable struct");
                binder = ResetLocal(arm.binder);
                binder->name = arm.pat.binder;
                binder->line = m->line;
                binder->depth = CurDepth() + 1;
                binder->ownerspec = frames.back().spec;
                binder->assigned = true;
                if (arm.pat.byref) {
                    // `Variant &b`: only variable-mode payloads may be
                    // bound by reference — a fixed-mode value may be
                    // overwritten with another variant, so references
                    // into its payload are illegal (§3.5, §8.1). A
                    // resizable value is assignable whole (§4.4), which
                    // replaces its variant the same way.
                    if (!enumtype->enu->varmode)
                        Error(arm.body, cat("cannot bind the payload of fixed-mode ",
                                            enumtype->enu->en->name, " by reference "
                                            "(§3.5); bind by value: ", found->name,
                                            " ", arm.pat.binder));
                    if (ClassOf(enumtype) == SC_RESIZABLE)
                        Error(arm.body, cat("cannot bind the payload of resizable ",
                                            enumtype->enu->en->name, " by reference: a "
                                            "whole assignment may replace its variant "
                                            "(§3.5); bind by value: ", found->name,
                                            " ", arm.pat.binder));
                    binder->type = ast.RefTo(vt, m->line);
                    BindProv(binder, sv);
                } else {
                    if (HasRelRefT(vt))
                        Error(arm.body, cat("payload of ", found->name, " contains "
                                            "relative references; bind it by reference "
                                            "(&", arm.pat.binder, ")"));
                    binder->type = vt;  // Payload copy, any mode (§8.1).
                    binder->isvar = false;
                    binder->copybind = true;
                    if (HoldsPlainRef(vt)) {
                        // A copied payload holding references: its contents
                        // are the scrutinee's.
                        ReadBack contents;
                        auto intemp = TempContents(sv, contents);
                        Roots held = intemp ? contents.roots : sv.AsRoots();
                        if (!intemp) held.Weaken();
                        RecordStore(binder, held, sv.byteview, nullptr,
                                    intemp ? contents.from : sv.Root());
                    }
                }
                arm.binder = binder;
            }
            DoArm(arm, binder);
        }
        if (!haswild)
            for (size_t i = 0; i < en->variants.size(); i++)
                if (!covered[i])
                    Error(m, cat("match does not cover variant ", en->name, ".",
                                 en->variants[i].name));
    } else if (IsIntT(LoadType(st))) {
        auto sit = LoadType(st)->intstorage;
        auto haswild = false;
        // A pattern's value: a literal or a named constant, which is a let
        // or const global, as an array size names (§11.1). A local of the
        // name hides the global here as it does anywhere else.
        auto PatternValue = [&](Node *n, bool &uns) {
            auto u = Is<Unary>(n);
            if (auto id = Is<Ident>(u ? u->child : n)) {
                if (auto vd = LookupVar(id->name, id->ns); vd && !vd->isglobal)
                    Error(n, cat("match pattern ", id->name, " names a local variable, "
                                 "not a constant"));
                auto g = ast.LookupGlobal(id->name, id->ns);
                if (!g) Error(n, cat("unknown constant ", id->name, " in an integer match"));
                if (g->isvar)
                    Error(n, cat("match pattern ", id->name, " names a var global, "
                                 "not a constant"));
            }
            Val v;
            bool literal;
            set<VarDecl *> visiting;
            if (!ConstIntValue(n, v, literal, visiting))
                Error(n, "constant integer expression expected for match pattern");
            uns = v.uns;
            return v.ival;
        };
        // Values order as unsigned keys: a u64's as they are, any other type's
        // (which fit an i64) with the sign bit flipped.
        auto key = [&](int64_t v) {
            return sit == IS_U64 ? (uint64_t)v : (uint64_t)v ^ (1ull << 63);
        };
        auto written = [&](const PatItem &pi) {
            return pi.hi ? cat(ExprStr(pi.lo), "..", ExprStr(pi.hi)) : ExprStr(pi.lo);
        };
        // Literals, negated ones included (the parser folds those), which
        // unlike named constants cannot be aliases of each other.
        auto literal = [](const PatItem &pi) {
            return Is<IntLit>(pi.lo) && (!pi.hi || Is<IntLit>(pi.hi));
        };
        auto shown = [&](const PatItem &pi, const ArmRange &r) {
            if (literal(pi)) return written(pi);
            auto num = [&](int64_t v) {
                return sit == IS_U64 ? to_string((uint64_t)v) : to_string(v);
            };
            return cat(written(pi), " (", num(r.lo),
                       pi.hi ? cat("..", num((int64_t)((uint64_t)r.hi + 1))) : "", ")");
        };
        // What earlier arms match: sorted, disjoint, non-adjacent key ranges,
        // so a pattern that several arms cover between them lies in one.
        vector<pair<uint64_t, uint64_t>> earlier;
        for (auto &arm : m->arms) {
            if (arm.pat.kind == P_WILDCARD) { haswild = true; DoArm(arm, nullptr); continue; }
            if (!arm.pat.binder.empty())
                Error(arm.pat.items[0].lo, cat("constant pattern ",
                                               Is<Ident>(arm.pat.items[0].lo)->name,
                                               " has no payload to bind"));
            arm.ranges.clear();
            for (auto &pi : arm.pat.items) {
                auto ul = false, uh = false;
                ArmRange r;
                r.lo = PatternValue(pi.lo, ul);
                auto end = pi.hi ? PatternValue(pi.hi, uh) : r.lo;
                // Pattern values must fit the scrutinee's type (a u64
                // scrutinee accepts any 64-bit pattern).
                if (sit != IS_U64) {
                    if (!FitsIntStorage(r.lo, ul, sit) || (pi.hi && !FitsIntStorage(end, uh, sit)))
                        Error(arm.body, cat("match pattern does not fit the scrutinee "
                                            "type ", TypeStr(st)));
                }
                // The arm keeps the last value it matches: the value past it,
                // which a range's end names, does not exist at the top of a type.
                r.hi = r.lo;
                if (pi.hi) {
                    if (sit == IS_U64 ? (uint64_t)end <= (uint64_t)r.lo : end <= r.lo)
                        Error(arm.body, "empty range in match pattern");
                    r.hi = (int64_t)((uint64_t)end - 1);
                }
                auto klo = key(r.lo), khi = key(r.hi);
                for (auto &[a, b] : earlier)
                    if (a <= klo && khi <= b)
                        Error(pi.lo, cat("match pattern ", shown(pi, r), " never matches: "
                                         "earlier arms match all of its values"));
                // Within an arm, differently named constants may overlap: they
                // can be one value's aliases.
                for (size_t j = 0; j < arm.ranges.size(); j++) {
                    auto &pj = arm.pat.items[j];
                    auto &rj = arm.ranges[j];
                    if (std::max(klo, key(rj.lo)) > std::min(khi, key(rj.hi))) continue;
                    if (written(pi) == written(pj))
                        Error(pi.lo, cat("match pattern ", written(pi), " is listed twice"));
                    if (literal(pi) && literal(pj))
                        Error(pi.lo, cat("match patterns ", written(pj), " and ", written(pi),
                                         " overlap"));
                }
                arm.ranges.push_back(r);
            }
            for (auto &r : arm.ranges) earlier.push_back({ key(r.lo), key(r.hi) });
            sort(earlier.begin(), earlier.end());
            vector<pair<uint64_t, uint64_t>> merged;
            for (auto &kr : earlier) {
                if (merged.empty() || (merged.back().second != UINT64_MAX &&
                                       kr.first > merged.back().second + 1))
                    merged.push_back(kr);
                else
                    merged.back().second = std::max(merged.back().second, kr.second);
            }
            earlier = std::move(merged);
            DoArm(arm, nullptr);
        }
        if (!haswild) Error(m, "integer match requires a _ arm");
    } else {
        Error(m, cat("cannot match on a value of type ", TypeStr(st)));
    }
    if (!first) RestoreFlow(acc);
    reachable = resultreach;
    if (!wantvalue) return VoidVal();
    return onjoin ? result : TempCopy(result);
}

inline Val TypeCheck::CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue) {
    auto onpath = argpath == x;
    auto onjoin = joinpath == x;
    x->breaks.clear();
    PushScope(SK_BLOCK, x);
    if (wantvalue) {
        scopes.back().breakexpected = expected;
        scopes.back().onargpath = onpath;
        scopes.back().onjoinpath = onjoin;
        scopes.back().inreturn = inreturn;
    }
    BlockScope bs(*this, x->body);
    CheckStmts(x->body);
    Val v = VoidVal();
    if (x->body->tail) {
        if (wantvalue) {
            PathScope ps(*this, onpath ? x->body->tail : nullptr);
            JoinPathScope js(*this, onjoin ? x->body->tail : nullptr);
            v = CheckValue(x->body->tail, expected, false, CopiesBranch(expected));
        } else {
            CheckStmtExpr(x->body->tail);
        }
    }
    if (!reachable) v.type = nullptr;
    auto sc = scopes.back();
    PopScope();
    x->body->exprtype = v.type ? v.type : ast.voidtype;
    reachable = reachable || sc.hasbreak;  // Exits via the tail or any break.
    if (!wantvalue) return VoidVal();
    CheckBranchRoot(v, CurDepth(), x->body->tail, "block");
    if (sc.breaktype) CheckBranchRoot(sc.breakvalue, CurDepth(), x, "block");
    Val r = sc.breaktype ? JoinBranches(v, v.type != nullptr, sc.breakvalue, true, x, wantvalue,
                                        onjoin) : v;
    if (!r.type) r.type = ast.voidtype;
    return onjoin ? r : TempCopy(r);
}

// A loop body: its statements, then a tail that is a statement like any
// other (only a `break` gives a loop a value, §6.5).
inline void TypeCheck::CheckLoopBody(Block *body) {
    BlockScope bs(*this, body);
    CheckStmts(body);
    if (body->tail) CheckStmtExpr(body->tail);
}

// A loop, whatever its header, checked to a fixpoint of what its body
// feeds back to its head (LoopPass): `pass` checks one iteration in the
// loop's scope, from the state `head`, which starts as the entry state and
// becomes the join of the entry with the pass's back edges -- the end of
// the body, every continue -- the state every iteration but the first
// starts in. A pass that changed no fact fed back and read no variable
// before its binding was checked against the settled facts, so its errors
// stood and it was the last; one that read such a variable is followed by
// a settled pass, which reads it as outside a loop would. The loop's
// scope, with its breaks, is the caller's to read off; the flow is left at
// the head, which is the state a loop exits in unless the caller knows
// better (a while's condition).
inline TypeCheck::Scope TypeCheck::CheckLoopPasses(Node *x, FlowState &head,
                                                   const function<void()> &pass) {
    auto entry = head;
    auto firstbase = storeevents.size();
    auto warnbase = cur.pendingwarnings.size();
    auto settled = false;
    Scope sc;
    for (auto passes = 0;; passes++) {
        // What a pass feeds back only grows, so the passes are bounded by
        // the variables in scope and their roots.
        if (passes >= 16)
            Error(x, "this loop's checking does not settle (internal limit of 16 passes)");
        RestoreFlow(head);
        cur.pendingwarnings.resize(warnbase);
        cur.looppasses.push_back({ (int)scopes.size(), firstbase, storeevents.size(), settled });
        PushScope(SK_LOOP, x);
        pass();
        auto &s = scopes.back();
        if (reachable) {
            s.backedge = s.backedges ? JoinFlow(s.backedge, SaveFlow()) : SaveFlow();
            s.backedges = true;
        }
        sc = s;
        PopScope();
        auto lp = cur.looppasses.back();
        cur.looppasses.pop_back();
        auto next = sc.backedges ? JoinFlow(entry, sc.backedge) : entry;
        auto changed = lp.changed || !SameFlow(next, head);
        head = next;
        if (changed) continue;
        if (settled || !lp.sawunbound) break;
        settled = true;
    }
    RestoreFlow(head);
    if (!WarningsHeld()) FlushWarnings();
    return sc;
}

inline Val TypeCheck::CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue) {
    auto head = SaveFlow();
    auto onpath = argpath == x;
    auto onjoin = joinpath == x;
    auto sc = CheckLoopPasses(x, head, [&] {
        x->breaks.clear();
        if (wantvalue) {
            scopes.back().breakexpected = expected;
            scopes.back().onargpath = onpath;
            scopes.back().onjoinpath = onjoin;
            scopes.back().inreturn = inreturn;
        }
        CheckLoopBody(x->body);
    });
    reachable = sc.hasbreak;  // A loop only exits via break.
    if (!wantvalue || !sc.breaktype) return VoidVal();
    CheckBranchRoot(sc.breakvalue, CurDepth(), x, "loop");
    return onjoin ? sc.breakvalue : TempCopy(sc.breakvalue);
}

inline void TypeCheck::CheckWhile(While *x) {
    // The condition runs before every iteration: what it narrows holds in
    // the body each time, a rebind inside the body un-narrows from that
    // point on, and the loop exits in the state the condition's last run
    // leaves, it being false.
    auto head = SaveFlow();
    FlowState exit;
    auto sc = CheckLoopPasses(x, head, [&] {
        CheckCond(x->cond);
        exit = SaveFlow();
        NarrowCond(x->cond, true);
        CheckLoopBody(x->body);
    });
    RestoreFlow(exit);
    if (sc.breaktype)
        Error(x, "break with a value exits loop/block only, not while");
}

inline void TypeCheck::CheckFor(ForLoop *x) {
    FlagScope rs(inreturn, false);   // No part of a value being returned.
    auto byref = x->byref;
    TypeExpr *bindtype = nullptr;
    TypeExpr *elemtype = nullptr;   // The array's element type, where it has one.
    Prov iterprov;   // What a reference binding points into.
    ReadBack contents;   // Where the elements point, when the array is a temporary.
    auto intemp = false;
    // A binder's written integer type (§6.5).
    auto written = [&](TypeExpr *te, const char *what) -> TypeExpr * {
        if (!te) return nullptr;
        auto t = Subst(te);
        ValidateType(t, x->line, VT_LOCAL);
        if (!IsIntT(t))
            Error(x, cat("a for loop's ", what, " takes an integer type, not ", TypeStr(t)));
        return t;
    };
    auto vartype = written(x->vartype, "binder");
    auto idxtype = written(x->idxtype, "index binder");
    // The end of a range, or a count, that is one past the binder's largest
    // value: a literal one, whose loop visits every value of a type narrower
    // than 64 bits with a counter of i64 (`for i: u8 in 0..256`).
    auto pastend = [&](Node *end) {
        Val c;
        bool literal;
        set<VarDecl *> visiting;
        return IntBits(vartype->intstorage) < 64 && ConstIntValue(end, c, literal, visiting) &&
               literal && !c.uns && c.ival == IntRange(vartype->intstorage).second + 1;
    };
    if (auto r = Is<RangeExpr>(x->iter)) {
        x->iterkind = IK_RANGE;
        if (x->byref) Error(x, "cannot iterate an integer range by reference");
        if (vartype) {
            // The bounds are values of the binder's type, a constant adapting
            // as at any typed destination (§3.1), and the loop counts at it.
            CheckValue(r->lo, vartype);
            if (pastend(r->hi)) {
                CheckIntAny(r->hi);
                r->exprtype = r->hi->exprtype = ast.inttypes[IS_I64];
            } else {
                CheckValue(r->hi, vartype);
                r->exprtype = vartype;
            }
            bindtype = vartype;
        } else {
            auto lo = CheckIntAny(r->lo);
            auto hi = CheckIntAny(r->hi);
            auto ct = UnifyNumeric(r, T_DOTDOT, lo, hi, lo.type, hi.type);
            RetypeOperands(r->lo, r->hi, lo, hi, ct);
            r->exprtype = ct;
            bindtype = ct;
        }
    } else {
        auto iv = CheckV(x->iter, nullptr);
        NoTemporaryLiteral(x->iter, iv.type);
        x->iter->exprtype = iv.type;
        auto t = iv.type;
        iterprov = iv;
        intemp = TempContents(iv, contents);
        if (t->kind == TY_REF && !t->ref->optional) {
            t = t->ref->sub;  // Iterate through refs.
            iterprov.ClearSlotRead();   // As DerefLValue.
            if (t->kind == TY_SLICE) iterprov = SlotView(iv, t);
        }
        t = LoadType(t);
        RequireComplete(t, x->line);
        if (IsIntT(t)) {
            x->iterkind = IK_COUNT;
            if (x->byref) Error(x, "cannot iterate an integer count by reference");
            // A count reads a reference as its pointee (§3.8); a sequence
            // keeps the reference, the loop iterating it where it lies.
            auto nv = DecayRef(iv);
            x->iter->exprtype = nv.type;
            bindtype = t;
            if (vartype) {
                // The count is the end of the range from 0, as above.
                if (!pastend(x->iter)) {
                    MustFit(nv, x->iter, vartype);
                    if (nv.litint) RetypeBranches(x->iter, vartype);
                    x->iter->exprtype = vartype;
                }
                bindtype = vartype;
            }
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            if (vartype)
                Error(x, cat("an array's element binds at its element type; a type on a for "
                             "loop's binder is for an integer range or count"));
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            elemtype = elem;
            x->iterkind = t->kind == TY_ARRAY ? IK_ARRAY : IK_SLICE;
            // Non-fixed elements bind by reference either way (§4.1).
            if (x->byref && ClassOf(elem) != SC_FIXED) {
                Warn(x, "redundant &: elements of this type bind by reference without it "
                        "(§4.1)");
            }
            byref = x->byref || ClassOf(elem) != SC_FIXED;
            // An element that is itself a reference binds as the one it holds
            // either way, there being no references to references (§3.8).
            if (byref && elem->kind != TY_REF) {
                bindtype = ast.RefTo(elem, x->line);
            } else {
                // An element that *is* a relative reference loads as a
                // plain one, exactly as indexing it does; only a value
                // with relative references *inside* it cannot be copied
                // out of the root they are measured against (§3.9).
                if (HasRelRefT(elem) && elem->kind != TY_REF)
                    Error(x, "elements containing relative references require the "
                             "&x binding form (copies are not supported)");
                bindtype = LoadType(elem);
            }
        } else {
            Error(x, cat("cannot iterate a value of type ", TypeStr(t)));
        }
        // An index binder's type holds every index the sequence can have: a
        // fixed-size or static-capacity array's length bounds them, anything
        // else's only the largest length there is (§10.4).
        if (idxtype && (x->iterkind == IK_ARRAY || x->iterkind == IK_SLICE)) {
            auto len = int64_t(1) << 48;
            if (t->kind == TY_ARRAY &&
                (t->arr->akind == A_FIXED || (t->arr->akind == A_LIMITED && t->arr->sizeexpr)))
                len = ArraySize(t->arr);
            if (len > 0 && !FitsIntStorage(len - 1, false, idxtype->intstorage))
                Error(x, cat("an index into ", TypeStr(t), " can reach ",
                             len == int64_t(1) << 48 ? string("2^48 - 1") : cat(len - 1),
                             ", which ", TypeStr(idxtype), " does not hold; bind the index as "
                             "an i64 and convert it with `as` (§6.5)"));
        }
    }
    if (idxtype && x->iterkind != IK_ARRAY && x->iterkind != IK_SLICE)
        Error(x, "the index of a range or count counts its iterations as an i64; a type on "
                 "the loop's binder gives the values their type");
    // A slice element bound by value, or a relative-reference one however it
    // is bound, was read out of the array, so where it points follows the
    // read-back rule (§9.5), not the array's own root.
    if (IsRefOrSlice(bindtype) && elemtype && (!byref || elemtype->kind == TY_REF) &&
        ((elemtype->kind == TY_REF && elemtype->ref->lenstorage >= 0) ||
         elemtype->kind == TY_SLICE)) {
        auto rb = ReadBackRoot(elemtype, iterprov, iterprov.byteview,
                               intemp ? &contents : nullptr);
        auto slotread = SlotReadable(elemtype);
        iterprov.TakeAlts(rb);
        for (auto &a : iterprov.alts) a.slotread = slotread;
        if (elemtype->cq) iterprov.writable = false;
    }
    auto head = SaveFlow();
    auto sc = CheckLoopPasses(x, head, [&] {
        auto vd = NewVar(x->var, bindtype, x->line, false, x->vdef);
        vd->assigned = true;
        vd->copybind = (x->iterkind == IK_ARRAY || x->iterkind == IK_SLICE) && !byref;
        if (!IsRefOrSlice(bindtype) && HoldsPlainRef(bindtype)) {
            // A holder element copied out: its contents are the array's.
            Roots held = intemp ? contents.roots : iterprov.AsRoots();
            if (!intemp) held.Weaken();
            RecordStore(vd, held, iterprov.byteview, nullptr,
                        intemp ? contents.from : iterprov.Root());
        }
        if (IsRefOrSlice(bindtype)) BindProv(vd, iterprov);
        x->vdef = vd;
        if (!x->idxvar.empty()) {
            auto idx = NewVar(x->idxvar, idxtype ? idxtype : ast.inttypes[IS_I64], x->line,
                              false, x->idxdef);
            idx->assigned = true;
            x->idxdef = idx;
        }
        CheckLoopBody(x->body);
    });
    if (sc.breaktype)
        Error(x, "break with a value exits loop/block only, not for");
}

inline void TypeCheck::CheckGuard(Guard *g) {
    CheckCond(g->cond);
    auto entry = SaveFlow();
    if (g->elseb) {
        NarrowCond(g->cond, false);
        CheckBlockVal(g->elseb, nullptr, false, SK_PLAIN);
        if (reachable)
            Error(g, "guard else block must diverge (return, break, continue, or abort)");
    } else {
        auto si = FindBreakScope(false);
        if (si >= 0) {
            auto &sc = scopes[si];
            if (sc.breaktype)
                Error(g, "bare guard exits a construct that requires a break value");
            sc.valuelessbreak = true;
            sc.hasbreak = true;
            g->implicitexit = 1;
        } else {
            ImplicitEmptyReturn(g);
            g->implicitexit = 2;
        }
    }
    RestoreFlow(entry);
    NarrowCond(g->cond, true);
}

inline void TypeCheck::ImplicitEmptyReturn(Node *at) {
    if (frames.back().isdefault) Error(at, "guard shorthand cannot exit a default");
    auto &f = CurRealFrame();
    if (!f.sf) Error(at, "guard shorthand cannot exit the top level");
    auto spec = f.spec;
    if (spec->retsknown) {
        if (!spec->rets.empty())
            Error(at, cat("bare guard would return without the required value(s) of ",
                          f.sf->name));
    } else {
        spec->rets.clear();
        spec->retsknown = true;
    }
}

inline TypeCheck::Frame &TypeCheck::CurRealFrame() {
    for (auto i = (int)frames.size() - 1; i >= 0; i--)
        if (!frames[i].isfunval) return frames[i];
    return frames[0];
}

inline int TypeCheck::FindBreakScope(bool forcontinue) {
    for (auto i = (int)scopes.size() - 1; i >= frames.back().scopebase; i--) {
        if (scopes[i].kind == SK_LOOP) return i;
        if (!forcontinue && scopes[i].kind == SK_BLOCK) return i;
        if (scopes[i].kind == SK_FN) break;
    }
    return -1;
}

inline void TypeCheck::CheckBreak(Break *b) {
    auto si = FindBreakScope(false);
    if (si < 0) Error(b, "break outside of a loop or block");
    if (b->val) {
        if (!Is<LoopExpr>(scopes[si].node) && !Is<EarlyBlock>(scopes[si].node))
            Error(b, "break with a value exits loop/block only");
        if (scopes[si].valuelessbreak)
            Error(b, "this construct mixes valueless and valued breaks");
        // Later breaks agree with the first; the first constructs into the
        // type the construct is expected to have, as its tail value does. A
        // first break's value that joins branches as a slice settles no type
        // yet (JoinBranches), and neither do integer constants or floats of
        // literals, which adapt to what a later break gives (§6.4). Where the
        // construct has no destination type, a reference or slice that
        // agrees with the first is stored nowhere by that: what receives the
        // construct's value judges it.
        auto be = scopes[si].breakexpected;
        auto prior = scopes[si].breakvalue;
        auto agree = scopes[si].breaktype && !prior.joinslice && !prior.litint && !prior.litfloat;
        auto expected = agree ? prior.type : be;
        auto unstored = agree && CopiesBranch(be) && IsRefOrSlice(expected);
        Val v;
        {
            // The break is a statement, but its value is the construct's.
            PathScope ps(*this, scopes[si].onargpath ? b->val : nullptr);
            JoinPathScope js(*this, scopes[si].onjoinpath ? b->val : nullptr);
            FlagScope rs(inreturn, scopes[si].inreturn);
            DestScope ds(*this, unstored ? Dest {} : curdst);
            v = CheckValue(b->val, expected, false, CopiesBranch(be));
        }
        // The construct's value is a new one: the break's type and what its
        // references point at, never the operand's storage or literal form,
        // but for a number's: an integer constant, or a float of literals,
        // adapts as a branch's does (§6.4).
        Val exit;
        exit.SetProv(v);
        exit.type = v.type;
        if (IntConsts(v, exit.litlo, exit.lithi)) {
            exit.litint = true;
            exit.nonneg = exit.litlo >= 0;
        }
        exit.litfloat = LitFloat(v);
        exit.isnull = v.isnull;
        exit.contents = v.contents;
        exit.holderset = v.holderset;
        exit.holderfrom = v.holderfrom;
        exit.storagebranches = v.storagebranches;
        exit.implicitcopy = v.implicitcopy;
        exit.joinslice = v.joinslice;
        exit.joinhasslice = v.joinhasslice;
        exit.joinat = v.joinat;
        exit.joina = v.joina;
        exit.joinb = v.joinb;
        exit.fnv = v.fnv;  // Which function it names, as static as its type (§7.6).
        // Nothing after the break can complete a [] that took no element type
        // here: the construct's value does not carry the literal form.
        if (v.emptyarr) Error(b->val, "cannot infer array element type");
        // Checking the value opened and closed scopes of its own, so the
        // construct's scope is addressed afresh rather than through a
        // reference taken before.
        auto &sc = scopes[si];
        sc.breakvalue = JoinBranches(sc.breakvalue, sc.breaktype != nullptr, exit, true, b, true,
                                     sc.onjoinpath);
        if (!sc.breaktype) sc.breaktype = v.type;
        sc.hasbreak = true;
        if (auto e = Is<EarlyBlock>(sc.node)) e->breaks.push_back(b);
        else if (auto l = Is<LoopExpr>(sc.node)) l->breaks.push_back(b);
    } else {
        auto &sc = scopes[si];
        if (sc.breaktype)
            Error(b, "this construct mixes valueless and valued breaks");
        sc.valuelessbreak = true;
        sc.hasbreak = true;
    }
    reachable = false;
}

// A continue is a back edge of its loop: what holds here joins what the
// next iteration starts in (CheckLoopPasses).
inline void TypeCheck::CheckContinue(Node *n) {
    auto si = FindBreakScope(true);
    if (si < 0) Error(n, "continue outside of a loop");
    auto &sc = scopes[si];
    sc.backedge = sc.backedges ? JoinFlow(sc.backedge, SaveFlow()) : SaveFlow();
    sc.backedges = true;
    reachable = false;
}

// ------------------------------------------------------------------
// Statements.

// An expression in statement position: control constructs want no value;
// other values are computed and discarded. Like any statement, it is no
// part of a value being returned (CheckStmt).
inline void TypeCheck::CheckStmtExpr(Node *n) {
    FlagScope rs(inreturn, false);
    NodeScope ns(*this, n);
    if (auto x = Is<IfExpr>(n)) { CheckIf(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    if (auto x = Is<Block>(n)) {
        CheckBlockVal(x, nullptr, false, SK_PLAIN);
        n->exprtype = ast.voidtype;
        return;
    }
    if (auto x = Is<MatchExpr>(n)) { CheckMatch(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    if (auto x = Is<EarlyBlock>(n)) { CheckEarlyBlock(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    if (auto x = Is<LoopExpr>(n)) { CheckLoop(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    CheckValue(n, nullptr);
}

inline void TypeCheck::CheckVarDecl(VarDecl *vd, bool global) {
    TypeExpr *ann = nullptr;
    if (vd->type) {
        ann = Subst(vd->type);
        ValidateType(ann, vd->line, global ? VT_GLOBAL : VT_LOCAL);
        if (vd->isconst) ann = ast.ConstOf(ann);   // `const x: T` is `let x: const T`.
    }
    auto MakeDef = [&](size_t i) -> VarDef * {
        if (global) return vd->defs[i];  // Pre-created by the driver.
        if (vd->defs.size() <= i) vd->defs.push_back(nullptr);
        auto d = vd->defs[i] = ResetLocal(vd->defs[i]);
        d->name = vd->names[i];
        d->line = vd->line;
        d->isvar = vd->isvar;
        d->reusable = vd->reusable;
        d->depth = CurDepth();
        d->ownerspec = frames.back().spec;
        return d;
    };
    // The calls into a recursive cycle this frame has been inside before the
    // initializer about to be checked (CheckCycleInit).
    auto fi = (int)frames.size() - 1;
    auto calls = frames[fi].cyclecalls;
    auto Finish = [&](VarDef *d, TypeExpr *t, const Val *v, Node *init) {
        if (vd->isconst) t = ast.ConstOf(t);
        if (t->kind == TY_VOID) Error(vd, "initializer has no value");
        if (t->kind == TY_FN)
            Error(vd, "function values are compile-time only and cannot be stored (§7.6)");
        d->type = t;
        if (v && IsRefOrSlice(t)) {
            BindRefProvenance(d, *v);
            if (t->cq) d->ref.writable = false;
            // An annotated one's binding FitsAt has noted.
            if (global && !ann && !v->isnull)
                NoteGlobalBinding(d, v->AsRoots(), v->byteview, PointeeOf(t), init->line);
        } else if (v && HoldsPlainRef(t)) {
            NoteHolderBinding(d, *v, init);
        }
        if (vd->reusable) {
            auto kw = vd->reusable == RU_SLICES ? "reusable[]" : "reusable";
            if (!vd->isvar) Error(vd, cat(kw, " requires var"));
            if (!IsArrayKind(t, A_GROW) || ClassOf(t->arr->sub) != SC_FIXED)
                Error(vd, cat(kw, " applies to grow-only arrays of fixed-size "
                                  "elements (§5.4)"));
        }
        if (!global) {
            CheckCycleInit(d, fi, calls);
            vars.push_back(d);
        }
    };
    // A var global declared without an initializer starts as its type's
    // default value (§11.1), as if that were written as its initializer.
    if (global && vd->inits.empty() && ann) {
        string why;
        if (!HasDefault(ann, why))
            Error(vd, cat("global ", vd->names[0], " needs an initializer, since ",
                          TypeStr(ann), " has no default value: ", why));
        vd->inits.push_back(DefaultValue(ann, vd->line));
    }
    if (vd->inits.empty()) {
        if (!ann) Error(vd, "a declaration without an initializer needs a type");
        if (!UninitOK(ann))
            Error(vd, cat("a value of type ", TypeStr(ann),
                          " must be constructed at its declaration (§4.2)"));
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = MakeDef(i);
            d->assigned = false;
            Finish(d, ann, nullptr, nullptr);
        }
        return;
    }
    if (vd->inits.size() == 1 && vd->names.size() > 1) {
        // let a, b = f();
        if (ann) Error(vd, "a type annotation is not supported on multi-value bindings");
        CheckValue(vd->inits[0], nullptr);
        auto call = Is<Call>(vd->inits[0]);
        if (!call || call->rettypes.size() != vd->names.size())
            Error(vd, cat((int64_t)vd->names.size(), " names need that many values"));
        auto rets = lastcallrets;
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = MakeDef(i);
            d->assigned = true;
            // A reference result decays to a copy of a fixed-size pointee,
            // as a single binding's does, unless the declaration binds by
            // reference (`.=`); one to a non-fixed value binds it (§4.1).
            auto rv = vd->byref || IsNonFixedRef(rets[i]) ? rets[i] : DecayRef(rets[i]);
            CheckBindingRoot(d, rv, vd->inits[0]);
            Finish(d, rv.type, &rv, vd->inits[0]);
        }
        return;
    }
    if (vd->inits.size() != vd->names.size())
        Error(vd, cat((int64_t)vd->names.size(), " name(s) with ",
                      (int64_t)vd->inits.size(), " initializer(s)"));
    for (size_t i = 0; i < vd->names.size(); i++) {
        auto d = MakeDef(i);
        Val v;
        calls = frames[fi].cyclecalls;
        {
            // The new variable's storage is the destination; a reference
            // or slice variable binds a value rather than storing one. An
            // annotated variable is a typed slot for constness (§9.5).
            DestScope ds(*this, Dest(d, ann && (ann->kind == TY_REF || ann->kind == TY_SLICE)));
            SlotScope ss(*this, ann != nullptr);
            auto refinit = Is<Unary>(vd->inits[i]);
            if (refinit && refinit->synth) {
                vd->inits[i] = refinit->child;
                refinit = nullptr;
            }
            if (vd->byref && !ann) {
                // `let r .= e;` binds a reference to e: an lvalue by
                // reference, a reference or slice value as it is (§3.8).
                v = CheckV(vd->inits[i], nullptr);
                if (v.isnull) Error(vd->inits[i], "null needs an annotated optional type");
                if (v.lvalue && !IsRefOrSlice(v.type)) {
                    vd->inits[i] = AutoRef(vd->inits[i], v);
                } else if (!IsRefOrSlice(v.type)) {
                    Error(vd->inits[i], ".= binds a reference: the initializer must be a "
                                        "reference, a slice, or storage (a variable, field "
                                        "or element)");
                }
                vd->inits[i]->exprtype = v.type;
            } else if (!ann && refinit && refinit->op == T_BITAND) {
                // `let r = &x;` keeps the reference (an explicit &); every
                // other un-annotated initializer decays to the pointee.
                v = CheckV(vd->inits[i], nullptr);
                vd->inits[i]->exprtype = v.type;
                if (IsPlainRef(v.type) && ClassOf(v.type->ref->sub) != SC_FIXED)
                    Warn(vd->inits[i], cat("redundant &: ", ExprStr(refinit->child),
                                           " binds by reference without it (§4.1)"));
            } else {
                v = CheckValue(vd->inits[i], ann, false, false, !ann);
                // An un-annotated binding of a non-fixed lvalue is a
                // reference to it (§4.1), like an untyped parameter's, and
                // one of a reference to such a value is that reference,
                // which CheckValue keeps.
                if (!ann && IsNonFixedLValue(v)) vd->inits[i] = AutoRef(vd->inits[i], v);
            }
        }
        if (v.emptyarr && !ann) {
            // `var out = [];` -- a grow-only array whose element type the
            // first push, append or assignment into it supplies (§4.2).
            if (!vd->isvar)
                Error(vd->inits[i], "a let bound to [] can never receive elements; "
                                    "annotate its type, or make it a var");
            v.type = PendingArray(vd->inits[i]->line);
            vd->inits[i]->exprtype = v.type;
            v.emptyarr = false;
        }
        if (v.isnull && !ann)
            Error(vd->inits[i], "null needs an annotated optional type");
        if (!ann) {
            NoRelRefCopy(vd->inits[i], v.type);
            CheckBindingRoot(d, v, vd->inits[i]);
        }
        d->assigned = true;
        // A `let` is never assigned again, so its initializer's
        // non-negativity is the name's, unless a writable reference to it
        // (or to a `let` the initializer read) changes it (§4.4,
        // RelyOnNonneg). A `var` can be assigned anything later.
        d->nonneg = !vd->isvar && v.nonneg;
        d->nonnegfrom = d->nonneg ? v.nonnegfrom : nullptr;
        Finish(d, ann ? ann : v.type, &v, vd->inits[i]);
        auto len = Is<Dot>(vd->inits[i]);
        if (!vd->isvar && !global && len && len->member == B_LEN &&
            d->type->kind == TY_INT && d->type->intstorage == IS_I64)
            d->markof = len->obj;
    }
}

// A binding without an annotation takes its value's own type, so no store
// rule (FitsAt) saw it; the variable must still not outlive what the value
// points into (§9.2) -- a temporary of the binding's own statement, say. A
// value pointing into a block whose value it is never gets here: the block
// rejected it as it closed (CheckBranchRoot).
inline void TypeCheck::CheckBindingRoot(VarDef *d, const Val &v, Node *at) {
    auto t = v.type;
    if (!t || v.isnull) return;
    auto isrs = IsRefOrSlice(t);
    if (!isrs && !HoldsPlainRef(t)) return;
    const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
    // A global is storage, which no reference into a grow-shrink array is
    // stored in (§5.2), as FitsAt says of an annotated one.
    if (d->isglobal) {
        if (auto gs = StoredIntoGrowShrink(v, roots, t, !isrs))
            Error(at, NeverStoredError(gs, MayPointWording(roots, gs)));
    }
    for (auto &a : roots.alts) {
        auto root = a.root;
        // The sentinel stands for a root not known yet, which a variable may hold.
        if (!root || root == temproot || Depth(root) <= Depth(d)) continue;
        auto what = !isrs ? "a value holding references" : t->kind == TY_SLICE ? "a slice"
                                                                                : "a reference";
        if (IsTemp(root))
            Error(at, cat("binding ", d->name, " to ", what, " rooted at a temporary, which "
                          "does not outlive it (§9.2): a temporary lasts until the end of its "
                          "statement, so bind it to a variable of its own first"));
        Error(at, cat("binding ", d->name, " to ", what, " rooted at ", root->name,
                      ", which does not outlive it (§9.2)"));
    }
}

// A non-fixed-size local is built where it is stored (§4.3), so its data
// stack is taken before its initializer runs: a call into the recursive
// cycle of frame fi there (JoinCycle counts them) keeps that stack across
// the call, as one in the local's scope does (§7.8).
inline void TypeCheck::CheckCycleInit(VarDef *d, int fi, int calls) {
    if (frames[fi].cyclecalls == calls || ClassOf(d->type) == SC_FIXED) return;
    Error(frames[fi].cyclecall,
          cat(FrameFnName(fi), " calls into its recursive cycle while initializing "
              "non-fixed-size local ", d->name, ", which is built in place (§7.8)"));
}

// §4.4: which lvalues accept `=` after construction.
inline void TypeCheck::AssignableClassCheck(TypeExpr *t, Node *at) {
    auto cls = ClassOf(t);
    if (cls == SC_VARIABLE && !(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED))
        Error(at, cat("a value of type ", TypeStr(t),
                      " is frozen at construction (§4.4); rebuild its container instead"));
}

// The right-hand side of a whole assignment, against the storage it
// replaces. Where that storage holds a resizable array (`arr`, rooted at
// `built`), the old elements are released first -- a shrink of the array,
// which anything still pointing into it rejects (§5.1, §5.2) -- and the new
// ones are built over them, so the assignment is a growth of it as well; the
// value then runs with that array under construction, which nothing it does
// may grow (§1.3(4)) or even use (§4.4).
inline Val TypeCheck::CheckAssignedValue(Assign *a, TypeExpr *target, TypeExpr *arr,
                                         const Roots &built, Dest dest) {
    if (arr) {
        ShrinkThrough(a, "assign", ExprStr(a->lval), built, target);
        NoteGrow(a, built, cat("assign ", ExprStr(a->lval)));
    }
    SlotScope ss(*this, true);
    auto base = cur.growlog.size();
    auto v = CheckValueAt(a->rhs, target, dest);
    if (arr && !built.None()) {
        CheckGrowsSince(base, built, cat("the value assigned to ", ExprStr(a->lval)));
        CheckBuiltUses(a->rhs, a->lval, built, arr);
    }
    if (v.type->kind == TY_VOID && reachable)
        Error(a, "the right-hand side has no value");
    return v;
}

inline void TypeCheck::CheckAssign(Assign *a) {
    auto lv = CheckLValue(a->lval);
    auto held = lv;
    auto throughref = a->op != T_DOTASSIGN && IsPlainRef(held.type);
    if (throughref) DerefLValue(held, a->lval);
    // A shrink only frees element storage. A field path from a variable, or
    // from a reference to a resizable value, stays in that value's own
    // storage (a frame object's prefix included). A path through an element,
    // a slice or another reference stays live, and so does the pointee that
    // `=` writes through a path ending in a reference.
    auto owns = [&](TypeExpr *t) {
        return t && t->kind != TY_SLICE &&
               (t->kind != TY_REF || ClassOf(t->ref->sub) == SC_RESIZABLE);
    };
    auto infields = [&](Node *n) {
        while (auto d = Is<Dot>(n)) {
            n = d->obj;
            if (!Is<Ident>(n) && !owns(n->exprtype)) return false;
        }
        auto id = Is<Ident>(n);
        return id && id->vdef && owns(id->vdef->type);
    };
    // The location, evaluated before the right-hand side (HeldOperands): a
    // reference to the slot itself, not a read-back of what it holds.
    if (throughref || !infields(a->lval)) {
        Val loc;
        loc.type = ast.RefTo(held.type, a->line);
        loc.SetProv(held);
        nodevals[a->lval] = loc;
    } else {
        nodevals.erase(a->lval);
    }
    if (a->op == T_DOTASSIGN) { CheckRebind(a, lv); return; }
    if (lv.isvarint)
        Error(a, "varint fields are written only at construction (§3.6)");
    if (lv.type->kind == TY_REF && lv.type->ref->lenstorage == IS_VARINT)
        Error(a, "varint-width relative references are written only at construction");
    // References are transparent: `=` through a reference-typed location
    // (a narrowed optional included, since lv.type is the narrowed type)
    // writes the pointee; rebinding is `.=`.
    if (IsPlainRef(lv.type)) {
        if (a->op == T_ASSIGN) PointeeAssign(a, lv, held);
        else CompoundAssign(a, lv.type->ref->sub, PointeeWritable(lv, a));
        a->pointee = true;
        return;
    }
    if (IsOptional(lv.type))
        Error(a, "optional value must be narrowed before writing through it, "
                 "or rebound with .=");
    if (a->op != T_ASSIGN) {
        NoCopyWrite(a, lv);
        NoLetAssign(a, lv);
        CompoundAssign(a, lv.type, WholeWritable(lv));
        if (lv.var) RequireAssigned(lv.var, a);
        return;
    }
    // Plain value assignment.
    auto target = lv.var ? lv.var->type : lv.type;
    if (lv.var && !lv.var->assigned) {
        // First assignment of an uninitialized local constructs it.
    } else {
        NoCopyWrite(a, lv);
        NoLetAssign(a, lv);
        if (!WholeWritable(lv))
            Error(a, "cannot assign through this path (const, or a read-only "
                     "instantiation, §9.5)");
        AssignableClassCheck(target, a);
    }
    if (IsPendingArray(target)) {
        // `var x = []; x = other;` completes x from the assigned array.
        auto av = DecayRef(CheckV(a->rhs, nullptr));
        CompletePending(target, PendingElemFromSeq(av, a), a->line);
    }
    // An uninitialized local's first assignment constructs it; every other
    // assignment of a resizable array, or of a value holding one, replaces
    // elements that are already there.
    Roots built;
    auto arr = ResizableArrayIn(target);
    if (arr && (!lv.var || lv.var->assigned)) {
        if (lv.var) built.Set(lv.var, true); else built = lv;
        if (arr->arr->akind == A_GROW &&
            (built.None() || built.Any([](const RootAlt &r) { return !r.root || !r.root->type; })))
            Error(a, "cannot assign a grow-only array through a reference: a shrink of "
                     "a grow-only array applies to a local of the function that owns "
                     "it (§5.1)");
    } else {
        arr = nullptr;
    }
    auto v = CheckAssignedValue(a, target, arr, built,
                                Dest(lv, lv.var && IsRefOrSlice(target), lv.reached));
    if (lv.var) {
        // Slice variables carry their value's provenance (refs use .=).
        if (target->kind == TY_SLICE) {
            if (!lv.var->refrootknown || lv.var->ref.Unknown()) BindRefProvenance(lv.var, v);
            else CheckRefRebindRoot(a, lv.var, v);
        }
        lv.var->assigned = true;
        KillNarrow(lv.var);
    }
}

// `.=`: rebinds the reference stored at the location (§3.8).
inline void TypeCheck::CheckRebind(Assign *a, LVal &lv) {
    auto target = lv.var ? lv.var->type : lv.type;
    if (target->kind != TY_REF)
        Error(a, cat(".= rebinds references; the target has type ", TypeStr(target)));
    // A varint-width relative reference cannot be re-encoded in place
    // (its byte length could change, §3.6/§3.9).
    if (target->ref->lenstorage == IS_VARINT)
        Error(a, "varint-width relative references are written only at construction");
    if (lv.var) {
        if (!lv.var->isvar && lv.var->assigned)
            Error(a, cat("cannot rebind let ", lv.var->name));
    } else {
        NoLetAssign(a, lv);
        if (!lv.writable)
            Error(a, "cannot assign through this path (const, or a read-only "
                     "instantiation, §9.5)");
    }
    Val v;
    bool wasplain;
    {
        DestScope ds(*this, lv.var ? Dest(lv.var, true) : Dest(lv, false, lv.reached));
        // `r .= &x` is the documented spelling of a rebind (§3.8), so an
        // explicit & is not redundant here as it is at a binding destination.
        SlotScope ss(*this, true);
        v = CheckV(a->rhs, target);
        if (BindsRef(v, target)) a->rhs = AutoRef(a->rhs, v, !target->cq);
        wasplain = IsPlainRef(v.type);
        MustFit(v, a->rhs, target);
    }
    a->rhs->exprtype = v.type;
    if (lv.var) {
        if (!lv.var->refrootknown || lv.var->ref.Unknown()) BindRefProvenance(lv.var, v);
        else if (!v.isnull) CheckRefRebindRoot(a, lv.var, v);
        lv.var->assigned = true;
        // Rebinding an optional settles its nullness — narrowed only when
        // the new value is provably non-null (a plain reference).
        if (target->ref->optional) {
            auto spec = CurRealFrame().spec;
            if (spec && lv.var->ownerspec != spec)
                spec->reboundoptionals.insert(lv.var);
            if (!v.isnull && wasplain) {
                lv.var->narrowed = NarrowedRef(target, a->line);
            } else {
                lv.var->narrowed = nullptr;
            }
        }
    }
}

// Provenance for writing the pointee of the reference at lv.
inline bool TypeCheck::PointeeWritable(LVal &lv, Node *at) {
    if (lv.var) {
        RequireAssigned(lv.var, at);
        return lv.var->ref.writable;
    }
    // Read out of a slot: writable unless the slot's type says const, the
    // only way a read-only reference got into it (§9.5).
    return !lv.type->cq;
}

// `r = v` through the reference at lv, whose pointee is the location `at`
// (DerefLValue): where the reference points is the destination.
inline void TypeCheck::PointeeAssign(Assign *a, LVal &lv, const LVal &at) {
    auto pt = lv.type->ref->sub;
    if (!PointeeWritable(lv, a))
        Error(a, "cannot write through this reference: non-writable provenance (§9.5)");
    if (pt->kind == TY_SLICE)
        NoteStaleRoots(cat("a slice is written through a reference at ", Where(a->line)));
    if (pt->kind == TY_INT && pt->intstorage == IS_VARINT)
        Error(a, "varint fields are written only at construction (§3.6)");
    AssignableClassCheck(pt, a);
    auto arr = ResizableArrayIn(pt);
    if (arr && arr->arr->akind == A_GROW)
        Error(a, cat("cannot assign ", arr == pt ? "a grow-only array" : "a value holding a "
                     "grow-only array", " through a reference: a shrink of a grow-only "
                     "array applies to a local of the function that owns it (§5.1)"));
    Roots built;
    if (arr) built = at;
    CheckAssignedValue(a, pt, arr, built, Dest(at, false, at.reached));
}

// A reference variable keeps one root for its whole life (see header
// note): re-assignments must carry the same root, or one at the same
// scope depth (which is equivalent for the outlives check).
inline void TypeCheck::CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv) {
    if (rv.byteview && !vd->ref.byteview) {
        vd->ref.byteview = true;
        NoteFact(vd);
    }
    // The variable may point wherever it did and wherever the new value may:
    // a later read sees either. A root it did not have joins only at the
    // depth of its binding.
    auto added = rv.Any([&](const RootAlt &a) { return !vd->ref.Has(a.root); });
    auto nr = rv.Root();
    if (added && Depth(nr) != Depth(vd->ref.Root()))
        Error(at, cat("re-binding ", vd->name, " with a reference rooted at a different "
                      "scope depth is not supported; declare a new variable"));
    // Nor does a variable that points into no grow-shrink array start to:
    // what read it before -- earlier in a loop, through a reference to it --
    // may have stored it (§5.2).
    auto was = GrowShrinkTaint(vd->ref, vd->type);
    if (!was) {
        if (auto gs = GrowShrinkTaint(rv, vd->type))
            Error(at, cat("re-binding ", vd->name, " to a reference that may point into a "
                          "grow-shrink array (", gs->name, "), where it pointed into none, is "
                          "not supported; declare a new variable (§5.2)"));
    }
    // A rebind to other storage keeps the lifetime bound but means the
    // variable no longer names one array: a read inside a loop this rebind
    // is in sees both on the next pass (CheckLoopPasses).
    if (vd->ref.Add(rv)) NoteFact(vd);
}

// A `let` binding or field is not assigned as a whole (§4.4).
inline void TypeCheck::NoLetAssign(Node *at, const LVal &lv) {
    if (lv.letbound && !lv.copyof)
        Error(at, cat("cannot assign to let ", lv.letname, " (§4.4)"));
}

// A by-value `for` or `match` binding is a copy of the element: a write
// would update the copy and nothing else (§6.5, §8.1).
inline void TypeCheck::NoCopyWrite(Node *at, const LVal &lv) {
    if (lv.copyof)
        Error(at, cat("cannot write ", lv.copyof->name, ": a by-value binding is a copy "
                      "of the element; bind it by reference (&", lv.copyof->name,
                      ") to write the element (§6.5)"));
}

// Whether `=`, a compound assignment or `++`/`--` may write the location as
// a whole. A variable is assigned as its binding allows (NoLetAssign,
// NoCopyWrite): the `const` of its type is about its contents, so a
// `var s: const u8[:]` moves on with `s = s[1..]` while its bytes stay
// read-only (§9.5). A field or element is as writable as the path to it.
inline bool TypeCheck::WholeWritable(const LVal &lv) {
    return lv.var || lv.writable;
}

inline void TypeCheck::CompoundAssign(Assign *a, TypeExpr *st, bool writable) {
    if (!writable)
        Error(a, "cannot assign through this path (const, or a read-only "
                 "instantiation, §9.5)");
    auto isbit = a->op == T_ANDEQ || a->op == T_OREQ || a->op == T_XOREQ;
    auto isshift = a->op == T_SHLEQ || a->op == T_SHREQ;
    if (st->kind == TY_INT && st->intstorage != IS_VARINT && isshift) {
        // A shift count is any integer type, masked to the width (§6.2).
        CheckIntAny(a->rhs);
    } else if (st->kind == TY_INT && st->intstorage != IS_VARINT) {
        // The update computes at the target's type; the operand must
        // reach it implicitly (literal fit or widening, §6.3).
        CheckValue(a->rhs, st);
    } else if (st->kind == TY_FLT && !isbit && !isshift) {
        CheckValue(a->rhs, st);
    } else if (st->kind == TY_INT) {
        Error(a, "varint fields are written only at construction (§3.6)");
    } else {
        Error(a, cat("operator ", TName(a->op), " cannot be applied to ", TypeStr(st)));
    }
}

inline void TypeCheck::CheckIncDec(IncDec *x) {
    auto lv = CheckLValue(x->lval);
    auto st = lv.type;
    auto writable = WholeWritable(lv);
    if (IsPlainRef(lv.type)) {
        st = lv.type->ref->sub;
        writable = PointeeWritable(lv, x);
    } else {
        NoCopyWrite(x, lv);
        NoLetAssign(x, lv);
        if (lv.var) RequireAssigned(lv.var, x);
    }
    if (!writable) Error(x, "cannot modify through this path (const, or a read-only "
                            "instantiation, §9.5)");
    if (!IsIntT(st))
        Error(x, cat(TName(x->op), " requires an integer lvalue, got ", TypeStr(st)));
}

}  // namespace goose
