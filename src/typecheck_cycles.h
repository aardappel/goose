// Goose compiler — cycle return roots (§7.8). A back edge lands in a
// specialization whose own returns have not been checked yet, so the roots its
// result should carry are recorded nowhere. Before checking a `recursive fn`
// body the checker therefore *predicts* them, from a purely syntactic scan of
// the returns of the function and of the functions those returns call,
// iterated to a fixpoint (a cycle's functions define each other's roots, so
// one pass does not settle it). The prediction is a set, one root per way a
// return can go, which seeds FnSpec::retroots; each back edge maps it through
// its own arguments and merges it as the branches of an `if` are merged, and
// every real return is checked against it. What the scan cannot pin down
// becomes the `cycleroot` sentinel: like a temporary it outlives nothing, so
// such a result may only be passed down, never stored or returned. A result
// holding references (a holder, §9.2) is seeded too, by the roots of what it
// holds; the scan reads no holder, so its first checked return stands in.
//
// The scan is purely syntactic, so it needs no checker state beyond the four
// things it cannot derive itself: the sentinel, what root the checker has
// recorded for one of its variables (RootOfVar), which variable a free
// variable's name denotes from the specialization being seeded (FreeVar), and
// whether a checked result type holds references (HoldsRefs). Its
// per-function results are cached in the typechecker, so syntax-only scratch
// state is discarded before optimization and code generation.
#pragma once

namespace goose {

// A returned reference's root as the syntactic cycle scan (§7.8,
// typecheck_cycles.h) can name it before any body is checked: one of the
// function's own reference parameters' pointees, a global, a local of an
// enclosing function (a free variable of a nested function, §7.5), static
// data (a string literal), or the function's own local storage (meaningful
// to the function itself, never to a caller).
enum RootDescKind { RD_PARAM, RD_GLOBAL, RD_FREE, RD_STATIC, RD_LOCAL };
struct RootDesc {
    RootDescKind kind = RD_PARAM;
    int param = 0;              // RD_PARAM: index into SFunction::params.
    VarDef *glob = nullptr;     // RD_GLOBAL.
    string_view name;           // RD_FREE / RD_LOCAL: the variable's name.
    bool operator==(const RootDesc &o) const {
        return kind == o.kind && param == o.param && glob == o.glob && name == o.name;
    }
};

// Every root a reference may have, as the scan sees it: the lattice the
// fixpoint climbs. No alternatives is "no return contributes yet", the
// optimistic bottom; `unknown`, something the scan cannot follow, its top.
struct RootSet {
    bool unknown = false;
    vector<RootDesc> alts;      // In the order the scan met them.
    static RootSet Unknown() {
        RootSet s;
        s.unknown = true;
        return s;
    }
    static RootSet Of(const RootDesc &d) {
        RootSet s;
        s.alts.push_back(d);
        return s;
    }
    void Join(const RootSet &o) {
        if (unknown) return;
        if (o.unknown) {
            unknown = true;
            alts.clear();
            return;
        }
        for (auto &d : o.alts)
            if (find(alts.begin(), alts.end(), d) == alts.end()) alts.push_back(d);
    }
    bool operator==(const RootSet &o) const {
        if (unknown != o.unknown || alts.size() != o.alts.size()) return false;
        for (auto &d : o.alts)
            if (find(alts.begin(), alts.end(), d) == alts.end()) return false;
        return true;
    }
    bool operator!=(const RootSet &o) const { return !(*this == o); }
};

// One name a function body binds, for the same scan: what a `return v` of a
// reference variable resolves to. A name bound twice, bound by a construct
// whose value the scan does not model (loop variables, match payloads,
// function-value parameters), or shadowing a parameter or global, is opaque.
struct LocalBind {
    string_view name;
    TypeExpr *type = nullptr;   // Declared type; only ref/slice ones carry a root.
    vector<Node *> binds;       // Initializers and `.=`/`=` right-hand sides.
    bool declared = false;
    bool byref = false;         // Declared with `.=`: a reference whatever its type.
    bool opaque = false;
};

struct CycleRoots {
    // The checker's root for one of its own VarDefs: the pointee's root when
    // the variable holds a reference or slice, its own storage otherwise.
    using RootOfVar = function<VarDef *(VarDef *vd, bool isref)>;
    // The variable a name resolves to through the lexical parent chain.
    using FreeVar = function<VarDef *(string_view name)>;
    // Whether a value of a checked type holds plain references or slices.
    using HoldsRefs = function<bool(TypeExpr *t)>;

    struct Bindings {
        vector<LocalBind> locals;
        vector<SFunction *> localfns;
    };
    struct Returns {
        vector<RootSet> values;
        bool settled = false;
    };
    // Presence records the first scan/enrollment, without separate flags on
    // SFunction. The checker owns these caches; each scan owns its worklist.
    struct Cache {
        unordered_map<SFunction *, Bindings> bindings;
        unordered_map<SFunction *, Returns> returns;
    };

    Ast &ast;
    Cache &cache;
    VarDef *cycleroot;
    RootOfVar rootof;
    FreeVar freevar;
    HoldsRefs holds;

    CycleRoots(Ast &_ast, Cache &_cache, VarDef *_cycleroot, RootOfVar _rootof, FreeVar _freevar,
               HoldsRefs _holds)
        : ast(_ast), cache(_cache), cycleroot(_cycleroot), rootof(_rootof), freevar(_freevar),
          holds(_holds) {}

    static RootSet Named(RootDescKind kind, string_view name) {
        RootDesc d;
        d.kind = kind;
        d.name = name;
        return RootSet::Of(d);
    }

    LocalBind *FindBind(SFunction *f, string_view name) {
        auto it = cache.bindings.find(f);
        if (it == cache.bindings.end()) return nullptr;
        for (auto &b : it->second.locals) if (b.name == name) return &b;
        return nullptr;
    }

    LocalBind &BindSlot(SFunction *f, string_view name) {
        if (auto b = FindBind(f, name)) return *b;
        auto &locals = cache.bindings.at(f).locals;
        locals.push_back(LocalBind { name });
        return locals.back();
    }

    void EnsureBinds(SFunction *f) {
        if (!cache.bindings.try_emplace(f).second) return;
        if (f->body) CollectBinds(f, f->body);
        for (auto &p : f->params)
            if (auto b = FindBind(f, p.name)) b->opaque = true;
        for (auto &b : cache.bindings.at(f).locals)
            if (!b.declared || ast.LookupGlobal(b.name, f->ns)) b.opaque = true;
    }

    void CollectBinds(SFunction *f, Node *n) {
        if (auto vd = Is<VarDecl>(n)) {
            for (size_t i = 0; i < vd->names.size(); i++) {
                auto &b = BindSlot(f, vd->names[i]);
                if (b.declared) b.opaque = true;   // Two declarations of one name.
                b.declared = true;
                b.type = vd->type;
                b.byref = vd->byref;
                if (vd->inits.size() == vd->names.size()) b.binds.push_back(vd->inits[i]);
                else b.opaque = true;              // Multi-value or absent initializer.
            }
        } else if (auto a = Is<Assign>(n)) {
            if (a->op == T_ASSIGN || a->op == T_DOTASSIGN)
                if (auto id = Is<Ident>(a->lval)) BindSlot(f, id->name).binds.push_back(a->rhs);
        } else if (auto fl = Is<ForLoop>(n)) {
            BindSlot(f, fl->var).opaque = true;
            if (!fl->idxvar.empty()) BindSlot(f, fl->idxvar).opaque = true;
        } else if (auto m = Is<MatchExpr>(n)) {
            for (auto &arm : m->arms)
                if (!arm.pat.binder.empty()) BindSlot(f, arm.pat.binder).opaque = true;
        } else if (auto fv = Is<FunVal>(n)) {
            if (fv->explicit_params) for (auto &p : fv->params) BindSlot(f, p.name).opaque = true;
            else BindSlot(f, "it").opaque = true;
        } else if (auto fd = Is<FnDecl>(n)) {
            cache.bindings.at(f).localfns.push_back(fd->sf);
        }
        n->Children([&](Node *c) { CollectBinds(f, c); });
    }

    // Mirrors Ident::Check: a global holding a reference or slice contributes
    // its pointee's root, any other global its own storage.
    VarDef *RootOfGlobal(VarDef *vd) {
        auto t = vd->type;
        return rootof(vd, t && IsRefOrSlice(t));
    }

    // Only single-name globals: LookupVar resolves a multi-name declaration's
    // uses to its first VarDef, which the scan will not second-guess.
    RootSet GlobalDesc(string_view name, string_view ns) {
        auto g = ast.LookupGlobal(name, ns);
        if (!g || g->names.size() != 1 || g->defs.size() != 1) return RootSet::Unknown();
        RootDesc d;
        d.kind = RD_GLOBAL;
        d.glob = g->defs[0];
        return RootSet::Of(d);
    }

    // The roots the reference `n` evaluates to may have. `busy` breaks the
    // recursion through locals defined in terms of each other.
    RootSet ScanExpr(SFunction *f, Node *n, vector<string_view> &busy, int depth) {
        if (!n || depth > 16) return RootSet::Unknown();
        if (Is<Dot>(n) || Is<Index>(n) || Is<SliceExpr>(n)) return ScanBase(f, n, busy, depth);
        if (auto u = Is<Unary>(n))
            return u->op == T_BITAND ? ScanBase(f, u->child, busy, depth) : RootSet::Unknown();
        if (auto c = Is<Call>(n)) return ScanCall(f, c, busy, depth);
        if (Is<NullLit>(n)) return RootSet {};   // Null names no root.
        if (Is<StrLit>(n)) {
            RootDesc d;
            d.kind = RD_STATIC;
            return RootSet::Of(d);
        }
        auto id = Is<Ident>(n);
        if (!id) return RootSet::Unknown();
        EnsureBinds(f);
        if (auto b = FindBind(f, id->name)) {
            if (b->opaque) return RootSet::Unknown();
            // A reference variable is rooted where its bindings point; a
            // value variable is storage this function owns. A variable
            // declared without a type is a reference only where the
            // declaration says so (`.=`, an `&` initializer), and storage
            // only where its initializer plainly builds a value.
            auto isref = b->byref || (b->type && IsRefOrSlice(b->type));
            if (!isref && !b->type)
                for (auto e : b->binds)
                    if (auto u = Is<Unary>(e); u && u->op == T_BITAND) isref = true;
            if (!isref) {
                auto value = b->type != nullptr;
                if (!value && !b->binds.empty()) {
                    value = true;
                    for (auto e : b->binds)
                        value &= Is<IntLit>(e) || Is<FltLit>(e) || Is<StrLit>(e) ||
                                 Is<BoolLit>(e) || Is<StructLit>(e) || Is<ArrayLit>(e);
                }
                if (!value) return RootSet::Unknown();
                return Named(RD_LOCAL, id->name);
            }
            for (auto nm : busy) if (nm == id->name) return RootSet::Unknown();
            busy.push_back(id->name);
            RootSet s;
            for (auto e : b->binds) s.Join(ScanExpr(f, e, busy, depth + 1));
            busy.pop_back();
            return s;
        }
        for (size_t i = 0; i < f->params.size(); i++) {
            if (f->params[i].name != id->name) continue;
            auto pt = f->params[i].type;
            if (!pt || !IsRefOrSlice(pt)) return RootSet::Unknown();
            RootDesc d;
            d.kind = RD_PARAM;
            d.param = (int)i;
            return RootSet::Of(d);
        }
        // A free variable: a local or parameter of an enclosing function,
        // which outlives every activation of this one.
        for (auto o = f->outer; o; o = o->outer) {
            EnsureBinds(o);
            auto b = FindBind(o, id->name);
            auto isparam = false;
            for (auto &p : o->params) isparam |= p.name == id->name;
            if ((b && b->declared) || isparam) return Named(RD_FREE, id->name);
        }
        return GlobalDesc(id->name, id->ns);
    }

    // Whether name is one of f's own locals or parameters.
    bool OwnName(SFunction *f, string_view name) {
        EnsureBinds(f);
        if (auto b = FindBind(f, name); b && b->declared) return true;
        for (auto &p : f->params) if (p.name == name) return true;
        return false;
    }

    // The declared type of the variable `name` denotes in f: a local's
    // annotation, a parameter's type, an enclosing function's, or a
    // global's. Null where there is none to read.
    TypeExpr *DeclaredType(SFunction *f, string_view name) {
        for (auto o = f; o; o = o->outer) {
            EnsureBinds(o);
            if (auto b = FindBind(o, name); b && b->declared) return b->opaque ? nullptr : b->type;
            for (auto &p : o->params) if (p.name == name) return p.type;
        }
        auto g = ast.LookupGlobal(name, f->ns);
        return g && g->defs.size() == 1 ? g->defs[0]->type : nullptr;
    }

    // A path's root is its base's as long as the path stays inside the
    // base's own storage: elements, fields, and relative references, which
    // point within the same root (§3.9). A plain reference or slice read
    // out of the base points elsewhere (§9.5), and so does a step the
    // declared types cannot follow.
    RootSet ScanBase(SFunction *f, Node *n, vector<string_view> &busy, int depth) {
        vector<Node *> steps;   // Innermost step first.
        for (;;) {
            if (auto d = Is<Dot>(n)) { steps.push_back(n); n = d->obj; continue; }
            if (auto ix = Is<Index>(n)) { steps.push_back(n); n = ix->obj; continue; }
            if (auto se = Is<SliceExpr>(n)) { steps.push_back(n); n = se->obj; continue; }
            break;
        }
        auto d = ScanExpr(f, n, busy, depth + 1);
        if (steps.empty() || d.unknown || d.alts.empty()) return d;
        auto id = Is<Ident>(n);
        auto t = id ? DeclaredType(f, id->name) : nullptr;
        if (!t) return RootSet::Unknown();
        if (t->kind == TY_REF) t = t->ref->sub;   // A reference variable: its pointee.
        for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
            if (auto dot = Is<Dot>(*it)) {
                if (t->kind != TY_STRUCT) return RootSet::Unknown();
                TypeExpr *ft = nullptr;
                for (auto &fl : t->struc->st->fields)
                    if (!fl.ispad && fl.name == dot->name) ft = fl.type;
                if (!ft) return RootSet::Unknown();
                t = ft;
            } else if (Is<SliceExpr>(*it)) {
                // A subrange of an array or slice views the same storage.
                if (t->kind != TY_ARRAY && t->kind != TY_SLICE) return RootSet::Unknown();
                continue;
            } else {
                if (t->kind == TY_ARRAY) t = t->arr->sub;
                else if (t->kind == TY_SLICE) t = t->sub;
                else return RootSet::Unknown();
            }
            if (!t) return RootSet::Unknown();
            if (t->kind == TY_REF) {
                if (t->ref->lenstorage < 0) return RootSet::Unknown();
                t = t->ref->sub;
            }
            if (t->kind == TY_SLICE || t->kind == TY_GENERIC || t->kind == TY_UNRESOLVED)
                return RootSet::Unknown();
        }
        return d;
    }

    RootSet ScanCall(SFunction *f, Call *c, vector<string_view> &busy, int depth) {
        // The argument list resolution builds: a UFCS receiver is argument 0.
        string_view name, ns;
        vector<Node *> args;
        if (auto d = Is<Dot>(c->callee)) {
            name = d->name;
            ns = d->ns;
            args.push_back(d->obj);
        } else if (auto id = Is<Ident>(c->callee)) {
            name = id->name;
            ns = id->ns;
        } else {
            return RootSet::Unknown();
        }
        for (auto a : c->args) args.push_back(a);
        // A nested function declared in this function or an enclosing one
        // shadows builtins and top-level functions.
        SFunction *callee = nullptr;
        for (auto o = f; o && !callee; o = o->outer) {
            EnsureBinds(o);
            for (auto lf : cache.bindings.at(o).localfns) if (lf->name == name) { callee = lf; break; }
        }
        if (!callee) {
            auto &cands = ast.LookupFunctions(name, ns);
            if (auto bd = LookupBuiltin(GlobalLeaf(name))) {
                // A member builtin wins over a same-named function at a.f()
                // sites when the receiver is an array, which the scan cannot
                // tell.
                if (!cands.empty()) return RootSet::Unknown();
                auto atrecv = bd->kind == B_PUSH || bd->kind == B_ALLOC_REF ||
                              bd->kind == B_ALLOC_SLICE || bd->kind == B_REALLOC_SLICE;
                if (!atrecv || args.empty()) return RootSet::Unknown();
                return ScanBase(f, args[0], busy, depth);
            }
            if (cands.size() != 1) return RootSet::Unknown();
            callee = cands[0];
        }
        // A scan of bindings rather than of returns runs outside the
        // fixpoint, and needs the callee's returns settled.
        if (descrunning) EnrollDescs(callee);
        else ReturnRootDescs(callee);
        auto &ds = cache.returns.at(callee).values;
        if (ds.empty()) return RootSet::Unknown();
        auto cs = ds[0];
        if (cs.unknown) return cs;
        RootSet s;
        for (auto &cd : cs.alts) {
            switch (cd.kind) {
                case RD_GLOBAL: case RD_STATIC:
                    s.Join(RootSet::Of(cd));
                    break;
                // A callee's free variable is this function's too, unless it
                // is this function's own local, which is then its own storage.
                case RD_FREE:
                    s.Join(OwnName(f, cd.name) ? Named(RD_LOCAL, cd.name) : RootSet::Of(cd));
                    break;
                // The callee's own locals mean nothing here.
                case RD_LOCAL:
                    return RootSet::Unknown();
                // Rooted at one of the callee's own parameters: follow the
                // argument this call site passes there.
                case RD_PARAM:
                    if (cd.param >= (int)args.size()) return RootSet::Unknown();
                    s.Join(ScanExpr(f, args[cd.param], busy, depth + 1));
                    break;
            }
        }
        return s;
    }

    vector<SFunction *> descqueue;   // Functions in the running fixpoint.
    bool descchanged = false;
    bool descrunning = false;

    // Bring a function into that fixpoint on first sight, so its own returns
    // get scanned too.
    void EnrollDescs(SFunction *f) {
        auto [it, inserted] = cache.returns.try_emplace(f);
        if (!inserted) return;
        it->second.values.resize(f->has_rets ? f->rets.size() : 0);
        descqueue.push_back(f);
        descchanged = true;
    }

    void ScanReturns(SFunction *f, Node *n, vector<RootSet> &ds, bool &usable) {
        if (auto r = Is<Return>(n)) {
            // `return … from g` exits another frame entirely; only a `from`
            // naming this function targets it (its innermost activation).
            // The return is in f's own body, so an unqualified name
            // resolves in f's namespace first: a leaf match there is f.
            auto ref = SplitName(r->from, r->ns);
            if (r->from.empty() || (ref.leaf == f->name && ref.ns == f->ns)) {
                if (r->vals.size() != ds.size()) {
                    usable = false;
                } else {
                    vector<string_view> busy;
                    for (size_t i = 0; i < ds.size(); i++)
                        ds[i].Join(ScanExpr(f, r->vals[i], busy, 0));
                }
            }
        }
        n->Children([&](Node *c) { ScanReturns(f, c, ds, usable); });
    }

    vector<RootSet> ComputeDescs(SFunction *f) {
        vector<RootSet> ds(f->has_rets ? f->rets.size() : 0);
        if (ds.empty()) return ds;
        if (f->isthread || !f->body) {
            for (auto &d : ds) d = RootSet::Unknown();
            return ds;
        }
        auto usable = true;
        ScanReturns(f, f->body, ds, usable);
        // A value-producing body tail is exactly `return tail` (§7.3).
        if (auto tail = f->body->tail) {
            auto fi = Is<IfExpr>(tail);
            auto asvalue = !(fi && !fi->elseb) && !Is<Guard>(tail) && !Is<Return>(tail);
            if (asvalue) {
                if (ds.size() != 1) {
                    usable = false;
                } else {
                    vector<string_view> busy;
                    ds[0].Join(ScanExpr(f, tail, busy, 0));
                }
            }
        }
        if (!usable) for (auto &d : ds) d = RootSet::Unknown();
        return ds;
    }

    // Iterate the closure reachable from f's returns until nothing moves; a
    // round cap keeps a pathological program from spinning, at the cost of
    // predicting nothing for it.
    void ReturnRootDescs(SFunction *f) {
        if (auto it = cache.returns.find(f); it != cache.returns.end() && it->second.settled)
            return;
        descqueue.clear();
        EnrollDescs(f);
        descrunning = true;
        auto settled = false;
        for (auto round = 0; round < 64 && !settled; round++) {
            descchanged = false;
            for (size_t i = 0; i < descqueue.size(); i++) {
                auto g = descqueue[i];
                auto nd = ComputeDescs(g);
                auto &ds = cache.returns.at(g).values;
                if (nd != ds) {
                    ds = std::move(nd);
                    descchanged = true;
                }
            }
            settled = !descchanged;
        }
        for (auto g : descqueue) {
            auto &result = cache.returns.at(g);
            if (!settled) for (auto &d : result.values) d = RootSet::Unknown();
            result.settled = true;
        }
        descqueue.clear();
        descrunning = false;
    }

    // One predicted root as this specialization sees it, with the exactness
    // and writability it carries (§9.5): a global owns its own storage, which
    // is writable when it is a `var`; a parameter's pointee stands for one
    // call-site array only where that argument's root did, and is writable
    // where the argument was (its root class may legitimately be null, where
    // the call site passed static data); static data is read-only. False for
    // what only one activation knows: its own storage, or a name no enclosing
    // scope declares.
    bool ResolveDesc(FnSpec *spec, const RootDesc &d, RetAlt &a) {
        a = RetAlt {};
        switch (d.kind) {
            case RD_PARAM: {
                if (d.param >= (int)spec->params.size() || d.param >= (int)spec->argtypes.size() ||
                    !IsRefOrSlice(spec->argtypes[d.param]))
                    return false;
                auto vd = spec->params[d.param];
                a.root = rootof(vd, true);
                a.exact = vd->refrootknown && vd->ref.rootexact;
                a.writable = vd->ref.writable;
                return true;
            }
            case RD_GLOBAL:
                a.root = RootOfGlobal(d.glob);
                a.exact = true;
                a.writable = !(d.glob->type && d.glob->type->cq);
                return true;
            case RD_FREE: {
                auto vd = freevar(d.name);
                if (!vd) return false;
                auto isref = vd->type && IsRefOrSlice(vd->type);
                a.writable = isref ? vd->ref.writable : !(vd->type && vd->type->cq);
                if (vd->isglobal) {
                    a.root = RootOfGlobal(vd);
                    a.exact = true;
                    return true;
                }
                a.root = rootof(vd, isref);
                a.exact = isref ? vd->refrootknown && vd->ref.rootexact : true;
                return true;
            }
            case RD_STATIC:
                a.exact = true;
                return true;
            case RD_LOCAL:
                return false;
        }
        return false;
    }

    void Seed(FnSpec *spec) {
        auto anyref = false, anyholder = false;
        for (auto rt : spec->rets) {
            anyref |= IsRefOrSlice(rt);
            anyholder |= !IsRefOrSlice(rt) && holds(rt);
        }
        if (!anyref && !anyholder) return;
        vector<RootSet> ds;
        if (anyref) {
            ReturnRootDescs(spec->sf);
            ds = cache.returns.at(spec->sf).values;
        }
        if (spec->retroots.size() < spec->rets.size()) spec->retroots.resize(spec->rets.size());
        for (size_t i = 0; i < spec->rets.size(); i++) {
            auto isrs = IsRefOrSlice(spec->rets[i]);
            if (!isrs && !holds(spec->rets[i])) continue;
            auto &rr = spec->retroots[i];
            rr.seeded = true;
            // The scan follows references, not what a holder holds: a holder
            // result's prediction is its first checked return.
            rr.predunknown = !isrs || i >= ds.size() || ds[i].unknown;
            for (size_t k = 0; !rr.predunknown && k < ds[i].alts.size(); k++) {
                RetAlt a;
                if (ResolveDesc(spec, ds[i].alts[k], a)) rr.pred.push_back(a);
                else rr.predunknown = true;
            }
            if (rr.predunknown) rr.pred.clear();
        }
    }

    // A return checked while back edges map the prediction (§7.8): what is
    // wrong with it, empty when it fits. Where the scan named nothing, the
    // first return stands in for the prediction, since no back edge was
    // given one before it. A root the prediction names may narrow what later
    // back edges are given, but not take back what earlier ones used; one it
    // misses joins it, which once a back edge has used the prediction only a
    // root no deeper than that back edge's result may, and one every
    // activation shares: a back edge gives a parameter's class the arguments
    // it passes. A return that may be the pointee of a parameter none of the
    // prediction's roots stands for leaves back edges nothing to map. `gs`
    // and `local`: a store may not keep the returned reference, its root
    // included, as it may not keep a reference into a grow-shrink array
    // (§5.2) or into what the cycle stores nothing into (§7.8). `unthread`:
    // the return may be what back edges were given as storable only while
    // threaded parameter classes stay threaded (RetRoot::usedthreads), and
    // is not storable itself; the caller breaks those classes.
    //
    // A holder result's return joins the prediction inexact, whatever it
    // is: a back edge's holder is taken apart by reading its fields, which
    // re-derives their roots inexactly out of a named holder (§9.5), and an
    // exact prediction would be taken back by the first return built from
    // them. A returned reference keeps its root through a variable (§9.2),
    // so a reference result's return joins as exact as it is.
    string ReturnConflict(FnSpec *spec, size_t i, const RetAlt &ret, bool gs, bool local,
                          bool &unthread) {
        if (!spec->inprogress || i >= spec->retroots.size()) return {};
        auto &rr = spec->retroots[i];
        if (!rr.seeded || rr.predlost) return {};
        auto joined = ret;
        if (i < spec->rets.size() && !IsRefOrSlice(spec->rets[i])) joined.exact = false;
        auto weakens = [&](bool lessexact, bool readonly, bool togs, bool tolocal) -> string {
            if (rr.usedexact && lessexact)
                return "this return weakens the reference root already used by the recursive "
                       "cycle (§7.8); use one source";
            if (rr.usedwritable && readonly)
                return "this return is read-only, but the recursive cycle already used a "
                       "writable result; declare the result const (§9.5)";
            if (rr.usedclean && togs)
                return "this return may point into a grow-shrink array, but the recursive "
                       "cycle already used a result that does not (§5.2, §7.8); use one source";
            if (rr.usedstorable && tolocal)
                return "this return may be rooted where the recursive cycle stores nothing, "
                       "but the cycle already used a result it could store (§7.8); use one "
                       "source";
            if (tolocal && !rr.usedthreads.empty()) unthread = true;
            return {};
        };
        // The classes of this activation's parameters, which a back edge maps
        // to the arguments it gives them, and whether the prediction has each.
        auto isclass = [&](VarDef *r) {
            for (auto p : spec->params) if (r && p->ref.root == r) return true;
            return false;
        };
        auto covered = true;
        for (auto p : spec->params) {
            auto c = p->ref.root;
            if (!isclass(c)) continue;
            auto has = false;
            for (auto &a : rr.pred) has |= a.root == c;
            covered &= has;
        }
        auto hidden = ret.hidesclass && !covered;
        if (rr.predunknown || rr.pred.empty()) {
            if (hidden) rr.predlost = true;
            else rr.pred.push_back(joined);
            rr.predunknown = false;
            return {};
        }
        auto named = false;
        for (auto &a : rr.pred) {
            if (a.root != ret.root) continue;
            named = true;
            if (auto bad = weakens(!ret.exact, !ret.writable, ret.intogs && !a.intogs,
                                   ret.cyclelocal && !a.cyclelocal);
                !bad.empty())
                return bad;
            a.exact = a.exact && ret.exact;
            a.writable = a.writable && ret.writable;
            if (!a.intogs) a.intogs = ret.intogs;
            a.cyclelocal = a.cyclelocal || ret.cyclelocal;
            a.hidesclass = a.hidesclass || ret.hidesclass;
        }
        if (hidden) {
            if (rr.used)
                return "this return may point where a parameter other than its root's does, "
                       "which the recursive cycle's back edges were not given (§7.8); use one "
                       "source";
            rr.predlost = true;
            return {};
        }
        if (named) return {};
        if (rr.used) {
            if (isclass(ret.root))
                return cat("this return's reference is rooted at ", ret.root->name,
                           ", which the recursive cycle's back edges were not given (§7.8); "
                           "use one source");
            if (ret.root && ret.root->depth > rr.useddepth)
                return cat("this return's reference is rooted at ", ret.root->name,
                           ", deeper than the result the recursive cycle's back edges were "
                           "given (§7.8); use one source");
            if (auto bad = weakens(true, !ret.writable, gs, local); !bad.empty()) return bad;
        }
        rr.pred.push_back(joined);
        return {};
    }
};

}  // namespace goose
