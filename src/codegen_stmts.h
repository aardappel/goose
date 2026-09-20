// Goose compiler — codegen's statements (definitions of CodeGen members,
// codegen.h): blocks and scopes, loops with their break and continue paths,
// declarations and assignment, and returns (§7.3, §7.9).
#pragma once

namespace goose {

// A Block's contents without emitting the braces/scope (the caller did).
inline void CodeGen::GenBlockInner(Block *b, Dst d, size_t first) {
    for (auto i = first; i < b->stmts.size(); ++i) GenStmt(b->stmts[i]);
    if (b->tail && !IsVoidT(b->tail->exprtype) && d.k != DK_DISCARD) GenAny(b->tail, d);
    else if (b->tail) GenAny(b->tail, Dst {});
}

// The argument bindings an inlined call's block starts with (`inline_arg`),
// made in the current scope rather than the block's, as an ordinary call
// makes its arguments in the caller's: a slice or reference argument can
// view a temporary, which the call's value may still view after the block
// exits, through the rest of the statement. Returns how many there are.
inline size_t CodeGen::GenInlineArgs(Block *b) {
    size_t n = 0;
    while (n < b->stmts.size()) {
        auto arg = Is<VarDecl>(b->stmts[n]);
        if (!arg || !arg->inline_arg) break;
        GenStmt2(arg);
        n++;
    }
    return n;
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
// Loops. Shape: for (<init>; ; <incr>) { [cond exit] body restores cnt:; }
// Goose break/continue always leave via gotos with explicit watermark
// restores; C break/continue are never emitted for them, so nesting
// inside generated switches stays safe. Every exit path restores only
// the watermarks of declarations it ran past, so the continue label
// sits after the fallthrough restores rather than sharing them.

inline void CodeGen::GenLoopBody(const function<void()> &condexit, Block *bodyb, Dst d,
                                 const string &forhead) {
    PushSc(SC_LOOP);
    auto si = (int)cscopes.size() - 1;
    cscopes[si].brklbl = Lbl();
    cscopes[si].cntlbl = Lbl();
    EnterDst(si, d);
    auto loopid = MarkLoopBegin();
    L(forhead.empty() ? "for (;;) {" : forhead);
    ind++;
    if (condexit) condexit();
    for (auto st : bodyb->stmts) GenStmt(st);
    if (bodyb->tail) GenStmt(bodyb->tail);
    auto &sc = cscopes.back();
    EmitRestores(sc);
    if (sc.usedcnt) L(sc.cntlbl, ":;");
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
    if (val) GenExitValue(val, si);
    EmitExitRestores(si);
    cscopes[si].usedbrk = true;
    L("goto ", cscopes[si].brklbl, ";");
    termjump = true;
}

// ------------------------------------------------------------------
// Exits delivering a value. A return, a break with a value and the catch of
// a long-distance return build their value at the top of the stack they
// deliver it to, and the receiver expects it where that top was when the
// function, inlined body or loop was entered. What a construction still
// under way has placed there since -- part of a literal, a claimed length
// prefix, an inlined callee's named result -- or, below a `return from`,
// what the unwound calls had built in the destination the target handed
// them, sits in front of the value: it is moved down over that, and the
// top follows it.

// Scope si receives exit values at d: what is open on its stack now, and
// the point its top on entry gets declared at if an exit needs it.
inline void CodeGen::EnterDst(int si, const Dst &d) {
    auto &s = cscopes[si];
    s.dst = d;
    if (d.k != DK_STACK) return;
    s.open0 = openat[d.s];
    s.topat = body.size();
    s.topind = ind;
}

// Declares `name` as stk's top at body offset `at`, where its scope begins,
// and shifts the offsets recorded by scopes that begin after it.
inline void CodeGen::DeclareTop0(size_t at, int indent, const string &name, const string &stk) {
    string line((size_t)indent * 4, ' ');
    Append(line, "uint8_t *", name, " = ", Top(stk), ";\n");
    body.insert(at, line);
    for (auto &s : cscopes) if (s.topat > at) s.topat += line.size();
}

inline string CodeGen::ScopeTop0(int si) {
    auto &s = cscopes[si];
    if (s.top0.empty()) {
        s.top0 = T();
        DeclareTop0(s.topat, s.topind, s.top0, s.dst.s);
    }
    return s.top0;
}

inline string CodeGen::DstTop0(size_t i) {
    if (dsttop0.size() <= i) dsttop0.resize(i + 1);
    if (dsttop0[i].empty()) {
        dsttop0[i] = T();
        DeclareTop0(0, 1, dsttop0[i], cat("gs_dst", i));
    }
    return dsttop0[i];
}

// An inlined body's named result reaching the destination it was bound to
// (OpenIbNrvo) is in place already: building it there writes only its
// count or its prefix. Returns its binding, or null for any other value.
inline const CodeGen::NrvoDest *CodeGen::NrvoBoundAt(Node *val, const string &stk,
                                                      const string &lenlv) {
    auto id = Is<Ident>(val);
    if (!id || !id->vdef) return nullptr;
    auto it = nrvo.find(id->vdef);
    if (it == nrvo.end() || !it->second.inlined || it->second.stk != stk ||
        it->second.lenlv != lenlv)
        return nullptr;
    return &it->second;
}

// Where an exit's value will start on stk, taken before it is built when a
// construction opened there since the exit's scope was entered (open0) may
// have placed something in front of it; "" when none can have.
inline string CodeGen::ExitStart(Node *val, const string &stk, const string &lenlv, int open0) {
    if (openat[stk] <= open0 || NrvoBoundAt(val, stk, lenlv)) return "";
    auto start = T();
    L("uint8_t *", start, " = ", Top(stk), ";");
    return start;
}

// The value built from `start` up to stk's top moves down to top0; a frame
// object's tail header, which lenlv receives, follows its elements.
inline void CodeGen::LandValue(const string &stk, const string &top0, const string &start,
                               TypeExpr *t, const string &lenlv) {
    L("if (", start, " != ", top0, ") {");
    ind++;
    auto n = T();
    L("int64_t ", n, " = (int64_t)(", Top(stk), " - ", start, ");");
    L("memmove(", top0, ", ", start, ", (size_t)", n, ");");
    L(TopW(stk), " = ", top0, " + ", n, ";");
    if (t && IsFrameObj(t)) {
        auto th = FoTailHdr(t, lenlv);
        L(th, ".base = ", top0, " + (", th, ".base - ", start, ");");
    }
    ind--;
    L("}");
}

// A break's value, or a return's leaving an inlined body, for scope si.
inline void CodeGen::GenExitValue(Node *val, int si) {
    auto d = cscopes[si].dst;
    auto start = d.k == DK_STACK ? ExitStart(val, d.s, d.lenlv, cscopes[si].open0) : "";
    GenAny(val, d);
    if (!start.empty())
        LandValue(d.s, ScopeTop0(si), start, d.t ? d.t : val->exprtype, d.lenlv);
}

// ------------------------------------------------------------------
// Declarations and assignment.

inline void CodeGen::BindLocal(VarDef *d, Node *init, bool forlocal) {
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
            stk = AllocStk(forlocal);
        }
        if (IsFrameObj(t)) {
            L(CT(t), " ", name, ";");
            if (nit == nrvo.end()) SaveBase(forlocal, stk, cat(FoTailHdr(t, name), ".base"));
            vstk[d] = stk;
            GenConstruct(init, stk, t, name);
            return;
        }
        L("gs_rhdr ", name, " = { ", Top(stk), ", 0 };");
        // The destination outlives us: no watermark to restore there.
        if (nit == nrvo.end()) SaveBase(forlocal, stk, cat(name, ".base"));
        vstk[d] = stk;
        if (d->reusable) {
            // Companion freelist: free slot indices on their own stack.
            auto flstk = AllocStk(forlocal);
            auto fln = Unique2(cat(name, "_fl"));
            L("gs_rhdr ", fln, " = { ", Top(flstk), ", 0 };");
            SaveBase(forlocal, flstk, cat(fln, ".base"));
            vpool[d] = { fln, flstk };
        }
        GenConstruct(init, stk, t, cat(name, ".len"));
        return;
    }
    if (IsBytesT(t)) {
        assert(init);
        auto nit = nrvo.find(d);
        auto stk = nit != nrvo.end() ? nit->second.stk : AllocStk(forlocal);
        L("uint8_t *", name, " = ", Top(stk), ";");
        // A named result lives at the destination, which outlives us.
        if (nit == nrvo.end()) SaveBase(forlocal, stk, name);
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
    // A single call forwards all its return values (§7.1). One the checker
    // adapted to our return type (a variant to its ADT, say) arrives in the
    // callee's type first and converts into our channel.
    if (vals.size() == 1 && sp->rets.size() > 1) {
        if (auto c = Is<Call>(vals[0]); c && c->rettypes.size() == sp->rets.size()) {
            vector<Dst> dsts;
            vector<string> tmps(sp->rets.size());
            vector<Loc> arrived(sp->rets.size());
            for (size_t i = 0; i < sp->rets.size(); i++) {
                auto ct = c->rettypes[i];
                if (!TEq(ct, sp->rets[i])) {
                    auto &lv = arrived[i];
                    string stk;
                    if (IsResz(ct)) {
                        auto h = RzTemp(ct, stk);
                        lv = RzTempLoc(ct, h, stk);
                        dsts.push_back(Dst { DK_STACK, stk, ct, RzLenLv(ct, h) });
                    } else if (IsBytesT(ct)) {
                        lv.t = ct;
                        lv.s = BytesTemp(stk);
                        lv.stk = stk;
                        dsts.push_back(Dst { DK_STACK, stk, ct });
                    } else {
                        lv.t = ct;
                        lv.val = true;
                        lv.s = T();
                        L(CT(ct), " ", lv.s, ";");
                        dsts.push_back(Dst { DK_LVALUE, lv.s, ct });
                    }
                    continue;
                }
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
            vector<string> starts(sp->rets.size());
            for (size_t i = 0; i < sp->rets.size(); i++)
                if (IsBytesT(sp->rets[i]))
                    starts[i] = ExitStart(c, cat("gs_dst", i), "", 0);
            auto rets = EmitCall(c, dsts[0], &dsts);
            for (size_t i = 0; i < sp->rets.size(); i++) {
                auto rt = sp->rets[i];
                if (auto lv = arrived[i]; lv.t) {
                    // Where the call left the value (its C result, say).
                    if (!IsResz(lv.t) && i < rets.size() && !rets[i].empty()) lv.s = rets[i];
                    auto dst = cat("gs_dst", i);
                    if (IsResz(rt)) {
                        ConstructFromLoc(lv, rt, dst, cat("(*gs_rl", i, ")"), c->line);
                    } else if (IsBytesT(rt)) {
                        ConstructFromLoc(lv, rt, dst, "", c->line);
                    } else {
                        auto x = LoadLoc(lv, rt, c->line);
                        if ((int)i == si.cret) retv = x;
                        else L("*gs_r", i, " = ", x, ";");
                    }
                    continue;
                }
                if (IsBytesT(rt)) continue;
                auto v = i < rets.size() && !rets[i].empty() ? rets[i] : tmps[i];
                if ((int)i == si.cret) retv = v;
                else L("*gs_r", i, " = ", v, ";");
            }
            for (size_t i = 0; i < sp->rets.size(); i++)
                if (!starts[i].empty())
                    LandValue(cat("gs_dst", i), DstTop0(i), starts[i], sp->rets[i],
                              cat("(*gs_rl", i, ")"));
            Epilogue(retv);
            return;
        }
    }
    assert(vals.size() == sp->rets.size());
    for (size_t i = 0; i < vals.size(); i++) {
        auto rt = sp->rets[i];
        auto id = Is<Ident>(vals[i]);
        auto named = id && id->vdef ? nrvo.find(id->vdef) : nrvo.end();
        auto dst = cat("gs_dst", i);
        // Inline destinations belong to their own block; only DetectNrvo's
        // entries alias this function's return destinations.
        auto direct = named != nrvo.end() && !named->second.inlined && named->second.stk == dst;
        if (IsResz(rt) || (emiter && i == 0)) {
            if (direct) {
                // Built at the destination; only the count (or the frame
                // object) travels.
                if (IsFrameObj(rt)) L("*gs_rl", i, " = ", HdrLv(id->vdef), ";");
                else L("*gs_rl", i, " = ", HdrLv(id->vdef), ".len;");
                continue;
            }
            auto lenlv = cat("(*gs_rl", i, ")");
            auto start = ExitStart(vals[i], dst, lenlv, 0);
            GenConstruct(vals[i], dst, rt, lenlv);
            if (!start.empty()) LandValue(dst, DstTop0(i), start, rt, lenlv);
        } else if (IsBytesT(rt)) {
            if (direct) {
                // In place already. A resizable local's elements sit at
                // the destination behind the length prefix reserved for
                // them at its declaration; the count goes in there now.
                if (IsResz(id->vdef->type)) EmitNrvoFinish(named->second);
                continue;
            }
            auto start = ExitStart(vals[i], dst, "", 0);
            GenConstruct(vals[i], dst, rt);
            if (!start.empty()) LandValue(dst, DstTop0(i), start, rt, "");
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
    auto t = r->targetspec;
    assert(t);
    auto tid = fromids[t];
    EnsureFromChannels(t);
    auto &rets = t->rets;
    assert(r->vals.size() == rets.size() ||
           (r->vals.size() == 1 && Is<Call>(r->vals[0])));
    // The values land at the channels' tops, which is not where the target's
    // caller expects them when anything was built in its destination since:
    // the catch moves them down from here (EmitRfCheck).
    for (size_t i = 0; i < rets.size(); i++)
        if (IsBytesT(rets[i]))
            L("gs_fval_", tid, "_", i, " = ", Top(cat("gs_fdst_", tid, "_", i)), ";");
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
        EmitCallInto(c, dsts);
    }
    assert(curinfo && curinfo->hasrf);
    PropagateReturn(cat(tid));
}

inline void CodeGen::EnsureFromChannels(FnSpec *t) {
    if (fromemitted.count(t)) return;
    fromemitted.insert(t);
    auto tid = fromids[t];
    auto &rets = t->rets;
    for (size_t i = 0; i < rets.size(); i++) {
        if (IsFix(rets[i])) {
            Append(data, "static GS_TLS ", CT(rets[i]), " gs_lret_", tid, "_", i, ";\n");
            continue;
        }
        Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
        Append(data, "static GS_TLS uint8_t *gs_fval_", tid, "_", i, ";\n");
        if (IsResz(rets[i]))
            Append(data, "static GS_TLS ", IsFrameObj(rets[i]) ? CT(rets[i]) : string("int64_t"),
                   " gs_lret_", tid, "_", i, ";\n");
    }
}

// ------------------------------------------------------------------
// Calls. Returns one entry per return value: a C expression for fixed
// values, the value's base pointer for bytes-class ones. d0 is the
// preferred destination for the first return (in-place construction);
// alldst supplies destinations for every return (multi-value receives).

// The first return value adjusted to the context it is received in: a
// reference received in a value context loads the pointee, and an array
// of another kind constructs the static-capacity limited array or slice
// expected. `want` is the receiver's type where it knows it; the call's
// checked type otherwise.
inline string CodeGen::CallVal0(Call *c, const string &r0, TypeExpr *want) {
    auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
    auto et = want ? want : c->exprtype;
    // copy(x) yields its value in the context's representation already.
    if (c->builtin == B_COPY) return r0;
    if (rt && et && IsStaticLimited(et)) {
        auto st = IsPlainRef(rt) ? rt->ref->sub : rt;
        if ((st->kind == TY_ARRAY || st->kind == TY_SLICE) && !TEq(st, et))
            return AdaptToFixed(CallResLoc(c, r0), et, c->line);
    }
    auto st = rt && IsPlainRef(rt) ? rt->ref->sub : rt;
    if (st && st->kind == TY_ARRAY && et && et->kind == TY_SLICE) {
        // An array result, or the array a reference result points at, passed
        // where a slice is expected (§3.10): sliced whole where it lies. A
        // result lives to the end of the statement like any temporary; one
        // that arrives as a C value is held in a named temp for the slice to
        // point into.
        auto lv = CallResLoc(c, r0);
        if (lv.val && st == rt) {
            auto tv = T();
            L(CT(rt), " ", tv, " = ", r0, ";");
            lv.s = tv;
        }
        return LoadLoc(lv, et, c->line);
    }
    if (rt && rt->kind == TY_REF && rt->ref->lenstorage < 0 && et &&
        et->kind != TY_REF && et->kind != TY_VOID) {
        if (IsVarintT(rt->ref->sub)) return cat("gs_zig_read(", r0, ")");
        if (IsResz(rt->ref->sub) || IsBytesT(rt->ref->sub)) return r0;
        return cat("(*", r0, ")");
    }
    return r0;
}

// The first return value as a location, in the representation its return
// type arrives in: a header for a resizable, a base pointer for another
// bytes-class value, a C value otherwise; a reference is its pointee.
inline CodeGen::Loc CodeGen::CallResLoc(Call *c, const string &r0) {
    auto rt = c->rettypes[0];
    Loc lv;
    if (IsPlainRef(rt)) {
        auto sub = rt->ref->sub;
        if (IsResz(sub)) return FatRefLoc(r0, sub);
        if (IsBytesT(sub)) return BytesLoc(r0, sub, Loc {});
        lv.t = sub;
        lv.val = true;
        lv.s = cat("(*", r0, ")");
        return lv;
    }
    if (IsResz(rt)) return RzTempLoc(rt, r0, "");
    if (IsBytesT(rt)) return BytesLoc(r0, rt, Loc {});
    lv.t = rt;
    lv.val = true;
    lv.s = r0;
    return lv;
}

}  // namespace goose
