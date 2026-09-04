// Goose compiler — codegen's text forms (definitions of CodeGen members,
// codegen.h): print, str and format (§3.7), rendering any value
// structurally or through a user format overload.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Text forms (§3.7): print, str and format share them. A scalar's text
// comes from a gs_fmt_* runtime helper writing at most GS_FMT_MAX bytes;
// a u8 array or slice contributes its bytes as they are.

// One print argument to stdout.
// ------------------------------------------------------------------
// Rendering aggregates (§3.7): the text of any value appended to a
// u8[>..] builder, structurally, or through a user `format` overload
// recorded on the call for that type.

inline TypeExpr *CodeGen::GrowU8() {
    if (growu8) return growu8;
    growu8 = ast.NewType(TY_ARRAY, Line {});
    growu8->arr = ast.NewDetail<TypeArray>();
    growu8->arr->sub = ast.inttypes[IS_U8];
    growu8->arr->akind = A_GROW;
    return growu8;
}

// A u8[>..] builder on a statement stack.
inline CodeGen::Loc CodeGen::TempBuilder() {
    string stk;
    auto h = RzTemp(GrowU8(), stk);
    return RzTempLoc(GrowU8(), h, stk);
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
    auto u8 = [&](TypeExpr *e) { return e->kind == TY_INT && e->intstorage == IS_U8; };
    if (t->kind == TY_ARRAY) return u8(t->arr->sub);
    if (t->kind == TY_SLICE) return u8(t->sub);
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
    if (lv.t->kind == TY_REF && t->kind != TY_REF) DerefLoc(lv, ln);
    if (auto sp = FmtSpecFor(c, t)) { EmitUserFormat(out, lv, sp, ln); return; }
    auto u8 = [&](TypeExpr *e) { return e->kind == TY_INT && e->intstorage == IS_U8; };
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
            RenderN(out, cat("gs_fmt_f64(", Top(out.stk), ", (double)(", x, "))"));
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
                DerefLoc(pl, ln);
                RenderLoc(out, pl, t->ref->sub, nested, c, ln);
                ind--;
                L("}");
                return;
            }
            DerefLoc(lv, ln);
            RenderLoc(out, lv, t->ref->sub, nested, c, ln);
            return;
        }
        case TY_ARRAY: case TY_SLICE: {
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            auto v = ArrayView(lv, ln);
            if (u8(elem)) {
                if (nested) {
                    RenderN(out, cat("gs_fmt_quoted(", Top(out.stk), ", (const uint8_t *)(",
                                     v.elems, "), ", v.len, ")"));
                } else {
                    auto n = T();
                    L("int64_t ", n, " = ", v.len, ";");
                    L("memcpy(", Top(out.stk), ", (const uint8_t *)(", v.elems, "), (size_t)",
                      n, ");");
                    Bump(out.stk, n);
                    L(out.lenlv, " += ", n, ";");
                }
                return;
            }
            RenderLit(out, "[");
            auto i = T();
            auto cnt = T();
            L("int64_t ", cnt, " = ", v.len, ";");
            string cur;
            if (!v.typedelems && !IsFix(elem)) {
                cur = T();
                L("const uint8_t *", cur, " = (const uint8_t *)(", v.elems, ");");
            }
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
                el.s = cat(v.elems, "[", i, "]");
                el.stk = lv.stk;
            } else if (IsFix(elem)) {
                el = BytesLoc(cat("((const uint8_t *)(", v.elems, ") + ", i, " * ",
                                  FixedSize(elem), ")"), elem, lv);
            } else {
                el = BytesLoc(cur, elem, lv);
            }
            RenderLoc(out, el, elem, true, c, ln);
            if (!cur.empty()) L(cur, " += ", SizeX(elem, cur), ";");
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
        case TY_VARIANT: {
            auto ei = EIVar(t);
            auto vi = VarIdx(ei->en, t->var->variant);
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
            return;
        }
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
                if (!ei->en->variants[vi].fields.empty()) {
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
                } else {
                    pl = lv;
                    pl.t = vt;
                }
                RenderLoc(out, pl, vt, nested, c, ln);
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

// A user `format(out, v)` overload applied to the value at lv.
inline void CodeGen::EmitUserFormat(Loc &out, Loc lv, FnSpec *sp, Line ln) {
    assert(!out.hdr.empty() && !out.stk.empty());
    MarkFlush();
    auto r = T();
    L("gs_rref ", r, " = { (gs_rhdr *)&", out.hdr, ", ", out.stk, " };");
    auto pt = sp->argtypes[1];
    string arg;
    if (pt->kind == TY_REF) {
        auto sub = pt->ref->sub;
        if (IsResz(sub)) {
            if (lv.hdr.empty() || lv.stk.empty())
                Fail(ln, "a format overload by reference needs a resizable with its own header");
            auto rr = T();
            L("gs_rref ", rr, " = { (gs_rhdr *)&", lv.hdr, ", ", lv.stk, " };");
            arg = rr;
        } else if (IsBytesT(sub)) {
            arg = lv.val ? cat("(uint8_t *)&", lv.s) : lv.s;
        } else {
            arg = lv.val ? cat("&", lv.s) : cat("(", CT(sub), " *)(", lv.s, ")");
        }
    } else {
        arg = LoadLoc(lv, pt, ln);
    }
    auto &ki = sinfo[sp];
    L(ki.cname, "(", r, ", ", arg, ki.needssp ? ", gs_sp" : "", ");");
    MarkReload();   // The callee grew the builder's stack.
}

// A value rendered into a builder of its own: the print and str paths.
inline CodeGen::Loc CodeGen::RenderToTemp(Node *a, Call *c) {
    auto b = TempBuilder();
    auto lv = GenLoc(a);
    RenderLoc(b, lv, a->exprtype, false, c, a->line);
    return b;
}

inline void CodeGen::EmitOutArg(Node *a, Call *c) {
    auto t = a->exprtype;
    if (!SimpleText(c, t)) {
        auto b = RenderToTemp(a, c);
        L("gs_out_bytes(", b.hdr, ".base, ", b.hdr, ".len);");
        return;
    }
    if (t->kind == TY_INT) {
        if (t->intstorage == IS_U64) L("gs_out_uint(", GenX(a), ");");
        else L("gs_out_int((int64_t)(", GenX(a), "));");
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
    if (t->kind == TY_FLT) return cat("gs_fmt_f64(", dst, ", (double)(", GenX(a), "))");
    assert(t->kind == TY_BOOL);
    return cat("gs_fmt_bool(", dst, ", ", GenX(a), ")");
}

// Appends the text of `a` to the growable u8 array at lv. On a resizable
// the bytes are written straight at its stack top (the reservation has
// room for them); a limited array formats into a buffer first, so the
// capacity check comes before anything lands in it.
inline void CodeGen::EmitFormatInto(Loc lv, Node *a, Line ln, Call *c) {
    auto v = ArrayView(lv, ln);
    auto limited = lv.t->arr->akind == A_LIMITED;
    auto t = a->exprtype;
    if (!SimpleText(c, t) && !limited) {
        // An aggregate renders straight into the resizable.
        Loc out = lv;
        out.lenlv = v.lenlv;
        if (out.hdr.empty()) Fail(ln, "internal: format destination without a header");
        RenderLoc(out, GenLoc(a), t, false, c, ln);
        return;
    }
    auto bytes = t->kind == TY_ARRAY || t->kind == TY_SLICE;
    auto n = T();
    string src;   // Where the bytes to append sit, when not already at the top.
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
    } else if (limited) {
        src = T();
        L("uint8_t ", src, "[GS_FMT_MAX];");
        L("int64_t ", n, " = ", FmtCall(a, src), ";");
    } else {
        L("int64_t ", n, " = ", FmtCall(a, Top(lv.stk)), ";");
    }
    if (limited) {
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
    assert(!lv.stk.empty());
    if (bytes) L("memcpy(", Top(lv.stk), ", ", src, ", (size_t)", n, ");");
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
        auto t = a->exprtype;
        auto n = T();
        if (!SimpleText(c, t)) {
            auto b = RenderToTemp(a, c);
            L("int64_t ", n, " = ", b.hdr, ".len;");
            L("memcpy(", Top(stk), ", ", b.hdr, ".base, (size_t)", n, ");");
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            auto se = GenSrcElems(a);
            L("int64_t ", n, " = ", se.n, ";");
            L("memcpy(", Top(stk), ", (const uint8_t *)(", se.elems, "), (size_t)", n, ");");
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

// Guaranteed NRVO (§7.3): a top-level local every return hands back in
// one nonfixed return position is allocated at that destination.
inline void CodeGen::DetectNrvo(FnSpec *sp) {
    nrvovars.clear();
    nrvo.clear();
    if (sp->rets.empty()) return;
    // Only bindings BindLocal places: a multi-name receive wires the
    // call's channels into locals of its own (VarDecl::CgStmt), which
    // are not at a return destination.
    set<const VarDef *> toplocals;
    for (auto st : sp->body->stmts)
        if (auto vd = Is<VarDecl>(st); vd && vd->defs.size() == 1)
            toplocals.insert(vd->defs[0]);
    for (size_t j = 0; j < sp->rets.size(); j++) {
        if (!IsBytesT(sp->rets[j])) continue;
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
            if (auto r = Is<Return>(n)) {
                if (r->target == sp->sf) {
                    if (r->vals.size() != sp->rets.size()) ok = false;
                    else consider(r->vals[j]);
                }
            }
            if (auto cc = Is<Call>(n)) walk(cc->fvbody);
            n->Children([&](Node *ch) { walk(ch); });
        };
        walk(sp->body);
        if (sp->body->tail && sp->rets.size() == 1 &&
            !IsVoidT(sp->body->tail->exprtype)) consider(sp->body->tail);
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
            nrvovars.insert(cand);
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
