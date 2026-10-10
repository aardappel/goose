// Goose compiler — codegen's statements (definitions of CodeGen members,
// codegen.h): blocks and scopes, loops with their break and continue paths,
// declarations and assignment, and returns (§7.3, §7.9).
#pragma once

namespace goose {

// A Block's contents without emitting the braces/scope (the caller did).
// The block is a region a stack it grows outside every loop can be cached
// over (TopW), rather than the whole body.
inline void CodeGen::GenBlockInner(Block *b, Dst d, size_t first, int exitscope) {
    auto region = MarkBlockBegin();
    for (auto i = first; i < b->stmts.size(); ++i) GenStmt(b->stmts[i]);
    if (b->tail && !IsVoidT(b->tail->exprtype) && d.k != DK_DISCARD) {
        // An inlined body's tail is a return too: another named result may
        // already occupy its destination, just as at an explicit return.
        if (exitscope >= 0) GenExitValue(b->tail, exitscope);
        else GenAny(b->tail, d);
    }
    else if (b->tail) GenAny(b->tail, Dst {});
    MarkLoopEnd(region);
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
                                 const string &forhead, Node *cond, size_t first,
                                 vector<const VarDef *> binders) {
    auto divisors = HoistDivisors(bodyb, cond, binders);
    PushSc(SC_LOOP);
    auto si = (int)cscopes.size() - 1;
    cscopes[si].brklbl = Lbl();
    cscopes[si].cntlbl = Lbl();
    EnterDst(si, d);
    auto loopid = MarkLoopBegin();
    L(forhead.empty() ? "for (;;) {" : forhead);
    ind++;
    // The condition is the loop's exit, a branch that stays, so only the
    // body's && and || may take the unconditional form.
    if (condexit) condexit();
    cscopes[si].straight = StraightCode(bodyb) && (!cond || StraightCode(cond));
    for (auto i = first; i < bodyb->stmts.size(); i++) GenStmt(bodyb->stmts[i]);
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
    for (auto &k : divisors) divmagic.erase(k);
    termjump = false;
}

// Whether n can compile to straight-line code once its ifs are converted:
// no loop, call, break, continue or return in it, which is the loop body a C
// compiler if-converts and vectorizes.
inline bool CodeGen::StraightCode(Node *n) {
    if (!n) return true;
    if (Is<While>(n) || Is<ForLoop>(n) || Is<LoopExpr>(n) || Is<Call>(n) || Is<Break>(n) ||
        Is<Continue>(n) || Is<Return>(n))
        return false;
    auto ok = true;
    n->Children([&](Node *ch) { ok = ok && StraightCode(ch); });
    return ok;
}

// Whether the code being emitted is in a loop StraightCode accepts, the
// innermost one around it in this function.
inline bool CodeGen::InStraightLoop() {
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (cscopes[i].kind == SC_LOOP) return cscopes[i].straight;
        if (cscopes[i].kind == SC_FN) return false;
    }
    return false;
}

// An unsigned `/` or `%`, or a signed one BCE found to be of nonnegative
// operands (Binary::nonneg), by a divisor DivisorKey names, which
// HoistDivisors may find a loop cannot change.
inline bool CodeGen::LoopDivisible(Binary *b) {
    if (b->op != T_DIV && b->op != T_MOD) return false;
    auto t = b->left->exprtype;
    if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT) return false;
    if (!IsUnsigned(t->intstorage) && !b->nonneg) return false;
    auto rt = b->right->exprtype;
    return rt && rt->kind == TY_INT && !DivisorKey(b->right).empty();
}

// What a divisor a loop may fix is known by, or "" where it is none: an
// integer variable, a field of a fixed-size struct variable (or of a field
// of one, and so on), or an integer conversion of one of those. `root` gets
// the variable and `path` whether fields lead from it to the value.
inline string CodeGen::DivisorKey(Node *n, const VarDef **root, bool *path) {
    auto isint = [](TypeExpr *t) { return t && t->kind == TY_INT && t->intstorage != IS_VARINT; };
    if (!n || fillvalues.count(n)) return "";
    if (auto id = Is<Ident>(n)) {
        auto vd = id->vdef;
        if (!vd || !vd->type) return "";
        if (!isint(vd->type) && (vd->type->kind != TY_STRUCT || !IsFix(vd->type))) return "";
        if (root) *root = vd;
        if (path) *path = false;
        return cat("v", (uintptr_t)vd);
    }
    if (auto d = Is<Dot>(n)) {
        auto ot = d->obj->exprtype;
        if (!d->IsField() || !ot || ot->kind != TY_STRUCT || !IsFix(ot)) return "";
        auto k = DivisorKey(d->obj, root, path);
        if (k.empty()) return "";
        if (path) *path = true;
        return cat(k, ".", d->fieldidx);
    }
    if (auto c = Is<AsCast>(n)) {
        if (!isint(c->totype) || !isint(c->child->exprtype)) return "";
        auto k = DivisorKey(c->child, root, path);
        if (k.empty()) return "";
        return cat("(", k, c->unchecked ? " as! " : " as ", IntStorageName(c->totype->intstorage),
                   ")");
    }
    return "";
}

// The value of a divisor DivisorKey names, computed ahead of a loop without
// the checks of its conversions: `ok` gets the condition under which none
// of them would fail (or stays "1"), and the value is used only under it.
inline string CodeGen::DivisorValue(Node *n, string &ok) {
    auto c = Is<AsCast>(n);
    if (!c) return GenX(n);
    auto x = DivisorValue(c->child, ok);
    auto st = c->child->exprtype;
    auto is = c->totype->intstorage;
    string cond;
    if (!c->unchecked && !TEq(st, c->totype)) {
        // As AsCast::CgX checks it.
        auto [lo, hi] = IntRange(is);
        if (st->intstorage == IS_U64) {
            if (is != IS_U64) cond = cat("(uint64_t)(", x, ") <= ", (uint64_t)hi, "ULL");
        } else {
            auto [slo, shi] = IntRange(st->intstorage);
            if (slo < lo) cond = cat("(int64_t)(", x, ") >= ", IntStr(lo));
            if (shi > hi)
                cond = cat(cond, cond.empty() ? "" : " && ", "(int64_t)(", x, ") <= ", IntStr(hi));
        }
    }
    if (!cond.empty()) ok = ok == "1" ? cond : cat(ok, " && ", cond);
    return cat("(", IntCT(is), ")(", x, ")");
}

// The variables of the specialization being emitted that a reference is
// made to anywhere in it, whose storage may then change where no assignment
// names them: the roots of `&` paths (written, or made where an lvalue binds
// by reference, §4.1), of `.=` bindings, of what a by-reference `for` or
// `match` binding or a function value's reference parameter is bound to,
// and of what a user `format` overload is handed (§3.7). An operand that is
// no path counts every variable it names.
inline const set<const VarDef *> &CodeGen::RefdLocals() {
    assert(curspec);
    if (refdspec == curspec) return refdlocals;
    refdspec = curspec;
    refdlocals.clear();
    function<void(Node *)> all = [&](Node *n) {
        if (!n) return;
        if (auto id = Is<Ident>(n); id && id->vdef) refdlocals.insert(id->vdef);
        RunChildren(n, all);
    };
    auto mark = [&](Node *n) {
        while (n) {
            if (auto id = Is<Ident>(n)) {
                if (id->vdef) refdlocals.insert(id->vdef);
                return;
            }
            if (auto d = Is<Dot>(n)) n = d->obj;
            else if (auto ix = Is<Index>(n)) n = ix->obj;
            else if (auto sl = Is<SliceExpr>(n)) n = sl->obj;
            else if (auto u = Is<Unary>(n)) n = u->child;
            else return all(n);
        }
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) mark(u->child);
        if (auto vd = Is<VarDecl>(n); vd && vd->byref) for (auto i : vd->inits) mark(i);
        if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN) mark(a->rhs);
        if (auto fl = Is<ForLoop>(n);
            fl && (fl->iterkind == IK_ARRAY || fl->iterkind == IK_SLICE) &&
            !(fl->vdef && fl->vdef->copybind))
            mark(fl->iter);
        if (auto me = Is<MatchExpr>(n))
            for (auto &arm : me->arms) if (arm.pat.byref) mark(me->scrutinee);
        if (auto c = Is<Call>(n)) {
            if (!c->fmtspecs.empty()) for (auto a : c->args) all(a);
            for (size_t i = 0; i < c->fvparams.size() && i < c->args.size(); i++) {
                auto pt = c->fvparams[i]->type;
                if (pt && (pt->kind == TY_REF || pt->kind == TY_SLICE)) mark(c->args[i]);
            }
        }
        RunChildren(n, walk);
    };
    walk(curspec->body);
    return refdlocals;
}

// The divisions in a loop by a divisor the loop cannot change (LoopDivisible)
// multiply by a magic number computed here, ahead of the loop, once for every
// loop nested in it as well: a hardware division takes several times as long
// as a multiply and a shift. The divisor is a variable bound before the loop
// -- named already, and bound nowhere inside, nor by the loop itself
// (`binders`, a `for`'s variables) -- or fields of one, or a conversion of
// either, and the loop (its condition included) writes nothing of that
// variable. Nothing else may: no reference is made to the variable anywhere
// in the function (RefdLocals), nor may another function's code reach it
// (captured, or a function's named result built at its destination). A
// global, which other functions' code may write, or a variable of global
// initialization code, which RefdLocals does not see, counts only as an
// integer `let` no writable reference is bound to (VarDef::refwrite). A
// conversion that would fail leaves the divisions plain, to fail where and
// when one runs. Returns the divisors it adds to divmagic, for the loop to
// retire. Tail-recursion elimination turns self-calls into a loop that
// assigns the parameters each round (optimize_tre.h), which is a write.
inline vector<string> CodeGen::HoistDivisors(Block *body, Node *cond,
                                             const vector<const VarDef *> &binders) {
    vector<string> added;
    set<const VarDef *> inner(binders.begin(), binders.end());
    vector<Binary *> divs;
    function<void(Node *)> all = [&](Node *n) {
        if (!n) return;
        if (auto id = Is<Ident>(n); id && id->vdef) inner.insert(id->vdef);
        RunChildren(n, all);
    };
    // The variable a written place lies in; every one an lvalue names that
    // is no path.
    auto written = [&](Node *n) {
        while (n) {
            if (auto id = Is<Ident>(n)) {
                if (id->vdef) inner.insert(id->vdef);
                return;
            }
            if (auto d = Is<Dot>(n)) n = d->obj;
            else if (auto ix = Is<Index>(n)) n = ix->obj;
            else if (auto sl = Is<SliceExpr>(n)) n = sl->obj;
            else if (auto u = Is<Unary>(n)) n = u->child;
            else return all(n);
        }
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || Is<FnDecl>(n) || Is<FunVal>(n)) return;
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) inner.insert(d);
        if (auto a = Is<Assign>(n)) written(a->lval);
        if (auto a = Is<IncDec>(n)) written(a->lval);
        if (auto fl = Is<ForLoop>(n)) { inner.insert(fl->vdef); inner.insert(fl->idxdef); }
        if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) inner.insert(arm.binder);
        if (auto c = Is<Call>(n)) {
            for (auto p : c->fvparams) inner.insert(p);
            if (c->builtin >= 0 && (BuiltinByKind(c->builtin).flags & BF_WRITE))
                written(c->FirstArg());
        }
        if (auto b = Is<Binary>(n); b && LoopDivisible(b)) divs.push_back(b);
        RunChildren(n, walk);
    };
    walk(body);
    walk(cond);
    for (auto b : divs) {
        const VarDef *root = nullptr;
        auto path = false;
        auto key = DivisorKey(b->right, &root, &path);
        if (divmagic.count(key) || inner.count(root) || root->captured || root->refwrite ||
            nrvo.count(root))
            continue;
        if (root->isglobal || !curspec) {
            if (path || root->isvar || !(root->isglobal ? gnames : vnames).count(root)) continue;
        } else if (!vnames.count(root) || RefdLocals().count(root)) {
            continue;
        }
        auto ok = string("1");
        auto x = DivisorValue(b->right, ok);
        auto v = T(), m = T(), more = T();
        L("uint8_t ", more, ";");
        L("uint64_t ", v, " = (uint64_t)(", x, ");");
        if (ok == "1") L("uint64_t ", m, " = gs_divu_gen(", v, ", &", more, ");");
        else L("uint64_t ", m, " = (", ok, ") ? gs_divu_gen(", v, ", &", more, ") : (", more,
               " = GS_DIVU_NONE, 0);");
        divmagic[key] = { v, m, more };
        added.push_back(key);
    }
    return added;
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
// Loops in blocks (ForLoop::stripk, ForLoop::sumred; optimize_loops.h).

// A local a body copy can declare again under a name of its own: one held
// in the C frame, which no other function reaches and which is no named
// result built at the destination.
inline bool CodeGen::DupLocal(VarDef *v) {
    return !v || (v->type && IsFix(v->type) && !IsLargeFixed(v->type) && !v->captured &&
                  !v->reusable && !nrvo.count(v));
}

// Whether a body can be emitted twice. Paths to variable-size storage are
// fine; a value of variable size built in the body, a nested function or a
// function value, and the locals DupLocal refuses are not.
inline bool CodeGen::Dupable(Node *n) {
    if (!n) return true;
    if (Is<FunVal>(n) || Is<FnDecl>(n)) return false;
    auto path = Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n);
    auto t = n->exprtype;
    if (!path && t && t->kind != TY_VOID &&
        (t->kind == TY_FN || !IsFix(t) || IsLargeFixed(t)))
        return false;
    auto ok = true;
    if (auto vd = Is<VarDecl>(n)) for (auto v : vd->defs) ok = ok && DupLocal(v);
    if (auto fl = Is<ForLoop>(n)) ok = DupLocal(fl->vdef) && DupLocal(fl->idxdef);
    if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) ok = ok && DupLocal(arm.binder);
    if (auto c = Is<Call>(n)) for (auto p : c->fvparams) ok = ok && DupLocal(p);
    RunChildren(n, [&](Node *ch) { ok = ok && Dupable(ch); });
    return ok;
}

// The number of iterations a loop's body runs in a block, or 0 where it runs
// them one at a time. The caller has checked that the iteration count is
// fixed at entry.
inline int CodeGen::BlockSize(ForLoop *f) {
    if (!f->stripk && !f->sumred) return 0;
    if (!DupLocal(f->vdef) || !DupLocal(f->idxdef) || !Dupable(f->body)) return 0;
    return f->stripk ? f->stripk : SUMBLOCK;
}

// Whether at least k iterations are left of a range whose counter ctr, of
// type ct, runs below hi: hi - ctr >= k, where the subtraction can overflow
// neither in C nor in the counter's own type. A counter that started at 0
// is never negative.
inline string CodeGen::AtLeastLeft(TypeExpr *ct, const string &hi, const string &ctr, int k,
                                   bool fromzero) {
    if (IntBits(ct->intstorage) < 64) return cat("(int64_t)", hi, " - (int64_t)", ctr, " >= ", k);
    if (IsUnsigned(ct->intstorage)) return cat(ctr, " < ", hi, " && ", hi, " - ", ctr, " >= ", k);
    if (fromzero) return cat(hi, " - ", ctr, " >= ", k);
    return cat(ctr, " < ", hi, " && (uint64_t)", hi, " - (uint64_t)", ctr, " >= ", k);
}

// One copy of a loop's body in the C loop the caller opened, ending where a
// continue in it lands.
inline void CodeGen::GenBodyCopy(Block *bodyb, int si) {
    for (auto st : bodyb->stmts) GenStmt(st);
    if (bodyb->tail) GenStmt(bodyb->tail);
    EmitRestores(cscopes[si]);
    if (cscopes[si].usedcnt) L(cscopes[si].cntlbl, ":;");
}

// `while (<more>) { k iterations }`, then `for (; <cond>; <step>)` for the
// rest; `bind` declares an iteration's loop variables, and `step` advances
// them. The two copies of the body share one Goose loop scope, so a break in
// either leaves both, while each has its own continue label.
//
// Strip-mined (stripk), the block is a loop of k iterations whose counter is
// the value of every `i % k` the body takes. An in-order sum (sumred)
// computes the block's k terms into a local array, then adds them to the sum
// one after another, as the loop would have.
inline void CodeGen::GenBlocked(ForLoop *f, int k, const string &more, const string &step,
                                const function<void()> &bind, const string &cond) {
    PushSc(SC_LOOP);
    auto si = (int)cscopes.size() - 1;
    cscopes[si].brklbl = Lbl();
    cscopes[si].cntlbl = Lbl();
    auto loopid = MarkLoopBegin();
    {
        NameScope ns(*this);
        L(f->stripk ? "/* loop shape: strip-mined by " : "/* loop shape: sum terms in blocks of ",
          k, " */");
        L("while (", more, ") {");
        ind++;
        auto l = T();
        auto inner = cat("for (int64_t ", l, " = 0; ", l, " < ", k, "; ", l, "++, ", step, ") {");
        if (f->stripk) {
            L(inner);
            ind++;
            bind();
            for (auto m : f->lanemods) substs[m] = cat("((", CT(m->exprtype), ")", l, ")");
            GenBodyCopy(f->body, si);
            for (auto m : f->lanemods) substs.erase(m);
            ind--;
            L("}");
        } else {
            auto a = Is<Assign>(f->body->stmts.empty() ? f->body->tail : f->body->stmts[0]);
            auto sum = GenLoc(a->lval);
            auto terms = T();
            L(CT(sum.t), " ", terms, "[", k, "];");
            L(inner);
            ind++;
            bind();
            PushSc(SC_STMT);
            termjump = false;
            auto r = GenX(a->rhs);
            L(terms, "[", l, "] = ", r, ";");
            PopSc();
            termjump = false;
            EmitRestores(cscopes[si]);
            ind--;
            L("}");
            auto l2 = T();
            L("for (int64_t ", l2, " = 0; ", l2, " < ", k, "; ", l2, "++) ", sum.s, " = ", sum.s,
              " + ", terms, "[", l2, "];");
        }
        ind--;
        L("}");
    }
    cscopes[si].saves.clear();
    cscopes[si].usedcnt = false;
    cscopes[si].cntlbl = Lbl();
    L("for (; ", cond, "; ", step, ") {");
    ind++;
    bind();
    GenBodyCopy(f->body, si);
    ind--;
    L("}");
    auto usedbrk = cscopes[si].usedbrk;
    auto brklbl = cscopes[si].brklbl;
    cscopes.back().saves.clear();
    PopSc();
    if (usedbrk) L(brklbl, ":;");
    MarkLoopEnd(loopid);
    termjump = false;
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
    if (d.k != DK_STACK || !NrvoBoundAt(val, d.s, d.lenlv)) NoSelfRelCopy(val);
    auto start = d.k == DK_STACK ? ExitStart(val, d.s, d.lenlv, cscopes[si].open0) : "";
    GenAny(val, d);
    if (!start.empty())
        LandValue(d.s, ScopeTop0(si), start, d.t ? d.t : val->exprtype, d.lenlv);
}

// ------------------------------------------------------------------
// Declarations and assignment.

inline void CodeGen::BindLocal(VarDef *d, Node *init, bool forlocal) {
    // An alias reads the variable it stands for; its initializer names that
    // variable and does nothing else.
    if (refalias.count(d)) {
        aliasbound.insert(d);
        return;
    }
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
            // A return of another value finds it in front (ExitStart).
            if (!nd.inlined) openat[stk]++;
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
        if (nit != nrvo.end() && !nit->second.inlined) openat[stk]++;
        L("uint8_t *", name, " = ", Top(stk), ";");
        // A named result lives at the destination, which outlives us.
        if (nit == nrvo.end()) SaveBase(forlocal, stk, name);
        vstk[d] = stk;
        GenConstruct(init, stk, t);
        return;
    }
    if (LargeFixedOnStack(t)) {
        FixedLocal(t, name, "", forlocal);
        vnames[d] = name;
        if (init) GenAny(init, Dst { DK_LVALUE, name, t });
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
    // A relative reference variable holds the offset (§3.9): whatever the
    // initializer, its value is the plain reference to encode.
    if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
        L(CT(t), " ", name, ";");
        EmitRelStoreAt(cat("(uint8_t *)&", name), t, GenX(init), init->line, true,
                       RelValue(init));
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
    if (IsStaticLimited(t)) {
        L(CT(t), " ", name, ";");
        if (GenIntoLimited(init, t, name, false)) return;
        L(name, " = ", GenXD(init, t), ";");
        return;
    }
    L(CT(t), " ", name, " = ", GenXD(init, t), ";");
}

// A gs_pref value from a &pool expression or another pool reference, or
// from a construct choosing among pools of one kind (MergeVals keeps the
// bits they share), which each branch builds into one temporary.
inline string CodeGen::GenPrefVal(Node *n) {
    if (IsCtl(n)) {
        auto t = T();
        L("gs_pref ", t, ";");
        GenAny(n, Dst { DK_LVALUE, t, n->exprtype, "", true });
        return t;
    }
    if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
        if (auto id = Is<Ident>(u->child); id && id->vdef && id->vdef->reusable) {
            auto vd = id->vdef;
            auto pit = vpool.find(vd);
            auto git = gpools.find(vd);
            // A captured pool arrives as a gs_pref (VarLoc).
            if (pit == vpool.end() && git == gpools.end()) return VName(vd);
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

inline void CodeGen::GenRelAssign(Loc lv, Node *lval, Node *rhs, Line ln) {
    auto rv = GenX(rhs);
    // Varint-width relative references are construction-only (typechecked).
    assert(lv.t->ref->lenstorage != IS_VARINT);
    auto f = RelValue(rhs);
    if (auto d = Is<Dot>(lval); d && d->IsField()) {
        auto ht = d->obj->exprtype;
        if (ht && ht->kind == TY_REF) ht = ht->ref->sub;
        if (ht && ht->kind == TY_STRUCT && IsFix(ht))
            f.apart = RelSlotApart(lv.t, ht, StructLayout(SI(ht)).offs[(size_t)d->fieldidx]);
    }
    EmitRelStoreAt(BytesAddrOf(lv), lv.t, rv, ln, true, f);
}

inline void CodeGen::GenRebind(Assign *a, Loc lv) {
    if (lv.t->kind == TY_REF && lv.t->ref->lenstorage >= 0) {
        GenRelAssign(lv, a->lval, a->rhs, a->line);
        return;
    }
    assert(lv.val);
    if (Is<NullLit>(a->rhs)) {
        if (IsResz(lv.t->ref->sub)) L("memset(&", lv.s, ", 0, sizeof(", lv.s, "));");
        else L(lv.s, " = NULL;");
        return;
    }
    L(lv.s, " = ", lv.ispref ? GenPrefVal(a->rhs) : GenX(a->rhs), ";");
}

// ------------------------------------------------------------------
// Returns: normal, forwarding a multi-value call, exiting an inlined
// body, and long-distance (§7.9).

// A multi-value call whose results a return forwards (§7.1) into channels of
// the returned types `rets`: chans[i] is where result i goes -- the stack a
// bytes-class value is built on (a resizable one's count into its lenlv), or
// the lvalue a fixed one lands in, a temporary where it names none. A result
// the checker adapted to its return type (CheckReturn: an integer to a wider
// type or a float, a variant to its ADT, a reference to its pointee) arrives
// in the callee's type first and converts into its channel; a variant that
// is not fixed-size builds its variable-mode ADT in place instead, behind
// the tag (GenAdtAdapted). Returns each fixed result's C expression, which
// the caller stores.
inline vector<string> CodeGen::GenForward(Call *c, const vector<TypeExpr *> &rets,
                                          const vector<Dst> &chans) {
    vector<Dst> dsts;
    vector<Loc> arrived(rets.size());
    // A tag written ahead of the call sits in front of whatever an exit
    // taken in the call's arguments builds on its stack (ExitStart).
    deque<OpenAt> open;
    for (size_t i = 0; i < rets.size(); i++) {
        auto ct = c->rettypes[i];
        if (TEq(ct, rets[i])) {
            auto d = chans[i];
            if (d.k == DK_DISCARD) {
                d = Dst { DK_LVALUE, T() };
                FixedLocal(rets[i], d.s);
            }
            dsts.push_back(d);
            continue;
        }
        if (ct->kind == TY_VARIANT && IsBytesT(ct)) {
            open.emplace_back(*this, chans[i].s);
            dsts.push_back(VariantBehindTag(ct, rets[i], chans[i]));
            continue;
        }
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
            FixedLocal(ct, lv.s);
            dsts.push_back(Dst { DK_LVALUE, lv.s, ct });
        }
    }
    auto res = EmitCall(c, dsts[0], &dsts);
    open.clear();
    vector<string> vals(rets.size());
    for (size_t i = 0; i < rets.size(); i++) {
        auto rt = rets[i];
        auto r = i < res.size() ? res[i] : string();
        if (auto lv = arrived[i]; lv.t) {
            // Where the call left the value (its C result, say).
            if (!IsResz(lv.t) && !r.empty()) lv.s = r;
            if (IsBytesT(rt)) ConstructFromLoc(lv, rt, chans[i].s, chans[i].lenlv, c->line);
            else vals[i] = LoadLoc(lv, rt, c->line);
        } else if (!IsBytesT(rt)) {
            vals[i] = !r.empty() ? r : dsts[i].s;
        }
    }
    return vals;
}

inline void CodeGen::GenNormalReturn(const vector<Node *> &vals) {
    auto sp = curspec;
    assert(sp);
    auto &si = *curinfo;
    string retv;
    // A single call forwards all its return values (§7.1).
    if (vals.size() == 1 && sp->rets.size() > 1) {
        if (auto c = Is<Call>(vals[0]); c && c->rettypes.size() == sp->rets.size()) {
            vector<Dst> chans(sp->rets.size());
            vector<string> starts(sp->rets.size());
            for (size_t i = 0; i < sp->rets.size(); i++) {
                auto rt = sp->rets[i];
                auto dst = cat("gs_dst", i);
                if (IsResz(rt)) chans[i] = Dst { DK_STACK, dst, rt, cat("(*gs_rl", i, ")") };
                else if (IsBytesT(rt)) chans[i] = Dst { DK_STACK, dst };
                else continue;
                starts[i] = ExitStart(c, dst, "", 0);
            }
            auto fixed = GenForward(c, sp->rets, chans);
            for (size_t i = 0; i < sp->rets.size(); i++) {
                if (IsBytesT(sp->rets[i])) continue;
                if ((int)i == si.cret) retv = fixed[i];
                else L("*gs_r", i, " = ", fixed[i], ";");
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
            NoSelfRelCopy(vals[i]);
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
            NoSelfRelCopy(vals[i]);
            auto start = ExitStart(vals[i], dst, "", 0);
            GenConstruct(vals[i], dst, rt);
            if (!start.empty()) LandValue(dst, DstTop0(i), start, rt, "");
        } else if ((int)i == si.cret) {
            retv = T();
            L(CT(rt), " ", retv, ";");
            GenAny(vals[i], Dst { DK_LVALUE, retv, rt });
        } else {
            auto tv = T();
            FixedLocal(rt, tv);
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
        EmitLenCheck(ls, count);
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
    if (nd.hdr.empty()) return;   // A packed non-resizable result is complete.
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
        vector<Dst> chans;
        for (size_t i = 0; i < rets.size(); i++) {
            auto stk = cat("gs_fdst_", tid, "_", i), lret = cat("gs_lret_", tid, "_", i);
            if (IsResz(rets[i])) chans.push_back(Dst { DK_STACK, stk, rets[i], lret });
            else if (IsBytesT(rets[i])) chans.push_back(Dst { DK_STACK, stk });
            else chans.push_back(Dst { DK_LVALUE, lret });
        }
        auto fixed = GenForward(Is<Call>(r->vals[0]), rets, chans);
        for (size_t i = 0; i < rets.size(); i++)
            if (!fixed[i].empty() && fixed[i] != chans[i].s) L(chans[i].s, " = ", fixed[i], ";");
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
            FixedLocal(rt, tv, r0);
            lv.s = tv;
        }
        return LoadLoc(lv, et, c->line);
    }
    if (rt && rt->kind == TY_REF && rt->ref->lenstorage < 0 && et &&
        et->kind != TY_REF && et->kind != TY_VOID) {
        if (IsVarintT(rt->ref->sub)) return cat("gs_zig_read(", r0, ")");
        if (IsResz(rt->ref->sub) || IsBytesT(rt->ref->sub)) return r0;
        return PointeeLv(r0, rt->ref->sub);
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
        lv.s = PointeeLv(r0, sub);
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
