// Goose compiler — codegen's `par for` (definitions of CodeGen members,
// codegen.h): the body outlined into a function over one chunk of the
// iterations, and the fork that runs the chunks on the program's pool
// (docs/design/child_threads.md §2.3, §4.9; runtime.h's gs_par_*).
//
// The loop's body becomes the body of a function of its own, nested in the
// function the loop is in (OutlineParLoops), whose parameters are the
// chunk's first index and the one past its last, and whose free variables
// are what the body names from outside it, passed as a nested function's are
// (§6.4): fixed ones by pointer, resizables as their header's address and
// stack. The loop itself stays the body of that function, walking the chunk.
// Where it stood, a call (Call::parfor) forks (EmitParFor): it evaluates the
// range, or the sequence's view, once, checks chunk_at's bounds, and where
// the program's pool can be claimed, cuts the iterations into contiguous
// chunks, one per worker, each a task whose packet holds the function's
// arguments for its chunk (an environment, gs_parenv_*), which a thunk
// unpacks on the worker (EmitParThunks). An integer reduction's argument is
// the address of the chunk's own accumulator, which the fork adds into the
// variable in chunk order once every chunk has ended. Where the pool is not
// to be had -- under TinyCC, which builds the program without threads, at
// GOOSE_THREADS=1, inside a pool's task, or with one iteration -- the fork
// calls the function once over the whole range, on its own thread.
#pragma once

namespace goose {

// The variables declared anywhere inside n (BCE::DeclaredIn), with the
// components of a root struct's.
inline void CodeGen::ParDeclared(Node *n, set<VarDef *> &out) {
    if (!n) return;
    auto add = [&](VarDef *v) {
        if (!v || !out.insert(v).second) return;
        for (auto c : v->components) if (c) out.insert(c);
    };
    if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) add(d);
    if (auto fl = Is<ForLoop>(n)) {
        add(fl->vdef);
        add(fl->idxdef);
    }
    if (auto m = Is<MatchExpr>(n))
        for (auto &arm : m->arms) add(arm.binder);
    if (auto c = Is<Call>(n)) for (auto p : c->fvparams) add(p);
    RunChildren(n, [&](Node *ch) { ParDeclared(ch, out); });
}

// Every `par for` of a live body, outlined (see above), wherever it stands as
// a statement: in a block, as a match's arm, or as an if's else, which the
// optimizer may have folded a block of one statement into. A par for in the
// body of another is outlined from the function that one became, in turn.
// One no walk reaches -- in a global initializer, which no function holds --
// runs as a plain loop, after the checks its fork would make
// (ForLoop::CgStmt, EmitChunkAtChecks).
inline void CodeGen::OutlineParLoops() {
    vector<FnSpec *> work;
    for (auto sp : ast.fnspecs)
        if (sp->live && sp->body) work.push_back(sp);
    for (size_t wi = 0; wi < work.size(); wi++) {
        auto sp = work[wi];
        function<void(Node *)> walk;
        auto stmt = [&](Node *&st) {
            if (auto fl = st ? Is<ForLoop>(st) : nullptr; fl && fl->ispar)
                st = OutlineParLoop(sp, fl, work);
            else
                walk(st);
        };
        walk = [&](Node *n) {
            if (!n || Is<FunVal>(n)) return;
            if (auto b = Is<Block>(n)) {
                for (auto &st : b->stmts) stmt(st);
                stmt(b->tail);
                return;
            }
            if (auto m = Is<MatchExpr>(n)) {
                walk(m->scrutinee);
                for (auto &arm : m->arms) {
                    walk(arm.expansion);
                    for (auto &pi : arm.pat.items) {
                        walk(pi.lo);
                        walk(pi.hi);
                    }
                    stmt(arm.body);
                }
                return;
            }
            if (auto x = Is<IfExpr>(n)) {
                walk(x->cond);
                walk(x->thenb);
                stmt(x->elseb);
                return;
            }
            RunChildren(n, walk);
        };
        walk(sp->body);
    }
}

// The par for fl of sp's body as a function of its own, which it adds to
// `work`; the call standing in its place.
inline Node *CodeGen::OutlineParLoop(FnSpec *sp, ForLoop *fl, vector<FnSpec *> &work) {
    usespar = true;
    auto line = fl->line;
    auto i64 = ast.inttypes[IS_I64];
    auto sf = new SFunction();
    ast.functions.push_back(sf);
    sf->name = ast.Intern(cat(sp->sf->name, "_par"));
    sf->ns = sp->sf->ns;
    sf->qname = sf->ns.empty() ? sf->name : ast.Intern(cat(sf->ns, "::", sf->name));
    sf->line = line;
    sf->end = fl->end;
    sf->isnested = true;
    sf->outer = sp->sf;
    sf->checkfree = sp->sf->checkfree;
    auto pspec = ast.NewFnSpec();
    pspec->sf = sf;
    pspec->lexparent = sp;
    pspec->live = true;
    pspec->uses = 1;
    pspec->retsknown = true;
    pspec->inpar = true;
    // The body of a recursive cycle's member is the cycle's, as the member
    // is (§7.8), whatever function it runs in now: its large fixed locals
    // stay native (LargeFixedOnStack), and it holds no data stack across a
    // call into the cycle (NoStackAcrossCycleCall, BoundStacks), which the
    // checker held the member's body to.
    pspec->incycle = sp->incycle;
    if (sp->incycle) pspec->cyclelink = sp;
    sf->specs.push_back(pspec);
    auto param = [&](const char *name, TypeExpr *t) {
        auto v = ast.NewVarDef();
        v->name = ast.Intern(name);
        v->type = t;
        v->line = line;
        v->isparam = true;
        v->assigned = v->maybeassigned = true;
        v->ownerspec = pspec;
        pspec->params.push_back(v);
        pspec->argtypes.push_back(t);
        pspec->roots.push_back(RootArg {});
        RootArg view;
        view.cls = -1;
        pspec->views.push_back(view);
        return v;
    };
    auto ident = [&](VarDef *v) {
        auto id = ast.New<Ident>(line, v->name);
        id->vdef = v;
        id->exprtype = v->type;
        return id;
    };
    auto lit = [&](TypeExpr *t) {
        auto l = ast.New<IntLit>(line, 0, "0");
        l->exprtype = t;
        return l;
    };
    auto call = ast.New<Call>(line, ast.New<Ident>(line, sf->name));
    call->spec = pspec;
    call->parfor = fl;
    call->exprtype = ast.voidtype;
    if (fl->iterkind == IK_RANGE || fl->iterkind == IK_COUNT) {
        // `for k in lo..hi` over the chunk, at the counter's own type.
        auto r = Is<RangeExpr>(fl->iter);
        auto ct = r ? r->exprtype : fl->iter->exprtype;
        auto lo = param("gs_par_lo", ct);
        auto hi = param("gs_par_hi", ct);
        call->args = { r ? r->lo : lit(ct), r ? r->hi : fl->iter };
        // A range's index binder counts the iterations before the chunk too.
        if (fl->idxdef) {
            fl->parix = param("gs_par_ix", i64);
            call->args.push_back(lit(i64));
        }
        auto nr = ast.New<RangeExpr>(line, ident(lo), ident(hi));
        nr->exprtype = ct;
        fl->iter = nr;
        fl->iterkind = IK_RANGE;
    } else {
        // The sequence's elements from the chunk's first index to the one
        // past its last, in a view of the whole sequence, which the fork
        // reads once; the body cannot resize it (§4.3 R1).
        auto st = fl->iter->exprtype;
        if (st->kind == TY_REF) st = st->ref->sub;
        auto elem = st->kind == TY_SLICE ? st->sub : st->arr->sub;
        fl->parlo = param("gs_par_lo", i64);
        fl->parhi = param("gs_par_hi", i64);
        auto seq = param("gs_par_seq", ast.SliceOf(elem, line));
        call->args = { lit(i64), lit(i64), fl->iter };
        fl->iter = ident(seq);
        fl->iterkind = IK_SLICE;
        fl->fixedlen = true;
    }
    // The loop is the chunk's now, a plain loop, which the walk of the new
    // function's body (OutlineParLoops) does not outline again; a par for in
    // its body is outlined from there. A chunk starts anywhere: no block
    // shape counting from the range's start.
    fl->ispar = false;
    fl->stripk = 0;
    fl->lanemods.clear();
    fl->sumred = false;
    // What the loop declares is the new function's own.
    set<VarDef *> mine;
    ParDeclared(fl, mine);
    for (auto v : mine) v->ownerspec = pspec;
    auto body = ast.New<Block>(line);
    body->stmts.push_back(fl);
    body->exprtype = ast.voidtype;
    pspec->body = body;
    work.push_back(pspec);
    return call;
}

// The C types of sp's parameters, in the order a call passes them, but for
// gs_sp and gs_at, which the thunk passes itself (SigParams).
inline vector<string> CodeGen::ParParamTypes(FnSpec *sp) {
    auto params = SigParams(sp, false);
    vector<string> out;
    if (params == "void") return out;
    for (size_t b = 0; b < params.size();) {
        auto e = std::min(params.find(", ", b), params.size());
        auto piece = params.substr(b, e - b);
        auto nb = piece.size();
        while (nb > 0 && (isalnum((unsigned char)piece[nb - 1]) || piece[nb - 1] == '_')) nb--;
        auto name = piece.substr(nb);
        auto type = piece.substr(0, nb);
        while (!type.empty() && type.back() == ' ') type.pop_back();
        if (name != "gs_sp" && name != "gs_at") out.push_back(type);
        b = e + 2;
    }
    return out;
}

// chunk_at's bounds, checked once, before any iteration of fl runs (§4.4):
// at its fork, or where it runs as a plain loop, unoutlined.
inline void CodeGen::EmitChunkAtChecks(ForLoop *fl, Line line) {
    for (auto &[a, b] : fl->chunkats) {
        // Against the array's length, or only as non-decreasing from 0 where
        // the array is no variable from outside the body (ParChunkCheck).
        string alen = "INT64_MAX";
        if (a) {
            auto al = VarLoc(a);
            if (al.t->kind == TY_REF) DerefLoc(al);
            auto av = ArrayView(al);
            alen = T();
            L("int64_t ", alen, " = ", av.len, ";");
        }
        auto bl = VarLoc(b);
        if (bl.t->kind == TY_REF) DerefLoc(bl);
        auto bv = ArrayView(bl);
        auto blen = T();
        L("int64_t ", blen, " = ", bv.len, ";");
        L("gs_check_chunk_offsets((const void *)(", bv.elems, "), ", blen, ", ", alen, ", ",
          LocArgs(line), ");");
    }
}

// The fork of a par for, standing where it was (OutlineParLoops).
inline vector<string> CodeGen::EmitParFor(Call *c) {
    auto fl = c->parfor;
    auto sp = c->spec;
    auto &ki = sinfo[sp];
    auto line = c->line;
    auto range = !fl->parlo;
    L("/* par for: its iterations in chunks on the program's pool, or here */");
    // The iterations, evaluated once, before any runs: a range's ends, or the
    // view of the sequence the loop walks.
    string lo, hi, seqdata, seqlen;
    auto ct = range ? CT(sp->argtypes[0]) : string("int64_t");
    if (range) {
        auto lox = GenX(c->args[0]);
        lo = T();
        L(ct, " ", lo, " = ", lox, ";");
        auto hix = GenX(c->args[1]);
        hi = T();
        L(ct, " ", hi, " = ", hix, ";");
    } else {
        auto lv = GenLoc(c->args[2]);
        if (lv.t->kind == TY_REF) DerefLoc(lv);
        auto v = ArrayView(lv);
        seqdata = T();
        L("void *", seqdata, " = (void *)(", v.elems, ");");
        seqlen = T();
        L("int64_t ", seqlen, " = ", v.len, ";");
        lo = "(int64_t)0";
        hi = seqlen;
    }
    EmitChunkAtChecks(fl, line);
    auto n = T();
    L("uint64_t ", n, " = ", hi, " > ", lo, " ? (uint64_t)", hi, " - (uint64_t)", lo, " : 0;");
    // What the function takes beyond the chunk: the sequence, then its free
    // variables as a call passes them (EmitFvArg), a reduction's among them,
    // and the caller's location for a standard library function's.
    vector<string> fixed;
    if (!range) {
        fixed.push_back(seqdata);
        fixed.push_back(seqlen);
    }
    vector<pair<size_t, VarDef *>> reds;
    for (auto fv : ki.freevars) {
        auto at = fixed.size();
        EmitFvArg(fv, fixed);
        for (auto &[rv, op] : fl->reductions)
            if (rv == fv && fixed.size() == at + 1) reds.push_back({ at, rv });
    }
    MarkFlush();
    auto ran = T();
    L("int ", ran, " = 0;");
    L("#if GS_NEED_THREADS");
    {
        auto pp = T();
        L("gs_pool ", pp, " = gs_par_claim(", n, " > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)",
          n, ");");
        L("if (", pp, ") {");
        ind++;
        auto thunk = EnsureParThunk(sp);
        auto &pt = parthunks[thunk];
        auto nc = T();
        L("int64_t ", nc, " = gs_par_chunks(", pp, ", ", n, " > (uint64_t)INT64_MAX ? INT64_MAX : "
          "(int64_t)", n, ");");
        // The chunk's own accumulators, each starting at its operation's
        // identity.
        vector<string> accs;
        for (auto &[at, rv] : reds) {
            accs.push_back(T());
            L(CT(rv->type), " ", accs.back(), "[GS_PAR_MAXCHUNKS];");
        }
        auto env = T();
        L(pt.env, " ", env, ";");
        L(env, ".gl = gs_gl;");
        auto first = range ? (fl->parix ? 3 : 2) : 2;   // The fields past the chunk's.
        for (size_t i = 0; i < fixed.size(); i++) L(env, ".a", first + i, " = ", fixed[i], ";");
        if (ki.located) L(env, ".at = ", CallerLoc(line), ";");
        auto ci = T(), per = T(), extra = T();
        L("uint64_t ", per, " = ", n, " / (uint64_t)", nc, ", ", extra, " = ", n, " % (uint64_t)",
          nc, ";");
        L("for (int64_t ", ci, " = 0; ", ci, " < ", nc, "; ", ci, "++) {");
        ind++;
        // Chunk ci of nc: contiguous, the first `extra` one longer.
        auto s = T(), e = T();
        L("uint64_t ", s, " = (uint64_t)", ci, " * ", per, " + ((uint64_t)", ci, " < ", extra,
          " ? (uint64_t)", ci, " : ", extra, ");");
        L("uint64_t ", e, " = ", s, " + ", per, " + ((uint64_t)", ci, " < ", extra, ");");
        L(env, ".a0 = (", ct, ")(", lo, " + ", s, ");");
        L(env, ".a1 = (", ct, ")(", lo, " + ", e, ");");
        if (range && fl->parix) L(env, ".a2 = (int64_t)", s, ";");
        for (size_t r = 0; r < reds.size(); r++) {
            auto rv = reds[r].second;
            auto op = fl->reductions[0].second;
            for (auto &[v, o] : fl->reductions) if (v == rv) op = o;
            L(accs[r], "[", ci, "] = ", op == T_ANDEQ ? cat("(", CT(rv->type), ")~(uint64_t)0")
                                                      : string("0"), ";");
            L(env, ".a", first + reds[r].first, " = &", accs[r], "[", ci, "];");
        }
        L("gs_par_submit(", pp, ", gs_par_run, ", thunk, ", &", env, ", sizeof ", env, ");");
        ind--;
        L("}");
        L("gs_par_join(", pp, ");");
        // The chunks' totals, in chunk order (§4.6).
        for (size_t r = 0; r < reds.size(); r++) {
            auto rv = reds[r].second;
            auto op = fl->reductions[0].second;
            for (auto &[v, o] : fl->reductions) if (v == rv) op = o;
            auto lv = VarLoc(rv).s;
            auto cj = T();
            L("for (int64_t ", cj, " = 0; ", cj, " < ", nc, "; ", cj, "++) ", lv, " = ",
              ArithX(op, rv->type, lv, cat(accs[r], "[", cj, "]"), line), ";");
        }
        L(ran, " = 1;");
        ind--;
        L("}");
    }
    L("#endif");
    // Run here: the whole range in one call.
    L("if (!", ran, ") {");
    ind++;
    vector<string> args;
    if (range) {
        args = { lo, hi };
        if (fl->parix) args.push_back("0");
    } else {
        args = { "0", seqlen };
    }
    for (auto &f : fixed) args.push_back(f);
    if (ki.needssp) {
        NoStackAcrossCycleCall(sp, line);
        NoteStackCall(sp, line);
        args.push_back(SpTop());
    }
    if (ki.located) args.push_back(CallerLoc(line));
    string argstr;
    for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
    L(ki.cname, "(", argstr, ");");
    ind--;
    L("}");
    MarkReload();
    if (ki.hasrf) EmitRfCheck(sp);
    return {};
}

// The entry thunk of sp's chunks on the pool's workers, by name, and the
// environment its packet holds: its body follows once the stacks the chunk's
// function needs are known (EmitParThunks).
inline string CodeGen::EnsureParThunk(FnSpec *sp) {
    for (auto &[name, pt] : parthunks)
        if (pt.spec == sp) return name;
    auto base = Sanitize(sp->sf->ns, sp->sf->name);
    auto name = Unique(cat("gs_parthunk_", base));
    auto &pt = parthunks[name];
    pt.spec = sp;
    pt.env = Unique(cat("gs_parenv_", base));
    auto types = ParParamTypes(sp);
    string d = cat("typedef struct {\n    void *gl;\n");
    for (size_t i = 0; i < types.size(); i++) Append(d, "    ", types[i], " a", i, ";\n");
    if (sinfo[sp].located) d += "    const gs_loc *at;\n";
    Append(d, "} ", pt.env, ";\n");
    tdecls += d;
    Append(protos, "#if GS_NEED_THREADS\nstatic void ", name, "(uint8_t *p);\n#endif\n");
    return name;
}

// The thunks: each installs the forking program instance's globals, opens a
// block of the stacks its chunk's function needs, and calls it. The most
// stacks any of them opens, which a worker's registry must hold.
inline int64_t CodeGen::EmitParThunks() {
    int64_t most = 0;
    for (auto &[name, pt] : parthunks) {
        auto sp = pt.spec;
        auto &ki = sinfo[sp];
        auto stacks = ProgramStacks({ sp });
        CheckStackLimit(cat("a par for's chunk (", sp->sf->name, ")"), stacks, sp->sf->line);
        most = std::max(most, stacks);
        auto n = ParParamTypes(sp).size();
        string args;
        for (size_t i = 0; i < n; i++) Append(args, i ? ", " : "", "e->a", i);
        if (ki.needssp) Append(args, n ? ", " : "", "0");
        if (ki.located) Append(args, n || ki.needssp ? ", " : "", "e->at");
        Append(code, "#if GS_NEED_THREADS\nstatic void ", name, "(uint8_t *p) {\n    ", pt.env,
               " *e = (", pt.env, " *)p;\n    gs_gl = e->gl;\n");
        if (stacks > 0) Append(code, "    gs_stack_block(", stacks, ");\n");
        Append(code, "    ", ki.cname, "(", args, ");\n}\n#endif\n\n");
    }
    return most;
}

}  // namespace goose
