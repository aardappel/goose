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
    // and slide its length prefix out. Specialization calls route to an
    // element-run twin (or the same fallback) inside EmitSpecCall, and a
    // function value's tail construction honors the destination as-is.
    if (d0.k == DK_STACK && !d0.lenlv.empty() && d0.t && d0.t->kind == TY_ARRAY &&
        d0.t->arr->akind == A_VAR && (c->builtin >= 0 || !c->dispatch.empty())) {
        auto base = T();
        L("uint8_t *", base, " = ", Top(d0.s), ";");
        auto rets = c->builtin >= 0 ? EmitBuiltin(c, Dst { DK_STACK, d0.s })
                                    : EmitDispatch(c, Dst { DK_STACK, d0.s }, alldst);
        EmitSlidePrefix(base, LenStore(d0.t->arr), d0.s, d0.lenlv);
        return rets;
    }
    if (c->builtin >= 0) return EmitBuiltin(c, d0);
    if (c->fvbody) return EmitFvCall(c, d0);
    if (!c->dispatch.empty()) return EmitDispatch(c, d0, alldst);
    assert(c->spec);
    if (c->spec->sf->isextern) return EmitExternCall(c, c->spec);
    return EmitSpecCall(c, c->spec, d0, alldst);
}

// A call to an extern fn (§7.10): the C function directly, each argument
// in its C type, no calling-convention extras; a fixed result lands in
// a temporary like any other C value.
inline vector<string> CodeGen::EmitExternCall(Call *c, FnSpec *sp) {
    usedexterns.insert(sp);
    auto an = CallArgNodes(c, sp->argtypes.size());
    string argstr;
    for (size_t i = 0; i < an.size(); i++) {
        auto pt = sp->argtypes[i];
        if (i) argstr += ", ";
        argstr += pt->kind == TY_REF ? GenX(an[i]) : GenXD(an[i], pt);
    }
    if (sp->rets.empty()) {
        L(sp->sf->cname, "(", argstr, ");");
        return {};
    }
    auto r0 = T();
    L(CT(sp->rets[0]), " ", r0, " = ", sp->sf->cname, "(", argstr, ");");
    return { r0 };
}

// The C prototype of an extern fn, from its Goose declaration (§7.10).
inline string CodeGen::ExternProto(FnSpec *sp) {
    string s = sp->rets.empty() ? string("void") : CT(sp->rets[0]);
    Append(s, " ", sp->sf->cname, "(");
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        if (i) s += ", ";
        s += CT(sp->argtypes[i]);
    }
    if (sp->argtypes.empty()) s += "void";
    s += ");\n";
    return s;
}

inline vector<Node *> CodeGen::CallArgNodes(Call *c, size_t nparams) {
    vector<Node *> an;
    if (auto dd = Is<Dot>(c->callee)) an.push_back(dd->obj);
    for (auto a : c->args) an.push_back(a);
    an.resize(nparams);   // Function-value arguments are compile-time only.
    return an;
}

// One argument by the parameter's calling convention; appends to args.
inline void CodeGen::EmitArg(FnSpec *sp, size_t i, Node *node, vector<string> &args) {
    auto pt = sp->argtypes[i];
    if (IsPoolParam(sp, i)) {
        args.push_back(GenPrefVal(node));
        return;
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
        return;
    }
    if (IsBytesT(pt)) {
        // By-value nonfixed argument: construct into a fresh slot (§7.2).
        string stk;
        auto base = BytesTemp(stk);
        GenConstruct(node, stk, pt);
        args.push_back(base);
        return;
    }
    args.push_back(GenXD(node, pt));
}

// A free variable of the callee, from the caller's frame (or passed on).
inline void CodeGen::EmitFvArg(const VarDef *fv, vector<string> &args) {
    auto name = VName(fv);
    auto ours = curspec && fv->ownerspec == curspec;
    if (fv->reusable) {
        // A pool local of ours: assemble; a passed-through one: forward.
        if (ours) {
            auto &p = vpool[fv];
            auto t = T();
            L("gs_pref ", t, " = { &", name, ", ", vstk[fv], ", &", p.first, ", ",
              p.second, " };");
            args.push_back(t);
        } else {
            args.push_back(name);
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
    for (size_t i = 0; i < an.size(); i++) EmitArg(sp, i, an[i], args);
    for (auto fv : ki.freevars) EmitFvArg(fv, args);
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
        } else if (i == 0 && dd.k == DK_STACK && dd.lenlv.empty() && dd.t &&
                   dd.t->kind == TY_ARRAY && dd.t->arr->akind == A_VAR &&
                   rt->kind == TY_ARRAY && rt->arr->akind == A_VAR &&
                   LenStore(dd.t->arr) != LenStore(rt->arr)) {
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
            if (dd.k == DK_STACK) stk = dd.s;
            else BytesTemp(stk);
            auto base = T();
            L("uint8_t *", base, " = ", Top(stk), ";");
            retex[i] = base;
            args.push_back(stk);
        } else if ((int)i != ki.cret) {
            if (dd.k == DK_LVALUE) {
                retex[i] = dd.s;
                args.push_back(cat("&", dd.s));
            } else {
                auto tv = T();
                L(CT(rt), " ", tv, ";");
                retex[i] = tv;
                args.push_back(cat("&", tv));
            }
        }
    }
    if (ki.needssp) args.push_back(SpTop());
    string argstr;
    for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
    auto callee = ername.empty() ? ki.cname : ername;
    // The callee reads and bumps the stacks it was handed, and no others.
    auto reach = SyncReach(sp, args);
    MarkFlush(reach);
    if (ki.cret >= 0) {
        auto r0 = T();
        L(CT(sp->rets[ki.cret]), " ", r0, " = ", callee, "(", argstr, ");");
        retex[ki.cret] = r0;
    } else {
        L(callee, "(", argstr, ");");
    }
    MarkReload(reach);
    if (!reprefixbase.empty()) EmitReprefix(sp, reprefix, reprefixbase, reprefixcnt);
    if (!slidebase.empty()) {
        // Value form arrived as [len][elems]; slide the prefix out and
        // report the count (the one caller-side copy of this fallback).
        EmitSlidePrefix(slidebase, LenStore(sp->rets[0]->arr), slide.s, slide.lenlv);
    }
    if (ki.hasrf) EmitRfCheck(sp);
    // A fixed first return requested onto a stack: store it (mixed cases
    // are handled above via cret; nothing more to do here).
    return retex;
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
    if (ls == IS_VARINT) {
        auto tmp = T();
        L("uint8_t ", tmp, "[10];");
        L("int64_t ", ms, " = gs_uleb_write(", tmp, ", (uint64_t)", count, ");");
        writepref = cat("memcpy(", base, ", ", tmp, ", (size_t)", ms, ");");
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
    auto caught = curspec && callee->needs.count(curspec->sf);
    if (caught) {
        auto tid = fromids[curspec->sf];
        EnsureFromChannels(curspec->sf);
        L("if (gs_rf == ", tid, ") {");
        ind++;
        // The propagation ends here, so restore the "nothing in flight"
        // state every other exit path relies on.
        L("gs_rf = 0;");
        string retv;
        for (size_t i = 0; i < curspec->rets.size(); i++) {
            if (IsResz(curspec->rets[i])) {
                // Elements are at our destination; forward the count.
                L("*gs_rl", i, " = gs_lret_", tid, "_", i, ";");
                continue;
            }
            if (IsBytesT(curspec->rets[i])) continue;   // Already at our destination.
            if ((int)i == curinfo->cret) retv = cat("gs_lret_", tid, "_", i);
            else L("*gs_r", i, " = gs_lret_", tid, "_", i, ";");
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
    string rv;
    Dst d = d0;
    if (wantsval && !IsBytesT(et) && d0.k != DK_LVALUE) {
        rv = T();
        L(CT(et), " ", rv, ";");
        d = Dst { DK_LVALUE, rv };
    } else if (wantsval && IsBytesT(et) && d0.k != DK_STACK) {
        string stk;
        rv = BytesTemp(stk);
        d = Dst { DK_STACK, stk };
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
    string p, sv, tag;
    if (varmode || isref) {
        if (isref) {
            auto x = GenPure(sn);
            p = varmode ? x : cat("((uint8_t *)", x, ")");
        } else {
            p = GenPtr(sn);
        }
        tag = varmode ? cat("*(", IntCT(TagStore(ei->en)), " *)", p)
                      : cat("((", CT(enumtype), " *)", p, ")->tag");
    } else {
        sv = GenPure(sn);
        tag = cat(sv, ".tag");
    }
    // Non-dispatch arguments evaluate once, before the switch.
    vector<string> shared(an.size());
    vector<string> sharedstk(an.size());
    for (size_t i = 0; i < an.size(); i++) {
        if ((int)i == pos) continue;
        auto pt = sp0->argtypes[i];
        if (IsPoolParam(sp0, i)) {
            shared[i] = GenPrefVal(an[i]);
        } else if (IsBytesT(pt)) {
            string stk;
            auto base = BytesTemp(stk);
            GenConstruct(an[i], stk);
            shared[i] = base;
            sharedstk[i] = stk;
        } else {
            shared[i] = GenPure(an[i]);
        }
    }
    // Shared return channels.
    vector<string> retex(sp0->rets.size());
    vector<string> dststk(sp0->rets.size());
    for (size_t i = 0; i < sp0->rets.size(); i++) {
        auto rt = sp0->rets[i];
        Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                              : (i == 0 ? d0 : Dst {});
        if (IsBytesT(rt)) {
            string stk;
            if (dd.k == DK_STACK) stk = dd.s;
            else BytesTemp(stk);
            auto base = T();
            L("uint8_t *", base, " = ", Top(stk), ";");
            retex[i] = base;
            dststk[i] = stk;
        } else {
            auto tv = T();
            L(CT(rt), " ", tv, ";");
            retex[i] = tv;
        }
    }
    L("switch (", tag, ") {");
    for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
        auto sp = c->dispatch[vi];
        auto &ki = sinfo[sp];
        L("case ", TagConst(ei, (int)vi), ": {");
        ind++;
        auto vt = VariantType(enumtype, (int)vi);
        auto byref = sp->argtypes[pos]->kind == TY_REF;
        string varg;
        string payload = varmode ? cat("(", p, " + ", ts, ")")
                       : isref ? cat("((uint8_t *)&((", CT(enumtype), " *)", p, ")->u.v_",
                                     Sanitize(ei->en->variants[vi].name), ")")
                               : "";
        if (byref) {
            if (IsBytesT(vt)) varg = payload;
            else {
                auto tv = T();
                L(CT(vt), " *", tv, " = (", CT(vt), " *)(", payload, ");");
                varg = tv;
            }
        } else if (IsBytesT(vt)) {
            string stk;
            auto base = BytesTemp(stk);
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(vt, payload), ";");
            L("memcpy(", Top(stk), ", ", payload, ", (size_t)", sz, ");");
            Bump(stk, sz);
            varg = base;
        } else if (ei->en->variants[vi].fields.empty()) {
            auto tv = T();
            L(CT(vt), " ", tv, " = {0};");
            varg = tv;
        } else if (!payload.empty()) {
            auto tv = T();
            L(CT(vt), " ", tv, " = *(", CT(vt), " *)(", payload, ");");
            varg = tv;
        } else {
            auto tv = T();
            L(CT(vt), " ", tv, " = ", sv, ".u.v_", Sanitize(ei->en->variants[vi].name),
              ";");
            varg = tv;
        }
        // Assemble the arm's call.
        vector<string> args;
        for (size_t i = 0; i < an.size(); i++) {
            if ((int)i == pos) {
                args.push_back(varg);
                if (IsBytesT(sp->argtypes[i]) && IsResz(sp->argtypes[i]))
                    Fail(c->line, "resizable by-value dispatch payloads are unsupported");
            } else {
                args.push_back(shared[i]);
                if (IsBytesT(sp->argtypes[i]) && IsResz(sp->argtypes[i]))
                    args.push_back(sharedstk[i]);
            }
        }
        for (auto fv : ki.freevars) EmitFvArg(fv, args);
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (IsBytesT(sp->rets[i])) args.push_back(dststk[i]);
            else if ((int)i != ki.cret) args.push_back(cat("&", retex[i]));
        }
        if (ki.needssp) args.push_back(SpTop());
        string argstr;
        for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
        auto reach = SyncReach(sp, args);
        MarkFlush(reach);
        if (ki.cret >= 0) L(retex[ki.cret], " = ", ki.cname, "(", argstr, ");");
        else L(ki.cname, "(", argstr, ");");
        MarkReload(reach);
        if (ki.hasrf) EmitRfCheck(sp);
        ind--;
        L("} break;");
    }
    L("default: GS_UNREACHABLE(", LocArgs(c->line), ");");
    L("}");
    return retex;
}

}  // namespace goose
