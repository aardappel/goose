// Goose compiler — codegen's statements (definitions of CodeGen members,
// codegen.h): blocks and scopes, loops with their break and continue paths,
// declarations and assignment, and returns (§7.3, §7.9).
#pragma once

namespace goose {

// The last emitted statement left via goto/return.

// A Block's contents without emitting the braces/scope (the caller did).
inline void CodeGen::GenBlockInner(Block *b, Dst d) {
    for (auto st : b->stmts) GenStmt(st);
    if (b->tail && !IsVoidT(b->tail->exprtype) && d.k != DK_DISCARD) GenAny(b->tail, d);
    else if (b->tail) GenAny(b->tail, Dst {});
}

inline void CodeGen::GenStmt(Node *n) {
    PushSc(SC_STMT);
    termjump = false;
    GenStmt2(n);
    if (termjump) cscopes.back().saves.clear();   // Nothing runs after a jump.
    PopSc();
    termjump = false;
}

// ------------------------------------------------------------------
// Loops. Shape: for (<init>; ; <incr>) { [cond exit] body cnt:; restores }
// Goose break/continue always leave via gotos with explicit watermark
// restores; C break/continue are never emitted for them, so nesting
// inside generated switches stays safe.

inline void CodeGen::GenLoopBody(const function<void()> &condexit, Block *bodyb, Dst d,
                                 const string &forhead) {
    PushSc(SC_LOOP);
    auto si = (int)cscopes.size() - 1;
    cscopes[si].brklbl = Lbl();
    cscopes[si].cntlbl = Lbl();
    cscopes[si].dst = d;
    auto loopid = MarkLoopBegin();
    L(forhead.empty() ? "for (;;) {" : forhead);
    ind++;
    if (condexit) condexit();
    for (auto st : bodyb->stmts) GenStmt(st);
    if (bodyb->tail) GenStmt(bodyb->tail);
    auto &sc = cscopes.back();
    if (sc.usedcnt) L(sc.cntlbl, ":;");
    EmitRestores(sc);
    ind--;
    L("}");
    auto usedbrk = sc.usedbrk;
    auto brklbl = sc.brklbl;
    cscopes.back().saves.clear();   // Restores already emitted in-loop.
    PopSc();
    if (usedbrk) L(brklbl, ":;");
    MarkLoopEnd(loopid);
    termjump = false;
}

inline void CodeGen::GenBreakPath(Node *val) {
    auto si = -1;
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (cscopes[i].kind == SC_LOOP || cscopes[i].kind == SC_BLOCK) { si = i; break; }
        if (cscopes[i].kind == SC_FN) break;
    }
    assert(si >= 0);
    if (val) GenAny(val, cscopes[si].dst);
    EmitExitRestores(si);
    cscopes[si].usedbrk = true;
    L("goto ", cscopes[si].brklbl, ";");
    termjump = true;
}

// ------------------------------------------------------------------
// Declarations and assignment.

inline void CodeGen::BindLocal(VarDef *d, Node *init) {
    auto name = LocalName(d);
    auto t = d->type;
    if (IsResz(t)) {
        assert(init);
        EmitCoreTypes();
        string stk;
        auto nit = nrvo.find(d);
        if (nit != nrvo.end()) {
            auto &nd = nit->second;
            stk = nd.stk;
            // The receiving prefix goes in front of the elements, so its
            // bytes are claimed before the first one lands. A varint takes
            // one byte here and is widened at the return only if the count
            // does not fit (EmitPrefixPatch).
            if (nd.prefix) {
                nd.pref = T();
                L("uint8_t *", nd.pref, " = ", Top(stk), ";");
                Bump(stk, cat(PrefixBytes(nd.ls)));
            }
            nd.hdr = name;
        } else {
            stk = AllocStk(true);
        }
        if (IsFrameObj(t)) {
            L(CT(t), " ", name, ";");
            if (nit == nrvo.end()) SaveBase(true, stk, cat(FoTailHdr(t, name), ".base"));
            vstk[d] = stk;
            GenConstruct(init, stk, t, name);
            return;
        }
        L("gs_rhdr ", name, " = { ", Top(stk), ", 0 };");
        // The destination outlives us: no watermark to restore there.
        if (nit == nrvo.end()) SaveBase(true, stk, cat(name, ".base"));
        vstk[d] = stk;
        if (d->reusable) {
            // Companion freelist: free slot indices on their own stack.
            auto flstk = AllocStk(true);
            auto fln = Unique2(cat(name, "_fl"));
            L("gs_rhdr ", fln, " = { ", Top(flstk), ", 0 };");
            SaveBase(true, flstk, cat(fln, ".base"));
            vpool[d] = { fln, flstk };
        }
        GenConstruct(init, stk, t, cat(name, ".len"));
        return;
    }
    if (IsBytesT(t)) {
        assert(init);
        auto stk = AllocStk(true);
        L("uint8_t *", name, " = ", Top(stk), ";");
        SaveBase(true, stk, name);
        vstk[d] = stk;
        GenConstruct(init, stk, t);
        return;
    }
    if (!init) {
        L(VarCT(d), " ", name, ";");
        return;
    }
    if (PrefVar(d)) {
        L("gs_pref ", name, " = ", GenPrefVal(init), ";");
        return;
    }
    // Literals holding relative references construct into the variable
    // itself rather than through an initializer copy.
    if (IsCtl(init) || Is<Call>(init) ||
        ((Is<StructLit>(init) || Is<ArrayLit>(init)) && HasRelRef(t))) {
        L(CT(t), " ", name, ";");
        GenAny(init, Dst { DK_LVALUE, name, t });
        return;
    }
    L(CT(t), " ", name, " = ", GenXD(init, t), ";");
}

// A gs_pref value from a &pool expression or another pool reference.
inline string CodeGen::GenPrefVal(Node *n) {
    if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
        if (auto id = Is<Ident>(u->child); id && id->vdef && id->vdef->reusable) {
            auto vd = id->vdef;
            auto pit = vpool.find(vd);
            auto git = gpools.find(vd);
            assert(pit != vpool.end() || git != gpools.end());
            auto &pl = pit != vpool.end() ? pit->second : git->second;
            auto t = T();
            L("gs_pref ", t, " = { &", HdrLv(vd), ", ", VStkOf(vd), ", &", pl.first,
              ", ", pl.second, " };");
            return t;
        }
        auto lv = GenLoc(u->child);
        if (lv.t->kind != TY_REF || !lv.ispref)
            Fail(n->line, "cannot form a reusable pool reference here");
        return lv.s;
    }
    if (auto id = Is<Ident>(n); id && id->vdef && PrefVar(id->vdef)) {
        return fvptr.count(id->vdef) ? cat("(*", VName(id->vdef), ")")
                                     : VName(id->vdef);
    }
    Fail(n->line, "cannot form a reusable pool reference from this expression");
}

inline void CodeGen::GenRelAssign(Loc lv, Node *rhs, Line ln) {
    auto rv = GenX(rhs);
    // Varint-width relative references are construction-only (typechecked).
    assert(lv.t->ref->lenstorage != IS_VARINT);
    EmitRelStoreAt(BytesAddrOf(lv), lv.t, rv, ln, true);
}

inline void CodeGen::GenRebind(Assign *a, Loc lv) {
    if (lv.t->kind == TY_REF && lv.t->ref->lenstorage >= 0) {
        GenRelAssign(lv, a->rhs, a->line);
        return;
    }
    assert(lv.val);
    if (Is<NullLit>(a->rhs)) {
        if (IsResz(lv.t->ref->sub)) L("memset(&", lv.s, ", 0, sizeof(", lv.s, "));");
        else L(lv.s, " = NULL;");
        return;
    }
    L(lv.s, " = ", GenX(a->rhs), ";");
}

// ------------------------------------------------------------------
// Returns: normal, forwarding a multi-value call, exiting an inlined
// body, and long-distance (§7.9).

inline void CodeGen::GenNormalReturn(const vector<Node *> &vals) {
    auto sp = curspec;
    assert(sp);
    auto &si = *curinfo;
    string retv;
    // A single call forwards all its return values (§7.1).
    if (vals.size() == 1 && sp->rets.size() > 1) {
        if (auto c = Is<Call>(vals[0]); c && c->rettypes.size() == sp->rets.size()) {
            vector<Dst> dsts;
            vector<string> tmps(sp->rets.size());
            for (size_t i = 0; i < sp->rets.size(); i++) {
                if (IsResz(sp->rets[i])) {
                    dsts.push_back(Dst { DK_STACK, cat("gs_dst", i), sp->rets[i],
                                         cat("(*gs_rl", i, ")") });
                } else if (IsBytesT(sp->rets[i])) {
                    dsts.push_back(Dst { DK_STACK, cat("gs_dst", i) });
                } else {
                    tmps[i] = T();
                    L(CT(sp->rets[i]), " ", tmps[i], ";");
                    dsts.push_back(Dst { DK_LVALUE, tmps[i] });
                }
            }
            auto rets = EmitCall(c, dsts[0], &dsts);
            for (size_t i = 0; i < sp->rets.size(); i++) {
                if (IsBytesT(sp->rets[i])) continue;
                auto v = i < rets.size() && !rets[i].empty() ? rets[i] : tmps[i];
                if ((int)i == si.cret) retv = v;
                else L("*gs_r", i, " = ", v, ";");
            }
            Epilogue(retv);
            return;
        }
    }
    assert(vals.size() == sp->rets.size());
    for (size_t i = 0; i < vals.size(); i++) {
        auto rt = sp->rets[i];
        if (IsResz(rt) || (emiter && i == 0)) {
            auto id = Is<Ident>(vals[i]);
            if (id && id->vdef && nrvovars.count(id->vdef)) {
                // Built at the destination; only the count (or the frame
                // object) travels.
                if (IsFrameObj(rt)) L("*gs_rl", i, " = ", HdrLv(id->vdef), ";");
                else L("*gs_rl", i, " = ", HdrLv(id->vdef), ".len;");
                continue;
            }
            GenConstruct(vals[i], cat("gs_dst", i), rt, cat("(*gs_rl", i, ")"));
        } else if (IsBytesT(rt)) {
            auto id = Is<Ident>(vals[i]);
            if (id && id->vdef && nrvovars.count(id->vdef)) {
                // In place already. A resizable local's elements sit at
                // the destination behind the length prefix reserved for
                // them at its declaration; the count goes in there now.
                if (IsResz(id->vdef->type)) EmitNrvoFinish(nrvo[id->vdef]);
                continue;
            }
            GenConstruct(vals[i], cat("gs_dst", i), rt);
        } else if ((int)i == si.cret) {
            retv = T();
            L(CT(rt), " ", retv, ";");
            GenAny(vals[i], Dst { DK_LVALUE, retv, rt });
        } else {
            auto tv = T();
            L(CT(rt), " ", tv, ";");
            GenAny(vals[i], Dst { DK_LVALUE, tv, rt });
            L("*gs_r", i, " = ", tv, ";");
        }
    }
    Epilogue(retv);
}

// Writes the count into the prefix bytes reserved at `pref`, in front of
// the `count` elements that run from `elems` to the top of `stk`. Fixed
// widths patch in place; a varint moves the elements up by one or two
// bytes only when the count outgrew its reserved byte.
inline void CodeGen::EmitPrefixPatch(const string &pref, IntStorage ls, const string &stk,
                                     const string &count, const string &elems) {
    if (ls != IS_VARINT) {
        L("*(", IntCT(ls), " *)", pref, " = (", IntCT(ls), ")(", count, ");");
        return;
    }
    L("if ((", count, ") < 128) {");
    ind++;
    L("*", pref, " = (uint8_t)(", count, ");");
    ind--;
    L("} else {");
    ind++;
    auto tmp = T(), ms = T();
    L("uint8_t ", tmp, "[10];");
    L("int64_t ", ms, " = gs_uleb_write(", tmp, ", (uint64_t)(", count, "));");
    L("memmove((", elems, ") + ", ms, " - 1, ", elems, ", (size_t)(", Top(stk), " - (",
      elems, ")));");
    L("memcpy(", pref, ", ", tmp, ", (size_t)", ms, ");");
    L(TopW(stk), " += ", ms, " - 1;");
    ind--;
    L("}");
}

// Completes a named result whose elements are already at the destination:
// the count goes to the receiving header, or into the prefix reserved in
// front of the elements when the destination is a value slot.
inline void CodeGen::EmitNrvoFinish(const NrvoDest &nd) {
    if (nd.fo) { L(nd.lenlv, " = ", nd.hdr, ";"); return; }
    if (!nd.prefix) { L(nd.lenlv, " = ", nd.hdr, ".len;"); return; }
    EmitPrefixPatch(nd.pref, nd.ls, nd.stk, cat(nd.hdr, ".len"), cat(nd.hdr, ".base"));
}

inline void CodeGen::Epilogue(const string &retv) {
    EmitExitRestores(0);
    MarkFlush();   // The caller and every later callee read stack tops from memory.
    for (auto &s : fdstsaves) L(s);
    L(retv.empty() ? "return;" : cat("return ", retv, ";"));
    termjump = true;
}

// The dummy C return value used on propagate paths. `rfval` names the
// target to propagate to, or is empty when gs_rf already holds it (an
// intermediate frame passing on a propagation it did not start).
inline void CodeGen::PropagateReturn(const string &rfval) {
    EmitExitRestores(0);
    MarkFlush();
    for (auto &s : fdstsaves) L(s);
    if (!rfval.empty()) L("gs_rf = ", rfval, ";");
    if (curinfo->cret >= 0) {
        auto d = T();
        L(CT(curspec->rets[curinfo->cret]), " ", d, " = {0};");
        L("return ", d, ";");
    } else {
        L("return;");
    }
    termjump = true;
}

// Long-distance return site: values into the target's channels, then
// propagate the discriminant.
inline void CodeGen::GenFromReturn(Return *r) {
    auto t = r->target;
    auto tid = fromids[t];
    EnsureFromChannels(t);
    auto &rets = FromRets(t);
    assert(r->vals.size() == rets.size() ||
           (r->vals.size() == 1 && Is<Call>(r->vals[0])));
    if (r->vals.size() == rets.size()) {
        for (size_t i = 0; i < rets.size(); i++) {
            if (IsResz(rets[i]))
                GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i],
                             cat("gs_lret_", tid, "_", i));
            else if (IsBytesT(rets[i]))
                GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i]);
            else GenAny(r->vals[i], Dst { DK_LVALUE, cat("gs_lret_", tid, "_", i), rets[i] });
        }
    } else {
        // Forward one call's values into the channels.
        auto c = Is<Call>(r->vals[0]);
        vector<Dst> dsts;
        for (size_t i = 0; i < rets.size(); i++) {
            if (IsResz(rets[i]))
                dsts.push_back(Dst { DK_STACK, cat("gs_fdst_", tid, "_", i), rets[i],
                                     cat("gs_lret_", tid, "_", i) });
            else if (IsBytesT(rets[i]))
                dsts.push_back(Dst { DK_STACK, cat("gs_fdst_", tid, "_", i) });
            else
                dsts.push_back(Dst { DK_LVALUE, cat("gs_lret_", tid, "_", i) });
        }
        auto cr = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
        for (size_t i = 0; i < dsts.size() && i < cr.size(); i++)
            if (dsts[i].k == DK_LVALUE && !cr[i].empty() && cr[i] != dsts[i].s)
                L(dsts[i].s, " = ", cr[i], ";");
    }
    assert(curinfo && curinfo->hasrf);
    PropagateReturn(cat(tid));
}

inline vector<TypeExpr *> &CodeGen::FromRets(SFunction *t) {
    auto it = fromrets.find(t);
    if (it != fromrets.end()) return *it->second;
    for (auto sp : t->specs)
        if (sp->live) { fromrets[t] = &sp->rets; return sp->rets; }
    // A target none of whose specs are live: any spec's types will do.
    assert(!t->specs.empty());
    fromrets[t] = &t->specs[0]->rets;
    return t->specs[0]->rets;
}

inline void CodeGen::EnsureFromChannels(SFunction *t) {
    if (fromemitted.count(t)) return;
    fromemitted.insert(t);
    auto tid = fromids[t];
    auto &rets = FromRets(t);
    for (size_t i = 0; i < rets.size(); i++) {
        if (IsResz(rets[i])) {
            Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
            Append(data, "static GS_TLS int64_t gs_lret_", tid, "_", i, ";\n");
        } else if (IsBytesT(rets[i])) {
            Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
        } else {
            Append(data, "static GS_TLS ", CT(rets[i]), " gs_lret_", tid, "_", i, ";\n");
        }
    }
}

// ------------------------------------------------------------------
// Calls. Returns one entry per return value: a C expression for fixed
// values, the value's base pointer for bytes-class ones. d0 is the
// preferred destination for the first return (in-place construction);
// alldst supplies destinations for every return (multi-value receives).

// The first return value adjusted for reference decay: a call that
// returns a reference received in a value context loads the pointee.
inline string CodeGen::CallVal0(Call *c, const string &r0) {
    auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
    auto et = c->exprtype;
    if (rt && rt->kind == TY_REF && rt->ref->lenstorage < 0 && et &&
        et->kind != TY_REF && et->kind != TY_VOID) {
        if (IsResz(rt->ref->sub) || IsBytesT(rt->ref->sub)) return r0;
        return cat("(*", r0, ")");
    }
    return r0;
}

}  // namespace goose
