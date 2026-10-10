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
//   [out-pointers for large fixed returns and additional small returns]
//   [destination stacks for nonfixed returns]
//   [int64_t gs_sp].
// The C return value is the first small fixed return, else void; a `return ...
// from` discriminant travels in the thread-local gs_rf, not the signature.

inline bool CodeGen::IsPoolParam(FnSpec *sp, size_t i) {
    return sp->roots[i].reusable && CarriesPool(sp->argtypes[i]);
}

// A pool or fat-reference argument carries its stack inside the value, so
// the argument text does not name it; its expression may: a reference taken
// of a variable or of a frame object's tail in one, or the one a parameter,
// an alias or a captured reference holds, none of them rebindable. The
// stacks it carries, or "" where the expression does not say.
inline string CodeGen::HandedStacks(Node *n) {
    auto c = n;
    if (auto un = Is<Unary>(n); un && un->op == T_BITAND) c = un->child;
    while (auto d = Is<Dot>(c)) {
        if (!d->IsField()) return "";
        c = d->obj;
        // A reference field on the way leads to another stack.
        if (!Is<Ident>(c) && c->exprtype && c->exprtype->kind == TY_REF) return "";
    }
    auto id = Is<Ident>(c);
    auto v = id ? id->vdef : nullptr;
    while (v && refalias.count(v)) v = const_cast<VarDef *>(refalias[v]);
    if (!v || !v->type || (!IsResz(v->type) && (!IsFatRef(v->type) || v->isvar)))
        return "";
    if (!v->isglobal && !vnames.count(v)) return "";
    auto lv = VarLoc(v);
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    if (lv.stk.empty()) return "";
    return cat(lv.stk, "\x01", lv.flstk, "\x01");
}

// What a call to `callee` with these arguments can reach: the argument text
// (a stack handed over appears in it verbatim), the stacks the reference
// arguments carry, and the callee's globals.
inline string CodeGen::SyncReach(FnSpec *callee, const vector<string> &args,
                                 const vector<Node *> &an) {
    if (!cachetops) return "*";
    for (auto fv : sinfo[callee].freevars)
        if (fv->reusable || (fv->type && HoldsFatRef(fv->type))) return "*";
    string handed;
    for (size_t i = 0; i < callee->argtypes.size(); i++) {
        auto pt = callee->argtypes[i];
        if (!IsPoolParam(callee, i) && !HoldsFatRef(pt)) continue;
        // A value holding a reference -- in a field, a payload, an element,
        // the array a reference refers to -- says nothing of which stack
        // that is.
        if (i >= an.size() || pt->kind != TY_REF || HoldsFatRef(pt->ref->sub)) return "*";
        auto st = HandedStacks(an[i]);
        if (st.empty()) return "*";
        handed += st;
    }
    // A cached reference parameter's stack, or a return destination, may be
    // a global's, which the callee reaches under a name of its own. Either,
    // or a captured variable's stack (a named result built at its function's
    // destination), may also be the destination of a `return ... from`
    // target up the chain, whose channel a propagating callee builds its
    // value in (§7.9). A callee with no other way to a stack than its
    // arguments (a pool or a captured reference it holds counts as passing
    // one, above) reaches none of them.
    auto &ki = sinfo[callee];
    if ((reftops || topdst || topcap) && ki.hasrf) return "*";
    if ((reftops || topdst) && !ki.globals.empty()) return "*";
    auto s = handed;
    for (auto &a : args) { s += a; s += '\x01'; }
    for (auto d : sinfo[callee].globals) {
        // A fat reference held in a global reaches whichever stack it was
        // bound to, which no name in the callee says.
        if (HoldsFatRef(d->type)) return "*";
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
    for (auto sp : livespecs) namecount[sp->sf->qname]++;
    for (auto sp : livespecs) {
        auto &si = sinfo[sp];
        auto base = Sanitize(sp->sf->ns, sp->sf->name);
        if (namecount[sp->sf->qname] > 1) Append(base, "_", sp->id);
        si.cname = Unique(base);
        si.hasrf = !sp->needs.empty();
        for (size_t i = 0; i < sp->rets.size(); i++)
            if (si.cret < 0 && IsFix(sp->rets[i]) && !IsLargeFixed(sp->rets[i]))
                si.cret = (int)i;
        for (auto t : sp->needs)
            if (!fromids.count(t)) fromids[t] = (int)fromids.size() + 1;
    }
    // Scan each finished body once. The call edges are local to this
    // analysis; only the summaries needed by emission survive it. Preserve
    // first-appearance order for captures, which fixes the C parameter order.
    // A body needs gs_sp wherever codegen puts anything on an indexed stack
    // (AllocStk): without it those indices count from 0, the outermost
    // callers' stacks, whose arrays may be growing meanwhile or have their
    // tops cached in locals. A node's type is the slot type the checker
    // fitted it to (FitsAt), so a value built as another type is found
    // where it is made.
    // A call whose free-variable arguments the optimizer replaced
    // (Call::fvremap) is an edge of its own: the callee's free variables
    // reach the caller as what that call passes for them.
    unordered_map<FnSpec *, vector<pair<FnSpec *, const Call *>>> callees;
    for (auto sp : livespecs) {
        auto &si = sinfo[sp];
        auto &calls = callees[sp];
        set<const VarDef *> seen;
        set<pair<FnSpec *, const Call *>> seencalls;
        for (auto pt : sp->argtypes) si.needssp |= NeedsStack(pt);
        for (auto rt : sp->rets) si.needssp |= NeedsStack(rt);
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto id = Is<Ident>(n)) {
                auto v = id->vdef;
                // A global initializer's locals have no ownerspec: every spec
                // naming one captures it, up to gs_init_globals, which declares it.
                if (v && v->ownerspec != sp && !v->isglobal && seen.insert(v).second)
                    si.freevars.push_back(v);
                if (v && v->isglobal && v->type &&
                    (IsBytesT(v->type) || HoldsFatRef(v->type)))
                    si.globals.insert(v);
            }
            if (n->exprtype && n->exprtype->kind != TY_VOID && n->exprtype->kind != TY_FN &&
                n->exprtype->kind != TY_GENERIC && NeedsStack(n->exprtype))
                si.needssp = true;
            // A slice literal still constructs a fixed array to point at.
            if (auto al = Is<ArrayLit>(n); al && al->exprtype->kind == TY_SLICE &&
                IsFix(al->exprtype->sub)) {
                auto count = al->fillval ? ((IntLit *)al->fillcount)->val
                                         : (int64_t)al->elems.size();
                si.needssp |= count > NATIVE_VALUE_LIMIT /
                    std::max<int64_t>(FixedSize(al->exprtype->sub), 1);
            }
            // A payload bound by value is copied onto a stack of its own.
            if (auto m = Is<MatchExpr>(n))
                for (auto &arm : m->arms) si.needssp |= arm.binder && NeedsStack(arm.binder->type);
            // A value adapted to a fixed-mode ADT from a variable-mode one, a
            // reference result's pointee included, is built as that first
            // (GenAdtAdapted).
            if (auto from = AdtFrom(n)) si.needssp |= NeedsStack(from);
            if (auto c = Is<Call>(n)) {
                auto add = [&](FnSpec *k) {
                    pair<FnSpec *, const Call *> e { k, c->fvremap.empty() ? nullptr : c };
                    if (k && k != sp && sinfo.count(k) && seencalls.insert(e).second)
                        calls.push_back(e);
                };
                if (c->builtin < 0) add(c->spec);
                for (auto d : c->dispatch) add(d);
                // Formatting a user type calls its format overload, and print
                // and thread_spawn assemble their text or packet on a stack.
                for (auto &fs : c->fmtspecs) add(fs.second);
                if (c->builtin == B_PRINT || c->builtin == B_THREAD_SPAWN) si.needssp = true;
                // A result is built as the call's own type (str() passed as
                // a slice or returned as a u8[..16], say) before it is fitted.
                for (auto rt : c->rettypes) si.needssp |= NeedsStack(rt);
                // A limited receiver takes anything rendered structurally
                // from a builder of its own (EmitFormatInto).
                if (c->builtin == B_FORMAT) {
                    auto an = c->ArgNodes();
                    auto recv = an[0]->exprtype;
                    if (recv->kind == TY_REF) recv = recv->ref->sub;
                    if (recv->kind == TY_ARRAY && recv->arr->akind == A_LIMITED)
                        for (size_t i = 1; i < an.size(); i++)
                            si.needssp |= !SimpleText(FmtContext(c, an[i]), an[i]->exprtype);
                }
                // A variable-mode scrutinee is copied for the arms that take
                // their variant by value, even when it arrives by reference
                // (EmitDispatch).
                if (!c->dispatch.empty()) {
                    auto pos = c->dispatcharg;
                    auto st = CallArgNodes(c, c->dispatch[0]->params.size())[pos]->exprtype;
                    if (st->kind == TY_REF) st = st->ref->sub;
                    if (st->kind == TY_ENUM && st->enu->varmode)
                        for (auto d : c->dispatch) si.needssp |= d->argtypes[pos]->kind != TY_REF;
                }
            }
            RunChildren(n, walk);
        };
        walk(sp->body);
    }
    // All three summaries follow the same call edges. Iterate together to
    // handle recursion without repeatedly traversing the AST or retaining a
    // separate global-touch table that must track the specialization table.
    for (auto changed = true; changed;) {
        changed = false;
        for (auto sp : livespecs) {
            auto &si = sinfo[sp];
            set<const VarDef *> seen(si.freevars.begin(), si.freevars.end());
            for (auto [callee, via] : callees[sp]) {
                auto &ki = sinfo[callee];
                // Self edges were omitted, so growing our list cannot
                // invalidate the callee's list as we read it.
                for (auto fv : ki.freevars) {
                    auto v = via ? via->FreeVarArg(fv) : fv;
                    if (v->ownerspec != sp && seen.insert(v).second) {
                        si.freevars.push_back(v);
                        changed = true;
                    }
                }
                for (auto v : ki.globals)
                    if (si.globals.insert(v).second) changed = true;
                if (ki.needssp && !si.needssp) {
                    si.needssp = true;
                    changed = true;
                }
            }
        }
    }
}

// A struct passed by value that is not 1, 2, 4 or 8 bytes travels by
// reference to a caller's copy in the Windows x64 convention (and above 16
// bytes in AArch64's). Reading a parameter through that pointer, clang
// reloads a fat reference's header address or a slice's data and length
// after every byte store, which may alias the copy; and where a caller
// writes the copy field by field and the callee reads it as one vector
// (passing it on to a callee of its own), the load stalls until the stores
// retire. A slice, a fat reference and a pool reference are therefore
// passed as their scalar members, each a parameter of its own, which no
// convention puts in memory the body can alias, and reassembled at entry.
// The members' C types are the struct's, but for a slice's elements, which
// take `void *` whatever they are.
inline vector<pair<string, string>> CodeGen::ParamFields(TypeExpr *t, bool pool) {
    if (pool)
        return { { "gs_rhdr *", "hdr" }, { "gs_stack *", "stk" }, { "gs_rhdr *", "fl" },
                 { "gs_stack *", "flstk" } };
    if (IsFatRef(t)) return { { "gs_rhdr *", "hdr" }, { "gs_stack *", "stk" } };
    if (t->kind == TY_SLICE) return { { "void *", "data" }, { "int64_t ", "len" } };
    return {};
}

// An argument for a parameter of type t: its members where ParamFields
// passes it so, read from a temporary unless it is a variable already.
inline void CodeGen::PushArg(vector<string> &args, TypeExpr *t, bool pool, const string &v) {
    auto fs = ParamFields(t, pool);
    if (fs.empty()) {
        args.push_back(v);
        return;
    }
    auto x = v;
    if (!all_of(x.begin(), x.end(), [](char c) { return isalnum((unsigned char)c) || c == '_'; })) {
        x = T();
        L(pool ? string("gs_pref") : CT(t), " ", x, " = ", v, ";");
    }
    for (auto &f : fs) args.push_back(cat(x, ".", f.second));
}

// The C parameter list of a spec, as (type, name) decl strings; also
// registers the parameter VarDefs' names and stack expressions, and, in the
// declaring form, collects in paramcopies the entry code reassembling the
// parameters ParamFields passes as members.
inline string CodeGen::SigParams(FnSpec *sp, bool decls, bool er) {
    auto &si = sinfo[sp];
    string s;
    auto add = [&](const string &d) {
        if (!s.empty()) s += ", ";
        s += d;
    };
    if (decls) paramcopies.clear();
    // A by-value parameter of C type ct named pn, or its members.
    auto addval = [&](TypeExpr *t, bool pool, const string &ct, const string &pn) {
        auto fs = ParamFields(t, pool);
        if (fs.empty()) {
            add(cat(ct, " ", pn));
            return;
        }
        string init;
        for (auto &f : fs) {
            auto m = decls ? Unique2(cat(pn, "_", f.second)) : cat(pn, "_", f.second);
            add(cat(f.first, m));
            Append(init, init.empty() ? "" : ", ", m);
        }
        if (decls) Append(paramcopies, "    ", ct, " ", pn, " = { ", init, " };\n");
    };
    for (size_t i = 0; i < sp->params.size(); i++) {
        auto vd = sp->params[i];
        auto pt = sp->argtypes[i];
        auto pn = decls ? LocalName(vd) : cat("p", i);
        if (IsPoolParam(sp, i)) {
            EmitCoreTypes();
            addval(pt, true, "gs_pref", pn);
        } else if (IsResz(pt)) {
            // By-value resizable: the header (or frame object) by value
            // plus its stack (C.3).
            EmitCoreTypes();
            add(cat(IsFrameObj(pt) ? CT(pt) : string("gs_rhdr"), " ", pn));
            add(cat("gs_stack *", pn, "_stk"));
            if (decls) vstk[vd] = cat(pn, "_stk");
        } else if (IsBytesT(pt)) {
            add(cat("uint8_t *", pn));
        } else if (IsLargeFixed(pt)) {
            // The caller makes an independent value copy on a data stack.
            // Passing its address avoids the C ABI's native-stack copy.
            add(cat(CT(pt), " *", pn));
            if (decls) fvptr.insert(vd);
        } else {
            addval(pt, false, CT(pt), pn);
        }
    }
    auto fvn = 0;
    for (auto fv : si.freevars) {
        auto fn = decls ? LocalName(const_cast<VarDef *>(fv)) : cat("fv", fvn++);
        auto ft = fv->type;
        if (fv->reusable) {
            EmitCoreTypes();
            addval(ft, true, "gs_pref", fn);
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

// The stacks of the classes PlanTopClasses admitted: each has a single
// spelling then (see the note above). A caller's stack arriving as a plain
// `gs_stack *` parameter otherwise never does, and keeps the memory form.
inline bool CodeGen::CacheableStk(const string &stk) {
    if (!cachetops) return false;
    if (stk.compare(0, 3, "GS(") == 0) return topown;
    if (gstkexprs.count(stk)) return topglob;
    if (reftops && refstkexprs.count(stk)) return true;
    if (topcap && capstkexprs.count(stk)) return true;
    if (topdst && stk.compare(0, 6, "gs_dst") == 0 && stk.size() > 6) {
        for (size_t i = 6; i < stk.size(); i++)
            if (!isdigit((unsigned char)stk[i])) return false;
        return true;
    }
    return false;
}

// A function owns a handful of stacks at most, so these stay linear scans.
inline int CodeGen::TopIdx(const string &stk) {
    for (size_t i = 0; i < toporder.size(); i++) if (toporder[i] == stk) return (int)i;
    toporder.push_back(stk);
    return (int)toporder.size() - 1;
}

// Whether a construction region of stk is open here (MarkConsBegin).
inline bool CodeGen::InConsOf(const string &stk) {
    for (auto id : loopstack) if (loopcons[id] == stk) return true;
    return false;
}

// The lvalue for a stack's top, emitted as a placeholder: which form it
// takes here depends on the regions, and those are only known once the
// whole body is. A stack no class caches has one only inside a
// construction at its top.
inline string CodeGen::Top(const string &stk) {
    if (!markers || (!CacheableStk(stk) && !InConsOf(stk))) return cat(stk, "->top");
    return cat(TOPMARK, TopIdx(stk), "@@");
}

// The same, at a point that grows or shrinks the stack -- which is what
// decides where the cached form is worth having. A watermark restore is
// not growth: it runs once on the way out of a scope and takes whichever
// form is already in effect there. A cached stack's growth counts against
// the innermost loop around it, or, outside every loop, against the
// innermost block (a growth in one branch of a long function then keeps
// no local live, and synced at every call, through the rest of it); a
// construction's counts against that construction where the stack is not
// otherwise cached.
inline string CodeGen::TopW(const string &stk) {
    if (CacheableStk(stk)) {
        auto loop = -1, block = -1;
        for (auto it = loopstack.rbegin(); it != loopstack.rend() && loop < 0; ++it) {
            if (loopcons[*it].empty()) loop = *it;
            else if (block < 0 && loopcons[*it] == BLOCKREGION) block = *it;
        }
        growth.push_back({ TopIdx(stk), loop >= 0 ? loop : block });
    } else if (markers) {
        for (auto it = loopstack.rbegin(); it != loopstack.rend(); ++it)
            if (loopcons[*it] == stk) {
                growth.push_back({ TopIdx(stk), *it });
                break;
            }
    }
    return Top(stk);
}

// A loop's edges, where the tops it grows are loaded and stored back.
inline int CodeGen::MarkLoopBegin() {
    if (!markers) return -1;
    auto id = (int)loopparent.size();
    loopparent.push_back(loopstack.empty() ? -1 : loopstack.back());
    loopcons.push_back("");
    loopstack.push_back(id);
    L(LOOPMARK, "b", id);
    return id;
}

// A block's edges, likewise (GenBlockInner).
inline int CodeGen::MarkBlockBegin() {
    if (!markers) return -1;
    auto id = (int)loopparent.size();
    loopparent.push_back(loopstack.empty() ? -1 : loopstack.back());
    loopcons.push_back(BLOCKREGION);
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

// A value built in place at a stack's top that no class caches -- a pushed
// literal of several fields, say -- has its fields written through a local
// of its own all the same, and the top stored once after them: nothing may
// grow or shrink that stack while the value is under construction (§3.10,
// growth during construction), so no other spelling of it moves its top
// meanwhile, and a call among the fields' initializers syncs it whatever
// the call reaches.
inline int CodeGen::MarkConsBegin(const string &stk) {
    if (!markers || CacheableStk(stk) || InConsOf(stk)) return -1;
    auto id = (int)loopparent.size();
    loopparent.push_back(loopstack.empty() ? -1 : loopstack.back());
    loopcons.push_back(stk);
    loopstack.push_back(id);
    L(LOOPMARK, "b", id);
    return id;
}

// Where each stack is cached: the region TopW counted every growth of it
// against -- the innermost loop around it, or block outside every loop, or
// construction -- less any of those nested inside another. A growth at the
// body's top level takes the whole body instead.
inline CodeGen::TopCachePlan CodeGen::PlanTopCaches() {
    TopCachePlan plan;
    plan.fnlocals.resize(toporder.size());
    vector<int> fnwide(toporder.size(), 0);
    for (auto g : growth) {
        if (g.loop < 0) fnwide[g.stk] = 1;
        else if (!plan.IsRegion(g.stk, g.loop)) plan.regions.push_back(g);
    }
    for (size_t i = 0; i < plan.regions.size();) {
        auto r = plan.regions[i];
        auto drop = fnwide[r.stk] != 0;
        for (auto p = loopparent[r.loop]; !drop && p >= 0; p = loopparent[p])
            drop = plan.IsRegion(r.stk, p);
        if (!drop) { i++; continue; }
        plan.regions.erase(plan.regions.begin() + i);
    }
    for (size_t i = 0; i < toporder.size(); i++) if (fnwide[i]) plan.fnlocals[i] = T();
    return plan;
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

// The count a growth site writes, for caching with the stack's top when its
// header qualifies (LenSlot).
inline void CodeGen::NoteLen(const string &stk, const string &lenlv) {
    string root;
    if (!CacheableStk(stk) || !LenRoot(lenlv, root)) return;
    auto k = TopIdx(stk);
    for (auto &s : lenslots) if (s.stk == k && s.lenlv == lenlv) return;
    lenslots.push_back({ k, lenlv, root });
}

// Whether `lenlv` is the count of a plain resizable header spelled one way
// -- a reference parameter's `p.hdr->len`, a global's `GS_GL->g.len`, a
// captured one's `(*v).len` -- and the header's spelling, which any other
// mention of it shares. A frame object's tail has others (the whole object
// copied), and a local's header is the C compiler's to keep in registers.
inline bool CodeGen::LenRoot(const string &lenlv, string &root) {
    auto ident = [&](size_t b, size_t e) {
        if (b >= e || isdigit((unsigned char)lenlv[b])) return false;
        for (auto i = b; i < e; i++)
            if (!isalnum((unsigned char)lenlv[i]) && lenlv[i] != '_') return false;
        return true;
    };
    auto ends = [&](const char *suf) {
        auto n = strlen(suf);
        return lenlv.size() > n && lenlv.compare(lenlv.size() - n, n, suf) == 0;
    };
    auto n = lenlv.size();
    if (ends(".hdr->len") && ident(0, n - 9)) {
        root = lenlv.substr(0, n - 5);
        return true;
    }
    if (lenlv.compare(0, 7, "GS_GL->") == 0 && ends(".len") && ident(7, n - 4)) {
        root = lenlv.substr(0, n - 4);
        return true;
    }
    if (lenlv.compare(0, 2, "(*") == 0 && ends(").len") && ident(2, n - 5)) {
        root = lenlv.substr(0, n - 4);
        return true;
    }
    return false;
}

// Replaces the markers: a region's edges load and store its local, a
// call's mark syncs the stacks it can reach that are cached where it
// stands, a jump out of a region flushes it on the way, and every top
// placeholder becomes the local in force there or the memory form. The
// counts of LenSlot ride along. A slot is kept only where its header is
// mentioned nowhere in its extent but as that count, its base, or between a
// call's flush and reload of the stack; each pass drops the slots it finds
// mentioned otherwise, until one finds none. A body that cached nothing
// still has its markers to remove.
inline string CodeGen::ExpandTopMarkers(const string &b, TopCachePlan &plan) {
    if (!markers) return b;
    vector<bool> lenok(lenslots.size(), true);
    for (;;) {
        auto clean = true;
        auto out = ExpandTopMarkers1(b, plan, lenok, clean);
        if (clean) return out;
    }
}

inline string CodeGen::ExpandTopMarkers1(const string &b, TopCachePlan &plan,
                                         vector<bool> &lenok, bool &clean) {
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
    vector<string> active = plan.fnlocals;   // Empty: the memory form here.
    vector<int> open;
    // Each slot's local while its stack's is in force, and whether the last
    // mark for that stack was a call's flush, which a reload ends.
    vector<string> lenlocal(lenslots.size());
    vector<bool> synced(toporder.size(), false);
    plan.fnlens.clear();
    for (size_t j = 0; j < lenslots.size(); j++) {
        if (!lenok[j] || plan.fnlocals[lenslots[j].stk].empty()) continue;
        lenlocal[j] = T();
        Append(plan.fnlens, "    int64_t ", lenlocal[j], " = ", lenslots[j].lenlv, ";\n");
    }
    auto store = [&](string &out, size_t ind0, int k) {
        out.append(ind0, ' ');
        Append(out, toporder[k], "->top = ", active[k], ";\n");
        for (size_t j = 0; j < lenslots.size(); j++) {
            if (lenslots[j].stk != k || lenlocal[j].empty()) continue;
            out.append(ind0, ' ');
            Append(out, lenslots[j].lenlv, " = ", lenlocal[j], ";\n");
        }
    };
    auto load = [&](string &out, size_t ind0, int k) {
        out.append(ind0, ' ');
        Append(out, active[k], " = ", toporder[k], "->top;\n");
        for (size_t j = 0; j < lenslots.size(); j++) {
            if (lenslots[j].stk != k || lenlocal[j].empty()) continue;
            out.append(ind0, ' ');
            Append(out, lenlocal[j], " = ", lenslots[j].lenlv, ";\n");
        }
    };
    auto isid = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    // Where `w` occurs in `s` as a whole spelling, not as part of a longer
    // identifier or member path.
    auto occurs = [&](const string &s, size_t at, const string &w) {
        if (s.compare(at, w.size(), w) != 0) return false;
        if (at > 0 && isid(w[0])) {
            auto c = s[at - 1];
            if (isid(c) || c == '.' || c == '>') return false;
        }
        auto e = at + w.size();
        return e >= s.size() || !isid(s[e]) || !isid(w.back());
    };
    string out;
    for (size_t i = 0, ind0 = 0; i < b.size();) {
        auto rest = NextLine(b, i, ind0);
        if (LineIs(rest, LOOPMARK)) {
            auto t = rest.substr(strlen(LOOPMARK));
            auto beg = t[0] == 'b';
            auto id = atoi(string(t.substr(1)).c_str());
            if (beg) open.push_back(id); else open.pop_back();
            for (size_t k = 0; k < toporder.size(); k++) {
                if (!plan.IsRegion((int)k, id)) continue;
                if (beg) {
                    active[k] = T();
                    out.append(ind0, ' ');
                    Append(out, "uint8_t *", active[k], " = ", toporder[k], "->top;\n");
                    for (size_t j = 0; j < lenslots.size(); j++) {
                        if (lenslots[j].stk != (int)k || !lenok[j]) continue;
                        lenlocal[j] = T();
                        out.append(ind0, ' ');
                        Append(out, "int64_t ", lenlocal[j], " = ", lenslots[j].lenlv, ";\n");
                    }
                    synced[k] = false;
                } else {
                    store(out, ind0, (int)k);
                    active[k].clear();
                    for (size_t j = 0; j < lenslots.size(); j++)
                        if (lenslots[j].stk == (int)k) lenlocal[j].clear();
                }
            }
            continue;
        }
        auto isflush = LineIs(rest, FLUSHMARK);
        if (isflush || LineIs(rest, RELOADMARK)) {
            auto reach = rest.substr(strlen(isflush ? FLUSHMARK : RELOADMARK));
            for (size_t k = 0; k < toporder.size(); k++) {
                if (active[k].empty()) continue;
                // A stack cached only for a construction syncs at every call:
                // the call may reach it under a name of its own.
                if (reach != "*" && CacheableStk(toporder[k]) &&
                    reach.find(toporder[k]) == string_view::npos)
                    continue;
                if (isflush) store(out, ind0, (int)k);
                else load(out, ind0, (int)k);
                synced[k] = isflush;
            }
            continue;
        }
        if (auto tgt = GotoTarget(rest); !tgt.empty()) {
            auto at = -1;
            for (size_t j = 0; j < lbname.size(); j++) if (lbname[j] == tgt) at = lbline[j];
            for (size_t k = 0; k < toporder.size(); k++) {
                if (active[k].empty() || !plan.fnlocals[k].empty()) continue;
                auto reg = -1;
                for (auto id : open) if (plan.IsRegion((int)k, id)) reg = id;
                if (reg >= 0 && at > loopbeg[reg] && at < loopend[reg]) continue;
                store(out, ind0, (int)k);
            }
        }
        string line;
        for (size_t p = 0; p < rest.size();) {
            auto m = rest.find(TOPMARK, p);
            if (m == string_view::npos) { line.append(rest.substr(p)); break; }
            line.append(rest.substr(p, m - p));
            auto ds = m + strlen(TOPMARK);
            auto e = rest.find("@@", ds);
            assert(e != string_view::npos);
            auto k = atoi(string(rest.substr(ds, e - ds)).c_str());
            if (active[k].empty()) Append(line, toporder[k], "->top");
            else line.append(active[k]);
            p = e + 2;
        }
        for (size_t j = 0; j < lenslots.size(); j++) {
            if (lenlocal[j].empty()) continue;
            auto &sl = lenslots[j];
            auto member = sl.lenlv.substr(sl.root.size(), sl.lenlv.size() - sl.root.size() - 3);
            string nl;
            for (size_t p = 0; p < line.size();) {
                if (!occurs(line, p, sl.root)) { nl += line[p++]; continue; }
                auto after = p + sl.root.size();
                if (occurs(line, p, sl.lenlv)) {
                    nl += lenlocal[j];
                    p += sl.lenlv.size();
                    continue;
                }
                // The header's address, even handed on, may be read through
                // a temporary before the call that syncs the stack.
                auto base = member + "base";
                auto isbase = line.compare(after, base.size(), base) == 0 &&
                              (after + base.size() >= line.size() || !isid(line[after + base.size()]));
                if (!isbase && !synced[sl.stk]) {
                    lenok[j] = false;
                    clean = false;
                }
                nl.append(sl.root);
                p = after;
            }
            line = nl;
        }
        out.append(ind0, ' ');
        out += line;
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

// Allocates a data stack index; `forlocal` skips the statement scopes
// inside the surrounding block so the value survives to that block's end,
// and no further: past it the index is free for what follows, calls
// included, which a recursive cycle's scratch locals rely on (§7.8).
// Returns the stack expression; the caller emits `uint8_t *base = X->top;`
// and registers it via SaveBase.
inline string CodeGen::AllocStk(bool forlocal) {
    auto k = stknext++;
    stkmax = std::max(stkmax, stknext);
    if (forlocal)
        for (auto i = (int)cscopes.size() - 1; i >= 0 && cscopes[i].kind == SC_STMT; i--)
            cscopes[i].stkbase = std::max(cscopes[i].stkbase, stknext);
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

inline string CodeGen::Unique2(const string &base) {
    auto name = base;
    for (auto n = 2; fnused.count(name) || used.count(name); n++) name = cat(base, "_", n);
    fnused.insert(name);
    return name;
}

}  // namespace goose
