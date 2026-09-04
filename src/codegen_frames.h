// Goose compiler — codegen's frames (definitions of CodeGen members,
// codegen.h): each specialization's C interface (C.3), data-stack top
// caching, scopes with their watermark restores, and the naming of globals
// and locals.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Per-specialization call interface. Signature shape (C.3 order):
//   [declared params (resizable by-value ones add a gs_stack*)]
//   [free-variable references, §7.5]
//   [out-pointers for fixed returns after the first]
//   [destination stacks for nonfixed returns]
//   [int64_t gs_sp].
// The C return value is the first fixed return, else void; a `return ...
// from` discriminant travels in the thread-local gs_rf, not the signature.

inline bool CodeGen::IsPoolParam(FnSpec *sp, size_t i) {
    auto &si = sinfo[sp];
    auto ri = si.refidx[i];
    return ri >= 0 && sp->roots[ri].reusable && sp->argtypes[i]->kind == TY_REF &&
           sp->argtypes[i]->ref->sub->kind == TY_ARRAY &&
           sp->argtypes[i]->ref->sub->arr->akind == A_GROW;
}

inline void CodeGen::ComputeGlobalTouch() {
    for (auto sp : livespecs) {
        auto &g = gtouch[sp];
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto id = Is<Ident>(n))
                if (auto v = id->vdef; v && v->isglobal && v->type && IsBytesT(v->type))
                    g.insert(v);
            if (auto c = Is<Call>(n)) walk(c->fvbody);
            n->Children([&](Node *ch) { walk(ch); });
        };
        walk(sp->body);
    }
    // Every key exists now, so absorbing never rehashes under the reference.
    for (auto changed = true; changed;) {
        changed = false;
        for (auto sp : livespecs) {
            auto &g = gtouch[sp];
            function<void(Node *)> walk = [&](Node *n) {
                if (!n) return;
                if (auto c = Is<Call>(n)) {
                    auto absorb = [&](FnSpec *k) {
                        if (!k || !gtouch.count(k)) return;
                        for (auto v : gtouch[k]) if (g.insert(v).second) changed = true;
                    };
                    if (c->builtin < 0) absorb(c->spec);
                    for (auto d : c->dispatch) absorb(d);
                    walk(c->fvbody);
                }
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
        }
    }
}

// A pool or fat-reference argument carries its stack inside the value, so
// the argument text does not name it; such a call syncs everything.
inline bool CodeGen::PassesOpaqueStack(FnSpec *sp) {
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        if (IsPoolParam(sp, i)) return true;
        auto pt = sp->argtypes[i];
        if (pt->kind == TY_REF && IsResz(pt->ref->sub)) return true;
    }
    for (auto fv : sinfo[sp].freevars) if (fv->reusable) return true;
    return false;
}

// What a call to `callee` with these arguments can reach: the argument text
// (a stack handed over appears in it verbatim) plus the callee's globals.
inline string CodeGen::SyncReach(FnSpec *callee, const vector<string> &args) {
    // A cached reference parameter's stack is opaque to this analysis: the
    // callee may reach it under a name of its own (a global only it
    // mentions, a pool it captures), so those bodies sync at every call.
    if (!cachetops || reftops || PassesOpaqueStack(callee)) return "*";
    string s;
    for (auto &a : args) { s += a; s += '\x01'; }
    for (auto d : gtouch[callee]) {
        auto it = gstks.find(d);
        if (it != gstks.end()) { s += it->second; s += '\x01'; }
        // A reusable pool's freelist is a second stack the same name
        // reaches: an alloc or a free in the callee moves it too.
        auto pit = gpools.find(d);
        if (pit != gpools.end()) { s += pit->second.second; s += '\x01'; }
    }
    return s;
}

inline void CodeGen::CollectSpecs() {
    for (auto sp : ast.fnspecs)
        if (sp->live && sp->body) livespecs.push_back(sp);
    // Names: suffix whenever the goose name maps to more than one live
    // spec (overloads or multiple specializations).
    map<string_view, int> namecount;
    for (auto sp : livespecs) namecount[sp->sf->name]++;
    for (auto sp : livespecs) {
        auto &si = sinfo[sp];
        auto base = Sanitize(sp->sf->name);
        if (sp->sf->name == "main") base = "gs_main";
        if (namecount[sp->sf->name] > 1) Append(base, "_", sp->id);
        si.cname = Unique(base);
        si.hasrf = !sp->needs.empty();
        auto ri = 0;
        for (auto pt : sp->argtypes)
            si.refidx.push_back(pt->kind == TY_REF || pt->kind == TY_SLICE ? ri++ : -1);
        for (size_t i = 0; i < sp->rets.size(); i++)
            if (si.cret < 0 && IsFix(sp->rets[i])) si.cret = (int)i;
        for (auto t : sp->needs)
            if (!fromids.count(t)) fromids[t] = (int)fromids.size() + 1;
    }
    // Free variables: every referenced VarDef another spec owns, in first-
    // appearance order, then a fixpoint pulling in callees' lists.
    for (auto sp : livespecs) {
        auto &fv = sinfo[sp].freevars;
        set<const VarDef *> seen;
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            auto add = [&](VarDef *v) {
                if (v && v->ownerspec && v->ownerspec != sp && !v->isglobal &&
                    seen.insert(v).second)
                    fv.push_back(v);
            };
            if (auto id = Is<Ident>(n)) add(id->vdef);
            if (auto c = Is<Call>(n)) walk(c->fvbody);
            n->Children([&](Node *ch) { walk(ch); });
        };
        walk(sp->body);
    }
    for (auto changed = true; changed;) {
        changed = false;
        for (auto sp : livespecs) {
            auto &si = sinfo[sp];
            set<const VarDef *> seen(si.freevars.begin(), si.freevars.end());
            function<void(Node *)> walk = [&](Node *n) {
                if (!n) return;
                if (auto c = Is<Call>(n)) {
                    auto absorb = [&](FnSpec *k) {
                        if (!k || k == sp || !sinfo.count(k)) return;
                        // Copy: a recursive callee chain could reach our
                        // own list while we grow it.
                        auto kfv = sinfo[k].freevars;
                        for (auto v : kfv) {
                            if (v->ownerspec != sp && seen.insert(v).second) {
                                si.freevars.push_back(v);
                                changed = true;
                            }
                        }
                    };
                    if (c->builtin < 0) absorb(c->spec);
                    for (auto d : c->dispatch) absorb(d);
                    walk(c->fvbody);
                }
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
        }
    }
    // needssp: any nonfixed value anywhere in the body, or a callee that
    // needs it. Iterate to a fixpoint (recursion makes one pass short).
    auto ownneed = [&](FnSpec *sp) {
        for (auto pt : sp->argtypes) if (IsBytesT(pt)) return true;
        for (auto rt : sp->rets) if (IsBytesT(rt)) return true;
        auto need = false;
        function<void(Node *)> walk = [&](Node *n) {
            if (!n || need) return;
            if (n->exprtype && n->exprtype->kind != TY_VOID && n->exprtype->kind != TY_FN &&
                n->exprtype->kind != TY_GENERIC && IsBytesT(n->exprtype)) {
                need = true;
                return;
            }
            if (auto c = Is<Call>(n)) walk(c->fvbody);
            n->Children([&](Node *ch) { walk(ch); });
        };
        walk(sp->body);
        return need;
    };
    for (auto sp : livespecs) sinfo[sp].needssp = ownneed(sp);
    for (auto changed = true; changed;) {
        changed = false;
        for (auto sp : livespecs) {
            if (sinfo[sp].needssp) continue;
            auto need = false;
            function<void(Node *)> walk = [&](Node *n) {
                if (!n || need) return;
                if (auto c = Is<Call>(n)) {
                    if (c->builtin < 0 && c->spec && sinfo.count(c->spec) &&
                        sinfo[c->spec].needssp) need = true;
                    for (auto d : c->dispatch) if (sinfo.count(d) && sinfo[d].needssp) need = true;
                    if (c->builtin == B_QGET || c->builtin == B_QPOLL)
                        if (c->rettypes.size() && IsBytesT(c->rettypes[0])) need = true;
                    walk(c->fvbody);
                }
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
            if (need) { sinfo[sp].needssp = true; changed = true; }
        }
    }
}

// The C parameter list of a spec, as (type, name) decl strings; also
// registers the parameter VarDefs' names and stack expressions.
inline string CodeGen::SigParams(FnSpec *sp, bool decls, bool er) {
    auto &si = sinfo[sp];
    string s;
    auto add = [&](const string &d) {
        if (!s.empty()) s += ", ";
        s += d;
    };
    for (size_t i = 0; i < sp->params.size(); i++) {
        auto vd = sp->params[i];
        auto pt = sp->argtypes[i];
        auto pn = decls ? LocalName(vd) : cat("p", i);
        if (IsPoolParam(sp, i)) {
            EmitCoreTypes();
            add(cat("gs_pref ", pn));
        } else if (IsResz(pt)) {
            // By-value resizable: the header (or frame object) by value
            // plus its stack (C.3).
            EmitCoreTypes();
            add(cat(IsFrameObj(pt) ? CT(pt) : string("gs_rhdr"), " ", pn));
            add(cat("gs_stack *", pn, "_stk"));
            if (decls) vstk[vd] = cat(pn, "_stk");
        } else if (IsBytesT(pt)) {
            add(cat("uint8_t *", pn));
        } else {
            add(cat(CT(pt), " ", pn));
        }
    }
    auto fvn = 0;
    for (auto fv : si.freevars) {
        auto fn = decls ? LocalName(const_cast<VarDef *>(fv)) : cat("fv", fvn++);
        auto ft = fv->type;
        if (fv->reusable) {
            EmitCoreTypes();
            add(cat("gs_pref ", fn));
        } else if (IsResz(ft)) {
            EmitCoreTypes();
            add(cat(IsFrameObj(ft) ? CT(ft) : string("gs_rhdr"), " *", fn));
            add(cat("gs_stack *", fn, "_stk"));
            if (decls) {
                vstk[fv] = cat(fn, "_stk");
                fvptr.insert(fv);
            }
        } else if (IsBytesT(ft)) {
            add(cat("uint8_t *", fn));
        } else {
            add(cat(VarCT(fv), " *", fn));
            if (decls) fvptr.insert(fv);
        }
    }
    for (size_t i = 0; i < sp->rets.size(); i++) {
        if (IsResz(sp->rets[i]) || (er && i == 0)) {
            // Elements go to the destination stack; the count comes back
            // through the length out-parameter (C.3's element-run form —
            // always for resizables, on demand for variable arrays), or
            // the whole frame object through its out-parameter.
            add(cat("gs_stack *gs_dst", i));
            if (IsResz(sp->rets[i]) && IsFrameObj(sp->rets[i]))
                add(cat(CT(sp->rets[i]), " *gs_rl", i));
            else
                add(cat("int64_t *gs_rl", i));
        } else if (IsBytesT(sp->rets[i])) {
            add(cat("gs_stack *gs_dst", i));
        } else if ((int)i != si.cret) {
            add(cat(CT(sp->rets[i]), " *gs_r", i));
        }
    }
    if (si.needssp) add("int64_t gs_sp");
    if (s.empty()) s = "void";
    return s;
}

inline string CodeGen::SigRet(FnSpec *sp) {
    auto &si = sinfo[sp];
    return si.cret >= 0 ? CT(sp->rets[si.cret]) : "void";
}

// Every global's dedicated stack expression.

// The function's own indexed stacks, plus -- per the mode -- either the
// globals' dedicated ones or the reference parameters': each has a single
// spelling in its mode (see the note above). A caller's stack arriving as
// a plain `gs_stack *` parameter never does, and keeps the memory form.
inline bool CodeGen::CacheableStk(const string &stk) {
    if (!cachetops) return false;
    if (stk.compare(0, 3, "GS(") == 0) return true;
    return reftops ? refstkexprs.count(stk) != 0 : gstkexprs.count(stk) != 0;
}

// A function owns a handful of stacks at most, so these stay linear scans.
inline int CodeGen::TopIdx(const string &stk) {
    for (size_t i = 0; i < toporder.size(); i++) if (toporder[i] == stk) return (int)i;
    toporder.push_back(stk);
    return (int)toporder.size() - 1;
}

inline bool CodeGen::IsRegion(int k, int id) {
    for (size_t i = 0; i < regstk.size(); i++)
        if (regstk[i] == k && regloop[i] == id) return true;
    return false;
}

// The lvalue for a stack's top, emitted as a placeholder: which form it
// takes here depends on the regions, and those are only known once the
// whole body is.
inline string CodeGen::Top(const string &stk) {
    if (!CacheableStk(stk)) return cat(stk, "->top");
    return cat(TOPMARK, TopIdx(stk), "@@");
}

// The same, at a point that grows or shrinks the stack -- which is what
// decides where the cached form is worth having. A watermark restore is
// not growth: it runs once on the way out of a scope and takes whichever
// form is already in effect there.
inline string CodeGen::TopW(const string &stk) {
    if (CacheableStk(stk)) {
        growstk.push_back(TopIdx(stk));
        growloop.push_back(loopstack.empty() ? -1 : loopstack.back());
    }
    return Top(stk);
}

// A loop's edges, where the tops it grows are loaded and stored back.
inline int CodeGen::MarkLoopBegin() {
    if (!cachetops) return -1;
    auto id = (int)loopparent.size();
    loopparent.push_back(loopstack.empty() ? -1 : loopstack.back());
    loopstack.push_back(id);
    L(LOOPMARK, "b", id);
    return id;
}

inline void CodeGen::MarkLoopEnd(int id) {
    if (id < 0) return;
    assert(loopstack.back() == id);
    loopstack.pop_back();
    L(LOOPMARK, "e", id);
}

// Where each stack is cached: the innermost loop around every growth of
// it, less any of those nested inside another. A growth outside every
// loop takes the whole body instead, which is the extent the local is
// live across in that case anyway.
inline void CodeGen::PlanTopCaches() {
    regstk.clear();
    regloop.clear();
    topfnlocal.assign(toporder.size(), string());
    vector<int> fnwide(toporder.size(), 0);
    for (size_t i = 0; i < growstk.size(); i++) {
        if (growloop[i] < 0) fnwide[growstk[i]] = 1;
        else if (!IsRegion(growstk[i], growloop[i])) {
            regstk.push_back(growstk[i]);
            regloop.push_back(growloop[i]);
        }
    }
    for (size_t i = 0; i < regstk.size();) {
        auto drop = fnwide[regstk[i]] != 0;
        for (auto p = loopparent[regloop[i]]; !drop && p >= 0; p = loopparent[p])
            drop = IsRegion(regstk[i], p);
        if (!drop) { i++; continue; }
        regstk.erase(regstk.begin() + i);
        regloop.erase(regloop.begin() + i);
    }
    for (size_t i = 0; i < toporder.size(); i++) if (fnwide[i]) topfnlocal[i] = T();
}

inline bool CodeGen::LineIs(string_view s, const char *pfx) {
    return s.compare(0, strlen(pfx), pfx) == 0;
}

// The label a jump on this line targets, or "" -- the conditional form
// `if (!(c)) goto L;` counts, since a flush placed before it is harmless
// on the path that does not take it.
inline string_view CodeGen::GotoTarget(string_view line) {
    auto g = line.find("goto ");
    if (g == string_view::npos) return {};
    auto b = g + 5;
    auto e = line.find(';', b);
    return e == string_view::npos ? string_view() : line.substr(b, e - b);
}

// The label this line defines, or "".
inline string_view CodeGen::LabelHere(string_view line) {
    if (line.size() < 3 || line.compare(line.size() - 2, 2, ":;") != 0) return {};
    auto name = line.substr(0, line.size() - 2);
    for (auto c : name)
        if (!isalnum((unsigned char)c) && c != '_') return {};
    return name;
}

// Line `i` of `b` without its indentation, whose width lands in `ind0`;
// `i` moves on to the next line.
inline string_view CodeGen::NextLine(const string &b, size_t &i, size_t &ind0) {
    auto eol = b.find('\n', i);
    if (eol == string::npos) eol = b.size();
    auto line = string_view(b).substr(i, eol - i);
    i = eol + 1;
    auto n = line.find_first_not_of(' ');
    ind0 = n == string_view::npos ? line.size() : n;
    return n == string_view::npos ? string_view() : line.substr(n);
}

// Replaces the markers: a region's edges load and store its local, a
// call's mark syncs the stacks it can reach that are cached where it
// stands, a jump out of a region flushes it on the way, and every top
// placeholder becomes the local in force there or the memory form.
// A body that cached nothing still has its markers to remove.
inline string CodeGen::ExpandTopMarkers(const string &b) {
    if (!cachetops) return b;
    // A region is the text between its loop's two markers, so a jump stays
    // inside it exactly when the line its label sits on does.
    vector<int> loopbeg(loopparent.size(), 0), loopend(loopparent.size(), 0);
    vector<string> lbname;
    vector<int> lbline;
    for (size_t i = 0, ind0 = 0, ln = 0; i < b.size(); ln++) {
        auto rest = NextLine(b, i, ind0);
        if (LineIs(rest, LOOPMARK)) {
            auto t = rest.substr(strlen(LOOPMARK));
            auto id = atoi(string(t.substr(1)).c_str());
            (t[0] == 'b' ? loopbeg : loopend)[id] = (int)ln;
        } else if (auto lb = LabelHere(rest); !lb.empty()) {
            lbname.push_back(string(lb));
            lbline.push_back((int)ln);
        }
    }
    vector<string> active = topfnlocal;   // Empty: the memory form here.
    vector<int> open;
    string out;
    for (size_t i = 0, ind0 = 0; i < b.size();) {
        auto rest = NextLine(b, i, ind0);
        if (LineIs(rest, LOOPMARK)) {
            auto t = rest.substr(strlen(LOOPMARK));
            auto beg = t[0] == 'b';
            auto id = atoi(string(t.substr(1)).c_str());
            if (beg) open.push_back(id); else open.pop_back();
            for (size_t k = 0; k < toporder.size(); k++) {
                if (!IsRegion((int)k, id)) continue;
                out.append(ind0, ' ');
                if (beg) {
                    active[k] = T();
                    Append(out, "uint8_t *", active[k], " = ", toporder[k], "->top;\n");
                } else {
                    Append(out, toporder[k], "->top = ", active[k], ";\n");
                    active[k].clear();
                }
            }
            continue;
        }
        auto isflush = LineIs(rest, FLUSHMARK);
        if (isflush || LineIs(rest, RELOADMARK)) {
            auto reach = rest.substr(strlen(isflush ? FLUSHMARK : RELOADMARK));
            for (size_t k = 0; k < toporder.size(); k++) {
                if (active[k].empty()) continue;
                if (reach != "*" && reach.find(toporder[k]) == string_view::npos) continue;
                out.append(ind0, ' ');
                if (isflush) Append(out, toporder[k], "->top = ", active[k], ";\n");
                else Append(out, active[k], " = ", toporder[k], "->top;\n");
            }
            continue;
        }
        if (auto tgt = GotoTarget(rest); !tgt.empty()) {
            auto at = -1;
            for (size_t j = 0; j < lbname.size(); j++) if (lbname[j] == tgt) at = lbline[j];
            for (size_t k = 0; k < toporder.size(); k++) {
                if (active[k].empty() || !topfnlocal[k].empty()) continue;
                auto reg = -1;
                for (auto id : open) if (IsRegion((int)k, id)) reg = id;
                if (reg >= 0 && at > loopbeg[reg] && at < loopend[reg]) continue;
                out.append(ind0, ' ');
                Append(out, toporder[k], "->top = ", active[k], ";\n");
            }
        }
        out.append(ind0, ' ');
        for (size_t p = 0; p < rest.size();) {
            auto m = rest.find(TOPMARK, p);
            if (m == string_view::npos) { out.append(rest.substr(p)); break; }
            out.append(rest.substr(p, m - p));
            auto ds = m + strlen(TOPMARK);
            auto e = rest.find("@@", ds);
            assert(e != string_view::npos);
            auto k = atoi(string(rest.substr(ds, e - ds)).c_str());
            if (active[k].empty()) Append(out, toporder[k], "->top");
            else out.append(active[k]);
            p = e + 2;
        }
        out += '\n';
    }
    return out;
}

inline void CodeGen::PushSc(int kind) {
    CScope s;
    s.kind = kind;
    s.stkbase = stknext;
    cscopes.push_back(s);
}

inline void CodeGen::EmitRestores(const CScope &s) {
    for (auto i = (int)s.saves.size() - 1; i >= 0; i--)
        L(Top(s.saves[i].first), " = ", s.saves[i].second, ";");
}

inline void CodeGen::PopSc() {
    EmitRestores(cscopes.back());
    stknext = cscopes.back().stkbase;
    cscopes.pop_back();
}

// Restores for exiting down to (and including) scope index `to`.
inline void CodeGen::EmitExitRestores(int to) {
    for (auto i = (int)cscopes.size() - 1; i >= to; i--) EmitRestores(cscopes[i]);
}

// Allocates a data stack index; `forlocal` skips enclosing statement
// scopes so the value survives to the end of the surrounding block.
// Returns the stack expression; the caller emits `uint8_t *base = X->top;`
// and registers it via SaveBase.
inline string CodeGen::AllocStk(bool forlocal) {
    auto k = stknext++;
    stkmax = std::max(stkmax, stknext);
    if (forlocal)
        for (auto &s : cscopes)
            if (s.kind == SC_STMT && s.stkbase < stknext) s.stkbase = stknext;
    return SpIdx(k);
}

inline void CodeGen::SaveBase(bool forlocal, const string &stk, const string &basevar) {
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (forlocal && cscopes[i].kind == SC_STMT) continue;
        cscopes[i].saves.push_back({ stk, basevar });
        return;
    }
    assert(false);
}

// Globals some `in pool` type names.

inline string CodeGen::PoolBase(const VarDef *pool) {
    // Global initializers run in declaration order, and a pool's base is
    // only set when its own initializer does: there is no point in the
    // function to hoist a load to, so read the header there.
    if (!curspec) return cat(gnames[pool], ".base");
    for (auto &p : poolbases) if (p.first == pool) return p.second;
    auto name = Unique2(cat("gs_pb_", Sanitize(pool->name)));
    poolbases.push_back({ pool, name });
    return name;
}

// A pool's element region is the same address its `in pool` offsets
// measure from, so element access reads the local too rather than the
// header a relative store may have made the C compiler reload.
inline string CodeGen::PoolBaseOr(const VarDef *vd, const string &hdrbase) {
    return poolglobals.count(vd) ? PoolBase(vd) : hdrbase;
}

inline string CodeGen::LocalName(VarDef *vd) {
    assert(!vd->isglobal);
    auto it = vnames.find(vd);
    if (it != vnames.end()) return it->second;
    return vnames[vd] = Unique2(Sanitize(vd->name));
}

inline string CodeGen::VName(const VarDef *vd) {
    auto it = vnames.find(vd);
    if (it != vnames.end()) return it->second;
    auto git = gnames.find(vd);
    assert(git != gnames.end());
    return git->second;
}

// ------------------------------------------------------------------
// Declarations and assignment.

inline string CodeGen::Unique2(const string &base) {
    auto name = base;
    for (auto n = 2; fnused.count(name) || used.count(name); n++) name = cat(base, "_", n);
    fnused.insert(name);
    return name;
}

}  // namespace goose
