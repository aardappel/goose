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
    while (v && !v->type && v->classfrom) v = v->classfrom;
    if (!v || !v->type) return true;   // Storage this frame cannot see: assume it can.
    return GrowShrinkContains(v->type, of);
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
        vector<VarDef *> cands;
        auto hasstatic = false;
        RootCandidates(LoadType(vd->type->ref->sub), Depth(vd), false, cands, hasstatic);
        if (!cands.empty()) {
            p.root = cands[0];
            for (auto c : cands) if (Depth(c) > Depth(p.root)) p.root = c;
            p.rootexact = cands.size() == 1 && !hasstatic;
        }
    }
    return p;
}

inline VarDef *TypeCheck::NewVar(string_view name, TypeExpr *type, Line l, bool isvar) {
    auto vd = ast.NewVarDef();
    vd->name = name;
    vd->type = type;
    vd->line = l;
    vd->isvar = isvar;
    vd->depth = CurDepth();
    vd->ownerspec = frames.back().spec;
    vars.push_back(vd);
    return vd;
}

// Name lookup: current frame's scopes innermost-out, then the lexical
// parent chain (free variables of nested fns / function values, §7.5),
// then globals, in the namespace order of docs/design/namespaces.md.
inline VarDef *TypeCheck::LookupVar(string_view name, string_view ns) {
    for (auto fi = (int)frames.size() - 1; fi >= 0;) {
        auto &f = frames[fi];
        auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                  : frames[fi + 1].varbase;
        for (auto i = limit - 1; i >= f.varbase; i--) {
            if (vars[i]->name == name) {
                if (fi != (int)frames.size() - 1) vars[i]->captured = true;
                return vars[i];
            }
        }
        fi = f.lexframe;
    }
    auto g = ast.LookupGlobal(name, ns);
    return g && !g->defs.empty() ? g->defs[0] : nullptr;
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
// frame index for lexparent chains.
inline int TypeCheck::FrameOfSpec(FnSpec *sp) {
    for (auto i = (int)frames.size() - 1; i >= 0; i--)
        if (frames[i].spec == sp && !frames[i].isfunval) return i;
    return -1;
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
    f.reachable = reachable;
    return f;
}

inline void TypeCheck::RestoreFlow(const FlowState &f) {
    for (size_t i = 0; i < f.st.size() && i < vars.size(); i++) {
        vars[i]->assigned = f.st[i].first;
        vars[i]->narrowed = f.st[i].second;
    }
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
    reachable = a.reachable || b.reachable;
}

// Optional narrowing (§3.8): a bare optional variable as a condition, and
// the obvious compositions. `sense` = the region where cond is true.
inline void TypeCheck::NarrowCond(Node *cond, bool sense) {
    if (auto id = Is<Ident>(cond)) {
        if (!id->vdef) return;
        auto t = id->vdef->type;
        if (t && t->kind == TY_REF && t->ref->optional && sense && !id->vdef->narrowed) {
            auto r = ast.NewType(TY_REF, cond->line);
            r->ref = ast.NewDetail<TypeRef>();
            r->ref->sub = t->ref->sub;
            r->cq = t->cq;
            id->vdef->narrowed = r;
        }
        return;
    }
    if (auto u = Is<Unary>(cond)) {
        if (u->op == T_NOT) NarrowCond(u->child, !sense);
        return;
    }
    if (auto b = Is<Binary>(cond)) {
        if ((b->op == T_ANDAND && sense) || (b->op == T_OROR && !sense)) {
            NarrowCond(b->left, sense);
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

// Names assigned or rebound anywhere below n: loop bodies clear these
// narrowings up front, since iteration 2 sees the assignment.
inline void TypeCheck::CollectAssignedNames(Node *n, set<string_view> &out) {
    if (!n) return;
    if (auto a = Is<Assign>(n))
        if (auto id = Is<Ident>(a->lval)) out.insert(id->name);
    n->Children([&](Node *c) { CollectAssignedNames(c, out); });
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
        auto isref = v->type && (v->type->kind == TY_REF || v->type->kind == TY_SLICE);
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
}

// ------------------------------------------------------------------
// Small type constructors and views.

inline TypeExpr *TypeCheck::RefTo(TypeExpr *t, Line l) {
    auto r = ast.NewType(TY_REF, l);
    r->ref = ast.NewDetail<TypeRef>();
    r->ref->sub = t;
    return r;
}

// A specific reason from the last failing FitsAt, if any.

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
    v.reusable = a.reusable && b.reusable;
    return v;
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
    if (x->elseb) x->elseb->exprtype = ev.type;
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
        if (wantvalue) v = CheckValue(b->tail, expected);
        else CheckStmtExpr(b->tail);
    } else if (wantvalue && reachable && expected && expected->kind != TY_VOID) {
        Error(b, "block used as a value must end in an expression");
    }
    if (!reachable) v.type = nullptr;  // Bottom: the block never produces.
    PopScope();
    b->exprtype = v.type ? v.type : ast.voidtype;
    return v;
}

inline Val TypeCheck::CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue) {
    auto sv = CheckV(m->scrutinee, nullptr);
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
        if (wantvalue) av = CheckValue(arm.body, expected);
        else CheckStmtExpr(arm.body);
        auto aflow = SaveFlow();
        if (!reachable) av.type = nullptr;
        PopScope();
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
            SVariant *found = nullptr;
            int vi = 0;
            for (auto i = 0; i < (int)en->variants.size(); i++)
                if (en->variants[i].name == arm.pat.variant) { found = &en->variants[i]; vi = i; break; }
            if (!found)
                Error(arm.body, cat("enum ", en->name, " has no variant named ",
                                    arm.pat.variant));
            if (covered[vi])
                Error(arm.body, cat("duplicate match arm for variant ", arm.pat.variant));
            covered[vi] = true;
            arm.variant = found;
            VarDef *binder = nullptr;
            if (!arm.pat.binder.empty()) {
                if (!found->has_payload && found->fields.empty())
                    Error(arm.body, cat("variant ", arm.pat.variant, " has no payload to bind"));
                auto vt = VariantTypeOf(enumtype, found, m->line);
                binder = ast.NewVarDef();
                binder->name = arm.pat.binder;
                binder->line = m->line;
                binder->depth = CurDepth() + 1;
                binder->ownerspec = frames.back().spec;
                binder->assigned = true;
                if (arm.pat.byref) {
                    // `Variant &b`: only variable-mode payloads may be
                    // bound by reference — a fixed-mode value may be
                    // overwritten with another variant, so references
                    // into its payload are illegal (§3.5, §8.1).
                    if (!enumtype->enu->varmode)
                        Error(arm.body, cat("cannot bind the payload of fixed-mode ",
                                            enumtype->enu->en->name, " by reference "
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
                        Val hv;
                        hv.root = CanonRoot(sv.root);
                        hv.rootexact = false;
                        RecordStore(binder, hv, nullptr, false, CanonRoot(sv.root));
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
    if (!wantvalue) return VoidVal();
    if (!result.type) result.type = ast.voidtype;
    return result;
}

inline Val TypeCheck::CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue) {
    ValueRegion vr(*this, wantvalue);
    PushScope(SK_BLOCK, x);
    if (wantvalue) scopes.back().breakexpected = expected;
    BlockScope bs(*this, x->body);
    CheckStmts(x->body);
    Val v = VoidVal();
    if (x->body->tail) {
        if (wantvalue) v = CheckValue(x->body->tail, expected);
        else CheckStmtExpr(x->body->tail);
    }
    if (!reachable) v.type = nullptr;
    auto sc = scopes.back();
    PopScope();
    x->body->exprtype = v.type ? v.type : ast.voidtype;
    reachable = reachable || sc.hasbreak;  // Exits via the tail or any break.
    if (!wantvalue) return VoidVal();
    Val r = sc.breaktype ? MergeVals(v, v.type != nullptr, sc.breakvalue, true,
                                     x, wantvalue, x->body->tail, nullptr) : v;
    r.type = UnifyBranch(v.type, sc.breaktype, x, wantvalue);
    if (!r.type) r.type = ast.voidtype;
    return r;
}

inline Val TypeCheck::CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue) {
    ValueRegion vr(*this, wantvalue);
    KillNarrowingsAssignedIn(x->body);
    PushLoopAssigned(x->body);
    auto entry = SaveFlow();
    PushScope(SK_LOOP, x);
    if (wantvalue) scopes.back().breakexpected = expected;
    for (auto st : x->body->stmts) CheckStmt(st);
    if (x->body->tail) CheckStmtExpr(x->body->tail);
    auto sc = scopes.back();
    PopScope();
    loopassigned.pop_back();
    RestoreFlow(entry);
    KillNarrowingsAssignedIn(x->body);
    reachable = sc.hasbreak;  // A loop only exits via break.
    Val v = wantvalue && sc.breaktype ? sc.breakvalue : VoidVal();
    return v;
}

inline void TypeCheck::CheckWhile(While *x) {
    CheckCond(x->cond);
    auto entry = SaveFlow();
    // Narrowings from before the loop that the body reassigns do not hold
    // on the second iteration; the condition's own do, since it runs
    // before every iteration, and a rebind inside the body un-narrows from
    // that point on.
    KillNarrowingsAssignedIn(x->body);
    PushLoopAssigned(x->body);
    NarrowCond(x->cond, true);
    PushScope(SK_LOOP, x);
    {
        BlockScope bs(*this, x->body);
        CheckStmts(x->body);
        if (x->body->tail) CheckStmtExpr(x->body->tail);
    }
    auto sc = scopes.back();
    PopScope();
    loopassigned.pop_back();
    RestoreFlow(entry);
    KillNarrowingsAssignedIn(x->body);
    if (sc.breaktype)
        Error(x, "break with a value exits loop/block only, not while");
}

inline void TypeCheck::CheckFor(ForLoop *x) {
    TypeExpr *bindtype = nullptr;
    TypeExpr *elemtype = nullptr;   // The array's element type, where it has one.
    Prov iterprov;   // What a reference binding points into.
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
        x->iter->exprtype = iv.type;
        auto t = iv.type;
        iterprov = iv;
        if (t->kind == TY_REF && !t->ref->optional) t = t->ref->sub;  // Iterate through refs.
        t = LoadType(t);
        RequireComplete(t, x->line);
        if (IsIntT(t)) {
            x->iterkind = IK_COUNT;
            if (x->byref) Error(x, "cannot iterate an integer count by reference");
            bindtype = t;
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            elemtype = elem;
            x->iterkind = t->kind == TY_ARRAY ? IK_ARRAY : IK_SLICE;
            // Non-fixed elements bind by reference either way (§4.1).
            if (!x->byref && ClassOf(elem) != SC_FIXED) {
                x->byref = true;
            } else if (x->byref && ClassOf(elem) != SC_FIXED) {
                Warn(x, "redundant &: elements of this type bind by reference without it "
                        "(§4.1)");
            }
            if (x->byref) {
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
    PushLoopAssigned(x->body);
    PushScope(SK_LOOP, x);
    auto vd = NewVar(x->var, bindtype, x->line, false);
    vd->assigned = true;
    vd->copybind = (x->iterkind == IK_ARRAY || x->iterkind == IK_SLICE) && !x->byref;
    if (bindtype->kind != TY_REF && bindtype->kind != TY_SLICE && HoldsPlainRef(bindtype)) {
        // A holder element copied out: its contents are the array's.
        Val hv;
        hv.root = CanonRoot(iterprov.root);
        hv.rootexact = false;
        RecordStore(vd, hv, nullptr, false, CanonRoot(iterprov.root));
    }
    if (bindtype->kind == TY_REF || bindtype->kind == TY_SLICE) {
        // A relative-reference or slice element bound by value was read
        // out of the array, so where it points follows the read-back rule
        // (§9.5), not the array's own root.
        if (!x->byref && elemtype &&
            ((elemtype->kind == TY_REF && elemtype->ref->lenstorage >= 0) ||
             elemtype->kind == TY_SLICE)) {
            auto rb = ReadBackRoot(elemtype, CanonRoot(iterprov.root), iterprov.rootexact);
            iterprov.root = rb.root;
            iterprov.rootexact = rb.exact;
            iterprov.rootfrom = rb.from;
            if (elemtype->cq) iterprov.writable = false;
        }
        BindProv(vd, iterprov);
    }
    x->vdef = vd;
    if (!x->idxvar.empty()) {
        auto idx = NewVar(x->idxvar, ast.inttypes[IS_I64], x->line, false);
        idx->assigned = true;
        x->idxdef = idx;
    }
    {
        BlockScope bs(*this, x->body);
        CheckStmts(x->body);
        if (x->body->tail) CheckStmtExpr(x->body->tail);
    }
    auto sc = scopes.back();
    PopScope();
    loopassigned.pop_back();
    RestoreFlow(entry);
    KillNarrowingsAssignedIn(x->body);
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
    auto &sc = scopes[si];
    if (b->val) {
        if (!Is<LoopExpr>(sc.node) && !Is<EarlyBlock>(sc.node))
            Error(b, "break with a value exits loop/block only");
        if (sc.valuelessbreak)
            Error(b, "this construct mixes valueless and valued breaks");
        // Later breaks agree with the first; the first constructs into the
        // type the construct is expected to have, as its tail value does.
        auto v = CheckValue(b->val, sc.breaktype ? sc.breaktype : sc.breakexpected);
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
        auto &sc2 = scopes[si];  // CheckValue may not reallocate, but be safe.
        sc2.breakvalue = MergeVals(sc2.breakvalue, sc2.breaktype != nullptr,
                                  exit, true, b, true, nullptr, b->val);
        if (!sc2.breaktype) sc2.breaktype = v.type;
        sc2.hasbreak = true;
    } else {
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
        auto d = ast.NewVarDef();
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
        if (v && (t->kind == TY_REF || t->kind == TY_SLICE)) {
            BindRefProvenance(d, *v);
            if (t->cq) d->ref.writable = false;
        } else if (v && HoldsPlainRef(t)) {
            NoteHolderBinding(d, *v);
        }
        if (vd->reusable) {
            if (!vd->isvar) Error(vd, "reusable requires var");
            if (!IsArrayKind(t, A_GROW) || ClassOf(t->arr->sub) != SC_FIXED)
                Error(vd, "reusable applies to grow-only arrays of fixed-size "
                          "elements (§5.4)");
        }
        NoteNonfixedLocal(t, vd->line, global);
        if (!global) {
            vd->defs.push_back(d);
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
            if (vd->byref && !ann) {
                // `let r .= e;` binds a reference to e: an lvalue by
                // reference, a reference or slice value as it is (§3.8).
                v = CheckV(vd->inits[i], nullptr);
                if (v.isnull) Error(vd->inits[i], "null needs an annotated optional type");
                if (v.lvalue && v.type->kind != TY_REF && v.type->kind != TY_SLICE) {
                    vd->inits[i] = AutoRef(vd->inits[i], v);
                } else if (v.type->kind != TY_REF && v.type->kind != TY_SLICE) {
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
        if (!ann) NoRelRefCopy(vd->inits[i], v.type);
        d->assigned = true;
        // A `let` has exactly one value, so its initializer's
        // non-negativity is the name's for good (§6.1). A `var` can be
        // assigned anything later.
        d->nonneg = !vd->isvar && v.nonneg;
        Finish(d, ann ? ann : v.type, &v);
    }
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

inline void TypeCheck::CheckAssign(Assign *a) {
    auto lv = CheckLValue(a->lval);
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
        auto t2 = av.type;
        TypeExpr *selem = nullptr;
        if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
        else if (t2->kind == TY_SLICE) selem = t2->sub;
        if (av.strlit) selem = ast.inttypes[IS_U8];
        if (!selem || av.emptyarr)
            Error(a, "cannot infer the element type of this array from this value");
        CompletePending(target, selem, a->line);
    }
    // Assigning a resizable array whole replaces its elements: a shrink
    // to anything referring into it (§5.1, §5.2).
    if (target->kind == TY_ARRAY && (!lv.var || lv.var->assigned)) {
        auto root = lv.var ? lv.var : CanonRoot(lv.root);
        if (target->arr->akind == A_GROW) {
            if (!root || !root->type)
                Error(a, "cannot assign a grow-only array through a reference: a shrink of "
                         "a grow-only array applies to a local of the function that owns "
                         "it (§5.1)");
            GrowOnlyShrinkAt(a, true, "assign", root);
        } else if (target->arr->akind == A_GROWSHRINK) {
            ShrinkGrowShrink(a, cat("assign ", ExprStr(a->lval)), root, ExprStr(a->lval));
        }
    }
    SlotScope ss(*this, true);
    auto v = CheckValueAt(a->rhs, target,
                          Dest { lv.root, lv.rootexact,
                                 lv.var && (target->kind == TY_REF || target->kind == TY_SLICE) });
    if (v.type->kind == TY_VOID && reachable)
        Error(a, "the right-hand side has no value");
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
            if (!v.isnull && wasplain) {
                auto r = ast.NewType(TY_REF, a->line);
                r->ref = ast.NewDetail<TypeRef>();
                r->ref->sub = target->ref->sub;
                r->cq = target->cq;
                lv.var->narrowed = r;
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
    if (pt->kind == TY_ARRAY && pt->arr->akind == A_GROW)
        Error(a, "cannot assign a grow-only array through a reference: a shrink of a "
                 "grow-only array applies to a local of the function that owns it (§5.1)");
    if (pt->kind == TY_ARRAY && pt->arr->akind == A_GROWSHRINK)
        ShrinkGrowShrink(a, cat("assign ", ExprStr(a->lval)),
                         CanonRoot(lv.var ? RefRootOf(lv.var) : lv.root), ExprStr(a->lval));
    SlotScope ss(*this, true);
    auto v = CheckValueAt(a->rhs, pt, lv.var ? Dest { RefRootOf(lv.var), RefExactOf(lv.var) }
                                             : Dest { lv.root, lv.rootexact });
    if (v.type->kind == TY_VOID && reachable)
        Error(a, "the right-hand side has no value");
}

// A reference variable keeps one root for its whole life (see header
// note): re-assignments must carry the same root, or one at the same
// scope depth (which is equivalent for the outlives check).
inline void TypeCheck::CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv) {
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

// ------------------------------------------------------------------
// Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

// One entry for every builtin (builtins.h), for both spellings — f(a, b)
// and a.f(b) arrive with a uniform argument list (receiver first). The
// table drives arity, receiver kind/provenance, and simple signatures;
// BF_CUSTOM entries get dedicated code below.
inline Val TypeCheck::CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv) {
    c->builtin = d.kind;
    if (c->trailing) Error(c, cat(d.name, " takes no function value"));
    if (!(d.flags & BF_TYARGS) && !c->tyargs.empty())
        Error(c, cat(d.name, " takes no type arguments"));
    if ((int)args.size() < d.minargs || (int)args.size() > d.maxargs)
        Error(c, cat(d.name, " takes ", (int64_t)d.minargs,
                     d.minargs == d.maxargs ? string() : cat("-", (int64_t)d.maxargs),
                     " argument(s), ", (int64_t)args.size(), " given"));
    // The fully custom builtins first.
    switch (d.kind) {
        case B_PRINT:
            for (auto a : args) CheckPrintable(c, d.name, a);
            return VoidVal();
        case B_STR: {
            // str(a, b, ...): a fresh u8[>..] holding the arguments' text,
            // built at the destination like any resizable result (§7.3).
            for (auto a : args) CheckPrintable(c, d.name, a);
            auto t = ast.NewType(TY_ARRAY, c->line);
            t->arr = ast.NewDetail<TypeArray>();
            t->arr->sub = ast.inttypes[IS_U8];
            t->arr->akind = A_GROW;
            c->rettypes.push_back(t);
            Val v;
            v.type = t;
            v.root = temproot;
            return v;
        }
        case B_ASSERT:
            CheckCond(args[0]);
            NarrowCond(args[0], true);  // assert(r) narrows onwards (§3.8).
            return VoidVal();
        case B_ABORT: case B_EXIT:
            // Both end the program (§9.3), so the code after them is
            // unreachable: this is what lets a `guard ... else` diverge
            // with an abort (§6.4).
            if (d.kind == B_ABORT) CheckArg(args[0], u8slice);
            else CheckIntAny(args[0]);
            reachable = false;
            return VoidVal();
        case B_THREAD_SPAWN: {
            auto wid = Is<Ident>(args[0]);
            SFunction *wsf = nullptr;
            if (wid)
                for (auto sf : ast.LookupFunctions(wid->name, wid->ns))
                    if (sf->isthread) wsf = sf;
            if (!wsf) Error(c, "thread_spawn's first argument names a thread_fn");
            wid->fnref = wsf;
            wid->exprtype = fntype;
            auto spec = EnsureThreadSpec(wsf, c->line);
            if (args.size() != 1 + spec->argtypes.size())
                Error(c, cat("thread_spawn(", wsf->name, ", ...) takes ",
                             (int64_t)spec->argtypes.size(), " worker argument(s)"));
            {
                DestScope ds(*this, Dest {});
                for (size_t i = 0; i < spec->argtypes.size(); i++)
                    CheckArg(args[1 + i], spec->argtypes[i]);
            }
            c->spec = spec;
            Val v;
            v.type = ast.inttypes[IS_I64];
            return v;
        }
        case B_QPUT: {
            auto av = CheckValue(args[0], nullptr);
            if (av.type->kind == TY_VOID || av.type->kind == TY_FN || !IsFlat(av.type))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(av.type)));
            return VoidVal();
        }
        case B_QGET: case B_QPOLL: {
            if (c->tyargs.size() != 1)
                Error(c, cat(d.name, "<T>() needs exactly one explicit type argument"));
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            if (!IsFlat(t))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(t)));
            c->rettypes.push_back(t);
            Val first;
            first.type = t;
            first.root = temproot;
            lastcallrets.clear();
            lastcallrets.push_back(first);
            if (d.kind == B_QPOLL) {
                Val b2;
                b2.type = ast.booltype;
                c->rettypes.push_back(ast.booltype);
                lastcallrets.push_back(b2);
            }
            return first;
        }
        case B_COPY: {
            // copy(x): a fresh value from stored one (§4.1), for the
            // destinations that never copy implicitly.
            auto av = CheckV(args[0], nullptr);
            args[0]->exprtype = av.type;
            if (av.type->kind == TY_SLICE)
                Error(c, "copy takes a value or a reference, not a slice");
            if (!av.lvalue && !IsPlainRef(av.type))
                Error(c, "copy of a temporary: the value is fresh already");
            auto v = DecayRef(av);
            v.lvalue = false;
            c->rettypes.push_back(v.type);
            return v;
        }
        case B_FROM_BYTES: {
            // from_bytes<T[>..]>(bytes): the image verified and copied into
            // a fresh array (docs/design/serialization.md §4). The result is
            // rooted at its own variable like any resizable one, so nothing
            // in §9 has to know it came from outside; the bool is the
            // verifier's verdict, and a rejected image leaves the array empty.
            if (c->tyargs.size() != 1)
                Error(c, "from_bytes<T[>..]>(bytes) needs exactly one explicit type argument");
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            // The kinds whose contents are exactly an element run plus a
            // count: a resizable's count lives in its header, a variable
            // array's in a length prefix the construction writes. A fixed or
            // limited array would need the count to match a capacity the
            // image does not carry, so those are rejected.
            auto ak = t->kind == TY_ARRAY ? t->arr->akind : A_FIXED;
            if (t->kind != TY_ARRAY || (ak != A_GROW && ak != A_GROWSHRINK && ak != A_VAR))
                Error(c, cat("from_bytes builds a resizable or variable array "
                             "(T[>..], T[>..<], T[]), not ", TypeStr(t)));
            auto el = t->arr->sub;
            if (el->kind == TY_VOID) Error(c, "from_bytes needs a known element type");
            string why;
            if (!VerifiableElem(el, el, why))
                Error(c, cat("from_bytes<", TypeStr(t), "> has no verifier: ", why));
            CheckArg(args[0], u8slice);
            c->rettypes.push_back(t);
            c->rettypes.push_back(ast.booltype);
            Val first;
            first.type = t;
            first.root = temproot;
            Val ok;
            ok.type = ast.booltype;
            lastcallrets.clear();
            lastcallrets.push_back(first);
            lastcallrets.push_back(ok);
            return first;
        }
        case B_DEFAULT: {
            // default<T>(): the value a T has before anything is written
            // to it, declared field defaults applied (§4.2).
            if (c->tyargs.size() != 1)
                Error(c, "default<T>() needs exactly one explicit type argument");
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            if (ClassOf(t) != SC_FIXED)
                Error(c, cat("default<T>() needs a fixed-size type, not ", TypeStr(t)));
            string why;
            if (!HasDefault(t, why))
                Error(c, cat("default<", TypeStr(t), ">() does not exist: ", why));
            c->rettypes.push_back(t);
            Val v;
            v.type = t;
            v.rootexact = true;   // A null optional or an empty slice: static.
            v.writable = true;    // And nothing to write, so it fits any slot (§9.5).
            return v;
        }
        default: break;
    }
    // Member receiver validation, from the table.
    Val rv;
    TypeExpr *elem = nullptr;
    auto ak = A_FIXED;
    if (d.flags & BF_MEMBER) {
        if (precv) {
            rv = *precv;
        } else {
            rv = CheckV(args[0], nullptr);
            args[0]->exprtype = rv.type;
        }
        auto rt = rv.type;
        if (IsPlainRef(rt)) rt = rt->ref->sub;
        auto got = 0;
        if (rt->kind == TY_ARRAY) {
            ak = rt->arr->akind;
            elem = rt->arr->sub;
            switch (ak) {
                case A_FIXED:      got = BR_FIXED; break;
                case A_VAR:        got = BR_VAR; break;
                case A_LIMITED:    got = BR_LIMITED; break;
                case A_GROW:       got = BR_GROW; break;
                case A_GROWSHRINK: got = BR_GROWSHRINK; break;
            }
        } else if (rt->kind == TY_SLICE) {
            got = BR_SLICE;
            elem = rt->sub;
        }
        if (!(got & d.recv))
            Error(c, cat(".", d.name, " is not available on ", TypeStr(rv.type)));
        if ((d.flags & BF_WRITE) && !rv.writable)
            Error(c, cat("cannot .", d.name, " through a non-writable value "
                         "(let, const, or a read-only instantiation, §9.5)"));
        if ((d.flags & BF_REUSABLE) && !rv.reusable)
            Error(c, cat(".", d.name, " exists on reusable pools only (§5.4)"));
    }
    // A pending `var x = []` receiver learns its element type from what
    // is first pushed or appended into it (§4.2).
    if (elem && elem->kind == TY_VOID) {
        auto rt = rv.type;
        if (IsPlainRef(rt)) rt = rt->ref->sub;
        if (d.kind == B_PUSH || d.kind == B_ALLOC_INDEX || d.kind == B_ALLOC_REF) {
            auto av = DecayRef(CheckV(args[1], nullptr));
            CompletePending(rt, PendingElemFrom(av, args[1]), c->line);
        } else if (d.kind == B_APPEND || d.kind == B_FORMAT) {
            TypeExpr *selem = nullptr;
            if (d.kind == B_FORMAT) {
                selem = ast.inttypes[IS_U8];
            } else {
                auto av = DecayRef(CheckV(args[1], nullptr));
                auto t2 = av.type;
                if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
                else if (t2->kind == TY_SLICE) selem = t2->sub;
                if (av.strlit) selem = ast.inttypes[IS_U8];
                if (!selem || av.emptyarr)
                    Error(c, "cannot infer the element type of this array from this value");
            }
            CompletePending(rt, selem, c->line);
        } else {
            RequireComplete(rt, c->line);
        }
        elem = rt->arr->sub;
    }
    // The serialization pair (docs/design/serialization.md §4). to_bytes
    // builds the image -- a varint byte count then the element region --
    // either as a fresh u8[>..] or appended to a builder the caller owns, so
    // its own header can go in front. bytes_of is the element region alone,
    // as a view: no copy, and no framing of its own.
    if (d.kind == B_TO_BYTES || d.kind == B_BYTES_OF) {
        string why;
        if (!ImageSafe(elem, why))
            Error(c, cat(d.name, " cannot write ", TypeStr(rv.type), " out: ", why));
        if (d.kind == B_BYTES_OF) {
            Val v;
            v.type = cu8slice;   // A read-only view, in its type too (§9.5).
            v.SetProv(rv);
            // The bytes of a live structure: reading them is what they are
            // for, and writing them would forge the relative references the
            // checker otherwise proves (§3.9), so the view is never writable.
            v.writable = false;
            v.reusable = false;
            v.byteview = true;
            c->rettypes.push_back(v.type);
            return v;
        }
        if (args.size() == 2) {
            auto ov = CheckV(args[1], nullptr);
            args[1]->exprtype = ov.type;
            auto ot = ov.type;
            if (IsPlainRef(ot)) ot = ot->ref->sub;
            auto ok = ot->kind == TY_ARRAY && IsU8(ot->arr->sub) &&
                      (ot->arr->akind == A_GROW || ot->arr->akind == A_GROWSHRINK ||
                       ot->arr->akind == A_LIMITED);
            if (!ok)
                Error(c, cat("to_bytes(a, out) appends to a growable u8 array, not ",
                             TypeStr(ov.type)));
            if (!ov.writable)
                Error(c, "cannot append through a non-writable value (let, or "
                         "non-writable provenance, §9.5)");
            return VoidVal();
        }
        auto t = ast.NewType(TY_ARRAY, c->line);
        t->arr = ast.NewDetail<TypeArray>();
        t->arr->sub = ast.inttypes[IS_U8];
        t->arr->akind = A_GROW;
        c->rettypes.push_back(t);
        Val v;
        v.type = t;
        v.root = temproot;
        return v;
    }
    // format(out, a, b, ...): the arguments' text appended to a growable
    // u8 array (§3.7).
    if (d.kind == B_FORMAT) {
        if (!IsU8(elem))
            Error(c, cat(".format appends text to u8 arrays, not ", TypeStr(rv.type)));
        for (size_t i = 1; i < args.size(); i++) CheckPrintable(c, d.name, args[i]);
        return VoidVal();
    }
    // A grow-only array shrinks only where nothing can still be rooted in
    // it (§5.1); pop and resize also need an element the shrink can find,
    // which a sequential array has not got.
    if (ak == A_GROW && (d.kind == B_POP || d.kind == B_RESIZE || d.kind == B_CLEAR)) {
        CheckGrowShrink(c, c->standalone, d.name, args[0], rv.type);
        if (d.kind != B_CLEAR && ClassOf(elem) != SC_FIXED)
            Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
    }
    // A grow-shrink array shrinks from anywhere, provided nothing in scope
    // refers into it (§5.2).
    if (ak == A_GROWSHRINK && (d.kind == B_POP || d.kind == B_RESIZE || d.kind == B_CLEAR))
        ShrinkGrowShrink(c, cat(d.name, " ", ExprStr(args[0])), CanonRoot(rv.root),
                         ExprStr(args[0]));
    // resize has two forms (§3.3); a target below zero is caught at runtime.
    if (d.kind == B_RESIZE) {
        CheckIntAny(args[1]);
        if (args.size() == 3) ElemArg(args[2], elem, rv);
        return VoidVal();
    }
    // index_of recovers the element index a reference stands for (§3.3).
    // The reference must be an element of this very array, which is what
    // an exact root at the receiver says (§9.2); the distance is then a
    // whole number of elements and inside the length, so the division is
    // exact and nothing has to be bounds-checked.
    if (d.kind == B_INDEX_OF) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, cat(".index_of needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
        auto av = CheckV(args[1], nullptr);
        if (av.lvalue && av.type->kind != TY_REF && TypeEq(av.type, elem))
            args[1] = AutoRef(args[1], av);
        else if (UserRefOf(args[1]))
            Warn(args[1], cat("redundant &: ", ExprStr(Is<Unary>(args[1])->child),
                              " is passed by reference without it (§4.1)"));
        args[1]->exprtype = av.type;
        if (!IsPlainRef(av.type) || !TypeEq(av.type->ref->sub, elem))
            Error(c, cat(".index_of takes a reference to an element of ", TypeStr(rv.type),
                         ", got ", TypeStr(av.type)));
        // A reference parameter in a pool class points into that global
        // pool (§3.9), so it is rooted there as exactly as a local one.
        auto recvroot = CanonRoot(rv.root);
        auto sameroot = CanonRoot(av.root) == recvroot ||
                        (recvroot && recvroot->isglobal && PoolOf(av.root) == recvroot);
        if (!av.rootexact || !sameroot) {
            auto why = av.rootexact ? string() : ReadBackWhy(av.type, av.rootfrom);
            Error(c, cat(".index_of needs a reference rooted at the array itself (§3.3); ",
                         !why.empty() ? why
                         : cat("this one is rooted at ",
                               av.root ? CanonRoot(av.root)->name
                                       : string_view("static data"))));
        }
    }
    // Signature-driven arguments.
    auto base = (d.flags & BF_MEMBER) ? 1 : 0;
    for (auto i = 0; d.args[i]; i++) {
        auto &an = args[base + i];
        switch (d.args[i]) {
            case 'i': CheckIntAny(an); break;
            case 'f': CheckValue(an, ast.flttypes[FS_F64]); break;
            case 'b': CheckValue(an, ast.booltype); break;
            case 'e': ElemArg(an, elem, rv); break;
            case 'a': {  // An array/slice of the receiver's element type.
                auto av = CheckV(an, nullptr);
                an->exprtype = av.type;
                auto t2 = av.type;
                if (IsPlainRef(t2)) t2 = t2->ref->sub;
                TypeExpr *selem = nullptr;
                if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
                if (t2->kind == TY_SLICE) selem = t2->sub;
                if (av.strlit) selem = ast.inttypes[IS_U8];
                if (!selem || !TypeEq(selem, elem))
                    Error(c, cat(".", d.name, " takes an array or slice of ",
                                 TypeStr(elem), ", got ", TypeStr(av.type)));
                break;
            }
            default: assert(false);
        }
    }
    // Returns, from the table.
    Val v = VoidVal();
    switch (d.rets[0]) {
        case 0: break;
        case 'i': v.type = ast.inttypes[IS_I64]; break;
        case 'b': v.type = ast.booltype; break;
        case 'e':
            v.type = LoadType(elem);
            v.root = temproot;
            break;
        case 'r':
            v.type = RefTo(elem, c->line);
            v.root = rv.root;
            v.rootexact = rv.rootexact;
            v.writable = rv.writable;
            break;
        default: assert(false);
    }
    return v;
}

}  // namespace goose
