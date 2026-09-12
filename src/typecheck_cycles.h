// Goose compiler — cycle return roots (§7.8). A back edge lands in a
// specialization whose own returns have not been checked yet, so the root its
// result should carry is recorded nowhere. Before checking a `recursive fn`
// body the checker therefore *predicts* every return root, from a purely
// syntactic scan of the returns of the function and of the functions those
// returns call, iterated to a fixpoint (a cycle's functions define each
// other's roots, so one pass does not settle it). The prediction seeds
// FnSpec::retroots, and every real return verifies it as it is checked. What
// the scan cannot pin down becomes the `cycleroot` sentinel: like a temporary
// it outlives nothing, so such a result may only be passed down, never stored
// or returned.
//
// The scan is purely syntactic, so it needs no checker state beyond the three
// things it cannot derive itself: the sentinel, what root the checker has
// recorded for one of its variables (RootOfVar), and which variable a free
// variable's name denotes from the specialization being seeded (FreeVar). Its
// per-function results are cached in the typechecker, so syntax-only scratch
// state is discarded before optimization and code generation.
#pragma once

namespace goose {

// A returned reference's root as the syntactic cycle scan (§7.8,
// typecheck_cycles.h) can name it before any body is checked: one of the
// function's own reference parameters' root classes, a global, a local of
// an enclosing function (a free variable of a nested function, §7.5), or the
// function's own local storage (meaningful to the function itself, never to
// a caller). RD_NONE is "no return contributes yet" (the fixpoint's
// optimistic bottom), RD_UNKNOWN its top.
enum RootDescKind { RD_NONE, RD_PARAM, RD_GLOBAL, RD_FREE, RD_LOCAL, RD_UNKNOWN };
struct RootDesc {
    RootDescKind kind = RD_NONE;
    int param = 0;              // RD_PARAM: index into SFunction::params.
    VarDef *glob = nullptr;     // RD_GLOBAL.
    string_view name;           // RD_FREE / RD_LOCAL: the variable's name.
    bool operator==(const RootDesc &o) const {
        return kind == o.kind && param == o.param && glob == o.glob && name == o.name;
    }
    bool operator!=(const RootDesc &o) const { return !(*this == o); }
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

    struct Bindings {
        vector<LocalBind> locals;
        vector<SFunction *> localfns;
    };
    struct Returns {
        vector<RootDesc> values;
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

    CycleRoots(Ast &_ast, Cache &_cache, VarDef *_cycleroot, RootOfVar _rootof, FreeVar _freevar)
        : ast(_ast), cache(_cache), cycleroot(_cycleroot), rootof(_rootof), freevar(_freevar) {}

    static RootDesc UnknownDesc() {
        RootDesc d;
        d.kind = RD_UNKNOWN;
        return d;
    }

    // All returns of one function must agree on the root (§9.2), so any
    // disagreement between two of them is already the top of this lattice.
    static RootDesc JoinDesc(const RootDesc &a, const RootDesc &b) {
        if (a.kind == RD_NONE) return b;
        if (b.kind == RD_NONE || a == b) return a;
        return UnknownDesc();
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
        return rootof(vd, t && (t->kind == TY_REF || t->kind == TY_SLICE));
    }

    // Only single-name globals: LookupVar resolves a multi-name declaration's
    // uses to its first VarDef, which the scan will not second-guess.
    RootDesc GlobalDesc(string_view name, string_view ns) {
        auto g = ast.LookupGlobal(name, ns);
        if (!g || g->names.size() != 1 || g->defs.size() != 1) return UnknownDesc();
        RootDesc d;
        d.kind = RD_GLOBAL;
        d.glob = g->defs[0];
        return d;
    }

    // The root of the reference `n` evaluates to. `busy` breaks the recursion
    // through locals defined in terms of each other.
    RootDesc ScanExpr(SFunction *f, Node *n, vector<string_view> &busy, int depth) {
        if (!n || depth > 16) return UnknownDesc();
        if (Is<Dot>(n) || Is<Index>(n) || Is<SliceExpr>(n)) return ScanBase(f, n, busy, depth);
        if (auto u = Is<Unary>(n))
            return u->op == T_BITAND ? ScanBase(f, u->child, busy, depth) : UnknownDesc();
        if (auto c = Is<Call>(n)) return ScanCall(f, c, busy, depth);
        auto id = Is<Ident>(n);
        if (!id) return UnknownDesc();
        EnsureBinds(f);
        if (auto b = FindBind(f, id->name)) {
            if (b->opaque) return UnknownDesc();
            // A reference variable is rooted where its bindings point; a
            // value variable is storage this function owns. A variable
            // declared without a type is a reference only where the
            // declaration says so (`.=`, an `&` initializer), and storage
            // only where its initializer plainly builds a value.
            auto isref = b->byref ||
                         (b->type && (b->type->kind == TY_REF || b->type->kind == TY_SLICE));
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
                if (!value) return UnknownDesc();
                RootDesc d;
                d.kind = RD_LOCAL;
                d.name = id->name;
                return d;
            }
            for (auto nm : busy) if (nm == id->name) return UnknownDesc();
            busy.push_back(id->name);
            RootDesc d;
            for (auto e : b->binds) {
                if (Is<NullLit>(e)) continue;   // Null binds no provenance.
                d = JoinDesc(d, ScanExpr(f, e, busy, depth + 1));
            }
            busy.pop_back();
            return d;
        }
        for (size_t i = 0; i < f->params.size(); i++) {
            if (f->params[i].name != id->name) continue;
            auto pt = f->params[i].type;
            if (!pt || (pt->kind != TY_REF && pt->kind != TY_SLICE)) return UnknownDesc();
            RootDesc d;
            d.kind = RD_PARAM;
            d.param = (int)i;
            return d;
        }
        // A free variable: a local or parameter of an enclosing function,
        // which outlives every activation of this one.
        for (auto o = f->outer; o; o = o->outer) {
            EnsureBinds(o);
            auto b = FindBind(o, id->name);
            auto isparam = false;
            for (auto &p : o->params) isparam |= p.name == id->name;
            if ((b && b->declared) || isparam) {
                RootDesc d;
                d.kind = RD_FREE;
                d.name = id->name;
                return d;
            }
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
    RootDesc ScanBase(SFunction *f, Node *n, vector<string_view> &busy, int depth) {
        vector<Node *> steps;   // Innermost step first.
        for (;;) {
            if (auto d = Is<Dot>(n)) { steps.push_back(n); n = d->obj; continue; }
            if (auto ix = Is<Index>(n)) { steps.push_back(n); n = ix->obj; continue; }
            if (auto se = Is<SliceExpr>(n)) { steps.push_back(n); n = se->obj; continue; }
            break;
        }
        auto d = ScanExpr(f, n, busy, depth + 1);
        if (steps.empty() || d.kind == RD_UNKNOWN || d.kind == RD_NONE) return d;
        auto id = Is<Ident>(n);
        auto t = id ? DeclaredType(f, id->name) : nullptr;
        if (!t) return UnknownDesc();
        if (t->kind == TY_REF) t = t->ref->sub;   // A reference variable: its pointee.
        for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
            if (auto dot = Is<Dot>(*it)) {
                if (t->kind != TY_STRUCT) return UnknownDesc();
                TypeExpr *ft = nullptr;
                for (auto &fl : t->struc->st->fields)
                    if (!fl.ispad && fl.name == dot->name) ft = fl.type;
                if (!ft) return UnknownDesc();
                t = ft;
            } else if (Is<SliceExpr>(*it)) {
                if (t->kind != TY_ARRAY && t->kind != TY_SLICE) return UnknownDesc();
            } else {
                if (t->kind == TY_ARRAY) t = t->arr->sub;
                else if (t->kind == TY_SLICE) t = t->sub;
                else return UnknownDesc();
            }
            if (!t) return UnknownDesc();
            if (t->kind == TY_REF) {
                if (t->ref->lenstorage < 0) return UnknownDesc();
                t = t->ref->sub;
            }
            if (t->kind == TY_SLICE || t->kind == TY_GENERIC || t->kind == TY_UNRESOLVED)
                return UnknownDesc();
        }
        return d;
    }

    RootDesc ScanCall(SFunction *f, Call *c, vector<string_view> &busy, int depth) {
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
            return UnknownDesc();
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
                if (!cands.empty()) return UnknownDesc();
                if ((bd->kind != B_PUSH && bd->kind != B_ALLOC_REF) || args.empty())
                    return UnknownDesc();
                return ScanBase(f, args[0], busy, depth);
            }
            if (cands.size() != 1) return UnknownDesc();
            callee = cands[0];
        }
        EnrollDescs(callee);
        auto &ds = cache.returns.at(callee).values;
        if (ds.empty()) return UnknownDesc();
        auto cd = ds[0];
        if (cd.kind == RD_NONE || cd.kind == RD_GLOBAL) return cd;
        // A callee's free variable is this function's too, unless it is
        // this function's own local, which is then its own storage. The
        // callee's own locals mean nothing here.
        if (cd.kind == RD_FREE) {
            if (!OwnName(f, cd.name)) return cd;
            RootDesc d;
            d.kind = RD_LOCAL;
            d.name = cd.name;
            return d;
        }
        if (cd.kind == RD_LOCAL) return UnknownDesc();
        // The callee's result is rooted at one of its own parameters: follow
        // the argument this call site passes there.
        if (cd.kind == RD_PARAM && cd.param < (int)args.size())
            return ScanExpr(f, args[cd.param], busy, depth + 1);
        return UnknownDesc();
    }

    vector<SFunction *> descqueue;   // Functions in the running fixpoint.
    bool descchanged = false;

    // Bring a function into that fixpoint on first sight, so its own returns
    // get scanned too.
    void EnrollDescs(SFunction *f) {
        auto [it, inserted] = cache.returns.try_emplace(f);
        if (!inserted) return;
        it->second.values.resize(f->has_rets ? f->rets.size() : 0);
        descqueue.push_back(f);
        descchanged = true;
    }

    void ScanReturns(SFunction *f, Node *n, vector<RootDesc> &ds, bool &usable) {
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
                        ds[i] = JoinDesc(ds[i], ScanExpr(f, r->vals[i], busy, 0));
                }
            }
        }
        n->Children([&](Node *c) { ScanReturns(f, c, ds, usable); });
    }

    vector<RootDesc> ComputeDescs(SFunction *f) {
        vector<RootDesc> ds(f->has_rets ? f->rets.size() : 0);
        if (ds.empty()) return ds;
        if (f->isthread || !f->body) {
            for (auto &d : ds) d = UnknownDesc();
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
                    ds[0] = JoinDesc(ds[0], ScanExpr(f, tail, busy, 0));
                }
            }
        }
        if (!usable) for (auto &d : ds) d = UnknownDesc();
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
            if (!settled) for (auto &d : result.values) d = UnknownDesc();
            result.settled = true;
        }
        descqueue.clear();
    }

    // The prediction as a root of this specialization, with the exactness
    // that root carries (§9.5): a global owns its own storage, a parameter
    // stands for one call-site array only where that argument's root did. A
    // parameter's root class may legitimately be null (the call site passed
    // static data).
    VarDef *ResolveDesc(FnSpec *spec, const RootDesc &d, bool &exact) {
        exact = false;
        if (d.kind == RD_PARAM && d.param < (int)spec->params.size() &&
            d.param < (int)spec->argtypes.size()) {
            auto pt = spec->argtypes[d.param];
            if (pt->kind == TY_REF || pt->kind == TY_SLICE) {
                auto vd = spec->params[d.param];
                exact = vd->refrootknown && vd->ref.rootexact;
                return rootof(vd, true);
            }
        }
        if (d.kind == RD_GLOBAL) { exact = true; return RootOfGlobal(d.glob); }
        if (d.kind == RD_FREE) {
            auto vd = freevar(d.name);
            if (!vd) return cycleroot;
            if (vd->isglobal) { exact = true; return RootOfGlobal(vd); }
            auto isref = vd->type && (vd->type->kind == TY_REF || vd->type->kind == TY_SLICE);
            exact = isref ? vd->refrootknown && vd->ref.rootexact : true;
            return rootof(vd, isref);
        }
        return cycleroot;
    }

    void Seed(FnSpec *spec) {
        auto anyref = false;
        for (auto rt : spec->rets)
            anyref |= rt->kind == TY_REF || rt->kind == TY_SLICE;
        if (!anyref) return;
        ReturnRootDescs(spec->sf);
        auto &ds = cache.returns.at(spec->sf).values;
        if (spec->retroots.size() < spec->rets.size()) spec->retroots.resize(spec->rets.size());
        for (size_t i = 0; i < spec->rets.size(); i++) {
            auto rk = spec->rets[i]->kind;
            if (rk != TY_REF && rk != TY_SLICE) continue;
            auto &rr = spec->retroots[i];
            rr.root = i < ds.size() ? ResolveDesc(spec, ds[i], rr.exact) : cycleroot;
            // Writability follows the root (§9.5): a global's storage is
            // writable when the global is a `var`, a parameter's when the
            // argument behind it was; a sentinel promises nothing.
            rr.writable = false;
            if (i < ds.size()) {
                auto &d = ds[i];
                if (d.kind == RD_GLOBAL)
                    rr.writable = d.glob && !(d.glob->type && d.glob->type->cq);
                else if (d.kind == RD_PARAM && d.param < (int)spec->params.size())
                    rr.writable = spec->params[d.param]->ref.writable;
                else if (d.kind == RD_FREE) {
                    if (auto vd = freevar(d.name)) {
                        auto isref = vd->type && (vd->type->kind == TY_REF ||
                                                  vd->type->kind == TY_SLICE);
                        rr.writable = isref ? vd->ref.writable : !(vd->type && vd->type->cq);
                    }
                }
            }
            rr.seeded = true;
        }
    }

    // The prediction the cycle's back edges were given must hold: what is
    // wrong with a checked return whose root contradicts it, empty when it
    // agrees. A sentinel prediction promised nothing to contradict.
    string ReturnConflict(FnSpec *spec, size_t i, VarDef *root, bool exact) {
        if (spec->checkedreturn || i >= spec->retroots.size() || !spec->retroots[i].seeded)
            return {};
        auto &rr = spec->retroots[i];
        if (rr.root == cycleroot) return {};
        if (rr.root == root) {
            // A back edge may already have stored the result relatively on the
            // strength of the prediction's exactness (§3.9).
            if (!exact && rr.exact)
                return cat("this return's reference only outlives ",
                           root ? root->name : string_view("static data"),
                           ", but the recursive cycle's returns give storage it owns "
                           "(§7.8); use one source");
            return {};
        }
        return cat("this return's reference is rooted at ",
                   root ? root->name : string_view("static data"),
                   ", but the recursive cycle's returns give ",
                   rr.root ? rr.root->name : string_view("static data"),
                   " (§7.8); use one source");
    }
};

}  // namespace goose
