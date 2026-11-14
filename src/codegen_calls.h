// Goose compiler — codegen's calls (definitions of CodeGen members,
// codegen.h): calls to specializations by the C.3 convention, extern
// functions (§7.10), function values spliced inline (§7.6), and case-function
// dispatch (§8.2).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Calls. Returns one entry per return value: a C expression for fixed
// values, the value's base pointer for bytes-class ones. d0 is the
// preferred destination for the first return (in-place construction);
// alldst supplies destinations for every return (multi-value receives).

// A variable array that arrived at `base` in value form ([len][elems])
// where the receiver wanted an element run: the count goes to lenlv and
// the elements slide down over the prefix, whose bytes the stack top
// gives back.
inline void CodeGen::EmitSlidePrefix(const string &base, IntStorage ls, const string &stk,
                                     const string &lenlv) {
    string metasz, count;
    if (ls == IS_VARINT) {
        metasz = cat("GS_ULEB_SIZE(", base, ")");
        count = cat("GS_ULEB_READ(", base, ")");
    } else {
        metasz = cat(IntSize(ls));
        count = cat("(int64_t)*(", IntCT(ls), " *)", base);
    }
    auto ms = T();
    L("int64_t ", ms, " = ", metasz, ";");
    L(lenlv, " = ", count, ";");
    L("memmove(", base, ", ", base, " + ", ms, ", (size_t)(", Top(stk), " - ", base,
      " - ", ms, "));");
    L(TopW(stk), " -= ", ms, ";");
}

inline vector<string> CodeGen::EmitCall(Call *c, Dst d0, vector<Dst> *alldst) {
    // An element-run destination for a variable-array result: builtins
    // and tag dispatch have no element-run form, so take the value form
    // and slide its length prefix out -- a builtin builds the value in the
    // receiver's layout, a dispatch's arms in their own. The arms of a
    // dispatch of resizable results emit the run themselves (EmitDispatch).
    // Specialization calls route to an element-run twin (or the same
    // fallback) inside EmitSpecCall, and a function value's tail
    // construction honors the destination as-is.
    if (d0.k == DK_STACK && !d0.lenlv.empty() && d0.t && d0.t->kind == TY_ARRAY &&
        d0.t->arr->akind == A_VAR) {
        auto vt = c->builtin >= 0 ? d0.t : nullptr;
        if (!c->dispatch.empty() && !c->dispatch[0]->rets.empty()) {
            auto rt = c->dispatch[0]->rets[0];
            if (rt->kind == TY_ARRAY && rt->arr->akind == A_VAR) vt = rt;
        }
        if (vt) {
            auto base = T();
            L("uint8_t *", base, " = ", Top(d0.s), ";");
            auto rets = c->builtin >= 0 ? EmitBuiltin(c, Dst { DK_STACK, d0.s, vt })
                                        : EmitDispatch(c, Dst { DK_STACK, d0.s, vt }, alldst);
            EmitSlidePrefix(base, LenStore(vt->arr), d0.s, d0.lenlv);
            return rets;
        }
    }
    if (c->builtin >= 0) return EmitBuiltin(c, d0);
    if (c->fvbody) return EmitFvCall(c, d0);
    if (!c->dispatch.empty()) return EmitDispatch(c, d0, alldst);
    assert(c->spec);
    if (c->spec->sf->isextern) return EmitExternCall(c, c->spec);
    return EmitSpecCall(c, c->spec, d0, alldst);
}

// A call whose every return value has a destination: bytes results land
// there directly, and a fixed result that came back as a C value is
// assigned to its lvalue (a channel the callee wrote itself needs nothing).
// A reference result whose destination wants the pointee's value (Dst::t,
// a binding the checker decayed) loads through the reference.
inline void CodeGen::EmitCallInto(Call *c, vector<Dst> &dsts) {
    auto rets = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
    for (size_t i = 0; i < dsts.size() && i < rets.size(); i++) {
        if (dsts[i].k != DK_LVALUE || rets[i].empty()) continue;
        if (NeedsDeref(c->rettypes[i], dsts[i].t)) {
            Loc lv;
            lv.t = c->rettypes[i];
            lv.val = true;
            lv.s = rets[i];
            L(dsts[i].s, " = ", LoadLoc(lv, dsts[i].t, c->line), ";");
        } else if (rets[i] != dsts[i].s) {
            L(dsts[i].s, " = ", rets[i], ";");
        }
    }
}

// A call to an extern fn (§7.10): the C function directly, or through the
// pointer the runtime bound from its shared library, each argument in its
// C type, no calling-convention extras but the location a runtime entry
// point may take (SFunction::takesloc); a fixed result lands in a
// temporary like any other C value.
inline vector<string> CodeGen::EmitExternCall(Call *c, FnSpec *sp) {
    if (std::find(usedexterns.begin(), usedexterns.end(), sp) == usedexterns.end())
        usedexterns.push_back(sp);
    auto callee = sp->sf->dylib.empty() ? sp->sf->cname : DlCall(sp);
    auto an = CallArgNodes(c, sp->argtypes.size());
    string argstr;
    // A builder argument (`u8[>..]&`, or a value holding one) is appended
    // to by the C side through its stack's top, so a cached top is written
    // back before the call and read again after it, as for a call into
    // Goose code.
    // An extern that reads every top (SFunction::readsalltops) sees none of
    // them through its arguments, so all cached ones are written back too;
    // CollectSpecs makes every caller up the chain do the same.
    auto grows = sp->sf->readsalltops;
    for (size_t i = 0; i < an.size(); i++) {
        auto pt = sp->argtypes[i];
        if (i) argstr += ", ";
        auto x = pt->kind == TY_REF ? GenX(an[i]) : GenXD(an[i], pt);
        argstr += Snapshot(pt, x);
        if (HoldsFatRef(pt)) grows = true;
    }
    if (sp->sf->takesloc) Append(argstr, an.empty() ? "" : ", ", CallerLoc(c->line));
    if (grows) MarkFlush();
    vector<string> rets;
    if (sp->rets.empty()) {
        L(callee, "(", argstr, ");");
    } else {
        auto r0 = T();
        L(CT(sp->rets[0]), " ", r0, " = ", callee, "(", argstr, ");");
        rets.push_back(r0);
    }
    if (grows) MarkReload();
    return rets;
}

// The C prototype of an extern fn, from its Goose declaration (§7.10); for
// one bound from a shared library, the declaration of the pointer it is
// called through, which gs_dl_bind fills, and the function calls go through
// to reach it, which leaves the call out of clang's function-type check
// (GS_DL_CALL).
inline string CodeGen::ExternProto(FnSpec *sp) {
    auto dynamic = !sp->sf->dylib.empty();
    auto ret = sp->rets.empty() ? string("void") : CT(sp->rets[0]);
    string s = dynamic ? "static " : "";
    s += ret;
    if (dynamic) Append(s, " (*", DlSlot(sp), ")(");
    else Append(s, " ", sp->sf->cname, "(");
    string params, args;
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        if (i) {
            s += ", ";
            params += ", ";
            args += ", ";
        }
        s += CT(sp->argtypes[i]);
        Append(params, CT(sp->argtypes[i]), " a", i);
        Append(args, "a", i);
    }
    if (sp->argtypes.empty()) s += "void", params = "void";
    s += ");\n";
    if (dynamic)
        Append(s, "static GS_DL_CALL ", ret, " ", DlCall(sp), "(", params, ") { ",
               sp->rets.empty() ? "" : "return ", DlSlot(sp), "(", args, "); }\n");
    return s;
}

// The arguments the callee's parameters take (Call::ArgNodes): the
// function-value arguments that follow them are compile-time only.
inline vector<Node *> CodeGen::CallArgNodes(Call *c, size_t nparams) {
    auto an = c->ArgNodes();
    an.resize(nparams);
    return an;
}

// One argument by the parameter's calling convention; appends to args.
inline bool CodeGen::EmitArg(FnSpec *sp, const vector<Node *> &an, size_t i,
                             vector<string> &args, bool later) {
    auto node = an[i];
    auto pt = sp->argtypes[i];
    if (IsPoolParam(sp, i)) {
        PushArg(args, pt, true, GenPrefVal(node));
        return false;
    }
    if (IsResz(pt)) {
        // By-value resizable argument: construct into a fresh slot and
        // pass the header (or frame object) by value with its stack
        // (§7.2, C.3).
        string stk;
        auto h = RzTemp(pt, stk);
        GenConstruct(node, stk, pt, RzLenLv(pt, h));
        args.push_back(h);
        args.push_back(stk);
        return false;
    }
    if (IsBytesT(pt)) {
        // By-value nonfixed argument: construct into a fresh slot (§7.2).
        string stk;
        auto base = BytesTemp(stk);
        GenConstruct(node, stk, pt);
        args.push_back(base);
        return false;
    }
    // A relative reference parameter takes the argument's offset (§3.9). The
    // `in pool` form's is the same in the callee's copy; a self-relative one
    // can only be null, which is zero wherever it is.
    if (IsRelRef(pt)) {
        auto t = T();
        L(CT(pt), " ", t, ";");
        EmitRelStoreAt(cat("(uint8_t *)&", t), pt, GenX(node), node->line, true, RelValue(node));
        args.push_back(t);
        return false;
    }
    // A variable of this function's own at its only use: the callee takes
    // its storage, which nothing reads again, rather than a copy (§7.2).
    // Named nowhere else, it is neither another argument nor where the
    // call's results go.
    if (PtrAbi(pt) && MovableArg(sp, node, pt)) {
        args.push_back(cat("&", GenX(node)));
        return false;
    }
    // A variable no one else can write while the callee runs, for a
    // parameter the callee only reads: the callee reads it where it lies.
    // The caller may still write it, as the call's result. Not to a callee
    // that builds a result at its location (NeedsResultLocation), which may
    // be this very variable, while it still reads its parameter.
    if (!later && PtrAbi(pt) && ParamUnwritten(sp, i) && !BuildsAtLocation(sp) &&
        StableArg(node, pt)) {
        args.push_back(cat("&", GenX(node)));
        return true;
    }
    if (DiesAtCall(node, pt)) {
        // Built in a temporary of its own, which the callee may have.
        auto tv = T();
        FixedLocal(pt, tv);
        GenAny(node, FreshLv(tv, pt));
        args.push_back(cat("&", tv));
        return false;
    }
    // A value read before later arguments that run statements lands in a
    // temp, as one passed by its address always does; otherwise C copies
    // the expression's value into the parameter at the call (GenOperandVal).
    // A root descriptor never changes once made, so one named outright
    // passes the members it holds now.
    auto x = GenXD(node, pt);
    auto named = splitroots.count(sp->params[i]) && !NamedRootDesc(x).empty();
    auto value = !named && (later || PtrAbi(pt) || (HasStmts(node) && !IsCName(x)))
                 ? Snapshot(pt, x) : x;
    if (PtrAbi(pt)) args.push_back(cat("&", value));
    else if (splitroots.count(sp->params[i])) PushRootMembers(args, pt, value);
    else PushArg(args, pt, false, value);
    return false;
}

// A variable named once in its function, outside any loop it is not
// declared in, and not in a deferred body or a nested function, which may
// run later or more than once: nothing reads it after that one use. There
// is no other use to order it against, and no reference into the variable
// without naming it. An Ident reached twice through a shared subtree counts
// twice. Last uses after earlier ones, or on one branch, are not found.
inline const set<const Ident *> &CodeGen::SingleUses(FnSpec *sp) {
    auto it = singleuses.find(sp);
    if (it != singleuses.end()) return it->second;
    struct U { int n = 0; const Ident *id = nullptr; int useloop = 0; int declloop = 0; };
    unordered_map<const VarDef *, U> u;
    int loops = 0;
    function<void(Node *, int, bool)> walk = [&](Node *n, int loop, bool hidden) {
        if (!n) return;
        if (auto id = Is<Ident>(n); id && id->vdef) {
            auto &e = u[id->vdef];
            e.n += hidden ? 2 : 1;
            e.id = id;
            e.useloop = loop;
        }
        if (auto vd = Is<VarDecl>(n))
            for (auto v : vd->defs) u[v].declloop = loop;
        if (auto m = Is<MatchExpr>(n))
            for (auto &a : m->arms) if (a.binder) u[a.binder].declloop = loop;
        auto inner = loop;
        if (Is<While>(n) || Is<LoopExpr>(n) || Is<ForLoop>(n)) inner = ++loops;
        if (auto f = Is<ForLoop>(n)) {
            if (f->vdef) u[f->vdef].declloop = inner;
            if (f->idxdef) u[f->idxdef].declloop = inner;
        }
        auto h = hidden || Is<Defer>(n) || Is<FunVal>(n) || Is<FnDecl>(n);
        RunChildren(n, [&](Node *c) { walk(c, inner, h); });
    };
    if (sp->body) walk(sp->body, 0, false);
    auto &out = singleuses[sp];
    for (auto &[v, e] : u)
        if (e.n == 1 && e.useloop == e.declloop) out.insert(e.id);
    return out;
}

// A callee that builds a result at its destination (NeedsResultLocation),
// piece by piece while it may still read its arguments.
inline bool CodeGen::BuildsAtLocation(FnSpec *sp) {
    for (auto rt : sp->rets) if (NeedsResultLocation(rt)) return true;
    return false;
}

// An argument whose storage the callee can have for its by-pointer
// parameter (PtrAbi) in place of the copy it is owed: a variable of this
// function at its only use (SingleUses), whose storage is this function's
// to give -- a local in its frame or on a data stack, or a parameter, which
// arrives as a pointer to a copy the caller made for it unless it is one
// its callers may lend a variable of their own for (ParamUnwritten) -- that
// nothing else reaches: not a global, captured or a pool, and not bound to
// a writable reference. A result built in place (NeedsResultLocation) is
// written piece by piece while the callee may still read its argument, so
// such a callee gets a copy.
inline bool CodeGen::MovableArg(FnSpec *sp, Node *n, TypeExpr *pt) {
    auto id = Is<Ident>(n);
    if (!id || !id->vdef || !curspec) return false;
    if (BuildsAtLocation(sp)) return false;
    auto v = id->vdef;
    if (v->isglobal || v->ownerspec != curspec || v->captured || v->refwrite || v->reusable ||
        !v->type || !TEq(v->type, pt) || splitroots.count(v) || refalias.count(v))
        return false;
    if (v->isparam ? !fvptr.count(v) : fvptr.count(v) > 0) return false;
    if (v->isparam) {
        auto pi = std::find(curspec->params.begin(), curspec->params.end(), v);
        if (pi == curspec->params.end() ||
            ParamUnwritten(curspec, (size_t)(pi - curspec->params.begin())))
            return false;
    }
    return SingleUses(curspec).count(id) > 0;
}

// A by-pointer parameter whose storage the callee never writes, so that it
// can be the caller's own variable: not `var` (§4.4), never written through
// a writable reference, nor captured, and no assignment, `++` or `&` in the
// body rooted at it -- which the optimizer's rewrites, a tail recursion's
// rebinding of the parameters (optimize_tre.h) among them, may make.
inline bool CodeGen::ParamUnwritten(FnSpec *sp, size_t i) {
    auto it = unwrittenparams.find(sp);
    if (it == unwrittenparams.end()) {
        vector<bool> ok(sp->params.size(), false);
        for (size_t p = 0; p < sp->params.size(); p++) {
            auto v = sp->params[p];
            // One handed on to a `var` parameter takes a copy of its own,
            // so that it can be handed on with no other (ConsumedParam).
            ok[p] = sp->body && !sp->sf->isextern && v && !v->isvar && !v->captured &&
                    !v->refwrite && !v->reusable && PtrAbi(sp->argtypes[p]) &&
                    !IsPoolParam(sp, p) && !ConsumedParam(sp, v);
        }
        auto written = [&](Node *lv) {
            auto v = Reassociator::RootVar(lv);
            for (size_t p = 0; p < sp->params.size(); p++)
                if (v && v == sp->params[p]) ok[p] = false;
        };
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto a = Is<Assign>(n)) written(a->lval);
            else if (auto x = Is<IncDec>(n)) written(x->lval);
            else if (auto u = Is<Unary>(n); u && u->op == T_BITAND) written(u->child);
            RunChildren(n, walk);
        };
        if (sp->body) walk(sp->body);
        it = unwrittenparams.emplace(sp, ok).first;
    }
    return i < it->second.size() && it->second[i];
}

// A variable of this function holding a value of the parameter's type, as
// a C object of its own (a by-reference binding has a reference type), that
// only this function's own statements can write: not a global, not one a
// nested function or function value reaches, never bound to a writable
// reference (§4.4). While its function waits on a call, nothing writes it.
inline bool CodeGen::StableArg(Node *n, TypeExpr *pt) {
    auto id = Is<Ident>(n);
    if (!id || !id->vdef || !curspec) return false;
    auto v = id->vdef;
    if (v->isglobal || v->ownerspec != curspec || v->captured || v->refwrite || v->reusable ||
        v->istemp || v->resulttemp || !v->type || !TEq(v->type, pt))
        return false;
    // A parameter arrives as a pointer to its storage; any other variable
    // is one in this frame or on a data stack.
    return v->isparam || !fvptr.count(v);
}

// A read-only by-pointer parameter that this spec hands, at its only use
// (SingleUses), to a parameter that changes it: a `var` one, or one its own
// callee consumes so. Its callers then give it storage of its own rather
// than lend a variable (ParamUnwritten), and MovableArg may hand that on.
// Within a recursive cycle, a spec still being looked at consumes nothing
// yet, which only costs a copy. Whether the hand-off then happens is
// MovableArg's to say at the call; where it does not, the copy is made
// there instead of at this spec's callers.
inline bool CodeGen::ConsumedParam(FnSpec *sp, const VarDef *v) {
    if (!sp || !sp->body || !v || v->isvar || !v->isparam) return false;
    auto it = consumedparams.find(sp);
    if (it != consumedparams.end()) return it->second.count(v) > 0;
    consumedparams[sp];   // In progress: nothing yet.
    auto &single = SingleUses(sp);
    set<const VarDef *> found;
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || Is<FunVal>(n) || Is<FnDecl>(n) || Is<Defer>(n)) return;
        if (auto c = Is<Call>(n); c && c->spec && c->builtin < 0 && !c->spec->sf->isextern) {
            auto an = CallArgNodes(c, c->spec->params.size());
            for (size_t i = 0; i < an.size(); i++) {
                auto id = Is<Ident>(an[i]);
                auto p = c->spec->params[i];
                if (!id || !id->vdef || !id->vdef->isparam || id->vdef->isvar || !p ||
                    !PtrAbi(c->spec->argtypes[i]) || !single.count(id))
                    continue;
                if (std::find(sp->params.begin(), sp->params.end(), id->vdef) == sp->params.end())
                    continue;
                if (p->isvar || ConsumedParam(c->spec, p)) found.insert(id->vdef);
            }
        }
        RunChildren(n, walk);
    };
    walk(sp->body);
    auto &out = consumedparams[sp];
    out = found;
    return out.count(v) > 0;
}

// §7.3 for a fixed result of an inlined body (OpenIbNrvo has the
// variable-size ones): where the destination is fresh, nothing reads it
// before the block's value is complete there, so the local the returns hand
// back (NamedResult: no other return reads it) can live there from its
// declaration on. A chain of inlined setters then works in one variable.
inline const VarDef *CodeGen::OpenIbFixed(InlineBlock *ib, const Dst &d) {
    if (d.k != DK_LVALUE || !d.fresh || d.pool || !d.t || !PtrAbi(d.t) ||
        NeedsResultLocation(d.t))
        return nullptr;
    if (!ib->spec || ib->spec->rets.size() != 1 || !TEq(ib->spec->rets[0], d.t)) return nullptr;
    auto vd = NamedResult(ib->body, ib->sf, 1, 0);
    if (!vd || !vd->type || !TEq(vd->type, d.t) || ibfixed.count(vd) || refalias.count(vd) ||
        vd->reusable)
        return nullptr;
    // A deferred body runs after a return of another value has overwritten
    // the local's storage, and may read the local. A `return ... from` past
    // this block (to the function, or to an inlined body around it) builds
    // its value in place at a destination that may be this very storage
    // (the function's gs_r0, say), reading the local as it goes; NamedResult
    // only looks at the returns to this block.
    auto unsafe = false;
    vector<SFunction *> inner { ib->sf };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || unsafe) return;
        if (Is<Defer>(n)) unsafe = true;
        if (auto r = Is<Return>(n);
            r && std::find(inner.begin(), inner.end(), r->target) == inner.end())
            unsafe = true;
        auto nested = Is<InlineBlock>(n);
        if (nested) inner.push_back(nested->sf);
        RunChildren(n, walk);
        if (nested) inner.pop_back();
    };
    walk(ib->body);
    if (unsafe) return nullptr;
    ibfixed[vd] = d.s;
    return vd;
}

// An argument whose value is built where the caller puts it, in a
// temporary nothing else holds, which the callee can then have: a call's
// result (EmitSpecCall's or EmitDispatch's own temporary), an inlined body's
// value (OpenIbFixed) or a struct literal (LeafAny), received as the type it
// was checked as. One adapted on the way (AdtFrom), or reaching a
// relative-reference type's slot, is left to the ordinary snapshot.
inline bool CodeGen::DiesAtCall(Node *n, TypeExpr *pt) {
    if (!PtrAbi(pt) || NeedsResultLocation(pt)) return false;
    // An inlined call's value and a literal are built where they are put
    // (OpenIbFixed, LeafAny).
    if ((Is<InlineBlock>(n) || Is<StructLit>(n)) && !AdtFrom(n))
        return n->exprtype && TEq(n->exprtype, pt);
    auto c = Is<Call>(n);
    if (!c || c->builtin >= 0 || c->fvbody || AdtFrom(n)) return false;
    if (c->dispatch.empty() && (!c->spec || c->spec->sf->isextern)) return false;
    return c->rettypes.size() == 1 && TEq(c->rettypes[0], pt) && c->exprtype &&
           TEq(c->exprtype, pt);
}

// A free variable of the callee, from the caller's frame (or passed on).
inline void CodeGen::EmitFvArg(const VarDef *fv, vector<string> &args) {
    auto name = VName(fv);
    // gs_init_globals has no curspec, and its locals no ownerspec.
    auto ours = fv->ownerspec == curspec;
    if (fv->reusable) {
        // A pool local of ours: assemble; a passed-through one: forward.
        if (ours) {
            auto &p = vpool[fv];
            auto t = T();
            L("gs_pref ", t, " = { &", name, ", ", vstk[fv], ", &", p.first, ", ",
              p.second, " };");
            PushArg(args, fv->type, true, t);
        } else {
            PushArg(args, fv->type, true, name);
        }
        return;
    }
    if (IsResz(fv->type)) {
        args.push_back(fvptr.count(fv) ? name : cat("&", name));
        args.push_back(ours ? vstk[fv] : cat(name, "_stk"));
        return;
    }
    if (IsBytesT(fv->type)) {
        args.push_back(name);
        return;
    }
    args.push_back(fvptr.count(fv) ? name : cat("&", name));
}

// Where a bytes-class return value goes: the destination's own stack where
// it has one, a temporary otherwise, and the address its bytes start at,
// which is that stack's top before the call writes anything.
inline string CodeGen::BytesResultBase(const Dst &dd, string &stk) {
    if (dd.k == DK_STACK) stk = dd.s;
    else BytesTemp(stk);
    auto base = T();
    L("uint8_t *", base, " = ", Top(stk), ";");
    return base;
}

inline vector<string> CodeGen::EmitSpecCall(Call *c, FnSpec *sp, Dst d0, vector<Dst> *alldst) {
    assert(sinfo.count(sp));
    auto &ki = sinfo[sp];
    auto an = CallArgNodes(c, sp->params.size());
    string ername, slidebase;
    Dst slide;
    // Set when the destination's length storage differs from the callee's
    // return type: the value arrives as an element run (or, without a run
    // twin, in the callee's own layout) and gets the destination's prefix
    // written in front of it after the call.
    string reprefixbase, reprefixcnt;
    Dst reprefix;
    vector<string> args;
    // Whether the arguments after each one run statements (EmitArg).
    vector<bool> later(an.size() + 1, false);
    for (size_t i = an.size(); i-- > 0;) later[i] = later[i + 1] || HasStmts(an[i]);
    // Whether an argument is a variable of the caller's that the callee
    // reads where it lies (EmitArg), which a result must not overwrite.
    auto lent = false;
    for (size_t i = 0; i < an.size(); i++) lent |= EmitArg(sp, an, i, args, later[i + 1]);
    for (auto fv : ki.freevars) EmitFvArg(c->FreeVarArg(fv), args);
    vector<string> retex(sp->rets.size());
    for (size_t i = 0; i < sp->rets.size(); i++) {
        auto rt = sp->rets[i];
        Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                              : (i == 0 ? d0 : Dst {});
        if (IsResz(rt)) {
            // Elements land at the destination's top; the count returns
            // through the length channel (into the receiver's header, or
            // a temporary one).
            string stk, lenlv;
            if (dd.k == DK_STACK && !dd.lenlv.empty()) {
                stk = dd.s;
                lenlv = dd.lenlv;
                retex[i] = "";
            } else {
                auto h = RzTemp(rt, stk);
                lenlv = RzLenLv(rt, h);
                retex[i] = h;
            }
            args.push_back(stk);
            args.push_back(cat("&", lenlv));
        } else if (i == 0 && dd.k == DK_STACK && !dd.lenlv.empty() && rt->kind == TY_ARRAY &&
                   rt->arr->akind == A_VAR) {
            // An element-run receiver of a variable-array result: use the
            // callee's element-run twin when it can have one; otherwise
            // take the value form and slide its length prefix out.
            auto er = EnsureEr(sp);
            if (!er.empty()) {
                ername = er;
                args.push_back(dd.s);
                args.push_back(cat("&", dd.lenlv));
                retex[i] = "";
            } else {
                slide = dd;
                slidebase = T();
                L("uint8_t *", slidebase, " = ", Top(dd.s), ";");
                args.push_back(dd.s);
                retex[i] = "";
            }
        } else if (i == 0 && NeedsReprefix(dd, rt)) {
            // A T[] result landing in a slot of another length storage
            // (a T[varint] field, say): the callee cannot write the
            // destination's layout, so its elements are taken raw and the
            // prefix is put in front of them afterwards (EmitReprefix).
            reprefix = dd;
            reprefixbase = T();
            L("uint8_t *", reprefixbase, " = ", Top(dd.s), ";");
            auto er = EnsureEr(sp);
            if (!er.empty()) {
                ername = er;
                reprefixcnt = T();
                L("int64_t ", reprefixcnt, " = 0;");
                // The run lands behind the destination's prefix: claim
                // those bytes now and patch the count in afterwards.
                Bump(dd.s, cat(PrefixBytes(LenStore(dd.t->arr))));
                args.push_back(dd.s);
                args.push_back(cat("&", reprefixcnt));
            } else {
                args.push_back(dd.s);
            }
            retex[i] = reprefixbase;
        } else if (IsBytesT(rt)) {
            string stk;
            retex[i] = BytesResultBase(dd, stk);
            args.push_back(stk);
        } else if ((int)i != ki.cret) {
            // A reference the destination loads through lands in a
            // temporary first (EmitCallInto). The callee builds a PtrAbi
            // result in place (GenNormalReturn), so only a fresh destination
            // takes one directly. An argument the callee reads where it
            // lies (StableArg) may be that destination, which a result
            // written before the callee's last read of it would change.
            auto ptrabi = PtrAbi(rt) && !NeedsResultLocation(rt);
            if (dd.k == DK_LVALUE && !NeedsDeref(rt, dd.t) &&
                (ptrabi || (lent && !NeedsResultLocation(rt))
                    ? dd.fresh && dd.t && TEq(rt, dd.t)
                    : !NeedsResultLocation(rt) || (dd.t && TEq(rt, dd.t)))) {
                retex[i] = dd.s;
                args.push_back(cat("&", dd.s));
            } else {
                auto tv = T();
                FixedLocal(rt, tv);
                retex[i] = tv;
                args.push_back(cat("&", tv));
            }
        }
    }
    if (ki.needssp) {
        NoStackAcrossCycleCall(sp, c->line);
        NoteStackCall(sp, c->line);
        args.push_back(SpTop());
    }
    if (ki.located) args.push_back(CallerLoc(c->line));
    string argstr;
    for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
    auto callee = ername.empty() ? ki.cname : ername;
    // The callee reads and bumps the stacks it was handed, and no others.
    auto reach = SyncReach(sp, args, an);
    MarkFlush(reach);
    if (ki.cret >= 0) {
        auto r0 = T();
        L(CT(sp->rets[ki.cret]), " ", r0, " = ", callee, "(", argstr, ");");
        retex[ki.cret] = r0;
    } else {
        L(callee, "(", argstr, ");");
    }
    MarkReload(reach);
    // Checked before the result is fixed up: a call that unwound left none,
    // and a value in flight to a target may sit behind its reserved prefix,
    // where a fixup would move it from where the target's catch finds it.
    if (ki.hasrf) EmitRfCheck(sp);
    if (!reprefixbase.empty()) EmitReprefix(sp, reprefix, reprefixbase, reprefixcnt);
    if (!slidebase.empty()) {
        // Value form arrived as [len][elems]; slide the prefix out and
        // report the count (the one caller-side copy of this fallback).
        EmitSlidePrefix(slidebase, LenStore(sp->rets[0]->arr), slide.s, slide.lenlv);
    }
    return retex;
}

// Whether a variable-array result of type rt reaches d as a value slot of a
// T[] of another length storage (a T[varint] field, say), whose layout the
// callee cannot write: the slot's prefix goes in front of the elements
// after the call (EmitReprefix).
inline bool CodeGen::NeedsReprefix(const Dst &d, TypeExpr *rt) {
    return d.k == DK_STACK && d.lenlv.empty() && d.t && d.t->kind == TY_ARRAY &&
           d.t->arr->akind == A_VAR && rt->kind == TY_ARRAY && rt->arr->akind == A_VAR &&
           LenStore(d.t->arr) != LenStore(rt->arr);
}

// The elements of a variable-array call result sit at `base` -- raw (the
// count in `cnt`, written after prefix bytes reserved before the call) or,
// when `cnt` is empty, behind the callee's own prefix -- and the
// destination wants its own prefix in front of them. The run form only
// patches the reserved bytes; the value form moves the elements by the
// difference in prefix sizes, the same cost as the value-form slide.
inline void CodeGen::EmitReprefix(FnSpec *sp, const Dst &dd, const string &base,
                                  const string &cnt) {
    auto ls = LenStore(dd.t->arr);
    if (!cnt.empty()) {
        EmitPrefixPatch(base, ls, dd.s, cnt, cat(base, " + ", PrefixBytes(ls)));
        return;
    }
    auto srcls = LenStore(sp->rets[0]->arr);
    auto count = T();
    auto oms = T();
    if (srcls == IS_VARINT) {
        L("int64_t ", count, " = GS_ULEB_READ(", base, ");");
        L("int64_t ", oms, " = GS_ULEB_SIZE(", base, ");");
    } else {
        L("int64_t ", count, " = (int64_t)*(", IntCT(srcls), " *)", base, ";");
        L("int64_t ", oms, " = ", IntSize(srcls), ";");
    }
    auto ms = T();
    string writepref;
    EmitLenCheck(ls, count);
    if (ls == IS_VARINT) {
        auto tmp = T();
        L("uint8_t ", tmp, "[10];");
        L("int64_t ", ms, " = gs_uleb_write(", tmp, ", (uint64_t)", count, ");");
        writepref = cat("GS_MEMCPY(", base, ", ", tmp, ", (size_t)", ms, ");");
    } else {
        L("int64_t ", ms, " = ", IntSize(ls), ";");
        writepref = cat("*(", IntCT(ls), " *)", base, " = (", IntCT(ls), ")", count, ";");
    }
    L("memmove(", base, " + ", ms, ", ", base, " + ", oms, ", (size_t)(", Top(dd.s), " - ",
      base, " - ", oms, "));");
    L(writepref);
    L(TopW(dd.s), " += ", ms, " - ", oms, ";");
}

inline void CodeGen::EmitRfCheck(FnSpec *callee) {
    if (norfcheck) return;
    L("if (gs_rf) {");
    ind++;
    auto caught = curspec && callee->needs.count(curspec);
    if (caught) {
        auto tid = fromids[curspec];
        EnsureFromChannels(curspec);
        L("if (gs_rf == ", tid, ") {");
        ind++;
        // The propagation ends here, so restore the "nothing in flight"
        // state every other exit path relies on.
        L("gs_rf = 0;");
        string retv;
        for (size_t i = 0; i < curspec->rets.size(); i++) {
            auto rt = curspec->rets[i];
            if (NeedsResultLocation(rt)) continue;   // The channel names the receiver itself.
            auto lret = cat("gs_lret_", tid, "_", i);
            // The value is on our destination, behind anything the calls it
            // unwound had built there: down to where our caller expects it.
            if (IsBytesT(rt))
                LandValue(cat("gs_dst", i), DstTop0(i), cat("gs_fval_", tid, "_", i), rt, lret);
            if (IsResz(rt)) {
                L("*gs_rl", i, " = ", lret, ";");   // The count, or the frame object.
                continue;
            }
            if (IsBytesT(rt)) continue;
            if ((int)i == curinfo->cret) retv = lret;
            else L("*gs_r", i, " = ", lret, ";");
        }
        Epilogue(retv);
        ind--;
        L("} else {");
        ind++;
        if (curinfo && curinfo->hasrf) PropagateReturn("");
        else L("gs_panic(\"unexpected long-distance return\");");
        ind--;
        L("}");
    } else if (curinfo && curinfo->hasrf) {
        PropagateReturn("");
    } else {
        L("gs_panic(\"unexpected long-distance return\");");
    }
    ind--;
    L("}");
    termjump = false;
}

// Calling a function value: the checked body instance is spliced inline
// (§7.6); its parameters are ordinary locals of this function.
inline vector<string> CodeGen::EmitFvCall(Call *c, Dst d0) {
    auto et = c->fvbody->exprtype;
    auto wantsval = !IsVoidT(et);
    // The call was checked against its destination and the body's value
    // without one, so the two can be different array or slice types
    // (FitsAt): an array where a slice is expected (§3.10), any array or
    // slice constructing an array of another kind (§4.2). The body then
    // builds its value as its own type in a temporary, which is sliced whole
    // or copied as the type the destination names, the call's where it names
    // none: the call's value is a copy (§4.1), so a slice of it never points
    // into a variable the body names. A bytes-class value is wanted only on
    // a stack; anywhere else it goes unused.
    auto want = d0.t ? d0.t : c->exprtype;
    auto adapt = wantsval && (want->kind == TY_ARRAY || want->kind == TY_SLICE) &&
                 !TEq(want, et) && (IsFix(want) || d0.k == DK_STACK);
    string rv, stk;
    Dst d = d0;
    if (wantsval && !IsBytesT(et) && (adapt || d0.k != DK_LVALUE)) {
        rv = T();
        FixedLocal(et, rv);
        d = Dst { DK_LVALUE, rv, et };
    } else if (wantsval && IsResz(et) && (adapt || d0.k != DK_STACK)) {
        rv = RzTemp(et, stk);
        d = Dst { DK_STACK, stk, et, RzLenLv(et, rv) };
    } else if (wantsval && IsBytesT(et) && (adapt || d0.k != DK_STACK)) {
        rv = BytesTemp(stk);
        d = Dst { DK_STACK, stk, et };
    } else if (wantsval && d0.k == DK_LVALUE) {
        rv = d0.s;
    }
    PushSc(SC_PLAIN);
    L("{");
    ind++;
    for (size_t i = 0; i < c->fvparams.size(); i++)
        BindLocal(c->fvparams[i], i < c->args.size() ? c->args[i] : nullptr);
    for (auto st : c->fvbody->stmts) GenStmt(st);
    if (c->fvbody->tail) {
        if (wantsval) GenAny(c->fvbody->tail, d);
        else GenAny(c->fvbody->tail, Dst {});
    }
    PopSc();
    ind--;
    L("}");
    termjump = false;
    if (!wantsval) return {};
    if (adapt) {
        Loc lv;
        if (IsResz(et)) {
            lv = RzTempLoc(et, rv, stk);
        } else if (IsBytesT(et)) {
            lv = BytesLoc(rv, et, Loc {});
        } else {
            lv.t = et;
            lv.val = true;
            lv.s = rv;
        }
        if (d0.k == DK_STACK) {
            if (IsFix(want)) EmitValStore(d0.s, want, LoadLoc(lv, want, c->line));
            else ConstructFromLoc(lv, want, d0.s, d0.lenlv, c->line);
            return {};
        }
        rv = LoadLoc(lv, want, c->line);
        if (d0.k == DK_LVALUE) {
            L(d0.s, " = ", rv, ";");
            rv = d0.s;
        }
    }
    // A value built on the stack destination is complete there, as the type
    // the destination names: nothing is left for the construction around the
    // call to store (ConstructCall), whose type can be another where an
    // inlined callee's `return` hands the call, checked as the callee's
    // result type, the caller's destination.
    if (rv.empty()) return {};
    return { rv };
}

// Case-function tag dispatch (§8.2): switch on the tag, call the matching
// specialization with the payload, sharing all other argument slots and
// return channels across the arms.
inline vector<string> CodeGen::EmitDispatch(Call *c, Dst d0, vector<Dst> *alldst) {
    auto sp0 = c->dispatch[0];
    auto an = CallArgNodes(c, sp0->params.size());
    auto pos = c->dispatcharg;
    // The scrutinee.
    auto sn = an[pos];
    auto st = sn->exprtype;
    TypeExpr *enumtype = nullptr;
    auto isref = false;
    if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
        enumtype = st->ref->sub;
        isref = true;
    } else {
        assert(st->kind == TY_ENUM);
        enumtype = st;
    }
    auto varmode = enumtype->enu->varmode;
    auto ei = EIOf(enumtype);
    auto ts = TagSize(ei->en);
    string p, copied, sv, tag;
    vector<string> snap(ei->en->variants.size());
    auto scrutinee = [&]() {
        if (varmode) {
            if (isref) {
                auto x = GenX(sn);
                p = T();
                L(CT(st), " ", p, " = ", x, ";");
            } else {
                p = GenPtr(sn);
            }
            tag = cat("*(", IntCT(TagStore(ei->en)), " *)", p);
            // A reference input can also decay to a by-value case parameter.
            // Copy its value now; reference cases retain the original pointer.
            // Only a variable-size variant needs the whole value on a data
            // stack. A fixed-size one is copied on its own and an empty one
            // not at all, so a call into a recursive cycle, whose by-value
            // parameters are fixed-size (§7.8), holds no stack across it.
            auto wholecopy = false;
            for (size_t vi = 0; vi < snap.size(); vi++)
                wholecopy |= c->dispatch[vi]->argtypes[pos]->kind != TY_REF &&
                             IsBytesT(VariantType(enumtype, (int)vi));
            if (wholecopy) {
                string stk;
                copied = BytesTemp(stk);
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(enumtype, p), ";");
                L("GS_MEMCPY(", Top(stk), ", ", p, ", (size_t)", sz, ");");
                Bump(stk, sz);
            } else {
                for (size_t vi = 0; vi < snap.size(); vi++) {
                    if (c->dispatch[vi]->argtypes[pos]->kind == TY_REF ||
                        EmptyLayout(ei->en->variants[vi].fields))
                        continue;
                    auto vt = VariantType(enumtype, (int)vi);
                    snap[vi] = T();
                    FixedLocal(vt, snap[vi], "{0}");
                    auto tagk = TagConst(ei, (int)vi), vct = CT(vt);
                    L("if (", tag, " == ", tagk, ") ", snap[vi], " = *(", vct, " *)(", p, " + ", ts,
                      ");");
                }
            }
        } else {
            auto value = GenXD(sn, enumtype);
            sv = T();
            FixedLocal(enumtype, sv, value);
            tag = cat(sv, ".tag");
        }
    };
    // Evaluate every argument in source order, including the dispatched
    // argument; its position is not necessarily the first one. The others
    // become the C arguments every arm passes, read before the switch (the
    // cases agree on those parameters' types).
    vector<vector<string>> shared(an.size());
    // Read before the switch, they count as read before later arguments
    // that run statements, so none is lent (EmitArg): the arms' results
    // may go wherever their destination is.
    for (size_t i = 0; i < an.size(); i++) {
        if ((int)i == pos) scrutinee();
        else {
            [[maybe_unused]] auto lent = EmitArg(sp0, an, i, shared[i], true);
            assert(!lent);
        }
    }
    // Shared return channels.
    vector<string> retex(sp0->rets.size());
    vector<string> dststk(sp0->rets.size());
    vector<string> dstlen(sp0->rets.size());
    // Set when a T[] result lands in a slot of another length storage: tag
    // dispatch has no element-run form (C.3), so every arm writes the value
    // in its own layout, and the slot's prefix replaces the arms' after the
    // switch.
    Dst reprefix;
    for (size_t i = 0; i < sp0->rets.size(); i++) {
        auto rt = sp0->rets[i];
        Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                              : (i == 0 ? d0 : Dst {});
        if (IsResz(rt)) {
            if (dd.k == DK_STACK && !dd.lenlv.empty()) {
                dststk[i] = dd.s;
                dstlen[i] = dd.lenlv;
            } else {
                auto h = RzTemp(rt, dststk[i]);
                dstlen[i] = RzLenLv(rt, h);
                retex[i] = h;
            }
        } else if (IsBytesT(rt)) {
            string stk;
            retex[i] = BytesResultBase(dd, stk);
            dststk[i] = stk;
            if (i == 0 && NeedsReprefix(dd, rt)) reprefix = dd;
        } else if (NeedsResultLocation(rt) && dd.k == DK_LVALUE && TEq(rt, dd.t)) {
            retex[i] = dd.s;
        } else {
            auto tv = T();
            FixedLocal(rt, tv);
            retex[i] = tv;
        }
    }
    L("switch (", tag, ") {");
    for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
        auto sp = c->dispatch[vi];
        auto &ki = sinfo[sp];
        L("case ", TagConst(ei, (int)vi), ": {");
        ind++;
        PushSc(SC_PLAIN);
        auto vt = VariantType(enumtype, (int)vi);
        auto byref = sp->argtypes[pos]->kind == TY_REF;
        string varg;
        string payload = varmode ? cat("(", byref ? p : copied, " + ", ts, ")") : "";
        if (byref) {
            if (IsBytesT(vt)) varg = payload;
            else {
                auto tv = T();
                L(CT(vt), " *", tv, " = (", CT(vt), " *)(", payload, ");");
                varg = tv;
            }
        } else if (IsBytesT(vt)) {
            // The whole by-value enum was already copied before later
            // arguments ran. Its payload is the callee's private value.
            varg = payload;
        } else if (EmptyLayout(ei->en->variants[vi].fields)) {
            auto tv = T();
            FixedLocal(vt, tv, "{0}");
            varg = tv;
        } else if (!snap[vi].empty()) {
            varg = snap[vi];
        } else if (!payload.empty()) {
            auto tv = T();
            FixedLocal(vt, tv, cat("*(", CT(vt), " *)(", payload, ")"));
            varg = tv;
        } else {
            auto tv = T();
            FixedLocal(vt, tv, cat(sv, ".u.v_", Sanitize(ei->en->variants[vi].name)));
            varg = tv;
        }
        // Assemble the arm's call.
        vector<string> args;
        for (size_t i = 0; i < an.size(); i++) {
            if ((int)i != pos) {
                args.insert(args.end(), shared[i].begin(), shared[i].end());
                continue;
            }
            auto pt = sp->argtypes[i];
            if (IsResz(pt)) Fail(c->line, "resizable by-value dispatch payloads are unsupported");
            if (PtrAbi(pt)) args.push_back(cat("&", varg));
            else PushArg(args, pt, IsPoolParam(sp, i), varg);
        }
        for (auto fv : ki.freevars) EmitFvArg(c->FreeVarArg(fv), args);
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (IsResz(sp->rets[i])) {
                args.push_back(dststk[i]);
                args.push_back(cat("&", dstlen[i]));
            } else if (IsBytesT(sp->rets[i])) args.push_back(dststk[i]);
            else if ((int)i != ki.cret) args.push_back(cat("&", retex[i]));
        }
        if (ki.needssp) {
            NoStackAcrossCycleCall(sp, c->line);
            NoteStackCall(sp, c->line);
            args.push_back(SpTop());
        }
        if (ki.located) args.push_back(CallerLoc(c->line));
        string argstr;
        for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
        auto reach = SyncReach(sp, args, an);
        MarkFlush(reach);
        if (ki.cret >= 0) L(retex[ki.cret], " = ", ki.cname, "(", argstr, ");");
        else L(ki.cname, "(", argstr, ");");
        MarkReload(reach);
        if (ki.hasrf) EmitRfCheck(sp);
        PopSc();
        ind--;
        L("} break;");
    }
    L("default: GS_UNREACHABLE(", LocArgs(c->line), ");");
    L("}");
    if (reprefix.t) EmitReprefix(sp0, reprefix, retex[0], "");
    return retex;
}

}  // namespace goose
