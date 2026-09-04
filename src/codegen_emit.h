// Goose compiler — codegen's function bodies (definitions of CodeGen
// members, codegen.h): named results (§7.3), the two top-caching modes,
// element-run twins (C.3), and the emission of every specialization, the
// globals (§11.1) and main.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Function bodies.

// §7.3 for a body the optimizer spliced in: an inlined call in a
// construction context still has a destination, so bind the block's named
// result to it rather than to a stack of its own -- its elements are then
// written where the value has to end up, and the `return` only completes
// the receiving metadata. Applies where the local's raw elements are
// exactly the ones the destination wants; returns the bound local, or null.
inline const VarDef *CodeGen::OpenIbNrvo(InlineBlock *ib, const Dst &d) {
    if (d.k != DK_STACK) return nullptr;
    auto vd = ib->namedresult;
    if (!vd || !IsResz(vd->type) || nrvo.count(vd)) return nullptr;
    auto ct = vd->type;
    NrvoDest nd;
    nd.stk = d.s;
    nd.inlined = true;
    if (!d.lenlv.empty()) {
        // An element-run or resizable receiver: raw elements plus a count.
        if (!d.t) return nullptr;
        auto elems = ct->kind == TY_ARRAY && d.t->kind == TY_ARRAY &&
                     (IsResz(d.t) || d.t->arr->akind == A_VAR) &&
                     TEq(ct->arr->sub, d.t->arr->sub);
        if (!TEq(ct, d.t) && !elems) return nullptr;
        nd.lenlv = d.lenlv;
        nd.fo = IsFrameObj(ct);
    } else {
        // A value receiver: the elements need a length prefix in front of
        // them. Its storage is the one the call itself would have written
        // -- the result type's, retargeted to the slot's when the receiver
        // names one (the retarget GenConstruct does at the leaf).
        auto at = ib->spec->rets[0];
        if (!at || at->kind != TY_ARRAY || at->arr->akind != A_VAR) return nullptr;
        if (d.t) {
            if (d.t->kind != TY_ARRAY || d.t->arr->akind != A_VAR ||
                !TEq(d.t->arr->sub, at->arr->sub)) return nullptr;
            at = d.t;
        }
        if (ct->kind != TY_ARRAY || !TEq(ct->arr->sub, at->arr->sub)) return nullptr;
        nd.prefix = true;
        nd.ls = LenStore(at->arr);
    }
    nrvo[vd] = nd;
    return vd;
}

// A reference to a resizable carries its stack inside the reference value,
// so a function holding one has a second spelling for a stack it may also
// name directly; that function keeps the plain memory form throughout.
inline bool CodeGen::CanCacheTops(FnSpec *sp) {
    auto ok = true;
    auto check = [&](const VarDef *v) {
        // A pool reference likewise carries its stack (and its freelist's).
        if (v && (PrefVar(v) || (v->type && v->type->kind == TY_REF &&
                                 IsResz(v->type->ref->sub))))
            ok = false;
    };
    for (auto p : sp->params) check(p);
    for (auto fv : sinfo[sp].freevars) check(fv);
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !ok) return;
        // A fat reference that is not held in a variable either -- one read
        // out of a field or an element, or returned by a call -- can be
        // dereferenced right here, and names its stack the same way.
        // Taking one of a variable does not: that value is only handed on.
        if (n->exprtype && IsFatRef(n->exprtype)) {
            auto un = Is<Unary>(n);
            if (!Is<Ident>(n) && !(un && Is<Ident>(un->child))) ok = false;
        }
        if (auto id = Is<Ident>(n)) check(id->vdef);
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) check(d);
        if (auto fl = Is<ForLoop>(n)) check(fl->vdef);
        if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) check(arm.binder);
        if (auto c = Is<Call>(n)) { walk(c->fvbody); for (auto p : c->fvparams) check(p); }
        n->Children([&](Node *ch) { walk(ch); });
    };
    walk(sp->body);
    return ok;
}

// Whether this specialization may cache the tops of the stacks it reaches
// through its reference parameters (the reftops mode; see the note above
// topcache). Every condition below rules out a second spelling of one of
// those stacks, or a way for one to move without a mark:
//  * each fat reference in the body is a parameter, named directly, so
//    `<p>.stk` is its one spelling and is in scope throughout. A reference
//    from a field, an element or an `&` here has no parameter to name;
//  * the fat parameters' root classes are distinct and every one of them
//    was rooted exactly at its call sites, which is what §10.2 proves
//    distinct: two exact roots that differ are two variables. An inexact
//    root only bounds a lifetime (§9.5), so two of them may still be one
//    stack, and two parameters in one class are not proven equal either;
//    both cases would need one cache for two stacks;
//  * nothing else the body names can be one of those stacks: no global of
//    a parameter's pointee type (a reference parameter may well be rooted
//    at one, and the specialization key does not record which), no captured
//    or by-value resizable, no return destination, no long-distance return
//    channel.
inline bool CodeGen::RefTopsOk(FnSpec *sp) {
    set<const VarDef *> fatparams, classes;
    vector<TypeExpr *> pointees;
    for (size_t i = 0; i < sp->params.size(); i++) {
        auto vd = sp->params[i];
        if (!IsFatRef(sp->argtypes[i])) continue;
        if (!vd->refrootknown || !vd->ref.root || !vd->ref.rootexact ||
            !classes.insert(vd->ref.root).second)
            return false;
        fatparams.insert(vd);
        pointees.push_back(sp->argtypes[i]->ref->sub);
    }
    if (fatparams.empty()) return false;
    for (auto pt : sp->argtypes) if (IsResz(pt)) return false;
    for (auto rt : sp->rets) if (IsBytesT(rt)) return false;
    if (fromids.count(sp->sf)) return false;
    for (auto fv : sinfo[sp].freevars)
        if (fv->reusable || (fv->type && (IsResz(fv->type) || IsFatRef(fv->type))))
            return false;
    auto ok = true;
    // A return exiting this function or an inlined body stays here; any
    // other target constructs into a gs_fdst_ channel, which is a stack
    // this body cannot place (§7.9).
    set<SFunction *> localexits { sp->sf };
    function<void(Node *)> ibs = [&](Node *n) {
        if (!n) return;
        if (auto ib = Is<InlineBlock>(n)) localexits.insert(ib->sf);
        if (auto c = Is<Call>(n)) ibs(c->fvbody);
        n->Children([&](Node *ch) { ibs(ch); });
    };
    ibs(sp->body);
    // A reference to a resizable always names a whole variable (§3.8 has no
    // spelling for a nested one), so a global with a dedicated stack can be
    // what a parameter points at only if it has that parameter's pointee
    // type; a global of any other type is a different stack.
    auto maybeparam = [&](TypeExpr *gt) {
        for (auto pt : pointees) if (TEq(pt, gt)) return true;
        return false;
    };
    auto check = [&](const VarDef *v) {
        if (!v || !v->type) return;
        if (IsFatRef(v->type) && !fatparams.count(v)) ok = false;
        if (v->isglobal && IsBytesT(v->type) && maybeparam(v->type)) ok = false;
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !ok) return;
        if (n->exprtype && IsFatRef(n->exprtype)) {
            auto id = Is<Ident>(n);
            if (!id || !fatparams.count(id->vdef)) { ok = false; return; }
        }
        if (auto id = Is<Ident>(n)) check(id->vdef);
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) check(d);
        if (auto fl = Is<ForLoop>(n)) check(fl->vdef);
        if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) check(arm.binder);
        if (auto r = Is<Return>(n); r && !localexits.count(r->target)) ok = false;
        if (auto c = Is<Call>(n)) { walk(c->fvbody); for (auto p : c->fvparams) check(p); }
        n->Children([&](Node *ch) { walk(ch); });
    };
    walk(sp->body);
    return ok;
}

inline void CodeGen::ResetFnState() {
    toporder.clear();
    growstk.clear();
    growloop.clear();
    loopparent.clear();
    loopstack.clear();
    regstk.clear();
    regloop.clear();
    topfnlocal.clear();
    refstkexprs.clear();
    cachetops = false;
    reftops = false;
    vnames.clear();
    views.clear();
    vstk.clear();
    vpool.clear();
    poolbases.clear();
    fvptr.clear();
    fnused.clear();
    nrvovars.clear();
    nrvo.clear();
    fdstsaves.clear();
    cscopes.clear();
    body.clear();
    tmpn = 0;
    stknext = 0;
    stkmax = 0;
    ind = 1;
    termjump = false;
}

// The element-run twin of a spec (C.3): same body compiled to emit raw
// elements at the destination plus a count, instead of a [len][elems]
// value. Returns its C name, or "" when the spec cannot have one.
inline string CodeGen::EnsureEr(FnSpec *sp) {
    auto it = ernames.find(sp);
    if (it != ernames.end()) return it->second;
    auto rt = sp->rets.size() == 1 ? sp->rets[0] : nullptr;
    auto ok = rt && rt->kind == TY_ARRAY && rt->arr->akind == A_VAR && sp->body &&
              !fromids.count(sp->sf);
    if (!ok) return ernames[sp] = "";
    auto name = Unique(cat(sinfo[sp].cname, "_er"));
    ernames[sp] = name;
    Append(protos, "static ", SigRet(sp), " ", name, "(", SigParams(sp, false, true),
           ");\n");
    erqueue.push_back(sp);
    return name;
}

inline void CodeGen::EmitSpec(FnSpec *sp, bool er) {
    curspec = sp;
    curinfo = &sinfo[sp];
    emiter = er;
    ResetFnState();
    cursp = curinfo->needssp;
    spexpr = cursp ? "gs_sp" : "0";
    cachetops = CanCacheTops(sp);
    reftops = !cachetops && RefTopsOk(sp);
    cachetops = cachetops || reftops;
    PushSc(SC_FN);
    auto params = SigParams(sp, true, er);
    // The spellings are only known once SigParams has named the parameters.
    if (reftops) {
        for (size_t i = 0; i < sp->params.size(); i++) {
            if (!IsFatRef(sp->argtypes[i])) continue;
            auto pn = LocalName(sp->params[i]);
            refstkexprs.insert(cat(pn, ".stk"));
            if (IsPoolParam(sp, i)) refstkexprs.insert(cat(pn, ".flstk"));
        }
    }
    // In the element-run form named results are not built at the
    // destination: the callee-side copy at return is the specified cost
    // of operating on the whole value first (§7.3).
    if (!er) DetectNrvo(sp);
    if (fromids.count(sp->sf)) {
        // This is a long-distance return target with nonfixed returns:
        // record our destinations for in-flight values (§7.9).
        EnsureFromChannels(sp->sf);
        auto tid = fromids[sp->sf];
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (!IsBytesT(sp->rets[i])) continue;
            auto sav = T();
            L("gs_stack *", sav, " = gs_fdst_", tid, "_", i, ";");
            L("gs_fdst_", tid, "_", i, " = gs_dst", i, ";");
            fdstsaves.push_back(cat("gs_fdst_", tid, "_", i, " = ", sav, ";"));
        }
    }
    for (auto st : sp->body->stmts) GenStmt(st);
    if (auto tail = sp->body->tail) {
        auto asvalue = !sp->rets.empty();
        if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) asvalue = false;
        if (Is<Guard>(tail)) asvalue = false;
        if (asvalue && !IsVoidT(tail->exprtype)) {
            vector<Node *> vals = { tail };
            GenNormalReturn(vals);
        } else {
            GenStmt(tail);
        }
    }
    if (!termjump) {
        if (curinfo->cret >= 0) {
            L("GS_UNREACHABLE(", LocArgs(sp->sf->line), ");");
            auto d = T();
            L(CT(sp->rets[curinfo->cret]), " ", d, " = {0};");
            L("return ", d, ";");
        } else {
            Epilogue("");
        }
    }
    cscopes.clear();
    // The regions and the markers resolve now that every stack this body
    // grows, and where it grows it, is known.
    PlanTopCaches();
    auto bodyout = ExpandTopMarkers(body);
    assert(bodyout.find("@@gs") == string::npos);
    Append(code, "static ", SigRet(sp), " ", er ? ernames[sp] : curinfo->cname, "(",
           params, ") {\n");
    if (stkmax > 0)
        Append(code, "    GS_ENSURE(", spexpr, " + ", stkmax, ");\n");
    // A whole-body cache loads once the stacks are known to exist; a
    // per-loop one declares and loads itself at its loop's edge.
    for (size_t i = 0; i < toporder.size(); i++)
        if (!topfnlocal[i].empty())
            Append(code, "    uint8_t *", topfnlocal[i], " = ", toporder[i], "->top;\n");
    // Every global pool's stack is reserved by gs_init_globals, which
    // main runs before anything else, so these are final on entry.
    for (auto &p : poolbases)
        Append(code, "    uint8_t *", p.second, " = ", gnames[p.first], ".base;\n");
    code += bodyout;
    code += "}\n\n";
    curspec = nullptr;
    curinfo = nullptr;
    emiter = false;
}

// ------------------------------------------------------------------
// Globals (§11.1): C globals plus dedicated data stacks for nonfixed
// ones; initializers run in declaration order before main.

// A C initializer for a compile-time constant of fixed, flat type. Fails
// for everything with a runtime component -- references, slices, ADTs,
// relative references -- leaving the value to gs_init_globals.
inline bool CodeGen::StaticInitX(Node *n, TypeExpr *t, string &out) {
    if (!n || !t || !IsFix(t) || HasRelRef(t)) return false;
    switch (t->kind) {
        case TY_INT: {
            auto i = Is<IntLit>(n);
            if (!i || t->intstorage == IS_VARINT) return false;
            out = i->val < 0 && t->intstorage == IS_U64 ? cat((uint64_t)i->val, "ULL")
                                                        : IntStr(i->val);
            return true;
        }
        case TY_FLT: {
            auto f = Is<FltLit>(n);
            if (!f) return false;
            out = FltStr(f->val, t->fltstorage == FS_F32);
            return true;
        }
        case TY_BOOL: {
            auto b = Is<BoolLit>(n);
            if (!b) return false;
            out = b->val ? "1" : "0";
            return true;
        }
        case TY_STRUCT: {
            auto sl = Is<StructLit>(n);
            auto si = SI(t);
            if (!sl || sl->sinst != si) return false;
            string s = "{ ";
            auto any = false;
            for (size_t fi = 0; fi < si->st->fields.size(); fi++) {
                if (si->st->fields[fi].ispad) continue;   // Pads zero-fill.
                Node *v = nullptr;
                for (size_t k = 0; k < sl->fieldindices.size(); k++)
                    if (sl->fieldindices[k] == (int)fi) { v = sl->inits[k].val; break; }
                if (!v && fi < si->defaults.size()) v = si->defaults[fi];
                string fx;
                if (!StaticInitX(v, si->ftypes[fi], fx)) return false;
                Append(s, any ? ", " : "", ".", Sanitize(si->st->fields[fi].name),
                       " = ", fx);
                any = true;
            }
            out = any ? s + " }" : "{ 0 }";
            return true;
        }
        case TY_ARRAY: {
            auto &a = *t->arr;
            if (a.akind != A_FIXED) return false;
            auto k = ArrSize(t->arr);
            if (k < 1 || k > MAXSTATICELEMS) return false;
            string s = "{ { ";
            if (auto str = Is<StrLit>(n)) {
                if (!IsU8T(a.sub) || (int64_t)str->val.size() != k) return false;
                for (int64_t e = 0; e < k; e++)
                    Append(s, e ? ", " : "", (int)(uint8_t)str->val[(size_t)e]);
            } else if (auto al = Is<ArrayLit>(n)) {
                string ex;
                if (al->fillval) {
                    if (!StaticInitX(al->fillval, a.sub, ex)) return false;
                    for (int64_t e = 0; e < k; e++) Append(s, e ? ", " : "", ex);
                } else {
                    if ((int64_t)al->elems.size() != k) return false;
                    for (int64_t e = 0; e < k; e++) {
                        if (!StaticInitX(al->elems[(size_t)e], a.sub, ex)) return false;
                        Append(s, e ? ", " : "", ex);
                    }
                }
            } else {
                return false;
            }
            out = s + " } }";
            return true;
        }
        default: return false;
    }
}

inline void CodeGen::EmitGlobalDecls() {
    for (auto g : ast.globals) {
        // The multi-value call form has one initializer for several defs;
        // only the one-init-per-def shape can carry a static initializer.
        auto perdef = g->inits.size() == g->defs.size();
        for (size_t di = 0; di < g->defs.size(); di++) {
            auto d = g->defs[di];
            auto name = Unique(Sanitize(d->name));
            gnames[d] = name;
            // Registered as they are created; see CacheableStk.
            auto reg = [&](const string &s) { gstkexprs.insert(s); };
            if (IsResz(d->type)) {
                EmitCoreTypes();
                auto stk = Unique(cat("gs_gstk_", name));
                Append(data, "static ", IsFrameObj(d->type) ? CT(d->type) : string("gs_rhdr"),
                       " ", name, ";\nstatic gs_stack ", stk, ";\n");
                gstks[d] = cat("(&", stk, ")");
                reg(gstks[d]);
                if (d->reusable) {
                    auto fln = Unique(cat(name, "_fl"));
                    auto flstk = Unique(cat("gs_gstk_", fln));
                    Append(data, "static gs_rhdr ", fln, ";\nstatic gs_stack ", flstk,
                           ";\n");
                    gpools[d] = { fln, cat("(&", flstk, ")") };
                    reg(gpools[d].second);
                }
            } else if (IsBytesT(d->type)) {
                auto stk = Unique(cat("gs_gstk_", name));
                Append(data, "static uint8_t *", name, ";\nstatic gs_stack ", stk,
                       ";\n");
                gstks[d] = cat("(&", stk, ")");
                reg(gstks[d]);
            } else {
                string init;
                if (perdef && !d->isvar && !PrefVar(d) &&
                    StaticInitX(g->inits[di], d->type, init)) {
                    Append(data, "static ", VarCT(d), " ", name, " = ", init, ";\n");
                    gstatic.insert(d);
                } else {
                    Append(data, "static ", VarCT(d), " ", name, ";\n");
                }
            }
        }
    }
}

inline void CodeGen::EmitGlobalInit() {
    curspec = nullptr;
    curinfo = nullptr;
    ResetFnState();
    cursp = false;
    spexpr = "0";
    PushSc(SC_FN);
    for (auto g : ast.globals) {
        if (g->inits.empty()) continue;
        PushSc(SC_STMT);
        if (g->defs.size() > 1 && g->inits.size() == 1) {
            auto c = Is<Call>(g->inits[0]);
            assert(c);
            vector<Dst> dsts;
            for (size_t i = 0; i < g->defs.size(); i++) {
                auto d = g->defs[i];
                if (IsResz(d->type)) {
                    InitGlobalStack(d);
                    dsts.push_back(Dst { DK_STACK, gstks[d], d->type, GlobalLenLv(d) });
                } else if (IsBytesT(d->type)) {
                    InitGlobalStack(d);
                    dsts.push_back(Dst { DK_STACK, gstks[d] });
                } else {
                    dsts.push_back(Dst { DK_LVALUE, gnames[d] });
                }
            }
            auto rets = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
            for (size_t i = 0; i < dsts.size() && i < rets.size(); i++)
                if (dsts[i].k == DK_LVALUE && !rets[i].empty() && rets[i] != dsts[i].s)
                    L(dsts[i].s, " = ", rets[i], ";");
        } else {
            for (size_t i = 0; i < g->defs.size(); i++) {
                auto d = g->defs[i];
                if (gstatic.count(d)) continue;   // Initialized at its declaration.
                if (IsResz(d->type)) {
                    InitGlobalStack(d);
                    GenConstruct(g->inits[i], gstks[d], d->type, GlobalLenLv(d));
                } else if (IsBytesT(d->type)) {
                    InitGlobalStack(d);
                    GenConstruct(g->inits[i], gstks[d]);
                } else if (PrefVar(d)) {
                    L(gnames[d], " = ", GenPrefVal(g->inits[i]), ";");
                } else {
                    GenAny(g->inits[i], Dst { DK_LVALUE, gnames[d] });
                }
            }
        }
        if (termjump) cscopes.back().saves.clear();
        PopSc();
        termjump = false;
    }
    EmitExitRestores(0);
    cscopes.clear();
    Append(code, "static void gs_init_globals(void) {\n");
    if (stkmax > 0) Append(code, "    GS_ENSURE(", stkmax, ");\n");
    code += body;
    code += "}\n\n";
}

// A resizable global's receiving lvalue: its header's count, or the
// whole frame object.
inline string CodeGen::GlobalLenLv(VarDef *d) {
    return IsFrameObj(d->type) ? gnames[d] : cat(gnames[d], ".len");
}

inline void CodeGen::InitGlobalStack(VarDef *d) {
    auto stk = gstks[d];
    L("gs_stack_init(", stk, ");");
    if (IsResz(d->type) && IsFrameObj(d->type)) {
        // The tail header is set when the value is constructed.
    } else if (IsResz(d->type)) {
        L(gnames[d], ".base = ", Top(stk), ";");
        L(gnames[d], ".len = 0;");
    } else {
        L(gnames[d], " = ", Top(stk), ";");
    }
    if (d->reusable) {
        auto &p = gpools[d];
        L("gs_stack_init(", p.second, ");");
        L(p.first, ".base = ", Top(p.second), ";");
        L(p.first, ".len = 0;");
    }
}

inline void CodeGen::EmitMain() {
    FnSpec *mainspec = nullptr;
    auto mit = ast.functionmap.find("main");
    if (mit != ast.functionmap.end() && !mit->second[0]->specs.empty())
        mainspec = mit->second[0]->specs[0];
    Append(code, "int main(int argc, char **argv) {\n    gs_argc = argc;\n    gs_argv = argv;\n"
                 "    gs_rt_init();\n    gs_init_globals();\n");
    if (mainspec && sinfo.count(mainspec)) {
        auto &mi = sinfo[mainspec];
        Append(code, "    ", mi.cname, "(", mi.needssp ? "0" : "", ");\n");
    }
    Append(code, "    return 0;\n}\n");
}

}  // namespace goose
