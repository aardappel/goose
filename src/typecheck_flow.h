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
    if (s.kind == SK_LOOP) ResolvePendingShrinks((int)scopes.size() - 1);
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

inline VarDef *TypeCheck::CanonRoot(VarDef *v) {
    while (v && v->rootalias) v = v->rootalias;
    return v;
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
// (§5.2): r holds one, or stands for a call-site root that does, or for the
// undetermined root of a back edge's result (§7.8), which may be one.
inline bool TypeCheck::IsGrowShrinkRoot(VarDef *r) {
    return r && (r == cycleroot || r->growshrink || (r->type && ContainsGrowShrink(r->type)));
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
    while (v && !v->type && v->classfrom) v = v->classfrom;
    if (!v || !v->type) return true;   // Storage this frame cannot see: assume it can.
    return GrowShrinkContains(v->type, of);
}

// Whether the reference or slice v of type t rooted at root, or for a holder
// any reference it holds, may point into a grow-shrink array: what §5.2
// keeps out of every field, element and global.
inline bool TypeCheck::IntoGrowShrink(const Val &v, VarDef *root, TypeExpr *t, bool holder) {
    vector<TypeExpr *> pointees;
    if (holder) RefPointees(t, pointees); else pointees.push_back(PointeeOf(t));
    auto intogs = v.byteview && IsGrowShrinkRoot(root) && MayBeViewed(root);
    for (auto pt : pointees) intogs |= GrowShrinkCanHold(root, pt);
    return intogs;
}

// Why a reference rooted at root, or a holder of one, is never stored: it
// may point into a grow-shrink array (§5.2), or it is a back edge's result,
// which may point anywhere, as may a parameter whose class stands for one.
inline string TypeCheck::NeverStoredError(VarDef *root) {
    auto from = root;
    while (from && !from->type && from->classfrom) from = from->classfrom;
    auto recresult = "the result of a recursive call whose returned reference's root the "
                     "cycle's returns do not determine (§7.8); it may only be passed down";
    if (root == cycleroot) return cat("storing ", recresult);
    if (from == cycleroot)
        return cat("storing a reference into ", root->name, ", which may be ", recresult);
    return cat("storing a reference into ", root->name,
               ", which holds a grow-shrink array: such a reference lives in a "
               "variable, is passed down or returned, and is never stored (§5.2)");
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

// Reading a reference variable's root as an identity. A loop body is
// checked once, so a read here sees the value a later rebind in the same
// loop leaves behind; noting the read lets that rebind reject the root
// change instead of silently invalidating this one.
inline bool TypeCheck::RefExactOf(VarDef *vd) {
    if (!vd->refrootknown || !vd->ref.rootexact) return false;
    for (auto i = (int)scopes.size() - 1; i >= vd->depth; i--)
        if (scopes[i].kind == SK_LOOP) { vd->refidentityused = true; break; }
    return true;
}

// Whether the reference or slice variable v may point into the array at
// `root`, which a shrink of that array is checked against (§5.1, §5.2): it
// is bound there; or it is a `var` bound at that depth, which a same-depth
// rebind could since have retargeted (§9.2); or its root only bounds the
// pointee's lifetime, at or below that depth; or it is not bound yet and
// may still commit to the array further down a loop body.
inline bool TypeCheck::RefMayPointInto(VarDef *v, VarDef *root) {
    auto r = RefRootOf(v);
    return r == root || (v->isvar && Depth(r) == Depth(root)) ||
           (!v->ref.rootexact && Depth(r) >= Depth(root)) ||
           (!v->refrootknown && Depth(v) >= Depth(root));
}

// Binds a reference variable to where p points.
inline void TypeCheck::BindProv(VarDef *vd, const Prov &p) {
    vd->ref = p;
    vd->ref.root = CanonRoot(p.root);
    vd->refrootknown = true;
}

// The first non-null binding of a reference variable fixes its provenance.
inline void TypeCheck::BindRefProvenance(VarDef *vd, const Val &v) {
    if (!v.isnull) BindProv(vd, v);
}

// Where a reference variable's value points, as a read of it sees it: the
// root it is committed to (the temp sentinel before any), the exactness a
// read may rely on (RefExactOf), and its provenance bits.
inline Prov TypeCheck::RefProvOf(VarDef *vd) {
    Prov p = vd->ref;
    p.root = RefRootOf(vd);
    p.rootexact = RefExactOf(vd);
    if (!vd->refrootknown && vd->type && vd->type->kind == TY_REF && vd->type->ref->optional) {
        // Bound only to null so far (or bound later in a loop body this
        // use precedes): what it can point at is whatever can hold the
        // pointee type at its own depth or outside, exactly when that is
        // one variable -- the read-back rule's answer (§9.5).
        vector<VarDef *> cands, bounds;
        auto hasstatic = false;
        RootCandidates(LoadType(vd->type->ref->sub), Depth(vd), false, !vd->type->cq, cands,
                       hasstatic, bounds);
        if (!cands.empty()) {
            p.root = cands[0];
            for (auto c : cands) if (Depth(c) > Depth(p.root)) p.root = c;
            p.rootexact = cands.size() == 1 && !hasstatic && bounds.empty();
        }
    }
    return p;
}

inline VarDef *TypeCheck::ResetLocal(VarDef *previous) {
    if (!previous) return ast.NewVarDef();
    // Cached nested specializations capture this identity. Recompute its
    // checking state, but retain the capture discovered on an earlier pass.
    auto captured = previous->captured;
    *previous = VarDef {};
    previous->captured = captured;
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
// it, which `use`, the node naming it, reports.
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
    if (found) return found;
    auto g = ast.LookupGlobal(name, ns);
    return g && !g->defs.empty() ? g->defs[0] : nullptr;
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

inline TypeCheck::FlowState TypeCheck::SaveFlow() {
    FlowState f;
    f.st.reserve(vars.size());
    for (auto v : vars) f.st.push_back({ v->assigned, v->narrowed });
    // Globals' narrowing participates too (assignment in branches).
    for (auto g : ast.globals)
        for (auto v : g->defs) f.globals.push_back({ v, v->narrowed });
    f.reachable = reachable;
    return f;
}

inline void TypeCheck::RestoreFlow(const FlowState &f) {
    for (size_t i = 0; i < f.st.size() && i < vars.size(); i++) {
        vars[i]->assigned = f.st[i].first;
        vars[i]->narrowed = f.st[i].second;
    }
    for (auto [v, narrowed] : f.globals) v->narrowed = narrowed;
    reachable = f.reachable;
}

// Joins two branch end states into the current state: a fact holds after
// the join iff it holds in every reachable branch.
inline void TypeCheck::MergeFlow(const FlowState &a, const FlowState &b) {
    for (size_t i = 0; i < vars.size(); i++) {
        auto aa = i < a.st.size() ? a.st[i] : pair<bool, TypeExpr *> { false, nullptr };
        auto bb = i < b.st.size() ? b.st[i] : pair<bool, TypeExpr *> { false, nullptr };
        vars[i]->assigned = (a.reachable ? aa.first : true) &&
                            (b.reachable ? bb.first : true);
        TypeExpr *n = nullptr;
        if (!a.reachable) n = bb.second;
        else if (!b.reachable) n = aa.second;
        else if (aa.second && bb.second) n = aa.second;
        vars[i]->narrowed = n;
    }
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

// Names rebound anywhere below n: loop bodies clear these narrowings up
// front, since iteration 2 sees the rebind. A plain `=` writes through a
// narrowed optional and leaves its nullness alone.
inline void TypeCheck::CollectAssignedNames(Node *n, set<string_view> &out,
                                            set<Node *> *seen,
                                            const vector<SFunction *> &locals, bool outer) {
    if (!n) return;
    set<Node *> local;
    if (!seen) seen = &local;
    if (!seen->insert(n).second) return;
    if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN)
        if (auto id = Is<Ident>(a->lval)) out.insert(id->name);
    if (Is<FnDecl>(n)) return;  // Declaring an uncalled function has no effects.
    if (auto b = Is<Block>(n)) {
        auto nested = locals;
        // A later local declaration cannot hide a call that precedes it.
        // Track the same declaration order as CheckStmts while following
        // only bodies reached by calls.
        for (auto st : b->stmts) {
            if (auto fd = Is<FnDecl>(st)) nested.push_back(fd->sf);
            else CollectAssignedNames(st, out, seen, nested, outer);
        }
        CollectAssignedNames(b->tail, out, seen, nested, outer);
        return;
    }
    auto localfn = [&](string_view name) -> SFunction * {
        for (auto i = locals.rbegin(); i != locals.rend(); ++i)
            if ((*i)->name == name) return *i;
        return outer ? LookupLocalFn(name) : nullptr;
    };
    // The next iteration sees rebindings performed by callees too, even
    // before their specializations have been checked for the first time.
    if (auto c = Is<Call>(n)) {
        vector<SFunction *> targets;
        if (auto id = Is<Ident>(c->callee)) {
            if (auto fb = outer ? LookupFnVal(id->name) : nullptr) {
                if (fb->named) targets.push_back(fb->named);
                else if (fb->fv) CollectAssignedNames(fb->fv->body, out, seen, locals, outer);
            } else if (auto sf = localfn(id->name)) targets.push_back(sf);
            else targets = ast.LookupFunctions(id->name, id->ns);
        } else if (auto d = Is<Dot>(c->callee)) {
            if (auto sf = localfn(d->name)) targets.push_back(sf);
            else targets = ast.LookupFunctions(d->name, d->ns);
        }
        for (auto sf : targets) {
            // A cached body may have bound its nested calls before a later
            // scope shadowed those names. Its recorded effects still apply;
            // scanning the source in the current environment cannot recover
            // those earlier bindings.
            for (auto spec : sf->specs)
                for (auto vd : spec->reboundoptionals) out.insert(vd->name);
            // Top-level functions cannot see the caller's local functions.
            // Nested functions declared in their own bodies are still tracked.
            if (sf->isnested) CollectAssignedNames(sf->body, out, seen, locals, outer);
            else CollectAssignedNames(sf->body, out, seen, {}, false);
        }
    }
    n->Children([&](Node *c) { CollectAssignedNames(c, out, seen, locals, outer); });
}

// The variables a loop body writes anywhere -- assigned whole, or through
// a field, element or member call -- whose contents a read earlier in the
// body cannot rely on: the next iteration sees the write.
inline void TypeCheck::CollectAssignedBases(Node *n, set<string_view> &out) {
    if (!n) return;
    auto base = [&](Node *l) {
        for (;;) {
            if (auto d = Is<Dot>(l)) { l = d->obj; continue; }
            if (auto ix = Is<Index>(l)) { l = ix->obj; continue; }
            if (auto sl = Is<SliceExpr>(l)) { l = sl->obj; continue; }
            if (auto u = Is<Unary>(l); u && u->op == T_BITAND) { l = u->child; continue; }
            break;
        }
        if (auto id = Is<Ident>(l)) out.insert(id->name);
    };
    if (auto a = Is<Assign>(n)) base(a->lval);
    if (auto c = Is<Call>(n))
        if (auto d = Is<Dot>(c->callee)) base(d->obj);
    n->Children([&](Node *c) { CollectAssignedBases(c, out); });
}

// A holder value bound to a new variable: the variable's contents are the
// value's, and the binding is a store like any other for the shrink rules.
inline void TypeCheck::NoteHolderBinding(VarDef *d, const Val &v) {
    Val hv = v;
    hv.root = CanonRoot(HolderRootOf(v));
    hv.rootexact = v.holderset && v.holderexact;
    RecordStore(d, hv, nullptr, false, v.holderfrom);
}

inline void TypeCheck::PushLoopAssigned(Node *body) {
    set<string_view> names;
    CollectAssignedBases(body, names);
    loopassigned.push_back(std::move(names));
    PrebindLoopRefs(body);
}

// A reference variable declared outside a loop and bound only inside it
// (`var last: Node? = null;` before the loop) would be rootless at a use
// earlier in the body than its rebind. The body is scanned for its `.=`
// targets ahead of the loop, and where every rebind resolves to one root
// syntactically, the variable takes that root now (§9.2); the first real
// binding then confirms it and supplies the full provenance.
inline void TypeCheck::PrebindLoopRefs(Node *body) {
    auto sf = frames.back().sf;
    if (!sf || !sf->body) return;
    map<string_view, vector<Node *>> targets;
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN)
            if (auto id = Is<Ident>(a->lval)) targets[id->name].push_back(a->rhs);
        n->Children([&](Node *c) { walk(c); });
    };
    walk(body);
    for (auto &[name, rhss] : targets) {
        auto vd = LookupVar(name, CurNs());
        if (!vd || vd->isglobal || vd->refrootknown || !vd->type || vd->type->kind != TY_REF)
            continue;
        if (vd->ownerspec != frames.back().spec) continue;
        auto cy = Cycles();
        RootDesc d;
        for (auto rhs : rhss) {
            if (Is<NullLit>(rhs)) continue;
            vector<string_view> busy;
            d = CycleRoots::JoinDesc(d, cy.ScanExpr(sf, rhs, busy, 0));
        }
        VarDef *root = nullptr;
        auto exact = false;
        if (!ResolvePrebind(d, root, exact)) continue;
        vd->ref.root = CanonRoot(root);
        vd->ref.rootexact = exact;
        vd->ref.rootfrom = nullptr;
        vd->refrootknown = true;
        vd->refprebound = true;
    }
}

// The variable a scanned root names from here, and whether it owns the
// storage exactly.
inline bool TypeCheck::ResolvePrebind(const RootDesc &d, VarDef *&root, bool &exact) {
    auto ofvar = [&](VarDef *v) {
        if (!v) return false;
        auto isref = v->type && IsRefOrSlice(v->type);
        if (isref) {
            if (!v->refrootknown) return false;
            root = RefRootOf(v);
            exact = v->ref.rootexact;
        } else {
            root = v;
            exact = true;
        }
        return true;
    };
    switch (d.kind) {
        case RD_GLOBAL: return ofvar(d.glob);
        case RD_LOCAL: case RD_FREE: return ofvar(LookupVar(d.name, CurNs()));
        case RD_PARAM: {
            auto spec = frames.back().spec;
            if (!spec || d.param >= (int)spec->params.size()) return false;
            return ofvar(spec->params[d.param]);
        }
        default: return false;
    }
}

inline bool TypeCheck::AssignedInEnclosingLoop(VarDef *vd) {
    for (auto &s : loopassigned) if (s.count(vd->name)) return true;
    return false;
}

inline void TypeCheck::KillNarrowingsAssignedIn(Node *body) {
    set<string_view> names;
    CollectAssignedNames(body, names);
    for (auto v : vars) if (names.count(v->name)) v->narrowed = nullptr;
    for (auto g : ast.globals)
        for (auto v : g->defs)
            for (auto name : names) {
                auto ref = SplitName(name, CurNs());
                if (ref.leaf == v->name && (!ref.qualified || ref.ns == g->ns))
                    v->narrowed = nullptr;
            }
}

inline set<VarDef *> TypeCheck::NarrowedOptionals() {
    set<VarDef *> out;
    for (auto v : vars) if (v->narrowed) out.insert(v);
    for (auto g : ast.globals) for (auto v : g->defs) if (v->narrowed) out.insert(v);
    return out;
}

// The optionals a checked body rebinds through the calls it made.
inline void TypeCheck::CollectCheckedRebinds(Node *n, set<VarDef *> &out) {
    if (!n || Is<FnDecl>(n)) return;
    if (auto c = Is<Call>(n)) {
        auto add = [&](FnSpec *sp) {
            if (!sp) return;
            out.insert(sp->reboundoptionals.begin(), sp->reboundoptionals.end());
            if (sp->inprogress)
                for (auto v : InProgressRebinds(sp)) out.insert(v);
        };
        add(c->spec);
        for (auto d : c->dispatch) add(d);
        for (auto &fs : c->fmtspecs) add(fs.second);
    }
    RunChildren(n, [&](Node *ch) { CollectCheckedRebinds(ch, out); });
}

// A fact assumed at the start of a loop body that the loop rebinds through a
// call the name scan could not follow did not hold on the next iteration, and
// does not hold after the loop.
inline void TypeCheck::FinishLoopNarrowing(Node *loop, const set<VarDef *> &assumed) {
    set<VarDef *> rebound;
    CollectCheckedRebinds(loop, rebound);
    for (auto v : rebound) {
        if (assumed.count(v))
            Error(loop, cat("optional ", v->name, " is rebound later in this loop, so its "
                            "narrowing does not hold on the next iteration (§3.8)"));
        v->narrowed = nullptr;
    }
}

// ------------------------------------------------------------------
// Small type constructors and views.

inline TypeExpr *TypeCheck::RefTo(TypeExpr *t, Line l) {
    auto r = ast.NewType(TY_REF, l);
    r->ref = ast.NewDetail<TypeRef>();
    r->ref->sub = t;
    return r;
}

// What an optional reference type narrows to (§3.8): a plain reference to
// the same pointee under the same qualifier.
inline TypeExpr *TypeCheck::NarrowedRef(TypeExpr *t, Line l) {
    auto r = RefTo(t->ref->sub, l);
    r->cq = t->cq;
    return r;
}

// Merges the values of two branches (for roots: the deeper — i.e. more
// conservative — root wins; writability must hold in both).
inline Val TypeCheck::MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at,
                                bool wantvalue, Node *anode, Node *bnode) {
    if (!areach) return b;
    if (!breach) return a;
    Val v;
    // An integer constant in one branch adapts to the other branch's
    // integer type, as it would at any typed destination (§3.1).
    auto adapt = [&](const Val &c, Node *cn, const Val &o) {
        if (!o.type || o.type->kind != TY_INT || !c.type || c.type->kind != TY_INT ||
            TypeEq(c.type, o.type) || o.ck == CK_INT || o.unsized)
            return false;
        if (c.unsized) {
            // A literal parameter adapts to the other branch like a
            // constant (§7.7).
            RecordLitAdapt(c, o.type, at->line);
        } else if (c.ck != CK_INT || !FitsIntStorage(c.ival, c.uns, o.type->intstorage)) {
            return false;
        }
        if (cn) RetypeConstBranch(cn, o.type);
        return true;
    };
    if (adapt(a, anode, b)) { v.type = b.type; }
    else if (adapt(b, bnode, a)) { v.type = a.type; }
    else v.type = UnifyBranch(a.type, b.type, at, wantvalue);
    // A holder value from either branch: the deeper bound wins.
    if (a.holderset || b.holderset) {
        auto ar = HolderRootOf(a), br = HolderRootOf(b);
        v.holderset = true;
        v.holderroot = Depth(ar) >= Depth(br) ? ar : br;
        v.holderexact = a.holderexact && b.holderexact && ar == br;
    }
    v.isnull = a.isnull && b.isnull;   // Both null: still a null, which names no root.
    v.root = Depth(a.root) >= Depth(b.root) ? a.root : b.root;
    v.rootexact = a.rootexact && b.rootexact && CanonRoot(a.root) == CanonRoot(b.root);
    v.rootfrom = a.rootfrom ? a.rootfrom : b.rootfrom;
    v.writable = a.writable && b.writable;
    v.reusable = a.reusable & b.reusable;
    v.byteview = a.byteview || b.byteview;
    return v;
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
    auto root = CanonRoot(isrs ? v.root : HolderRootOf(v));
    // The sentinels stand for roots not known yet, as for a binding.
    if (!root || root == temproot || root == cycleroot ||
        Depth(root) <= depth + (IsTemp(root) ? 1 : 0))
        return;
    auto what = !isrs ? "holds references" : t->kind == TY_SLICE ? "is a slice" : "is a reference";
    if (IsTemp(root))
        Error(at, cat("the ", construct, "'s value ", what, " rooted at a temporary, which does "
                      "not outlive it (§9.2): a temporary lasts until the end of its statement, "
                      "or of the block whose final expression made it"));
    Error(at, cat("the ", construct, "'s value ", what, " rooted at ", root->name,
                  ", which does not outlive it (§9.2)"));
}

// A branch that was an integer constant now has the merged type: the
// constant node and the blocks down to it.
inline void TypeCheck::RetypeConstBranch(Node *n, TypeExpr *t) {
    if (!n) return;
    n->exprtype = t;
    if (auto b = Is<Block>(n)) { RetypeConstBranch(b->tail, t); return; }
    if (auto e = Is<EarlyBlock>(n)) { RetypeConstBranch(e->body, t); return; }
    if (auto i = Is<IfExpr>(n)) {
        RetypeConstBranch(i->thenb, t);
        RetypeConstBranch(i->elseb, t);
    }
}

inline Val TypeCheck::CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue) {
    CheckCond(x->cond);
    auto entry = SaveFlow();
    NarrowCond(x->cond, true);
    auto tv = CheckBlockVal(x->thenb, expected, wantvalue, SK_PLAIN);
    auto aflow = SaveFlow();
    RestoreFlow(entry);
    Val ev = VoidVal();
    NarrowCond(x->cond, false);
    if (auto ei = Is<IfExpr>(x->elseb)) {
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
    return MergeVals(tv, aflow.reachable, ev, bflow.reachable, x, wantvalue, x->thenb, x->elseb);
}

inline Val TypeCheck::CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue, int scopekind,
                                    Node *scopenode) {
    ValueRegion vr(*this, wantvalue);
    PushScope(scopekind, scopenode);
    BlockScope bs(*this, b);
    CheckStmts(b);
    Val v = VoidVal();
    if (b->tail) {
        if (wantvalue) v = CheckValue(b->tail, expected, false,
                                     !expected || expected->kind == TY_VOID);
        else CheckStmtExpr(b->tail);
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
    auto sv = CheckV(m->scrutinee, nullptr);
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
    Node *resultnode = nullptr;   // The arm `result` came from, while it is one arm's.
    auto resultreach = false;
    auto first = true;
    FlowState acc;
    auto DoArm = [&](MatchArm &arm, VarDef *binder) {
        RestoreFlow(entry);
        PushScope(SK_PLAIN);
        if (binder) vars.push_back(binder);
        Val av;
        if (wantvalue) av = CheckValue(arm.body, expected, false,
                                      !expected || expected->kind == TY_VOID);
        else CheckStmtExpr(arm.body);
        auto aflow = SaveFlow();
        if (!reachable) av.type = nullptr;
        PopScope();
        CheckBranchRoot(av, CurDepth(), arm.body, "match arm");
        if (first) {
            result = av;
            resultnode = arm.body;
            resultreach = aflow.reachable;
            acc = aflow;
            first = false;
        } else {
            result = MergeVals(result, resultreach, av, aflow.reachable, m, wantvalue,
                               resultnode, arm.body);
            resultnode = nullptr;
            resultreach = resultreach || aflow.reachable;
            // Accumulate the join of all arms' flow.
            auto save = SaveFlow();
            MergeFlow(acc, aflow);
            acc = SaveFlow();
            RestoreFlow(save);
        }
    };
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
            if (arm.pat.kind != P_VARIANT)
                Error(arm.body, "ADT match arms are variant names (or _)");
            auto found = en->FindVariant(arm.pat.variant);
            if (!found)
                Error(arm.body, cat("enum ", en->name, " has no variant named ",
                                    arm.pat.variant));
            auto vi = en->VariantIndex(found);
            if (covered[vi])
                Error(arm.body, cat("duplicate match arm for variant ", arm.pat.variant));
            covered[vi] = true;
            arm.variant = found;
            VarDef *binder = nullptr;
            if (!arm.pat.binder.empty()) {
                if (!found->has_payload)
                    Error(arm.body, cat("variant ", arm.pat.variant, " has no payload to bind"));
                auto vt = VariantTypeOf(enumtype, found, m->line);
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
                                            "(§3.5); bind by value: ", arm.pat.variant,
                                            " ", arm.pat.binder));
                    if (ClassOf(enumtype) == SC_RESIZABLE)
                        Error(arm.body, cat("cannot bind the payload of resizable ",
                                            enumtype->enu->en->name, " by reference: a "
                                            "whole assignment may replace its variant "
                                            "(§3.5); bind by value: ", arm.pat.variant,
                                            " ", arm.pat.binder));
                    binder->type = RefTo(vt, m->line);
                    BindProv(binder, sv);
                } else {
                    if (HasRelRefT(vt))
                        Error(arm.body, cat("payload of ", arm.pat.variant, " contains "
                                            "relative references; bind it by reference "
                                            "(&", arm.pat.binder, ")"));
                    binder->type = vt;  // Payload copy, any mode (§8.1).
                    binder->isvar = false;
                    binder->copybind = true;
                    NoteNonfixedLocal(vt, m->line, !frames.back().spec);
                    if (HoldsPlainRef(vt)) {
                        // A copied payload holding references: its contents
                        // are the scrutinee's.
                        ReadBack contents;
                        auto intemp = TempContents(sv, contents);
                        Val hv;
                        hv.root = intemp ? contents.root : CanonRoot(sv.root);
                        hv.rootexact = intemp && contents.exact;
                        hv.byteview = sv.byteview;
                        RecordStore(binder, hv, nullptr, false,
                                    intemp ? contents.from : CanonRoot(sv.root));
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
        for (auto &arm : m->arms) {
            if (arm.pat.kind == P_WILDCARD) { haswild = true; DoArm(arm, nullptr); continue; }
            if (arm.pat.kind == P_VARIANT)
                Error(arm.body, cat("unknown pattern ", arm.pat.variant,
                                    " in an integer match"));
            arm.lo = ConstIntOrError(arm.pat.lo, "match pattern");
            arm.hi = arm.pat.kind == P_RANGE
                         ? ConstIntOrError(arm.pat.hi, "match pattern")
                         : arm.lo + 1;
            // Pattern values must fit the scrutinee's type (a u64
            // scrutinee accepts any 64-bit pattern).
            if (sit != IS_U64) {
                auto ul = Is<IntLit>(arm.pat.lo) && Is<IntLit>(arm.pat.lo)->uns;
                auto uh = arm.pat.hi && Is<IntLit>(arm.pat.hi) &&
                          Is<IntLit>(arm.pat.hi)->uns;
                if (!FitsIntStorage(arm.lo, ul, sit) ||
                    (arm.pat.kind == P_RANGE && !FitsIntStorage(arm.hi, uh, sit)))
                    Error(arm.body, cat("match pattern does not fit the scrutinee "
                                        "type ", TypeStr(st)));
            }
            if (arm.hi <= arm.lo && !(sit == IS_U64 && (arm.lo < 0 || arm.hi < 0)))
                Error(arm.body, "empty range in match pattern");
            DoArm(arm, nullptr);
        }
        if (!haswild) Error(m, "integer match requires a _ arm");
    } else {
        Error(m, cat("cannot match on a value of type ", TypeStr(st)));
    }
    if (!first) RestoreFlow(acc);
    reachable = resultreach;
    return wantvalue ? result : VoidVal();
}

inline Val TypeCheck::CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue) {
    ValueRegion vr(*this, wantvalue);
    PushScope(SK_BLOCK, x);
    if (wantvalue) scopes.back().breakexpected = expected;
    BlockScope bs(*this, x->body);
    CheckStmts(x->body);
    Val v = VoidVal();
    if (x->body->tail) {
        if (wantvalue) v = CheckValue(x->body->tail, expected, false,
                                     !expected || expected->kind == TY_VOID);
        else CheckStmtExpr(x->body->tail);
    }
    if (!reachable) v.type = nullptr;
    auto sc = scopes.back();
    PopScope();
    x->body->exprtype = v.type ? v.type : ast.voidtype;
    reachable = reachable || sc.hasbreak;  // Exits via the tail or any break.
    if (!wantvalue) return VoidVal();
    CheckBranchRoot(v, CurDepth(), x->body->tail, "block");
    if (sc.breaktype) CheckBranchRoot(sc.breakvalue, CurDepth(), x, "block");
    Val r = sc.breaktype ? MergeVals(v, v.type != nullptr, sc.breakvalue, true,
                                     x, wantvalue, x->body->tail, nullptr) : v;
    r.type = UnifyBranch(v.type, sc.breaktype, x, wantvalue);
    if (!r.type) r.type = ast.voidtype;
    return r;
}

// A loop body: its statements, then a tail that is a statement like any
// other (only a `break` gives a loop a value, §6.5).
inline void TypeCheck::CheckLoopBody(Block *body) {
    BlockScope bs(*this, body);
    CheckStmts(body);
    if (body->tail) CheckStmtExpr(body->tail);
}

// Closing a loop, whatever its header: the scope's record for the caller to
// read the breaks off, the names its body assigns, the flow as it was before
// the loop, and the narrowings its body turned out to rebind -- which the
// body assumed on its first iteration and the next one would not have
// (§3.8).
inline TypeCheck::Scope TypeCheck::EndLoop(Node *x, Block *body, const FlowState &entry,
                                           const set<VarDef *> &assumed) {
    auto sc = scopes.back();
    PopScope();
    loopassigned.pop_back();
    RestoreFlow(entry);
    KillNarrowingsAssignedIn(body);
    FinishLoopNarrowing(x, assumed);
    return sc;
}

inline Val TypeCheck::CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue) {
    ValueRegion vr(*this, wantvalue);
    KillNarrowingsAssignedIn(x->body);
    auto assumed = NarrowedOptionals();
    PushLoopAssigned(x->body);
    auto entry = SaveFlow();
    PushScope(SK_LOOP, x);
    if (wantvalue) scopes.back().breakexpected = expected;
    CheckLoopBody(x->body);
    auto sc = EndLoop(x, x->body, entry, assumed);
    reachable = sc.hasbreak;  // A loop only exits via break.
    if (!wantvalue || !sc.breaktype) return VoidVal();
    CheckBranchRoot(sc.breakvalue, CurDepth(), x, "loop");
    return sc.breakvalue;
}

inline void TypeCheck::CheckWhile(While *x) {
    // Narrowings from before the loop that the body or the condition rebinds
    // do not hold on the second iteration, nor in the condition that runs
    // again after it; the condition's own narrowings do, since it runs before
    // every iteration, and a rebind inside the body un-narrows from that
    // point on.
    KillNarrowingsAssignedIn(x->body);
    KillNarrowingsAssignedIn(x->cond);
    auto assumed = NarrowedOptionals();
    CheckCond(x->cond);
    auto entry = SaveFlow();
    {
        // What the condition narrows by itself holds in the body on every
        // iteration, so only the narrowings from before the loop that it
        // does not establish again are assumed across iterations.
        auto keep = SaveFlow();
        for (auto v : assumed) v->narrowed = nullptr;
        NarrowCond(x->cond, true);
        for (auto v : NarrowedOptionals()) assumed.erase(v);
        RestoreFlow(keep);
    }
    PushLoopAssigned(x->body);
    NarrowCond(x->cond, true);
    PushScope(SK_LOOP, x);
    CheckLoopBody(x->body);
    auto sc = EndLoop(x, x->body, entry, assumed);
    if (sc.breaktype)
        Error(x, "break with a value exits loop/block only, not while");
}

inline void TypeCheck::CheckFor(ForLoop *x) {
    auto byref = x->byref;
    TypeExpr *bindtype = nullptr;
    TypeExpr *elemtype = nullptr;   // The array's element type, where it has one.
    Prov iterprov;   // What a reference binding points into.
    ReadBack contents;   // Where the elements point, when the array is a temporary.
    auto intemp = false;
    if (auto r = Is<RangeExpr>(x->iter)) {
        auto lo = CheckIntAny(r->lo);
        auto hi = CheckIntAny(r->hi);
        auto ct = UnifyNumeric(r, T_DOTDOT, lo, hi, lo.type, hi.type);
        RetypeOperands(r->lo, r->hi, lo, hi, ct);
        r->exprtype = ct;
        x->iterkind = IK_RANGE;
        if (x->byref) Error(x, "cannot iterate an integer range by reference");
        bindtype = ct;
    } else {
        auto iv = CheckV(x->iter, nullptr);
        NoTemporaryLiteral(x->iter, iv.type);
        x->iter->exprtype = iv.type;
        auto t = iv.type;
        iterprov = iv;
        intemp = TempContents(iv, contents);
        if (t->kind == TY_REF && !t->ref->optional) t = t->ref->sub;  // Iterate through refs.
        t = LoadType(t);
        RequireComplete(t, x->line);
        if (IsIntT(t)) {
            x->iterkind = IK_COUNT;
            if (x->byref) Error(x, "cannot iterate an integer count by reference");
            // A count reads a reference as its pointee (§3.8); a sequence
            // keeps the reference, the loop iterating it where it lies.
            x->iter->exprtype = DecayRef(iv).type;
            bindtype = t;
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            elemtype = elem;
            x->iterkind = t->kind == TY_ARRAY ? IK_ARRAY : IK_SLICE;
            // Non-fixed elements bind by reference either way (§4.1).
            if (x->byref && ClassOf(elem) != SC_FIXED) {
                Warn(x, "redundant &: elements of this type bind by reference without it "
                        "(§4.1)");
            }
            byref = x->byref || ClassOf(elem) != SC_FIXED;
            if (byref) {
                bindtype = RefTo(elem, x->line);
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
    }
    auto entry = SaveFlow();
    KillNarrowingsAssignedIn(x->body);
    auto assumed = NarrowedOptionals();
    PushLoopAssigned(x->body);
    PushScope(SK_LOOP, x);
    auto vd = NewVar(x->var, bindtype, x->line, false, x->vdef);
    vd->assigned = true;
    vd->copybind = (x->iterkind == IK_ARRAY || x->iterkind == IK_SLICE) && !byref;
    if (!IsRefOrSlice(bindtype) && HoldsPlainRef(bindtype)) {
        // A holder element copied out: its contents are the array's.
        Val hv;
        hv.root = intemp ? contents.root : CanonRoot(iterprov.root);
        hv.rootexact = intemp && contents.exact;
        hv.byteview = iterprov.byteview;
        RecordStore(vd, hv, nullptr, false, intemp ? contents.from : CanonRoot(iterprov.root));
    }
    if (IsRefOrSlice(bindtype)) {
        // A relative-reference or slice element bound by value was read
        // out of the array, so where it points follows the read-back rule
        // (§9.5), not the array's own root.
        if (!byref && elemtype &&
            ((elemtype->kind == TY_REF && elemtype->ref->lenstorage >= 0) ||
             elemtype->kind == TY_SLICE)) {
            auto rb = ReadBackRoot(elemtype, CanonRoot(iterprov.root), iterprov.rootexact,
                                   iterprov.byteview, intemp ? &contents : nullptr);
            iterprov.root = rb.root;
            iterprov.rootexact = rb.exact;
            iterprov.rootfrom = rb.from;
            if (elemtype->cq) iterprov.writable = false;
        }
        BindProv(vd, iterprov);
    }
    x->vdef = vd;
    if (!x->idxvar.empty()) {
        auto idx = NewVar(x->idxvar, ast.inttypes[IS_I64], x->line, false, x->idxdef);
        idx->assigned = true;
        x->idxdef = idx;
    }
    CheckLoopBody(x->body);
    auto sc = EndLoop(x, x->body, entry, assumed);
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
        // type the construct is expected to have, as its tail value does.
        auto expected = scopes[si].breaktype ? scopes[si].breaktype : scopes[si].breakexpected;
        auto be = scopes[si].breakexpected;
        auto v = CheckValue(b->val, expected, false, !be || be->kind == TY_VOID);
        // The construct's value is a new one: the break's type and what its
        // references point at, never the operand's storage or literal form.
        Val exit;
        exit.SetProv(v);
        exit.type = v.type;
        exit.isnull = v.isnull;
        exit.holderroot = v.holderroot;
        exit.holderexact = v.holderexact;
        exit.holderset = v.holderset;
        exit.holderfrom = v.holderfrom;
        exit.fnv = v.fnv;  // Which function it names, as static as its type (§7.6).
        // Nothing after the break can complete a [] that took no element type
        // here: the construct's value does not carry the literal form.
        if (v.emptyarr) Error(b->val, "cannot infer array element type");
        // Checking the value opened and closed scopes of its own, so the
        // construct's scope is addressed afresh rather than through a
        // reference taken before.
        auto &sc = scopes[si];
        sc.breakvalue = MergeVals(sc.breakvalue, sc.breaktype != nullptr, exit, true, b, true,
                                  nullptr, b->val);
        if (!sc.breaktype) sc.breaktype = v.type;
        sc.hasbreak = true;
    } else {
        auto &sc = scopes[si];
        if (sc.breaktype)
            Error(b, "this construct mixes valueless and valued breaks");
        sc.valuelessbreak = true;
        sc.hasbreak = true;
    }
    reachable = false;
}

inline void TypeCheck::CheckContinue(Node *n) {
    if (FindBreakScope(true) < 0) Error(n, "continue outside of a loop");
    reachable = false;
}

// ------------------------------------------------------------------
// Statements.

// An expression in statement position: control constructs want no value;
// other values are computed and discarded.
inline void TypeCheck::CheckStmtExpr(Node *n) {
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
    auto Finish = [&](VarDef *d, TypeExpr *t, const Val *v) {
        if (vd->isconst) t = ast.ConstOf(t);
        if (t->kind == TY_VOID) Error(vd, "initializer has no value");
        if (t->kind == TY_FN)
            Error(vd, "function values are compile-time only and cannot be stored (§7.6)");
        d->type = t;
        if (v && IsRefOrSlice(t)) {
            BindRefProvenance(d, *v);
            if (t->cq) d->ref.writable = false;
        } else if (v && HoldsPlainRef(t)) {
            NoteHolderBinding(d, *v);
        }
        if (vd->reusable) {
            auto kw = vd->reusable == RU_SLICES ? "reusable[]" : "reusable";
            if (!vd->isvar) Error(vd, cat(kw, " requires var"));
            if (!IsArrayKind(t, A_GROW) || ClassOf(t->arr->sub) != SC_FIXED)
                Error(vd, cat(kw, " applies to grow-only arrays of fixed-size "
                                  "elements (§5.4)"));
        }
        NoteNonfixedLocal(t, vd->line, global);
        if (!global) {
            vars.push_back(d);
        }
    };
    if (vd->inits.empty()) {
        if (!ann) Error(vd, "a declaration without an initializer needs a type");
        if (!UninitOK(ann))
            Error(vd, cat("a value of type ", TypeStr(ann),
                          " must be constructed at its declaration (§4.2)"));
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = MakeDef(i);
            d->assigned = false;
            Finish(d, ann, nullptr);
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
            // Reference returns decay in inference, like everywhere, unless
            // the declaration binds by reference (`.=`).
            auto rv = vd->byref ? rets[i] : DecayRef(rets[i]);
            CheckBindingRoot(d, rv, vd->inits[0]);
            Finish(d, rv.type, &rv);
        }
        return;
    }
    if (vd->inits.size() != vd->names.size())
        Error(vd, cat((int64_t)vd->names.size(), " name(s) with ",
                      (int64_t)vd->inits.size(), " initializer(s)"));
    for (size_t i = 0; i < vd->names.size(); i++) {
        auto d = MakeDef(i);
        Val v;
        {
            // The new variable's storage is the destination; a reference
            // or slice variable binds a value rather than storing one. An
            // annotated variable is a typed slot for constness (§9.5).
            DestScope ds(*this, Dest { d, true, ann && (ann->kind == TY_REF ||
                                                        ann->kind == TY_SLICE) });
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
                v = CheckValue(vd->inits[i], ann);
                // An un-annotated binding of a non-fixed lvalue is a
                // reference to it (§4.1), like an untyped parameter's.
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
        // A `let` has exactly one value, so its initializer's
        // non-negativity is the name's for good (§6.1). A `var` can be
        // assigned anything later.
        d->nonneg = !vd->isvar && v.nonneg;
        Finish(d, ann ? ann : v.type, &v);
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
    auto root = CanonRoot(isrs ? v.root : HolderRootOf(v));
    // The sentinels stand for roots not known yet, which a variable may hold.
    if (!root || root == temproot || root == cycleroot || Depth(root) <= Depth(d)) return;
    auto what = !isrs ? "a value holding references" : t->kind == TY_SLICE ? "a slice"
                                                                            : "a reference";
    if (IsTemp(root))
        Error(at, cat("binding ", d->name, " to ", what, " rooted at a temporary, which does "
                      "not outlive it (§9.2): a temporary lasts until the end of its "
                      "statement, so bind it to a variable of its own first"));
    Error(at, cat("binding ", d->name, " to ", what, " rooted at ", root->name,
                  ", which does not outlive it (§9.2)"));
}

inline void TypeCheck::NoteNonfixedLocal(TypeExpr *t, Line l, bool global) {
    if (global) return;
    if (ClassOf(t) == SC_FIXED) return;
    auto spec = frames.back().spec;
    if (!spec) return;
    if (!spec->has_nonfixed_local) {
        spec->has_nonfixed_local = true;
        spec->nonfixedline = l;
    }
    if (spec->incycle || spec->sf->isrec)
        Error(l, cat("function ", spec->sf->name, " is (in) a recursive cycle and may "
                     "not own non-fixed-size locals (§7.8)"));
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
                                         VarDef *built, bool builtexact, Dest dest) {
    if (arr) {
        auto rest = shrinkrest;
        shrinkrest = a->rhs;
        ShrinkThrough(a, true, "assign", ExprStr(a->lval), built, builtexact, target);
        shrinkrest = rest;
        NoteGrow(a, built, builtexact, cat("assign ", ExprStr(a->lval)));
    }
    SlotScope ss(*this, true);
    auto base = growlog.size();
    auto v = CheckValueAt(a->rhs, target, dest);
    if (arr && built) {
        CheckGrowsSince(base, built, builtexact, cat("the value assigned to ", ExprStr(a->lval)));
        CheckBuiltUses(a->rhs, a->lval, built, builtexact, arr);
    }
    if (v.type->kind == TY_VOID && reachable)
        Error(a, "the right-hand side has no value");
    return v;
}

inline void TypeCheck::CheckAssign(Assign *a) {
    TempScope temps(*this);
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
    if (throughref || !infields(a->lval)) HoldLocation(a->lval, held);
    if (a->op == T_DOTASSIGN) { CheckRebind(a, lv); return; }
    if (lv.isvarint)
        Error(a, "varint fields are written only at construction (§3.6)");
    if (lv.type->kind == TY_REF && lv.type->ref->lenstorage == IS_VARINT)
        Error(a, "varint-width relative references are written only at construction");
    // References are transparent: `=` through a reference-typed location
    // (a narrowed optional included, since lv.type is the narrowed type)
    // writes the pointee; rebinding is `.=`.
    if (IsPlainRef(lv.type)) {
        if (a->op == T_ASSIGN) PointeeAssign(a, lv);
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
        CompoundAssign(a, lv.type, lv.writable);
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
        if (!lv.writable)
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
    VarDef *built = nullptr;
    auto builtexact = false;
    auto arr = ResizableArrayIn(target);
    if (arr && (!lv.var || lv.var->assigned)) {
        built = lv.var ? lv.var : CanonRoot(lv.root);
        builtexact = lv.var != nullptr || lv.rootexact;
        if (arr->arr->akind == A_GROW && (!built || !built->type))
            Error(a, "cannot assign a grow-only array through a reference: a shrink of "
                     "a grow-only array applies to a local of the function that owns "
                     "it (§5.1)");
    } else {
        arr = nullptr;
    }
    auto v = CheckAssignedValue(a, target, arr, built, builtexact,
                                Dest { lv.root, lv.rootexact,
                                       lv.var && IsRefOrSlice(target) });
    if (lv.var) {
        // Slice variables carry their value's provenance (refs use .=).
        if (target->kind == TY_SLICE) {
            if (!lv.var->refrootknown) BindRefProvenance(lv.var, v);
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
        DestScope ds(*this, lv.var ? Dest { lv.var, true, true }
                                   : Dest { lv.root, lv.rootexact });
        // `r .= &x` is the documented spelling of a rebind (§3.8), so an
        // explicit & is not redundant here as it is at a binding destination.
        SlotScope ss(*this, true);
        v = CheckV(a->rhs, target);
        if (BindsRef(v, target)) a->rhs = AutoRef(a->rhs, v);
        wasplain = IsPlainRef(v.type);
        MustFit(v, a->rhs, target, false);
    }
    a->rhs->exprtype = v.type;
    if (lv.var) {
        if (!lv.var->refrootknown) {
            BindRefProvenance(lv.var, v);
        } else if (lv.var->refprebound && !v.isnull && CanonRoot(v.root) == lv.var->ref.root) {
            // The root the loop scan predicted; this binding's provenance
            // is the real one.
            BindRefProvenance(lv.var, v);
            lv.var->refprebound = false;
        } else if (!v.isnull) {
            CheckRefRebindRoot(a, lv.var, v);
        }
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

inline void TypeCheck::PointeeAssign(Assign *a, LVal &lv) {
    auto pt = lv.type->ref->sub;
    if (!PointeeWritable(lv, a))
        Error(a, "cannot write through this reference: non-writable provenance (§9.5)");
    if (pt->kind == TY_INT && pt->intstorage == IS_VARINT)
        Error(a, "varint fields are written only at construction (§3.6)");
    AssignableClassCheck(pt, a);
    auto arr = ResizableArrayIn(pt);
    if (arr && arr->arr->akind == A_GROW)
        Error(a, cat("cannot assign ", arr == pt ? "a grow-only array" : "a value holding a "
                     "grow-only array", " through a reference: a shrink of a grow-only "
                     "array applies to a local of the function that owns it (§5.1)"));
    auto built = arr ? CanonRoot(lv.var ? RefRootOf(lv.var) : lv.root) : nullptr;
    auto builtexact = arr && (lv.var ? RefExactOf(lv.var) : lv.rootexact);
    CheckAssignedValue(a, pt, arr, built, builtexact,
                       lv.var ? Dest { RefRootOf(lv.var), RefExactOf(lv.var) }
                              : Dest { lv.root, lv.rootexact });
}

// A reference variable keeps one root for its whole life (see header
// note): re-assignments must carry the same root, or one at the same
// scope depth (which is equivalent for the outlives check).
inline void TypeCheck::CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv) {
    vd->ref.byteview = vd->ref.byteview || rv.byteview;
    auto nr = CanonRoot(rv.root);
    if (nr != vd->ref.root && Depth(nr) != Depth(vd->ref.root))
        Error(at, cat("re-binding ", vd->name, " with a reference rooted at a different "
                      "scope depth is not supported; declare a new variable"));
    if (nr == vd->ref.root) {
        // The root is unchanged, so only the new value's own exactness can
        // weaken what the variable stands for.
        if (!rv.rootexact) { vd->ref.rootexact = false; vd->ref.rootfrom = rv.rootfrom; }
        return;
    }
    // A same-depth rebind keeps the lifetime bound but moves the pointee to
    // other storage, so the variable no longer names one array. A read
    // earlier in an enclosing loop has already seen this value, and cannot
    // be revisited, so its claim has to be rejected here.
    if (vd->refidentityused)
        Error(at, cat("re-binding ", vd->name, " to storage rooted at ",
                      nr ? nr->name : string_view("static data"), " after its root ",
                      vd->ref.root ? vd->ref.root->name : string_view("static data"),
                      " was used as the identity of a relative reference (§3.9)"));
    vd->ref.root = nr;
    vd->ref.rootexact = false;
    vd->ref.rootfrom = rv.rootfrom;
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
    auto writable = lv.writable;
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
