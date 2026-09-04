// Goose compiler — the per-node codegen implementations (the CgX / CgAny /
// CgStmt virtuals): the pass reads top to bottom here; the shared machinery
// is CodeGen's, codegen.h and the codegen_*.h it lists. CgX yields a C
// expression for fixed-class values, CgAny routes any value to a destination
// (all branches of a control construct reach the same one, §4.3), CgStmt
// emits statement position.
#pragma once

namespace goose {

// ---- CgX ------------------------------------------------------------------

inline string IntLit::CgX(CodeGen &) {
    // A u64-typed value above i64.max has negative bits; emit it unsigned.
    if (val < 0 && exprtype && exprtype->kind == TY_INT && exprtype->intstorage == IS_U64)
        return cat((uint64_t)val, "ULL");
    return CodeGen::IntStr(val);
}

inline string FltLit::CgX(CodeGen &) {
    return CodeGen::FltStr(val, exprtype && exprtype->kind == TY_FLT &&
                                exprtype->fltstorage == FS_F32);
}

inline string BoolLit::CgX(CodeGen &) { return val ? "1" : "0"; }

inline string NullLit::CgX(CodeGen &cg) {
    if (exprtype && exprtype->kind == TY_REF && cg.IsResz(exprtype->ref->sub)) {
        auto t = cg.T();
        cg.L("gs_rref ", t, " = { 0, 0 };");
        return t;
    }
    return "NULL";
}

inline string StrLit::CgX(CodeGen &cg) {
    auto et = exprtype;
    if (et->kind == TY_SLICE) {
        auto t = cg.T();
        cg.L(cg.CT(et), " ", t, " = { ", cg.StrRaw(val), ", ", val.size(), " };");
        return t;
    }
    // A fixed u8[k] array value.
    assert(et->kind == TY_ARRAY && et->arr->akind == A_FIXED);
    auto t = cg.T();
    cg.L(cg.CT(et), " ", t, ";");
    if (!val.empty())
        cg.L("memcpy(", t, ".e, ", cg.StrRaw(val), ", ", val.size(), ");");
    return t;
}

inline string Ident::CgX(CodeGen &cg) {
    assert(vdef);   // Named-function values never reach runtime.
    return cg.LoadLoc(cg.VarLoc(vdef), exprtype, line);
}

inline string Unary::CgX(CodeGen &cg) {
    if (op == T_BITAND) return cg.GenRefVal(child, line);
    auto x = cg.GenX(child);
    switch (op) {
        case T_MINUS:
            if (child->exprtype->kind == TY_FLT) return cat("(-", x, ")");
            return cat("gs_neg_", cg.IntSfx(child->exprtype->intstorage), "(", x, ")");
        case T_NOT: {
            auto ct = child->exprtype;
            if (ct->kind == TY_REF && cg.IsResz(ct->ref->sub))
                return cat("(", x, ".hdr == 0)");
            return cat("(uint8_t)(!", x, ")");
        }
        case T_BITNOT:
            // The cast undoes C's promotion to int for the narrow types.
            return cat("(", cg.CT(exprtype), ")(~(", x, "))");
        default: assert(false); return x;
    }
}

inline string Binary::CgX(CodeGen &cg) {
    if (op == T_ANDAND || op == T_OROR) {
        // Short-circuit with left-to-right statement emission: the right
        // operand's statements may only run when the left allows.
        auto l = cg.GenTruth(left);
        auto t = cg.T();
        cg.L("uint8_t ", t, " = (uint8_t)(", l, op == T_ANDAND ? " != 0);" : " != 0);");
        cg.L("if (", op == T_ANDAND ? t : cat("!", t), ") {");
        cg.ind++;
        auto r = cg.GenTruth(right);
        cg.L(t, " = (uint8_t)(", r, " != 0);");
        cg.ind--;
        cg.L("}");
        return t;
    }
    auto lt = cg.OperandT(left->exprtype);
    // Null tests (§3.8).
    if (op == T_EQ || op == T_NEQ) {
        auto lnull = Is<NullLit>(left) != nullptr, rnull = Is<NullLit>(right) != nullptr;
        if (lnull || rnull) {
            if (lnull && rnull) return op == T_EQ ? "1" : "0";
            auto other = lnull ? right : left;
            // A narrowed optional variable's exprtype may already be the
            // decayed pointee; the null test reads the reference itself.
            string x;
            TypeExpr *ot = other->exprtype;
            if (auto id = Is<Ident>(other); id && id->vdef && cg.IsOpt(id->vdef->type)) {
                auto lv = cg.VarLoc(id->vdef);
                x = lv.s;
                ot = id->vdef->type;
            } else {
                x = cg.GenX(other);
            }
            auto addr = ot->kind == TY_REF && cg.IsResz(ot->ref->sub)
                            ? cat(x, ".hdr") : x;
            return cat("(", addr, op == T_EQ ? " == NULL)" : " != NULL)");
        }
    }
    // Resizable operands compare as element ranges through their views.
    if ((op == T_EQ || op == T_NEQ) && cg.IsResz(cg.OperandT(left->exprtype))) {
        if (left->exprtype->kind != right->exprtype->kind ||
            cg.OperandT(left->exprtype)->kind != TY_ARRAY)
            cg.Fail(line, "comparing resizable structs is unsupported");
        auto la = cg.GenLoc(left);
        if (la.t->kind == TY_REF) cg.DerefLoc(la, line);
        auto ra = cg.GenLoc(right);
        if (ra.t->kind == TY_REF) cg.DerefLoc(ra, line);
        auto lvw = cg.ArrayView(la, line), rvw = cg.ArrayView(ra, line);
        auto eq = cg.GenRangeEq(lvw.elem, lvw.elems, lvw.len, rvw.elems, rvw.len);
        return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
    }
    // Order of evaluation is left-to-right (§2): if the right operand
    // needs statements, the left must land in a temp first.
    auto leftfirst = cg.HasStmts(right);
    string l, r;
    if (leftfirst) {
        l = cg.GenPureVal(left);
        r = cg.GenVal(right);
    } else {
        l = cg.GenVal(left);
        r = cg.GenVal(right);
    }
    auto isint = lt->kind == TY_INT && lt->intstorage != IS_VARINT;
    auto isflt = lt->kind == TY_FLT;
    auto f32 = exprtype && exprtype->kind == TY_FLT &&
               exprtype->fltstorage == FS_F32;
    // Operands were unified by the typechecker; casting to the common C type
    // realizes any implicit widening (and truncates nothing).
    auto ct = isint || isflt ? cg.CT(lt) : "";
    auto lc = isint || isflt ? cat("(", ct, ")(", l, ")") : l;
    auto rc = isint || isflt ? cat("(", ct, ")(", r, ")") : r;
    switch (op) {
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            if (isflt) {
                if (op == T_MOD) return cat(f32 ? "fmodf(" : "fmod(", lc, ", ", rc, ")");
                const char *o = op == T_PLUS ? " + " : op == T_MINUS ? " - "
                                : op == T_MUL ? " * " : " / ";
                return cat("(", lc, o, rc, ")");
            }
            if (isint) {
                auto sfx = cg.IntSfx(lt->intstorage);
                switch (op) {
                    case T_PLUS:  return cat("gs_add_", sfx, "(", l, ", ", r, ")");
                    case T_MINUS: return cat("gs_sub_", sfx, "(", l, ", ", r, ")");
                    case T_MUL:   return cat("gs_mul_", sfx, "(", l, ", ", r, ")");
                    case T_DIV:   return cat("gs_div_", sfx, "(", l, ", ", r, ", ",
                                             cg.LocArgs(line), ")");
                    default:      return cat("gs_mod_", sfx, "(", l, ", ", r, ", ",
                                             cg.LocArgs(line), ")");
                }
            }
            // Elementwise math on identical struct/fixed-array types (§6.1).
            return cg.GenElemwise(this, l, r);
        }
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ: {
            const char *o = op == T_LT ? " < " : op == T_GT ? " > "
                            : op == T_LTEQ ? " <= " : " >= ";
            return cat("(uint8_t)(", lc, o, rc, ")");
        }
        case T_EQ: case T_NEQ: {
            if (isint || isflt) {
                auto eq = cat("(", lc, " == ", rc, ")");
                return op == T_EQ ? cat("(uint8_t)", eq) : cat("(uint8_t)(!", eq, ")");
            }
            auto eq = cg.GenEquality(lt, l, r);
            return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
        }
        case T_BITAND: return cat("(", ct, ")(", lc, " & ", rc, ")");
        case T_BITOR:  return cat("(", ct, ")(", lc, " | ", rc, ")");
        case T_XOR:    return cat("(", ct, ")(", lc, " ^ ", rc, ")");
        case T_SHL:    return cat("gs_shl_", cg.IntSfx(lt->intstorage), "(", l,
                                  ", (int64_t)(", r, "))");
        case T_SHR:    return cat("gs_shr_", cg.IntSfx(lt->intstorage), "(", l,
                                  ", (int64_t)(", r, "))");
        default: assert(false); return l;
    }
}

inline string Dot::CgX(CodeGen &cg) {
    if (variantconst) {
        auto et = exprtype;
        auto ei = einst;
        auto vi = cg.VarIdx(ei->en, variantconst);
        if (et->kind == TY_ENUM && !et->enu->varmode) {
            auto t = cg.T();
            cg.L(cg.CT(et), " ", t, ";");
            cg.L(t, ".tag = ", cg.TagConst(ei, vi), ";");
            return t;
        }
        cg.Fail(line, "variant constant in a non-fixed context reached GenX");
    }
    if (member >= 0) {   // .len / .cap property.
        auto lv = cg.GenLoc(obj);
        if (lv.t->kind == TY_REF) cg.DerefLoc(lv, line);
        if (lv.t->kind == TY_ARRAY && member == B_CAP) {
            auto &a = *lv.t->arr;
            assert(a.akind == A_LIMITED);
            if (cg.ArrSize(lv.t->arr) >= 0) return cat(cg.ArrSize(lv.t->arr));
            return cat("(int64_t)*(uint32_t *)(", lv.s, ")");
        }
        auto v = cg.ArrayView(lv, line);
        return cat("(", v.len, ")");
    }
    auto lv = cg.GenLoc(this);
    return cg.LoadLoc(lv, exprtype, line);
}

inline string Index::CgX(CodeGen &cg) {
    auto lv = cg.GenLoc(this);
    return cg.LoadLoc(lv, exprtype, line);
}

inline string SliceExpr::CgX(CodeGen &cg) { return cg.GenSlice(this); }
// `as` range-checks in debug builds (GS_RANGE and friends are identity
// casts unless the C is compiled with -DGS_DEBUG=1); `as!` always wraps
// or truncates (§6.3).
inline string AsCast::CgX(CodeGen &cg) {
    auto x = cg.GenX(child);
    auto st = child->exprtype;
    auto tt = exprtype;
    // u64 is the one source whose values exceed the int64 range the checks
    // compute in; it gets unsigned-compare variants.
    auto su64 = st->kind == TY_INT && st->intstorage == IS_U64;
    if (tt->kind == TY_INT) {
        auto is = tt->intstorage;
        auto tct = cg.IntCT(is);
        if (st->kind == TY_FLT) {
            if (unchecked) return cat("(", tct, ")gs_f2iwrap(", x, ")");
            if (is == IS_U64) return cat("(uint64_t)GS_F2U(", x, ")");
            // GS_F2I checks the exact i64 value; GS_RANGE narrows further.
            if (cg.IntSize(is) == 8) return cat("(", tct, ")GS_F2I(", x, ")");
            auto [lo, hi] = cg.IntRange(is);
            return cat("(", tct, ")GS_RANGE(GS_F2I(", x, "), ", cg.IntStr(lo), ", ",
                       cg.IntStr(hi), ")");
        }
        if (unchecked || cg.TEq(st, tt)) return cat("(", tct, ")(", x, ")");
        if (su64) {
            // From u64: value-preserving iff it does not exceed the target's
            // maximum (all targets' maxima fit an unsigned compare).
            if (is == IS_U64) return cat("(", x, ")");
            auto [lo, hi] = cg.IntRange(is);
            (void)lo;
            return cat("(", tct, ")GS_RANGE_U(", x, ", ", (uint64_t)hi, "ULL)");
        }
        if (is == IS_U64)   // Source ≤ i64.max: only negatives are out of range.
            return cat("(uint64_t)GS_RANGE((int64_t)(", x, "), 0, INT64_MAX)");
        if (cg.IntSize(is) == 8) return cat("(", tct, ")(", x, ")");
        auto [lo, hi] = cg.IntRange(is);
        return cat("(", tct, ")GS_RANGE((int64_t)(", x, "), ", cg.IntStr(lo), ", ",
                   cg.IntStr(hi), ")");
    }
    assert(tt->kind == TY_FLT);
    if (tt->fltstorage == FS_F32) {
        if (!unchecked && st->kind == TY_FLT && st->fltstorage != FS_F32)
            return cat("GS_F2F32(", x, ")");
        if (!unchecked && st->kind == TY_INT) {
            if (IntBits(st->intstorage) <= 16) return cat("(float)(", x, ")");  // Exact.
            if (su64) return cat("GS_U2F32(", x, ")");
            return cat("GS_I2F32((int64_t)(", x, "))");
        }
        return cat("(float)(", x, ")");
    }
    if (!unchecked && st->kind == TY_INT) {
        if (IntBits(st->intstorage) <= 32) return cat("(double)(", x, ")");  // Exact.
        if (su64) return cat("GS_U2F(", x, ")");
        return cat("GS_I2F(", x, ")");
    }
    return cat("(double)(", x, ")");
}

inline string Call::CgX(CodeGen &cg) {
    auto rets = cg.EmitCall(this, Dst {});
    assert(!rets.empty());
    return cg.CallVal0(this, rets[0]);
}

// Fixed struct/variant/ADT literal as a C value: a temporary the caller
// copies from. Values containing relative references are built at their
// destination instead (FixedLitAt), since offsets from a temporary would
// not survive the copy.
inline string StructLit::CgX(CodeGen &cg) {
    auto tv = cg.T();
    cg.L(cg.CT(exprtype), " ", tv, ";");
    cg.StructLitAt(this, tv, false);
    return tv;
}

inline string ArrayLit::CgX(CodeGen &cg) { return cg.GenFixedArrayLit(this); }

inline string Block::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string IfExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string MatchExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string EarlyBlock::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string LoopExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string InlineBlock::CgX(CodeGen &cg) { return cg.CtlValX(this); }

// Statements and compile-time-only nodes have no value expression.
inline string While::CgX(CodeGen &cg) { cg.Fail(line, "internal: While as value"); }
inline string ForLoop::CgX(CodeGen &cg) { cg.Fail(line, "internal: ForLoop as value"); }
inline string Guard::CgX(CodeGen &cg) { cg.Fail(line, "internal: Guard as value"); }
inline string Return::CgX(CodeGen &cg) { cg.Fail(line, "internal: Return as value"); }
inline string Break::CgX(CodeGen &cg) { cg.Fail(line, "internal: Break as value"); }
inline string Continue::CgX(CodeGen &cg) { cg.Fail(line, "internal: Continue as value"); }
inline string RangeExpr::CgX(CodeGen &cg) { cg.Fail(line, "internal: RangeExpr as value"); }
inline string FunVal::CgX(CodeGen &cg) { cg.Fail(line, "internal: FunVal as value"); }
inline string SelfRef::CgX(CodeGen &cg) { cg.Fail(line, "internal: self as value"); }
inline string VarDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: VarDecl as value"); }
inline string Assign::CgX(CodeGen &cg) { cg.Fail(line, "internal: Assign as value"); }
inline string IncDec::CgX(CodeGen &cg) { cg.Fail(line, "internal: IncDec as value"); }
inline string FnDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: FnDecl as value"); }
inline string StructDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }
inline string EnumDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }
inline string AliasDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }

// ---- CgAny ----------------------------------------------------------------

inline void Block::CgAny(CodeGen &cg, const Dst &d) {
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.L("{");
    cg.ind++;
    cg.GenBlockInner(this, d);
    cg.PopSc();
    cg.ind--;
    cg.L("}");
}

inline void IfExpr::CgAny(CodeGen &cg, const Dst &d) {
    auto c = cg.GenTruth(cond);
    cg.L("if (", c, ") {");
    cg.ind++;
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.GenBlockInner(thenb, d);
    cg.PopSc();
    cg.ind--;
    if (elseb) {
        cg.L("} else {");
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (auto b = Is<Block>(elseb)) cg.GenBlockInner(b, d);
        else cg.GenAny(elseb, d);
        cg.PopSc();
        cg.ind--;
    }
    cg.L("}");
    cg.termjump = false;
}

inline void MatchExpr::CgAny(CodeGen &cg, const Dst &d) {
    auto st = scrutinee->exprtype;
    TypeExpr *enumtype = nullptr;
    auto isref = false;
    if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
        enumtype = st->ref->sub;
        isref = true;
    } else if (st->kind == TY_ENUM) {
        enumtype = st;
    }
    if (!enumtype) {   // Integer match: an if-chain in arm order.
        auto x = cg.GenPure(scrutinee);
        // Compare at the scrutinee's type (unsigned scrutinees compare
        // unsigned; pattern constants carry that type's value bits).
        auto sct = cg.CT(scrutinee->exprtype);
        auto k = [&](int64_t v) { return cat("(", sct, ")", cg.IntStr(v)); };
        auto xs = cat("(", sct, ")(", x, ")");
        auto first = true;
        for (auto &arm : arms) {
            if (arm.pat.kind == P_WILDCARD) {
                cg.L(first ? "{" : "} else {");
            } else if (arm.hi == arm.lo + 1) {
                cg.L(first ? "" : "} else ", "if (", xs, " == ", k(arm.lo), ") {");
            } else {
                cg.L(first ? "" : "} else ", "if (", xs, " >= ", k(arm.lo), " && ", xs,
                  " < ", k(arm.hi), ") {");
            }
            first = false;
            cg.ind++;
            cg.PushSc(CodeGen::SC_PLAIN);
            cg.GenAny(arm.body, d);
            cg.PopSc();
            cg.ind--;
        }
        cg.L("}");
        cg.termjump = false;
        return;
    }
    auto varmode = enumtype->enu->varmode;
    auto ei = cg.EIOf(enumtype);
    auto ts = cg.TagSize(ei->en);
    string p, sv, tag;
    if (varmode || isref) {
        if (isref) {
            auto x = cg.GenPure(scrutinee);
            p = varmode ? x : cat("((uint8_t *)", x, ")");
        } else {
            p = cg.GenPtr(scrutinee);
        }
        tag = varmode ? cat("*(", cg.IntCT(cg.TagStore(ei->en)), " *)", p)
                      : cat("((", cg.CT(enumtype), " *)", p, ")->tag");
    } else {
        sv = cg.GenPure(scrutinee);
        tag = cat(sv, ".tag");
    }
    cg.L("switch (", tag, ") {");
    auto haswild = false;
    for (auto &arm : arms) {
        if (arm.pat.kind == P_WILDCARD) {
            haswild = true;
            cg.L("default: {");
        } else {
            auto vi = cg.VarIdx(ei->en, arm.variant);
            cg.L("case ", cg.TagConst(ei, vi), ": {");
        }
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (arm.binder) {
            auto vi = cg.VarIdx(ei->en, arm.variant);
            auto vt = cg.VariantType(enumtype, vi);
            auto bn = cg.LocalName(arm.binder);
            string payload = varmode
                ? cat("(", p, " + ", ts, ")")
                : (isref ? cat("((uint8_t *)&((", cg.CT(enumtype), " *)", p, ")->u.v_",
                               cg.Sanitize(arm.variant->name), ")")
                         : "");
            if (arm.pat.byref) {
                // Variable-mode payloads only (§8.1).
                if (cg.IsBytesT(vt)) cg.L("uint8_t *", bn, " = ", payload, ";");
                else cg.L(cg.CT(vt), " *", bn, " = (", cg.CT(vt), " *)(", payload, ");");
            } else if (cg.IsBytesT(vt)) {
                // By-value copy of a variable payload onto its own stack.
                auto stk = cg.AllocStk(true);
                cg.L("uint8_t *", bn, " = ", cg.Top(stk), ";");
                cg.SaveBase(true, stk, bn);
                cg.vstk[arm.binder] = stk;
                auto sz = cg.T();
                cg.L("int64_t ", sz, " = ", cg.SizeX(vt, payload), ";");
                cg.L("memcpy(", cg.Top(stk), ", ", payload, ", (size_t)", sz, ");");
                cg.Bump(stk, sz);
            } else if (arm.variant->fields.empty()) {
                cg.L(cg.CT(vt), " ", bn, " = {0};");
            } else if (!payload.empty()) {
                cg.L(cg.CT(vt), " ", bn, " = *(", cg.CT(vt), " *)(", payload, ");");
            } else {
                cg.L(cg.CT(vt), " ", bn, " = ", sv, ".u.v_", cg.Sanitize(arm.variant->name), ";");
            }
        }
        cg.GenAny(arm.body, d);
        cg.PopSc();
        cg.ind--;
        cg.L("} break;");
    }
    if (!haswild)
        cg.L("default: GS_UNREACHABLE(", cg.LocArgs(line), ");");
    cg.L("}");
    cg.termjump = false;
}

inline void EarlyBlock::CgAny(CodeGen &cg, const Dst &d) {
    cg.PushSc(CodeGen::SC_BLOCK);
    auto si = (int)cg.cscopes.size() - 1;
    cg.cscopes[si].brklbl = cg.Lbl();
    cg.cscopes[si].dst = d;
    cg.L("{");
    cg.ind++;
    cg.GenBlockInner(body, d);
    auto brk = cg.cscopes.back().usedbrk;
    auto lbl = cg.cscopes.back().brklbl;
    cg.PopSc();
    cg.ind--;
    cg.L("}");
    if (brk) cg.L(lbl, ":;");
    cg.termjump = false;
}

inline void LoopExpr::CgAny(CodeGen &cg, const Dst &d) {
    CodeGen::ViewScope vs(cg, hoistrefs, line);
    cg.GenLoopBody({}, body, d);
}

inline void InlineBlock::CgAny(CodeGen &cg, const Dst &d) {
    auto named = cg.OpenIbNrvo(this, d);
    cg.PushSc(CodeGen::SC_IB);
    auto si = (int)cg.cscopes.size() - 1;
    cg.cscopes[si].ibsf = sf;
    cg.cscopes[si].brklbl = cg.Lbl();
    cg.cscopes[si].dst = d;
    cg.GenAny(body, d);
    auto brk = cg.cscopes.back().usedbrk;
    auto lbl = cg.cscopes.back().brklbl;
    cg.PopSc();
    if (brk) cg.L(lbl, ":;");
    if (named) cg.nrvo.erase(named);
    cg.termjump = false;
}

inline void Call::CgAny(CodeGen &cg, const Dst &d) {
    auto rets = cg.EmitCall(this, d);
    // Fixed-value results wire into the destination here; bytes results and
    // channel-passed returns were handled in place.
    if (!rets.empty() && !cg.IsVoidT(exprtype) && !cg.IsBytesT(exprtype)) {
        auto r0 = cg.CallVal0(this, rets[0]);
        if (d.k == DK_LVALUE && r0 != d.s) cg.L(d.s, " = ", r0, ";");
        else if (d.k == DK_STACK) cg.EmitValStore(d.s, exprtype, r0);
    }
}

inline void IntLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void FltLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void BoolLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void StrLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void NullLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Ident::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void ArrayLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void StructLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Unary::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Binary::CgAny(CodeGen &cg, const Dst &d) {
    // An elementwise result (struct/fixed-array typed, §6.1) writes its
    // members straight into a plain destination — including one that aliases
    // an operand (see GenElemwiseInto).
    if (d.k == DK_LVALUE && exprtype &&
        (exprtype->kind == TY_STRUCT || exprtype->kind == TY_ARRAY)) {
        string l, r;
        cg.ElemwiseOperands(this, l, r);
        cg.GenElemwiseInto(this, l, r, d.s);
        return;
    }
    cg.LeafAny(this, d);
}
inline void Dot::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Index::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void SliceExpr::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void AsCast::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void RangeExpr::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }

// Statement nodes reached with a discard destination just emit themselves.
inline void While::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void ForLoop::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Guard::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Return::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Break::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Continue::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void FunVal::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: FunVal emitted"); }
inline void SelfRef::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: self emitted"); }
inline void VarDecl::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Assign::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void IncDec::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void FnDecl::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void StructDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }
inline void EnumDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }
inline void AliasDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }

// ---- CgStmt ---------------------------------------------------------------

inline void VarDecl::CgStmt(CodeGen &cg) {
    // Multi-name binding from one call: wire the call's channels straight
    // into the locals.
    if (names.size() > 1 && inits.size() == 1) {
        auto c = Is<Call>(inits[0]);
        assert(c);
        vector<Dst> dsts;
        for (size_t i = 0; i < defs.size(); i++) {
            auto d = defs[i];
            auto rt = c->rettypes[i];
            auto name = cg.LocalName(d);
            if (cg.IsResz(rt) && cg.IsFrameObj(rt)) {
                cg.EmitCoreTypes();
                auto stk = cg.AllocStk(true);
                cg.L(cg.CT(rt), " ", name, ";");
                cg.SaveBase(true, stk, cat(cg.FoTailHdr(rt, name), ".base"));
                cg.vstk[d] = stk;
                dsts.push_back(Dst { DK_STACK, stk, d->type, name });
            } else if (cg.IsResz(rt)) {
                cg.EmitCoreTypes();
                auto stk = cg.AllocStk(true);
                cg.L("gs_rhdr ", name, " = { ", cg.Top(stk), ", 0 };");
                cg.SaveBase(true, stk, cat(name, ".base"));
                cg.vstk[d] = stk;
                dsts.push_back(Dst { DK_STACK, stk, d->type, cat(name, ".len") });
            } else if (cg.IsBytesT(rt)) {
                auto stk = cg.AllocStk(true);
                cg.L("uint8_t *", name, " = ", cg.Top(stk), ";");
                cg.SaveBase(true, stk, name);
                cg.vstk[d] = stk;
                dsts.push_back(Dst { DK_STACK, stk });
            } else {
                cg.L(cg.CT(d->type), " ", name, ";");
                dsts.push_back(Dst { DK_LVALUE, name });
            }
        }
        auto rets = cg.EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
        for (size_t i = 0; i < dsts.size() && i < rets.size(); i++)
            if (dsts[i].k == DK_LVALUE && !rets[i].empty() && rets[i] != dsts[i].s)
                cg.L(dsts[i].s, " = ", rets[i], ";");
        return;
    }
    for (size_t i = 0; i < defs.size(); i++)
        cg.BindLocal(defs[i], i < inits.size() ? inits[i] : nullptr);
}

inline void Assign::CgStmt(CodeGen &cg) {
    auto lv = cg.GenLoc(lval);
    if (op == T_DOTASSIGN) { cg.GenRebind(this, lv); return; }
    if (pointee) cg.DerefLoc(lv, line);
    // Compound operators: full-width int/flt locations only (TC).
    if (op != T_ASSIGN) {
        assert(lv.val);
        auto r = cg.GenX(rhs);
        auto sfx = lv.t->kind == TY_INT ? cg.IntSfx(lv.t->intstorage) : "";
        switch (op) {
            case T_PLUSEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " + (", r, ");");
                else cg.L(lv.s, " = gs_add_", sfx, "(", lv.s, ", ", r, ");");
                break;
            case T_MINUSEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " - (", r, ");");
                else cg.L(lv.s, " = gs_sub_", sfx, "(", lv.s, ", ", r, ");");
                break;
            case T_MULEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " * (", r, ");");
                else cg.L(lv.s, " = gs_mul_", sfx, "(", lv.s, ", ", r, ");");
                break;
            case T_DIVEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " / (", r, ");");
                else cg.L(lv.s, " = gs_div_", sfx, "(", lv.s, ", ", r, ", ",
                          cg.LocArgs(line), ");");
                break;
            case T_MODEQ:
                if (lv.t->kind == TY_FLT)
                    cg.L(lv.s, " = ", lv.t->fltstorage == FS_F32 ? "fmodf(" : "fmod(", lv.s,
                      ", ", r, ");");
                else cg.L(lv.s, " = gs_mod_", sfx, "(", lv.s, ", ", r, ", ",
                          cg.LocArgs(line), ");");
                break;
            case T_ANDEQ: cg.L(lv.s, " = (", cg.CT(lv.t), ")(", lv.s, " & (", r, "));"); break;
            case T_OREQ:  cg.L(lv.s, " = (", cg.CT(lv.t), ")(", lv.s, " | (", r, "));"); break;
            case T_XOREQ: cg.L(lv.s, " = (", cg.CT(lv.t), ")(", lv.s, " ^ (", r, "));"); break;
            default: assert(false);
        }
        return;
    }
    // Plain assignment, by the target's representation (§4.4).
    auto t = lv.t;
    if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
        // Relative-reference slot: encode from the plain reference value.
        cg.GenRelAssign(lv, rhs, line);
        return;
    }
    if (cg.IsResz(t)) {
        // Whole-resizable assignment: clear (top back to the value start),
        // then construct the new contents in place (§4.4).
        if (cg.IsFrameObj(t)) {
            assert(lv.val && !lv.stk.empty());
            cg.L(cg.TopW(lv.stk), " = ", cg.FoTailHdr(t, lv.s), ".base;");
            cg.GenConstruct(rhs, lv.stk, lv.t, lv.s);
            return;
        }
        assert(!lv.val && !lv.stk.empty() && !lv.lenlv.empty());
        cg.L(cg.TopW(lv.stk), " = ", lv.s, ";");
        cg.GenConstruct(rhs, lv.stk, lv.t, lv.lenlv);
        return;
    }
    if (!lv.val) {
        // Bytes-class fixed-capacity array [..]: copy contents within cap.
        assert(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED);
        string srcstk;
        auto src = cg.GenPtr(rhs, &srcstk);
        auto nn = cg.T();
        cg.L("int64_t ", nn, " = (int64_t)*(uint32_t *)(", src, " + 4);");
        cg.L("if (", nn, " > (int64_t)*(uint32_t *)(", lv.s, ")",
          ") gs_abort(GS_E_CAPACITY, ", cg.LocArgs(line), ");");
        cg.L("*(uint32_t *)((", lv.s, ") + 4) = (uint32_t)", nn, ";");
        cg.L("memcpy((", lv.s, ") + 8, ", src, " + 8, (size_t)(", nn, " * ",
          cg.FixedSize(t->arr->sub), "));");
        return;
    }
    cg.GenAny(rhs, Dst { DK_LVALUE, lv.s, lv.t });
}

inline void IncDec::CgStmt(CodeGen &cg) {
    auto lv = cg.GenLoc(lval);
    if (lv.t->kind == TY_REF) cg.DerefLoc(lv, line);
    assert(lv.val);
    auto o = op == T_INC ? "gs_add_" : "gs_sub_";
    cg.L(lv.s, " = ", o, cg.IntSfx(lv.t->intstorage), "(", lv.s, ", 1);");
}

inline void FnDecl::CgStmt(CodeGen &) {}   // Nested declarations are separate specs.
inline void While::CgStmt(CodeGen &cg) {
    Dst d;
    CodeGen::ViewScope vs(cg, hoistrefs, line);
    cg.GenLoopBody([&]() {
        auto c = cg.GenTruth(cond);
        auto si = (int)cg.cscopes.size() - 1;
        // The loop scope has no allocations yet, so a plain goto exits
        // cleanly; the condition's own temps were allocated in this
        // scope's compile-time list and are restored... none yet either.
        cg.L("if (!(", c, ")) goto ", cg.cscopes[si].brklbl, ";");
        cg.cscopes[si].usedbrk = true;
    }, body, d);
}

inline void ForLoop::CgStmt(CodeGen &cg) {
    auto d = Dst {};
    CodeGen::ViewScope vs(cg, hoistrefs, line);
    auto iv = vdef ? cg.LocalName(vdef) : cg.T();
    auto ix = idxdef ? cg.LocalName(idxdef) : "";
    if (iterkind == IK_RANGE) {
        // The loop variable runs at the range's own type: narrow counters
        // stay narrow through the body (§6.5).
        auto ict = cg.CT(vdef->type);
        auto r = Is<RangeExpr>(iter);
        auto lo = cg.GenX(r->lo);
        auto lov = cg.T();
        cg.L(ict, " ", lov, " = ", lo, ";");
        auto hi = cg.GenX(r->hi);
        auto hiv = cg.T();
        cg.L(ict, " ", hiv, " = ", hi, ";");
        if (!ix.empty()) cg.L("int64_t ", ix, " = 0;");
        cg.GenLoopBody({}, body, d,
                    cat("for (", ict, " ", iv, " = ", lov, "; ", iv, " < ", hiv, "; ", iv,
                        "++", ix.empty() ? "" : cat(", ", ix, "++"), ") {"));
        return;
    }
    if (iterkind == IK_COUNT) {
        auto ict = cg.CT(vdef->type);
        auto n = cg.GenX(iter);
        auto nv = cg.T();
        cg.L(ict, " ", nv, " = ", n, ";");
        if (!ix.empty()) cg.L("int64_t ", ix, " = 0;");
        cg.GenLoopBody({}, body, d,
                    cat("for (", ict, " ", iv, " = 0; ", iv, " < ", nv, "; ", iv, "++",
                        ix.empty() ? "" : cat(", ", ix, "++"), ") {"));
        return;
    }
    // Arrays and slices. The length re-reads each iteration (growth during
    // iteration is legal, §5.2); element access goes through the view.
    auto lv = cg.GenLoc(iter);
    if (lv.t->kind == TY_REF) cg.DerefLoc(lv, line);
    auto v = cg.ArrayView(lv, line);
    // Where BCE proved the body cannot resize it, both halves of the view are
    // loop-invariant. Only a length that is an actual memory load is worth
    // spelling as a local: that is the one the C backend cannot hoist for
    // itself, since an element store in the body might alias it. A resizable
    // owned here keeps its length in a frame header (C.2) -- an ordinary local
    // the backend already knows nothing aliases, and hoisting it by hand only
    // lengthens a live range.
    auto memlen = v.len.find('*') != string::npos || v.len.find("->") != string::npos;
    if (fixedlen && memlen) {
        auto nv = cg.T();
        cg.L("int64_t ", nv, " = ", v.len, ";");
        auto bv = cg.T();
        if (v.typedelems) cg.L(cg.CT(v.elem), " *", bv, " = ", v.elems, ";");
        else cg.L("uint8_t *", bv, " = (uint8_t *)(", v.elems, ");");
        v.len = nv;
        v.elems = bv;
    }
    auto gi = ix.empty() ? cg.T() : ix;
    auto et = vdef->type;
    if (!cg.IsFix(v.elem)) {
        // Sequential walk, &-binding only; the cursor advances in the
        // increment clause so `continue` behaves.
        auto p = cg.T();
        cg.L("uint8_t *", p, " = (uint8_t *)(", v.elems, ");");
        cg.GenLoopBody([&]() {
            if (byref) cg.L("uint8_t *", iv, " = ", p, ";");
        }, body, d,
            cat("for (int64_t ", gi, " = 0; ", gi, " < (", v.len, "); ", gi, "++, ", p,
                " += ", cg.SizeX(v.elem, p), ") {"));
        return;
    }
    auto esz = cg.FixedSize(v.elem);
    cg.GenLoopBody([&]() {
        string elem = v.typedelems
                          ? cat(v.elems, "[", gi, "]")
                          : cat("(*(", cg.CT(v.elem), " *)((", v.elems, ") + ", gi, " * ",
                                esz, "))");
        if (byref) {
            cg.L(cg.CT(et), " ", iv, " = &", elem, ";");
        } else if (v.elem->kind == TY_REF && v.elem->ref->lenstorage >= 0) {
            // A relative-reference element bound by value: the binding is the
            // decoded pointer, as indexing the array would give (§3.9).
            auto fa = cg.T();
            cg.L("uint8_t *", fa, " = (uint8_t *)&", elem, ";");
            auto off = cg.T();
            cg.L("int64_t ", off, " = (int64_t)*(", cg.RelCT(v.elem), " *)(", fa, ");");
            auto target = cat("(", cg.CT(et), ")(", cg.RelOrigin(v.elem, fa), " + ", off, ")");
            if (v.elem->ref->optional)
                cg.L(cg.CT(et), " ", iv, " = ", off, " ? ", target, " : NULL;");
            else
                cg.L(cg.CT(et), " ", iv, " = ", target, ";");
        } else {
            cg.L(cg.CT(et), " ", iv, " = ", elem, ";");
        }
    }, body, d,
        cat("for (int64_t ", gi, " = 0; ", gi, " < (", v.len, "); ", gi, "++) {"));
}

inline void Guard::CgStmt(CodeGen &cg) {
    auto c = cg.GenTruth(cond);
    cg.L("if (!(", c, ")) {");
    cg.ind++;
    cg.PushSc(CodeGen::SC_PLAIN);
    if (elseb) {
        cg.GenBlockInner(elseb, Dst {});
    } else if (implicitexit == 1) {
        cg.GenBreakPath(nullptr);
    } else {
        cg.GenNormalReturn({});
    }
    cg.cscopes.back().saves.clear();   // The block diverged.
    cg.PopSc();
    cg.ind--;
    cg.L("}");
    cg.termjump = false;
}

inline void Return::CgStmt(CodeGen &cg) {
    // Exiting an inlined body?
    for (auto i = (int)cg.cscopes.size() - 1; i >= 0; i--) {
        if (cg.cscopes[i].kind == CodeGen::SC_IB && cg.cscopes[i].ibsf == target) {
            if (!vals.empty()) {
                cg.GenAny(vals[0], cg.cscopes[i].dst);
                for (size_t j = 1; j < vals.size(); j++) cg.GenAny(vals[j], Dst {});
            }
            cg.EmitExitRestores(i);
            cg.cscopes[i].usedbrk = true;
            cg.L("goto ", cg.cscopes[i].brklbl, ";");
            cg.termjump = true;
            return;
        }
        if (cg.cscopes[i].kind == CodeGen::SC_FN) break;
    }
    if (cg.curspec && target == cg.curspec->sf) {
        cg.GenNormalReturn(vals);
        return;
    }
    cg.GenFromReturn(this);
}

inline void Break::CgStmt(CodeGen &cg) { cg.GenBreakPath(val); }
inline void Continue::CgStmt(CodeGen &cg) {
    auto si = -1;
    for (auto i = (int)cg.cscopes.size() - 1; i >= 0; i--) {
        if (cg.cscopes[i].kind == CodeGen::SC_LOOP) { si = i; break; }
        if (cg.cscopes[i].kind == CodeGen::SC_FN) break;
    }
    assert(si >= 0);
    cg.EmitExitRestores(si + 1);
    cg.cscopes[si].usedcnt = true;
    cg.L("goto ", cg.cscopes[si].cntlbl, ";");
    cg.termjump = true;
}

// Everything else in statement position evaluates for effect.
inline void IntLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void FltLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void BoolLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void StrLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void NullLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Ident::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void ArrayLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void StructLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Unary::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Binary::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Dot::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Index::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void SliceExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void AsCast::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void RangeExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Call::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Block::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void IfExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void MatchExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void EarlyBlock::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void LoopExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void InlineBlock::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void FunVal::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: FunVal statement"); }
inline void SelfRef::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: self statement"); }
inline void StructDecl::CgStmt(CodeGen &) {}
inline void EnumDecl::CgStmt(CodeGen &) {}
inline void AliasDecl::CgStmt(CodeGen &) {}

}  // namespace goose
