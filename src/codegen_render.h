// Goose compiler — codegen's text forms (definitions of CodeGen members,
// codegen.h): print, str and format (§3.7), rendering any value
// structurally or through a user format overload.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Text forms (§3.7): print, str and format share them. A scalar's text
// comes from a gs_fmt_* runtime helper writing at most GS_FMT_MAX bytes;
// a u8 array or slice contributes its bytes as they are, and any other
// value renders structurally into a u8[>..] builder, or through a user
// `format` overload recorded on the call for its type.

inline TypeExpr *CodeGen::GrowU8() {
    if (growu8) return growu8;
    growu8 = ast.ArrayOf(ast.inttypes[IS_U8], A_GROW, Line {});
    return growu8;
}

// A u8[>..] builder on a statement stack.
inline CodeGen::Loc CodeGen::TempBuilder() {
    string stk;
    auto h = RzTemp(GrowU8(), stk);
    return RzTempLoc(GrowU8(), h, stk);
}

inline Call *CodeGen::FmtContext(Call *c, Node *arg) {
    if (!c || c->fmtcontexts.empty()) return c;
    auto args = c->ArgNodes();
    size_t start = c->builtin == B_FORMAT ? 1 : 0;
    for (size_t i = start; i < args.size(); i++)
        if (args[i] == arg) return c->fmtcontexts.at(i - start);
    return c;
}

inline FnSpec *CodeGen::FmtSpecFor(Call *c, TypeExpr *t) {
    if (!c) return nullptr;
    for (auto &fs : c->fmtspecs) if (TEq(fs.first, t)) return fs.second;
    return nullptr;
}

// The scalar, bool, u8-array cases EmitOutArg/FmtCall handle directly.
inline bool CodeGen::SimpleText(Call *c, TypeExpr *t) {
    if (FmtSpecFor(c, t)) return false;
    if (t->kind == TY_INT || t->kind == TY_FLT || t->kind == TY_BOOL) return true;
    if (t->kind == TY_ARRAY) return IsU8(t->arr->sub);
    if (t->kind == TY_SLICE) return IsU8(t->sub);
    return false;
}

inline void CodeGen::RenderLit(Loc &out, const string &text) {
    L("memcpy(", Top(out.stk), ", ", StrRaw(text), ", ", text.size(), ");");
    Bump(out.stk, cat(text.size()));
    L(out.lenlv, " += ", text.size(), ";");
}

// nexpr writes at the builder's top and yields the byte count.
inline void CodeGen::RenderN(Loc &out, const string &nexpr) {
    auto n = T();
    L("int64_t ", n, " = ", nexpr, ";");
    Bump(out.stk, n);
    L(out.lenlv, " += ", n, ";");
}

inline void CodeGen::RenderLoc(Loc &out, Loc lv, TypeExpr *t, bool nested, Call *c, Line ln) {
    // A reference location whose checked type already decayed (a narrowed
    // optional, a plain reference in value position): the pointee.
    if (lv.t->kind == TY_REF && t->kind != TY_REF) DerefLoc(lv);
    if (auto sp = FmtSpecFor(c, t)) { EmitUserFormat(out, lv, sp, ln); return; }
    // A value of a type this rendering is already inside of is the next
    // level of a type that reaches itself through references: its render
    // function takes it from here, level by level (RenderFn). A resizable
    // tail of a struct with a variable-size prefix has no header of its own
    // to refer to, and is rendered here, one more level, as far as the
    // reference to the next.
    auto nominal = t->kind == TY_STRUCT || t->kind == TY_VARIANT || t->kind == TY_ENUM;
    if (nominal && (!IsResz(t) || (!lv.hdr.empty() && !lv.stk.empty()))) {
        for (auto r : rendering) {
            if (!TEq(r, t)) continue;
            EmitRenderCall(out, lv, t, ln);
            return;
        }
    }
    struct Inside {
        vector<TypeExpr *> &types;
        bool in;
        ~Inside() { if (in) types.pop_back(); }
    } inside { rendering, nominal };
    if (nominal) rendering.push_back(t);
    // An overload rendering one part of a value may rebind a reference on the
    // way to it: the other parts are read from where the value lay when its
    // rendering began, which the checker holds meanwhile (HeldOperands).
    if (lv.viaref && c && !c->fmtspecs.empty() && nominal) PinLoc(lv);
    switch (t->kind) {
        case TY_INT: {
            auto vt = t->intstorage == IS_VARINT ? ast.inttypes[IS_I64] : t;
            auto x = LoadLoc(lv, vt, ln);
            if (vt->intstorage == IS_U64)
                RenderN(out, cat("gs_fmt_u64(", Top(out.stk), ", ", x, ")"));
            else
                RenderN(out, cat("gs_fmt_i64(", Top(out.stk), ", (int64_t)(", x, "))"));
            return;
        }
        case TY_FLT: {
            auto x = LoadLoc(lv, t, ln);
            if (IsF32(t)) RenderN(out, cat("gs_fmt_f32(", Top(out.stk), ", ", x, ")"));
            else RenderN(out, cat("gs_fmt_f64(", Top(out.stk), ", (double)(", x, "))"));
            return;
        }
        case TY_BOOL: {
            auto x = LoadLoc(lv, t, ln);
            RenderN(out, cat("gs_fmt_bool(", Top(out.stk), ", ", x, ")"));
            return;
        }
        case TY_REF: {
            if (lv.t->kind != TY_REF) Fail(ln, "internal: reference render of a value");
            auto x = LoadLoc(lv, lv.t, ln);
            if (t->ref->optional) {
                auto isnull = IsResz(t->ref->sub) ? cat("(", x, ").hdr == 0")
                                                        : cat("(", x, ") == NULL");
                L("if (", isnull, ") {");
                ind++;
                RenderLit(out, "null");
                ind--;
                L("} else {");
                ind++;
                Loc pl = lv;
                DerefLoc(pl);
                RenderLoc(out, pl, t->ref->sub, nested, c, ln);
                ind--;
                L("}");
                return;
            }
            DerefLoc(lv);
            RenderLoc(out, lv, t->ref->sub, nested, c, ln);
            return;
        }
        case TY_ARRAY: case TY_SLICE: {
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            auto v = ArrayView(lv);
            if (IsU8(elem)) {
                if (nested) {
                    RenderN(out, cat("gs_fmt_quoted(", Top(out.stk), ", (const uint8_t *)(",
                                     v.elems, "), ", v.len, ")"));
                } else {
                    auto n = T();
                    L("int64_t ", n, " = ", v.len, ";");
                    L(CopyFn(v.nullable), "(", Top(out.stk), ", (const uint8_t *)(", v.elems,
                      "), (size_t)", n, ");");
                    Bump(out.stk, n);
                    L(out.lenlv, " += ", n, ";");
                }
                return;
            }
            RenderLit(out, "[");
            auto i = T();
            auto cnt = T();
            L("int64_t ", cnt, " = ", v.len, ";");
            // The elements are read once, as the count is: an overload
            // rendering one may re-point the slice holding them, or rebind a
            // reference on the way to them, and the rest are still the ones
            // the rendering began with, which the checker holds meanwhile.
            // Variable-size elements are walked with this as the cursor.
            auto p = T();
            if (v.typedelems) L(CT(elem), " *", p, " = ", v.elems, ";");
            else L("const uint8_t *", p, " = (const uint8_t *)(", v.elems, ");");
            L("for (int64_t ", i, " = 0; ", i, " < ", cnt, "; ", i, "++) {");
            ind++;
            L("if (", i, ") {");
            ind++;
            RenderLit(out, ", ");
            ind--;
            L("}");
            Loc el;
            if (v.typedelems) {
                el.t = elem;
                el.val = true;
                el.s = cat(p, "[", i, "]");
                el.stk = lv.stk;
            } else if (IsFix(elem)) {
                el = BytesLoc(cat("(", p, " + ", i, " * ", FixedSize(elem), ")"), elem, lv);
            } else {
                el = BytesLoc(p, elem, lv);
            }
            // Only a reference an element holds can still be rebound.
            el.viaref = elem->kind == TY_REF;
            RenderLoc(out, el, elem, true, c, ln);
            if (!v.typedelems && !IsFix(elem)) L(p, " += ", SizeX(elem, p), ";");
            ind--;
            L("}");
            RenderLit(out, "]");
            return;
        }
        case TY_STRUCT: {
            auto si = SI(t);
            string name;
            t->Dump(name);
            RenderLit(out, cat(name, " { "));
            auto first = true;
            for (auto i = 0; i < (int)si->st->fields.size(); i++) {
                if (si->st->fields[i].ispad) continue;
                if (!first) RenderLit(out, ", ");
                first = false;
                RenderLoc(out, FieldLocAt(lv, i), si->ftypes[i], true, c, ln);
            }
            RenderLit(out, " }");
            return;
        }
        case TY_VARIANT:
            RenderVariant(out, lv, t, c, ln);
            return;
        case TY_ENUM: {
            auto ei = EIOf(t);
            auto varmode = t->enu->varmode;
            auto ts = TagSize(ei->en);
            string tag;
            if (lv.val) tag = cat(lv.s, ".tag");
            else if (varmode) tag = cat("*(", IntCT(TagStore(ei->en)), " *)(", lv.s, ")");
            else tag = cat("((", CT(t), " *)(", lv.s, "))->tag");
            L("switch (", tag, ") {");
            for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                L("case ", TagConst(ei, (int)vi), ": {");
                ind++;
                auto vt = VariantType(t, (int)vi);
                Loc pl;
                if (!EmptyLayout(ei->en->variants[vi].fields)) {
                    if (lv.val) {
                        pl = lv;
                        pl.t = vt;
                        pl.s = cat(lv.s, ".u.v_", Sanitize(ei->en->variants[vi].name));
                    } else if (varmode) {
                        pl = BytesLoc(cat("((", lv.s, ") + ", ts, ")"), vt, lv);
                    } else {
                        pl = BytesLoc(cat("((uint8_t *)&((", CT(t), " *)(", lv.s, "))->u.v_",
                                          Sanitize(ei->en->variants[vi].name), ")"), vt, lv);
                    }
                    // An overload rendering one part of a fixed-mode value
                    // may overwrite it with another variant, and nothing may
                    // refer into its payload meanwhile (§3.5): the parts are
                    // rendered from a copy, where the checker has them lie
                    // (CheckRenderable). No fixed-mode payload holds
                    // self-relative references, which a copy would not keep
                    // (§3.9).
                    if (!varmode && c && !c->fmtspecs.empty()) {
                        auto cp = T();
                        L(CT(vt), " ", cp, " = ", pl.s, ";");
                        pl.s = cp;
                        pl.viaref = false;
                    }
                } else {
                    pl = lv;
                    pl.t = vt;
                }
                // As the variant's literal, whatever overload the argument
                // has for the variant type, which the checker only looks
                // for where a value has that type.
                RenderVariant(out, pl, vt, c, ln);
                L("break;");
                ind--;
                L("}");
            }
            L("default: break;");
            L("}");
            return;
        }
        default:
            Fail(ln, cat("cannot render a value of type ", Mangle(t)));
    }
}

// A variant's positional literal, its payload's parts rendered from lv.
inline void CodeGen::RenderVariant(Loc &out, Loc lv, TypeExpr *t, Call *c, Line ln) {
    auto ei = EIVar(t);
    auto vi = ei->en->VariantIndex(t->var->variant);
    auto &v = ei->en->variants[vi];
    if (v.fields.empty()) {
        RenderLit(out, cat(ei->en->name, ".", v.name));
        return;
    }
    RenderLit(out, cat(v.name, " { "));
    auto first = true;
    for (auto i = 0; i < (int)v.fields.size(); i++) {
        if (v.fields[i].ispad) continue;
        if (!first) RenderLit(out, ", ");
        first = false;
        RenderLoc(out, FieldLocAt(lv, i), ei->vftypes[vi][i], true, c, ln);
    }
    RenderLit(out, " }");
}

// A reference to the value at lv, of type sub, for a function taking one: a
// resizable's header and stack, a bytes value's address, a fixed value's
// typed address.
inline string CodeGen::RefArg(const Loc &lv, TypeExpr *sub, Line ln) {
    if (IsResz(sub)) {
        if (lv.hdr.empty() || lv.stk.empty())
            Fail(ln, "a format overload by reference needs a resizable with its own header");
        auto rr = T();
        L("gs_rref ", rr, " = { (gs_rhdr *)&", lv.hdr, ", ", lv.stk, " };");
        return rr;
    }
    if (IsBytesT(sub)) return lv.val ? cat("(uint8_t *)&", lv.s) : lv.s;
    if (IsVarintT(lv.t)) {
        // An i64 read out of varint storage, which holds no i64: the
        // overload is given a temporary holding the value (CheckPrintable).
        auto x = T();
        L(CT(sub), " ", x, " = ", LoadLoc(lv, sub, ln), ";");
        return cat("&", x);
    }
    return lv.val ? cat("&", lv.s) : cat("(", CT(sub), " *)(", lv.s, ")");
}

// A user `format(out, v)` overload applied to the value at lv.
inline void CodeGen::EmitUserFormat(Loc &out, Loc lv, FnSpec *sp, Line ln) {
    assert(!out.hdr.empty() && !out.stk.empty());
    MarkFlush();
    auto r = T();
    L("gs_rref ", r, " = { (gs_rhdr *)&", out.hdr, ", ", out.stk, " };");
    auto pt = sp->argtypes[1];
    auto arg = pt->kind == TY_REF ? RefArg(lv, pt->ref->sub, ln) : LoadLoc(lv, pt, ln);
    auto &ki = sinfo[sp];
    // The callee's stacks start above everything live here, the builder's
    // and the value's included, like any other call's (SpTop).
    L(ki.cname, "(", r, ", ", arg, ki.needssp ? cat(", ", SpTop()) : "", ");");
    MarkReload();   // The callee grew the builder's stack.
}

// The function rendering the levels of a value of type t below the first,
// one per type. It is handed the builder and a reference to the value, as
// an overload taking it by reference is, and its body is emitted after the
// specializations' (EmitRenderFn).
inline string CodeGen::RenderFn(TypeExpr *t, Line ln) {
    auto m = Mangle(t);
    if (auto it = renderfns.find(m); it != renderfns.end()) return it->second;
    auto name = Unique(cat("gs_render_", m));
    renderfns[m] = name;
    auto vt = IsResz(t) ? string("gs_rref") : IsBytesT(t) ? string("uint8_t *") : cat(CT(t), " *");
    auto sig = cat("static void ", name, "(gs_rref gs_out, ", vt, " gs_v)");
    Append(protos, sig, ";\n");
    renderqueue.push_back({ t, ln, sig });
    return name;
}

inline void CodeGen::EmitRenderCall(Loc &out, const Loc &lv, TypeExpr *t, Line ln) {
    assert(!out.hdr.empty() && !out.stk.empty());
    auto fn = RenderFn(t, ln);
    MarkFlush();
    auto r = T();
    L("gs_rref ", r, " = { (gs_rhdr *)&", out.hdr, ", ", out.stk, " };");
    auto arg = RefArg(lv, t, ln);
    L(fn, "(", r, ", ", arg, ");");
    MarkReload();
}

// A render function's body (RenderFn): the value gs_v refers to, rendered
// into the builder gs_out as the level RenderLoc was inside of when it
// reached it.
inline void CodeGen::EmitRenderFn(RenderFnReq r) {
    curspec = nullptr;
    curinfo = nullptr;
    ResetFnState();
    spexpr = "0";
    PushSc(SC_FN);
    auto out = FatRefLoc("gs_out", GrowU8());
    Loc v;
    if (IsResz(r.t)) {
        v = FatRefLoc("gs_v", r.t);
    } else {
        v.t = r.t;
        v.val = !IsBytesT(r.t);
        v.s = v.val ? PointeeLv("gs_v", r.t) : string("gs_v");
    }
    RenderLoc(out, v, r.t, true, nullptr, r.ln);
    cscopes.clear();
    assert(stkmax == 0);
    auto decls = HoistAggregateDecls(body);
    Append(code, r.sig, " {\n");
    code += decls;
    code += body;
    code += "}\n\n";
}

// Where a rendered argument lies: where GenLoc addresses it, but a call's
// reference result, which the argument decayed to its pointee, is held as
// the reference and read where it points, as a reference variable is. A
// format overload taking the value, or a part of it, by reference is given
// what the reference names, which is where the checker binds it (§3.7,
// DecayRef's slot), not a copy of it. A varint pointee is rendered as the
// i64 it decodes to, a value no storage holds.
inline CodeGen::Loc CodeGen::RenderedLoc(Node *a) {
    auto c = Is<Call>(a);
    auto ib = Is<InlineBlock>(a);
    TypeExpr *rt = nullptr;
    if (c && !c->rettypes.empty()) rt = c->rettypes[0];
    else if (ib && ib->spec && ib->spec->rets.size() == 1) rt = ib->spec->rets[0];
    if (!rt || !IsPlainRef(rt) || IsVarintT(rt->ref->sub)) return GenLoc(a);
    Loc lv;
    lv.t = rt;
    lv.val = true;
    lv.s = T();
    if (c) {
        auto rets = EmitCall(c, Dst {});
        assert(!rets.empty());
        L(CT(rt), " ", lv.s, " = ", rets[0], ";");
    } else {
        L(CT(rt), " ", lv.s, ";");
        GenAny(ib, Dst { DK_LVALUE, lv.s, rt });
    }
    return lv;
}

// A value rendered into a builder of its own: the print and str paths.
inline CodeGen::Loc CodeGen::RenderToTemp(Node *a, Call *c) {
    auto b = TempBuilder();
    auto lv = RenderedLoc(a);
    RenderLoc(b, lv, a->exprtype, false, c, a->line);
    return b;
}

inline void CodeGen::EmitOutArg(Node *a, Call *c) {
    c = FmtContext(c, a);
    auto t = a->exprtype;
    if (!SimpleText(c, t)) {
        auto b = RenderToTemp(a, c);
        L("gs_out_bytes(", b.hdr, ".base, ", b.hdr, ".len);");
        return;
    }
    if (t->kind == TY_INT) {
        if (t->intstorage == IS_U64) L("gs_out_uint(", GenX(a), ");");
        else L("gs_out_int((int64_t)(", GenX(a), "));");
    } else if (IsF32(t)) {
        L("gs_out_f32(", GenX(a), ");");
    } else if (t->kind == TY_FLT) {
        L("gs_out_flt((double)(", GenX(a), "));");
    } else if (t->kind == TY_BOOL) {
        L("gs_out_bool(", GenX(a), ");");
    } else {
        auto se = GenSrcElems(a);
        L("gs_out_bytes((const uint8_t *)(", se.elems, "), ", se.n, ");");
    }
}

// The C expression formatting scalar `a` at `dst`, yielding the byte count.
inline string CodeGen::FmtCall(Node *a, const string &dst) {
    auto t = a->exprtype;
    if (t->kind == TY_INT) {
        if (t->intstorage == IS_U64) return cat("gs_fmt_u64(", dst, ", ", GenX(a), ")");
        return cat("gs_fmt_i64(", dst, ", (int64_t)(", GenX(a), "))");
    }
    if (IsF32(t)) return cat("gs_fmt_f32(", dst, ", ", GenX(a), ")");
    if (t->kind == TY_FLT) return cat("gs_fmt_f64(", dst, ", (double)(", GenX(a), "))");
    assert(t->kind == TY_BOOL);
    return cat("gs_fmt_bool(", dst, ", ", GenX(a), ")");
}

// Appends the text of `a` to the growable u8 array at lv. On a resizable
// the bytes are written straight at its stack top (the reservation has
// room for them); a limited array formats into a buffer first, so the
// capacity check comes before anything lands in it.
inline void CodeGen::EmitFormatInto(Loc lv, Node *a, Line ln, Call *c) {
    c = FmtContext(c, a);
    auto v = ArrayView(lv);
    auto limited = lv.t->arr->akind == A_LIMITED;
    auto t = a->exprtype;
    if (!SimpleText(c, t) && !limited) {
        // An aggregate renders straight into the resizable.
        Loc out = lv;
        out.lenlv = v.lenlv;
        if (out.hdr.empty()) Fail(ln, "internal: format destination without a header");
        RenderLoc(out, RenderedLoc(a), t, false, c, ln);
        return;
    }
    auto bytes = t->kind == TY_ARRAY || t->kind == TY_SLICE;
    auto n = T();
    string src;   // Where the bytes to append sit, when not already at the top.
    auto nullable = false;
    if (!SimpleText(c, t)) {
        // Into a limited array: rendered aside, then copied under the
        // capacity check like any bytes.
        auto b = RenderToTemp(a, c);
        L("int64_t ", n, " = ", b.hdr, ".len;");
        src = cat(b.hdr, ".base");
        bytes = true;
    } else if (bytes) {
        auto se = GenSrcElems(a);
        L("int64_t ", n, " = ", se.n, ";");
        src = cat("(const uint8_t *)(", se.elems, ")");
        nullable = se.nullable;
    } else if (limited) {
        src = T();
        L("uint8_t ", src, "[GS_FMT_MAX];");
        L("int64_t ", n, " = ", FmtCall(a, src), ";");
    } else {
        L("int64_t ", n, " = ", FmtCall(a, Top(lv.stk)), ";");
    }
    // Bytes to copy -- a limited array's, under its capacity check, or a
    // rendered or array source into a resizable -- append like any others.
    if (limited || bytes) {
        AppendBytes(lv, src, n, ln, nullable);
        return;
    }
    // A scalar's text is at the resizable's top already: only the count moves.
    assert(!lv.stk.empty());
    Bump(lv.stk, n);
    L(v.lenlv, " += ", n, ";");
}

// str(a, b, ...): the arguments' text as a fresh u8[>..] at the
// destination. The elements go to the destination's top and the count to
// the receiving header -- or into a length prefix reserved in front of the
// elements when the destination is a value slot (a u8[] element or field),
// exactly as a named result lands there (§7.3).
inline vector<string> CodeGen::EmitStr(Call *c, vector<Node *> &an, Dst d0, Line ln) {
    EmitCoreTypes();
    string stk = d0.s, lenlv = d0.lenlv, pref, hdr;
    IntStorage ls = IS_U32;
    if (d0.k != DK_STACK) {
        // No destination of its own: a temporary on a statement stack.
        hdr = RzTemp(GrowU8(), stk);
        lenlv = cat(hdr, ".len");
    } else if (lenlv.empty()) {
        if (!d0.t || d0.t->kind != TY_ARRAY || d0.t->arr->akind != A_VAR)
            Fail(ln, "str() needs a resizable or variable-array destination");
        ls = LenStore(d0.t->arr);
        pref = T();
        L("uint8_t *", pref, " = ", Top(stk), ";");
        Bump(stk, cat(PrefixBytes(ls)));
    }
    auto elems = T();
    L("uint8_t *", elems, " = ", Top(stk), ";");
    auto cnt = T();
    L("int64_t ", cnt, " = 0;");
    for (auto a : an) {
        auto context = FmtContext(c, a);
        auto t = a->exprtype;
        auto n = T();
        if (!SimpleText(context, t)) {
            auto b = RenderToTemp(a, context);
            L("int64_t ", n, " = ", b.hdr, ".len;");
            L("memcpy(", Top(stk), ", ", b.hdr, ".base, (size_t)", n, ");");
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            auto se = GenSrcElems(a);
            L("int64_t ", n, " = ", se.n, ";");
            L(CopyFn(se.nullable), "(", Top(stk), ", (const uint8_t *)(", se.elems, "), (size_t)",
              n, ");");
        } else {
            L("int64_t ", n, " = ", FmtCall(a, Top(stk)), ";");
        }
        Bump(stk, n);
        L(cnt, " += ", n, ";");
    }
    if (!pref.empty()) EmitPrefixPatch(pref, ls, stk, cnt, elems);
    else L(lenlv, " = ", cnt, ";");
    if (!hdr.empty()) return { hdr };
    return {};
}

// ------------------------------------------------------------------
// Function bodies.

// Structural named-result discovery on the final body. Both real functions
// and inlined bodies use it, so rewrites need not maintain an AST annotation.
inline const VarDef *CodeGen::NamedResult(Block *fnbody, SFunction *target,
                                         size_t nrets, size_t resultidx) {
    // Only bindings BindLocal places: a multi-name receive wires a call's
    // channels into locals of its own, which are not at a return destination.
    set<const VarDef *> toplocals;
    for (auto st : fnbody->stmts)
        if (auto vd = Is<VarDecl>(st); vd && vd->defs.size() == 1)
            toplocals.insert(vd->defs[0]);
    const VarDef *cand = nullptr;
    auto ok = true;
    auto consider = [&](Node *val) {
        auto id = Is<Ident>(val);
        if (!id || !id->vdef || !toplocals.count(id->vdef)) { ok = false; return; }
        if (cand && cand != id->vdef) { ok = false; return; }
        cand = id->vdef;
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !ok) return;
        if (auto r = Is<Return>(n); r && r->target == target) {
            if (r->vals.size() != nrets) ok = false;
            else consider(r->vals[resultidx]);
        }
        RunChildren(n, walk);
    };
    walk(fnbody);
    if (fnbody->tail && nrets == 1 && !IsVoidT(fnbody->tail->exprtype)) consider(fnbody->tail);
    return ok ? cand : nullptr;
}

// Guaranteed NRVO (§7.3): a top-level local every return hands back in
// one nonfixed return position is allocated at that destination.
inline void CodeGen::DetectNrvo(FnSpec *sp) {
    nrvo.clear();
    // A long-distance return into this function lands its value over the
    // named result at the catch (EmitRfCheck).
    if (sp->rets.empty()) return;
    for (size_t j = 0; j < sp->rets.size(); j++) {
        if (!IsBytesT(sp->rets[j])) continue;
        auto cand = NamedResult(sp->body, sp->sf, sp->rets.size(), j);
        // One local can be built at one destination only.
        auto ok = cand != nullptr && !nrvo.count(cand);
        // The local's layout must be the return type's, or a resizable
        // whose elements become the returned variable array's (that
        // array's prefix is reserved ahead of them); anything else is
        // copied on return like a non-named result.
        auto rzarr = false;
        if (ok && cand) {
            auto rt = sp->rets[j];
            auto ct = cand->type;
            auto same = TEq(ct, rt);
            rzarr = IsResz(ct) && ct->kind == TY_ARRAY && rt->kind == TY_ARRAY &&
                    rt->arr->akind == A_VAR && TEq(ct->arr->sub, rt->arr->sub);
            if (!same && !rzarr) ok = false;
        }
        if (ok && cand) {
            NrvoDest nd;
            nd.stk = cat("gs_dst", j);
            if (rzarr) {
                nd.prefix = true;
                nd.ls = LenStore(sp->rets[j]->arr);
            } else {
                nd.lenlv = cat("(*gs_rl", j, ")");
                nd.fo = IsFrameObj(cand->type);
            }
            nrvo[cand] = nd;
        }
    }
}

}  // namespace goose
