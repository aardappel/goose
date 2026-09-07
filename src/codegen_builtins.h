// Goose compiler — codegen's builtins (definitions of CodeGen members,
// codegen.h): the builtin functions and array members (§3.3, §5.4), queues
// and threads (§11.2).
#pragma once

namespace goose {

// Element type mangle -> queue global.

inline string CodeGen::QueueFor(TypeExpr *t) {
    usesthreads = true;
    auto m = Mangle(t);
    auto it = queues.find(m);
    if (it != queues.end()) return it->second;
    auto name = Unique(cat("gs_q_", m));
    Append(data, "static gs_queue ", name, " = GS_QUEUE_INIT;\n");
    return queues[m] = name;
}

// The receiver of a member operation, dereferenced, with its stack.
inline CodeGen::Loc CodeGen::RecvLoc(Node *n) {
    auto lv = GenLoc(n);
    if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
    return lv;
}

inline TypeExpr *CodeGen::MakeSliceT(TypeExpr *elem, Line l) {
    auto t = ast.NewType(TY_SLICE, l);
    t->sub = elem;
    return t;
}

// The C type a length-field write casts to, per receiver representation.
inline string CodeGen::LenCast(const Loc &lv) {
    auto ak = lv.t->arr->akind;
    if (ak == A_LIMITED) return lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t";
    if (ak == A_VAR) return IntCT(LenStore(lv.t->arr));
    return "int64_t";
}

inline vector<string> CodeGen::EmitBuiltin(Call *c, Dst d0) {
    vector<Node *> an;
    if (auto dd = Is<Dot>(c->callee)) an.push_back(dd->obj);
    for (auto a : c->args) an.push_back(a);
    auto ln = c->line;
    switch ((BuiltinKind)c->builtin) {
        case B_PRINT: {
            // The whole line is rendered before any of it is written, so
            // an argument that prints on its own (a call) cannot interleave
            // with it (§3.7).
            auto b = TempBuilder();
            for (auto a : an) EmitFormatInto(b, a, ln, c);
            L("gs_out_bytes(", b.hdr, ".base, ", b.hdr, ".len);");
            L("gs_out_nl();");
            return {};
        }
        case B_STR: return EmitStr(c, an, d0, ln);
        case B_FORMAT: {
            auto lv = RecvLoc(an[0]);
            for (size_t i = 1; i < an.size(); i++) EmitFormatInto(lv, an[i], ln, c);
            return {};
        }
        case B_ASSERT: {
            auto x = GenTruth(an[0]);
            L("if (!(", x, ")) gs_abort(GS_E_ASSERT, ", LocArgs(ln), ");");
            return {};
        }
        case B_ABORT: {
            auto x = GenPure(an[0]);
            L("gs_abort_msg(", x, ".data, ", x, ".len, ", LocArgs(ln), ");");
            return {};
        }
        case B_EXIT:
            L("gs_exit(", GenX(an[0]), ");");
            return {};
        case B_COPY: return { GenXD(an[0], c->exprtype) };
        case B_TO_BYTES: return EmitToBytes(an, d0, ln);
        case B_BYTES_OF: return EmitBytesOf(c, an, ln);
        case B_FROM_BYTES: return EmitFromBytes(c, an, d0, ln);
        case B_DEFAULT: {
            auto t = c->rettypes[0];
            auto tv = T();
            L(CT(t), " ", tv, ";");
            EmitDefaultInto(tv, t);
            return { tv };
        }
        case B_HARDWARE_THREADS: return { "gs_hardware_threads()" };
        case B_THREAD_WAIT: {
            usesthreads = true;
            L("gs_thread_wait(", GenX(an[0]), ", ", LocArgs(ln), ");");
            return {};
        }
        case B_THREAD_SPAWN: return EmitThreadSpawn(c, an);
        case B_QPUT: {
            auto t = an[0]->exprtype;
            auto q = QueueFor(t);
            if (IsResz(t)) {
                // The image of a resizable is [int64 count][fixed fields]
                // [tail elements], built on a scratch stack: a frame
                // object's fixed fields are the bytes before its innermost
                // tail header, any other shape's the static prefix
                // EmitRzCopy walks.
                auto src = GenLoc(an[0]);
                if (src.t->kind == TY_REF) DerefLoc(src, ln);
                string stk;
                auto base = BytesTemp(stk);
                if (IsFrameObj(t)) {
                    assert(src.val);
                    auto th = FoTailHdr(t, src.s);
                    EmitValStore(stk, ast.inttypes[IS_I64], cat(th, ".len"));
                    auto pre = FoPrefixSize(t);
                    L("memcpy(", Top(stk), ", &", src.s, ", ", pre, ");");
                    Bump(stk, pre);
                    EmitCopyElems(stk, FoTailArr(t)->arr->sub, cat(th, ".base"),
                                  cat(th, ".len"));
                } else {
                    auto lenv = T();
                    L("int64_t ", lenv, ";");
                    EmitValStore(stk, ast.inttypes[IS_I64], "0");
                    EmitRzCopy(src, t, stk, lenv, ln);
                    L("*(int64_t *)", base, " = ", lenv, ";");
                }
                L("gs_qput(&", q, ", ", base, ", ", Top(stk), " - ", base, ");");
            } else if (IsBytesT(t)) {
                auto p = GenPtr(an[0]);
                L("gs_qput(&", q, ", ", p, ", ", SizeX(t, p), ");");
            } else {
                auto tv = T();
                L(CT(t), " ", tv, " = ", GenX(an[0]), ";");
                L("gs_qput(&", q, ", &", tv, ", ", FixedSize(t), ");");
            }
            return {};
        }
        case B_QGET: case B_QPOLL: {
            auto t = c->rettypes[0];
            auto q = QueueFor(t);
            auto nn = T();
            auto poll = c->builtin == B_QPOLL;
            L("gs_qnode *", nn, poll ? " = gs_qpoll(&" : " = gs_qget(&", q, ");");
            string got;
            if (poll) {
                got = T();
                L("uint8_t ", got, " = ", nn, " != NULL;");
            }
            if (IsResz(t)) {
                // The image is [int64 count][fixed fields][tail elements]
                // (see qput). The elements go to the destination's top;
                // the count, and a frame object's fixed fields, to the
                // receiving header or frame object (a temporary without
                // a receiver).
                EmitCoreTypes();
                string stk = d0.k == DK_STACK && !d0.lenlv.empty() ? d0.s : "";
                string lenlv = d0.k == DK_STACK ? d0.lenlv : "";
                string hv;
                if (stk.empty()) {
                    hv = RzTemp(t, stk);
                    lenlv = RzLenLv(t, hv);
                }
                auto fo = IsFrameObj(t);
                auto cnt = fo ? cat(FoTailHdr(t, lenlv), ".len") : lenlv;
                auto image = cat("(uint8_t *)(", nn, " + 1) + 8");
                auto n = cat("(", nn, "->size - 8)");
                if (fo) {
                    // The zero value until the image is read (a missed poll
                    // leaves it), with the tail header at the top, where
                    // the elements land.
                    L("memset(&", lenlv, ", 0, sizeof(", lenlv, "));");
                    L(FoTailHdr(t, lenlv), ".base = ", Top(stk), ";");
                } else if (poll) {
                    L(lenlv, " = 0;");
                }
                L(poll ? cat("if (", nn, ") {") : string("{"));
                ind++;
                if (fo) {
                    auto pre = FoPrefixSize(t);
                    L("memcpy(&", lenlv, ", ", image, ", ", pre, ");");
                    image = cat(image, " + ", pre);
                    n = cat("(", nn, "->size - 8 - ", pre, ")");
                }
                L(cnt, " = *(int64_t *)(", nn, " + 1);");
                L("memcpy(", Top(stk), ", ", image, ", (size_t)", n, ");");
                Bump(stk, n);
                L("free(", nn, ");");
                ind--;
                L("}");
                return poll ? vector<string> { hv, got } : vector<string> { hv };
            }
            if (IsBytesT(t)) {
                string stk = d0.k == DK_STACK ? d0.s : "";
                string base = T();
                if (stk.empty()) {
                    stk = AllocStk(false);
                    L("uint8_t *", base, " = ", Top(stk), ";");
                    SaveBase(false, stk, base);
                } else {
                    L("uint8_t *", base, " = ", Top(stk), ";");
                }
                if (poll) L("if (", nn, ") {");
                else L("{");
                ind++;
                L("memcpy(", Top(stk), ", ", nn, " + 1, (size_t)", nn, "->size);");
                Bump(stk, cat(nn, "->size"));
                L("free(", nn, ");");
                ind--;
                if (poll) {
                    // A missed poll still yields a valid (zero) value.
                    L("} else {");
                    ind++;
                    L("memset(", Top(stk), ", 0, ", ZeroSize(t), ");");
                    Bump(stk, cat(ZeroSize(t)));
                    ind--;
                }
                L("}");
                return poll ? vector<string> { base, got } : vector<string> { base };
            }
            auto tv = T();
            L(CT(t), " ", tv, ";");
            if (poll) {
                L("memset(&", tv, ", 0, sizeof(", tv, "));");
                L("if (", nn, ") { memcpy(&", tv, ", ", nn, " + 1, sizeof(", tv,
                  ")); free(", nn, "); }");
            } else {
                L("memcpy(&", tv, ", ", nn, " + 1, sizeof(", tv, "));");
                L("free(", nn, ");");
            }
            return poll ? vector<string> { tv, got } : vector<string> { tv };
        }
        case B_PUSH: return EmitPush(c, an, ln);
        case B_APPEND: EmitAppend(an, ln); return {};
        case B_INDEX_OF: {
            // The checker proved the reference is an element of this very
            // array (§3.3), so the distance is a whole number of elements
            // inside the length: an exact divide, nothing to check.
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv, ln);
            auto rx = GenX(an[1]);
            return { cat("(((uint8_t *)(", rx, ") - (uint8_t *)(", v.elems, ")) / ",
                         FixedSize(v.elem), ")") };
        }
        case B_POP: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv, ln);
            auto elem = v.elem;
            auto esz = FixedSize(elem);
            auto nl = T();
            L("int64_t ", nl, " = ", v.len, " - 1;");
            // On empty, the stored length would wrap (limited arrays) or
            // the stack top drop below the elements (grow-shrink).
            L("if (", nl, " < 0) gs_abort(GS_E_POP, ", LocArgs(ln), ");");
            L(v.lenlv, " = (", LenCast(lv), ")", nl, ";");
            auto tv = T();
            auto ak = lv.t->arr->akind;
            if (ak == A_GROWSHRINK || ak == A_GROW) {
                // The array tops its stack: the element region ends at top.
                L(TopW(lv.stk), " -= ", esz, ";");
                L(CT(elem), " ", tv, " = *(", CT(elem), " *)", Top(lv.stk), ";");
            } else if (v.typedelems) {
                L(CT(elem), " ", tv, " = ", v.elems, "[", nl, "];");
            } else {
                L(CT(elem), " ", tv, " = *(", CT(elem), " *)((", v.elems, ") + ", nl,
                  " * ", esz, ");");
            }
            return { tv };
        }
        case B_RESIZE: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv, ln);
            auto elem = v.elem;
            auto esz = FixedSize(elem);
            auto ak = lv.t->arr->akind;
            auto nn = GenPure(an[1]);
            string fv;
            if (an.size() > 2) fv = GenPure(an[2]);
            auto ol = T();
            L("int64_t ", ol, " = ", v.len, ";");
            // A negative target length shrinks past empty: the same
            // corruption as a pop on an empty array. An unsigned `n` above
            // INT64_MAX casts negative and is rejected here as well.
            L("if ((int64_t)(", nn, ") < 0) gs_abort(GS_E_RESIZENEG, ", LocArgs(ln),
              ");");
            L("if (", nn, " < ", ol, ") {");
            ind++;
            L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
            if (ak == A_GROWSHRINK || ak == A_GROW)
                L(TopW(lv.stk), " = (uint8_t *)(", v.elems, ") + ", nn, " * ", esz, ";");
            ind--;
            L("} else if (", nn, " > ", ol, ") {");
            ind++;
            if (fv.empty()) {
                L("gs_abort(GS_E_RESIZEFILL, ", LocArgs(ln), ");");
            } else {
                if (ak == A_LIMITED) {
                    auto capx = lv.val ? cat(ArrSize(lv.t->arr))
                                       : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
                    L("if (", nn, " > ", capx,
                      ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln),
                      ");");
                }
                auto iv = T();
                L("for (int64_t ", iv, " = ", ol, "; ", iv, " < ", nn, "; ", iv, "++) {");
                ind++;
                if (ak == A_LIMITED) {
                    if (v.typedelems) L(v.elems, "[", iv, "] = ", fv, ";");
                    else L("*(", CT(elem), " *)((", v.elems, ") + ", iv, " * ", esz,
                           ") = ", fv, ";");
                } else {
                    L("*(", CT(elem), " *)", Top(lv.stk), " = ", fv, ";");
                    L(TopW(lv.stk), " += ", esz, ";");
                }
                ind--;
                L("}");
                L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
            }
            ind--;
            L("}");
            return {};
        }
        case B_CLEAR: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv, ln);
            // A resizable is the topmost value on its stack for its whole
            // life (§1.3), so both flavors hand the element region back by
            // dropping the top to the base; a limited array's capacity is
            // reserved and only its length moves.
            auto ak = lv.t->arr->akind;
            if (ak == A_GROWSHRINK || ak == A_GROW)
                L(TopW(lv.stk), " = (uint8_t *)(", v.elems, ");");
            L(v.lenlv, " = 0;");
            return {};
        }
        case B_ALLOC_INDEX: case B_ALLOC_REF: return EmitAlloc(c, an, ln);
        case B_FREE: {
            auto lv = RecvLoc(an[0]);
            assert(!lv.fl.empty());
            auto x = GenX(an[1]);
            L("*(int64_t *)", Top(lv.flstk), " = ", x, ";");
            L(TopW(lv.flstk), " += 8;");
            L(lv.fl, ".len++;");
            return {};
        }
        default:
            Fail(ln, cat("builtin not implemented: ", builtindefs[c->builtin].name));
    }
}

// ------------------------------------------------------------------
// Text forms (§3.7): print, str and format share them. A scalar's text
// comes from a gs_fmt_* runtime helper writing at most GS_FMT_MAX bytes;
// a u8 array or slice contributes its bytes as they are.

// One print argument to stdout.
// ------------------------------------------------------------------
// Rendering aggregates (§3.7): the text of any value appended to a
// u8[>..] builder, structurally, or through a user `format` overload
// recorded on the call for that type.

inline vector<string> CodeGen::EmitPush(Call *c, vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    auto v = ArrayView(lv, ln);
    auto elem = v.elem;
    auto ak = lv.t->arr->akind;
    string ref;
    if (ak == A_LIMITED) {
        auto esz = FixedSize(elem);
        auto capx = lv.val ? cat(ArrSize(lv.t->arr)) : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
        auto nl = T();
        L("int64_t ", nl, " = ", v.len, ";");
        L("if (", nl, " >= ", capx, ") gs_abort(GS_E_CAPACITY, ",
          LocArgs(ln), ");");
        auto e = T();
        if (v.typedelems) L(CT(elem), " *", e, " = &", v.elems, "[", nl, "];");
        else L(CT(elem), " *", e, " = (", CT(elem), " *)((", v.elems, ") + ", nl, " * ",
               esz, ");");
        if (elem->kind == TY_REF && elem->ref->lenstorage >= 0) {
            // A relative-reference element stores the offset from its
            // own slot, not the pointer (§3.9).
            EmitRelStoreAt(cat("(uint8_t *)", e), elem, GenX(an[1]), ln, true);
        } else {
            auto ev = T();
            L(CT(elem), " ", ev, " = ", GenXD(an[1], elem), ";");
            L("*", e, " = ", ev, ";");
        }
        L(v.lenlv, " = (", lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t", ")(", nl,
          " + 1);");
        ref = e;
    } else {
        assert(!lv.stk.empty());
        auto e = T();
        if (IsBytesT(elem)) {
            L("uint8_t *", e, " = ", Top(lv.stk), ";");
        } else {
            L(CT(elem), " *", e, " = (", CT(elem), " *)", Top(lv.stk), ";");
        }
        GenConstruct(an[1], lv.stk, elem);
        L(v.lenlv, "++;");
        ref = e;
    }
    // push returns a reference on grow-only and limited arrays (§3.3).
    // An un-annotated receiver decayed the reference to an element copy.
    if (c->exprtype && c->exprtype->kind != TY_REF && c->exprtype->kind != TY_VOID &&
        !IsBytesT(elem))
        return { cat("(*", ref, ")") };
    return { ref };
}

inline void CodeGen::EmitAppend(vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    auto v = ArrayView(lv, ln);
    auto elem = v.elem;
    auto ak = lv.t->arr->akind;
    auto src = an[1];
    // append(f()) where f returns a resizable: the callee emits raw
    // elements at our top and hands back the count -- contiguous by
    // construction (§7.3).
    if (auto call = Is<Call>(src); call && IsResz(src->exprtype) && ak != A_LIMITED) {
        auto nn = T();
        L("int64_t ", nn, " = 0;");
        EmitCall(call, Dst { DK_STACK, lv.stk, src->exprtype, nn });
        L(v.lenlv, " += ", nn, ";");
        return;
    }
    // append(f()) where f returns a variable array: request the
    // element-run form (C.3) -- raw elements at our top plus a count.
    // Callees that cannot supply it fall back to a value-form call with
    // its length prefix slid out (inside EmitSpecCall).
    if (auto call = Is<Call>(src); call && IsBytesT(src->exprtype) && ak != A_LIMITED) {
        assert(src->exprtype->kind == TY_ARRAY);
        auto nn = T();
        L("int64_t ", nn, " = 0;");
        EmitCall(call, Dst { DK_STACK, lv.stk, src->exprtype, nn });
        L(v.lenlv, " += ", nn, ";");
        return;
    }
    auto se = GenSrcElems(src);
    auto nn = T();
    L("int64_t ", nn, " = ", se.n, ";");
    if (ak == A_LIMITED) {
        auto esz = FixedSize(elem);
        auto capx = lv.val ? cat(ArrSize(lv.t->arr)) : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
        auto ol = T();
        L("int64_t ", ol, " = ", v.len, ";");
        L("if (", ol, " + ", nn, " > ", capx,
          ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
        L("memcpy(", v.typedelems ? cat(v.elems, " + ", ol)
                                  : cat("(", v.elems, ") + ", ol, " * ", esz),
          ", ", se.elems, ", (size_t)(", nn, " * ", esz, "));");
        L(v.lenlv, " = (", lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t", ")(", ol,
          " + ", nn, ");");
        return;
    }
    assert(!lv.stk.empty());
    EmitCopyElems(lv.stk, elem, se.elems, nn);
    L(v.lenlv, " += ", nn, ";");
}

inline vector<string> CodeGen::EmitAlloc(Call *c, vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    assert(!lv.fl.empty() && !lv.stk.empty());
    auto v = ArrayView(lv, ln);
    auto elem = v.elem;
    auto esz = FixedSize(elem);
    // A literal holding relative references is built once the slot is
    // known, so its offsets measure from the slot; anything else keeps
    // its value evaluated ahead of the freelist bookkeeping.
    auto atslot = (Is<StructLit>(an[1]) || Is<ArrayLit>(an[1])) && HasRelRef(elem);
    auto ev = atslot ? string() : GenPure(an[1]);
    auto iv = T();
    L("int64_t ", iv, ";");
    L("if (", lv.fl, ".len > 0) {");
    ind++;
    L(lv.fl, ".len--;");
    L(TopW(lv.flstk), " -= 8;");
    L(iv, " = *(int64_t *)", Top(lv.flstk), ";");
    ind--;
    L("} else {");
    ind++;
    L(iv, " = ", lv.lenlv, ";");
    L(lv.lenlv, "++;");
    L(TopW(lv.stk), " += ", esz, ";");
    ind--;
    L("}");
    auto e = T();
    L(CT(elem), " *", e, " = (", CT(elem), " *)((", v.elems, ") + ", iv, " * ", esz,
      ");");
    if (atslot) FixedLitAtLv(an[1], cat("(*", e, ")"), true);
    else L("*", e, " = ", ev, ";");
    if (c->builtin == B_ALLOC_INDEX) return { iv };
    if (c->exprtype && c->exprtype->kind != TY_REF) return { cat("(*", e, ")") };
    return { e };
}

inline vector<string> CodeGen::EmitThreadSpawn(Call *c, vector<Node *> &an) {
    usesthreads = true;
    auto sp = c->spec;
    auto thunk = EnsureThreadThunk(sp);
    string stk;
    auto base = BytesTemp(stk);
    for (size_t i = 0; i < sp->argtypes.size(); i++) GenConstruct(an[1 + i], stk);
    return { cat("gs_thread_spawn(", thunk, ", ", base, ", ", Top(stk), " - ", base,
                 ")") };
}

inline string CodeGen::EnsureThreadThunk(FnSpec *sp) {
    auto it = thunks.find(sp);
    if (it != thunks.end()) return it->second;
    auto name = Unique(cat("gs_tmain_", Sanitize(sp->sf->name)));
    thunks[sp] = name;
    Append(protos, "static void ", name, "(uint8_t *p);\n");
    auto &ki = sinfo[sp];
    string b;
    Append(b, "static void ", name, "(uint8_t *p) {\n");
    vector<string> args;
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        auto pt = sp->argtypes[i];
        if (IsBytesT(pt)) {
            if (IsResz(pt))
                Fail(sp->sf->line, "resizable by-value thread arguments are unsupported");
            Append(b, "    uint8_t *a", i, " = p;\n");
            // Advance past the value; a size fn may be emitted on demand.
            Append(b, "    p += ", SizeX(pt, cat("a", i)).c_str(), ";\n");
            args.push_back(cat("a", i));
        } else {
            Append(b, "    ", CT(pt), " a", i, " = *(", CT(pt), " *)p; p += ",
                   FixedSize(pt), ";\n");
            args.push_back(cat("a", i));
        }
    }
    assert(ki.freevars.empty() && !ki.hasrf && sp->rets.empty());
    if (ki.needssp) args.push_back("0");
    string argstr;
    for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
    Append(b, "    ", ki.cname, "(", argstr, ");\n}\n\n");
    code += b;
    return name;
}

// ------------------------------------------------------------------
// Serialization (docs/design/serialization.md): to_bytes and bytes_of write
// an array's element region out, from_bytes verifies one back in.

// An image is little-endian by definition (§7 of the design), which every
// target this compiles for is; the test folds away, and exists so that a
// big-endian host fails loudly rather than writing bytes only it can read.
inline void CodeGen::EmitLeCheck(Line ln) {
    EmitCoreTypes();
    L("if (!gs_is_le()) gs_abort(GS_E_ENDIAN, ", LocArgs(ln), ");");
}

// The element region of a to_bytes/bytes_of receiver: a byte pointer, and
// the byte count -- which for variable elements is a walk, since an element
// count says nothing about the span.
inline void CodeGen::PayloadOf(Node *n, Line ln, string &src, string &sz) {
    auto nt = n->exprtype;
    auto rt = nt->kind == TY_REF ? nt->ref->sub : nt;
    auto elem = rt->kind == TY_SLICE ? rt->sub : rt->arr->sub;
    SrcElems se;
    if (rt->kind == TY_ARRAY || nt->kind == TY_REF) {
        // The receiver as a location, through a reference where it is one;
        // GenSrcElems is for the value forms it does not reach.
        auto v = ArrayView(RecvLoc(n), ln);
        auto cnt = T();
        L("int64_t ", cnt, " = ", v.len, ";");
        se.elems = v.elems;
        se.n = cnt;
    } else {
        se = GenSrcElems(n);
    }
    src = T();
    L("const uint8_t *", src, " = (const uint8_t *)(", se.elems, ");");
    sz = T();
    if (IsFix(elem)) {
        L("int64_t ", sz, " = (", se.n, ") * ", FixedSize(elem), ";");
    } else {
        auto i = T();
        L("int64_t ", sz, " = 0;");
        L("for (int64_t ", i, " = 0; ", i, " < (", se.n, "); ", i, "++) ", sz, " += ",
          SizeFn(elem), "(", src, " + ", sz, ");");
    }
}

// `n` bytes at `src` appended to a growable u8 array, in the two shapes
// format appends text to: a resizable writes at its stack top, a limited
// array copies under a capacity check.
inline void CodeGen::AppendBytes(const Loc &lv, const string &src, const string &n, Line ln) {
    auto v = ArrayView(lv, ln);
    if (lv.t->arr->akind == A_LIMITED) {
        auto capx = lv.val ? cat(ArrSize(lv.t->arr))
                           : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
        auto ol = T();
        L("int64_t ", ol, " = ", v.len, ";");
        L("if (", ol, " + ", n, " > ", capx, ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
        L("memcpy(", v.typedelems ? cat(v.elems, " + ", ol) : cat("(", v.elems, ") + ", ol),
          ", ", src, ", (size_t)", n, ");");
        L(v.lenlv, " = (", LenCast(lv), ")(", ol, " + ", n, ");");
        return;
    }
    L("memcpy(", Top(lv.stk), ", ", src, ", (size_t)", n, ");");
    Bump(lv.stk, n);
    L(v.lenlv, " += ", n, ";");
}

// The destination a resizable or variable builtin result is built at, in the
// shapes §7.3 admits: the caller's header, a value slot that takes a length
// prefix in front of the elements, or a fresh temporary when the result has
// no destination of its own. `elems` is where the elements start.
inline CodeGen::RzDest CodeGen::OpenRzDest(TypeExpr *t, Dst d0, Line ln, const char *what) {
    EmitCoreTypes();
    RzDest rd;
    auto resz = IsResz(t);
    rd.stk = d0.s;
    rd.lenlv = d0.lenlv;
    if (d0.k != DK_STACK) {
        if (resz) {
            rd.hdr = RzTemp(t, rd.stk);
            rd.lenlv = RzLenLv(t, rd.hdr);
        } else {
            rd.hdr = BytesTemp(rd.stk);   // a bytes value: its base pointer
        }
    }
    if (rd.lenlv.empty()) {
        // A value slot: the count goes into a length prefix reserved in front
        // of the elements, in the slot's own length storage where the
        // destination has one and the result's otherwise.
        auto at = d0.t && d0.t->kind == TY_ARRAY && d0.t->arr->akind == A_VAR ? d0.t
                  : resz ? nullptr : t;
        if (!at) Fail(ln, cat(what, "() needs a resizable or variable-array destination"));
        rd.ls = LenStore(at->arr);
        rd.pref = T();
        L("uint8_t *", rd.pref, " = ", Top(rd.stk), ";");
        Bump(rd.stk, cat(PrefixBytes(rd.ls)));
    }
    rd.elems = T();
    L("uint8_t *", rd.elems, " = ", Top(rd.stk), ";");
    return rd;
}

inline void CodeGen::CloseRzDest(RzDest &rd, const string &count) {
    if (!rd.pref.empty()) EmitPrefixPatch(rd.pref, rd.ls, rd.stk, count, rd.elems);
    else L(rd.lenlv, " = ", count, ";");
}

// bytes_of(a): the element region as a u8[:], no copy. The view is never
// writable at the language level (§9.5), so the const the payload pointer
// carries is dropped here rather than expressed in the slice type.
inline vector<string> CodeGen::EmitBytesOf(Call *c, vector<Node *> &an, Line ln) {
    EmitLeCheck(ln);
    string src, sz;
    PayloadOf(an[0], ln, src, sz);
    auto s = T();
    L(CT(c->rettypes[0]), " ", s, " = { (uint8_t *)", src, ", ", sz, " };");
    return { s };
}

// to_bytes(a): the image -- a varint byte count, then the element region --
// as a fresh u8[>..], or appended to a builder the caller owns so that its
// own header can go in front. Copying rather than viewing keeps the image
// independent of the array's later growth.
inline vector<string> CodeGen::EmitToBytes(vector<Node *> &an, Dst d0, Line ln) {
    EmitLeCheck(ln);
    string src, sz;
    PayloadOf(an[0], ln, src, sz);
    auto pfx = T(), pn = T();
    L("uint8_t ", pfx, "[10];");
    L("int64_t ", pn, " = gs_uleb_write(", pfx, ", (uint64_t)", sz, ");");
    if (an.size() > 1) {
        auto out = RecvLoc(an[1]);
        AppendBytes(out, pfx, pn, ln);
        AppendBytes(out, src, sz, ln);
        return {};
    }
    auto rd = OpenRzDest(GrowU8(), d0, ln, "to_bytes");
    L("memcpy(", Top(rd.stk), ", ", pfx, ", (size_t)", pn, ");");
    Bump(rd.stk, pn);
    L("memcpy(", Top(rd.stk), ", ", src, ", (size_t)", sz, ");");
    Bump(rd.stk, sz);
    auto tot = T();
    L("int64_t ", tot, " = ", pn, " + ", sz, ";");
    CloseRzDest(rd, tot);
    return rd.hdr.empty() ? vector<string> {} : vector<string> { rd.hdr };
}

// from_bytes<T[...]>(bytes): the framing prefix, then the verifier over the
// payload, then a copy of it into the result's element region. A rejected
// image leaves the array empty, and the bool says which happened.
inline vector<string> CodeGen::EmitFromBytes(Call *c, vector<Node *> &an, Dst d0, Line ln) {
    EmitLeCheck(ln);
    auto t = c->rettypes[0];
    auto elem = t->arr->sub;
    auto sl = GenPure(an[0]);
    auto vf = VerifyFn(elem);
    // The prefix has to account for exactly the rest of the slice: it is what
    // a stream reader sizes its read from, and the first thing here that can
    // say these bytes are not an image at all.
    auto pay = T(), pn = T(), ok = T(), uv = T(), kv = T();
    L("const uint8_t *", pay, " = ", sl, ".data;");
    L("int64_t ", pn, " = 0;");
    L("uint8_t ", ok, " = 0;");
    L("uint64_t ", uv, " = 0;");
    L("int64_t ", kv, " = gs_uleb_check(", sl, ".data, ", sl, ".data + ", sl, ".len, &",
      uv, ");");
    L("if (", kv, " && ", uv, " <= (uint64_t)INT64_MAX && (int64_t)", uv, " == ", sl,
      ".len - ", kv, ") {");
    ind++;
    L(pay, " += ", kv, "; ", pn, " = (int64_t)", uv, "; ", ok, " = 1;");
    ind--;
    L("}");
    string bm = "NULL";
    if (!IsFix(elem) && HasRelRefAny(elem)) {
        // One bit per payload byte: where a variable element starts, which is
        // what the framing pass hands the link pass. Its own stack, so the
        // scratch is gone at the end of the statement -- and an image needing
        // more of that stack than it reserves is one this cannot verify,
        // which is a false rather than a guard-page abort.
        string bstk;
        bm = BytesTemp(bstk);
        auto bn = T();
        L("int64_t ", bn, " = (", pn, " + 7) / 8;");
        L("if (", bn, " > GS_BM_MAX) { ", bn, " = 0; ", ok, " = 0; }");
        L("memset(", bm, ", 0, (size_t)", bn, ");");
        Bump(bstk, bn);
    }
    auto cnt = T();
    L("int64_t ", cnt, " = ", ok, " ? ", vf, "(", pay, ", ", pn, ", ", bm, ") : -1;");
    L(ok, " = ", cnt, " >= 0;");
    auto rd = OpenRzDest(t, d0, ln, "from_bytes");
    L("if (", ok, ") {");
    ind++;
    L("memcpy(", Top(rd.stk), ", ", pay, ", (size_t)", pn, ");");
    Bump(rd.stk, pn);
    ind--;
    L("}");
    CloseRzDest(rd, cat("(", ok, " ? ", cnt, " : 0)"));
    return { rd.hdr, ok };
}

}  // namespace goose
