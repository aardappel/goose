// Goose compiler — the per-node typecheck implementations (the Check virtual):
// the pass reads top to bottom here; the shared machinery (calls, control
// flow, literals' construction rules) is TypeCheck's, typecheck.h and the
// typecheck_*.h it lists. Values may denote references; the CheckValue,
// CheckArg and Operand wrappers apply transparency.
#pragma once

namespace goose {

inline Val IntLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.inttypes[uns ? IS_U64 : IS_I64];
    v.ck = CK_INT;
    v.ival = val;
    v.uns = uns;
    v.nonneg = uns || val >= 0;
    return v;
}

inline Val FltLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.flttypes[FS_F64];
    v.ck = CK_FLT;
    v.fval = val;
    return v;
}

inline Val BoolLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.booltype;
    return v;
}

inline Val NullLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    v.isnull = true;
    // Its own type only matters when nothing adapts it; FitsAt checks isnull.
    v.type = expected && expected->kind == TY_REF && expected->ref->optional
                 ? expected : tc.nulltype;
    return v;
}

// `self` is consumed by CheckInits in the only position that gives it a
// meaning, so reaching the general expression path means it is misplaced.
inline Val SelfRef::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "self is only valid as the initializer of a non-optional "
                   "relative-reference field of the literal containing it (§3.9)");
}

inline Val StrLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    v.strlit = true;
    v.rootexact = true;   // Static data owns what it holds.
    v.writable = false;
    if (expected) {
        // A string literal constructs any u8-element array type (§3.7).
        if (expected->kind == TY_ARRAY && tc.IsU8(expected->arr->sub)) {
            if (expected->arr->akind == A_FIXED &&
                tc.ArraySize(expected->arr) != (int64_t)val.size())
                tc.Error(this, cat("string literal of length ", (int64_t)val.size(),
                                   " does not fit ", tc.TypeStr(expected)));
            v.type = expected;
            return v;
        }
        if (expected->kind == TY_SLICE && tc.IsU8(expected->sub)) {
            v.type = expected;
            return v;
        }
    }
    v.type = tc.u8slice;
    return v;
}

inline Val Ident::Check(TypeCheck &tc, TypeExpr *) {
    if (auto vd = tc.LookupVar(name)) {
        vdef = vd;
        tc.RequireAssigned(vd, this);
        Val v;
        auto t = vd->narrowed ? vd->narrowed : vd->type;
        v.type = tc.LoadType(t);
        if (t->kind == TY_REF || t->kind == TY_SLICE) {
            v.SetProv(tc.RefProvOf(vd));
            v.lvalue = t->kind == TY_SLICE;   // A slice variable is storage; a reference is the pointee's path.
        } else {
            v.root = vd;
            v.rootexact = true;
            v.writable = vd->isvar;
            v.reusable = vd->reusable;
            v.nonneg = vd->nonneg;
            v.lvalue = true;
        }
        return v;
    }
    // A generic parameter bound to a function value, or a named function: a
    // compile-time function value (§7.6).
    if (auto fb = tc.LookupFnVal(name)) {
        Val v;
        v.type = tc.fntype;
        v.fnv = *fb;
        return v;
    }
    if (auto sf = tc.LookupLocalFn(name)) {
        fnref = sf;
        Val v;
        v.type = tc.fntype;
        v.fnv.named = sf;
        v.fnv.env = tc.frames.back().lexspec;
        return v;
    }
    auto fit = tc.ast.functionmap.find(name);
    if (fit != tc.ast.functionmap.end()) {
        if (fit->second.size() != 1)
            tc.Error(this, cat("overloaded function ", name, " cannot be a function value"));
        fnref = fit->second[0];
        Val v;
        v.type = tc.fntype;
        v.fnv.named = fit->second[0];
        return v;
    }
    tc.Error(this, cat("unknown identifier: ", name));
}

inline Val ArrayLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    if (capexpr) {
        // [..cap]: an empty limited array with a runtime capacity (§5.3).
        tc.CheckIntAny(capexpr);
        if (!expected || expected->kind != TY_ARRAY ||
            expected->arr->akind != A_LIMITED || expected->arr->sizeexpr)
            tc.Error(this, "[..cap] constructs a limited array of construction-time "
                           "capacity; it needs a T[..] destination");
        v.type = expected;
        return v;
    }
    TypeExpr *elem = nullptr;
    int64_t wantcount = -1;
    if (expected && expected->kind == TY_ARRAY) {
        elem = expected->arr->sub;
        if (expected->arr->akind == A_FIXED) wantcount = tc.ArraySize(expected->arr);
    } else if (expected && expected->kind == TY_SLICE) {
        elem = expected->sub;
    }
    if (fillval) {
        auto cnt = tc.ConstIntOrError(fillcount, "array fill count");
        if (cnt < 0) tc.Error(this, "array fill count cannot be negative");
        auto ev = tc.CheckValue(fillval, elem);
        if (!elem) elem = ev.type;
        if (wantcount >= 0 && cnt != wantcount)
            tc.Error(this, cat("fill count ", cnt, " does not match array size ", wantcount));
        v.type = expected && expected->kind == TY_ARRAY
                     ? expected : tc.FixedArrayOf(elem, cnt, line);
        return v;
    }
    if (elems.empty() && !elem) {
        // [] adapts to any array type; callers re-check with an expected type
        // or report the missing context.
        v.emptyarr = true;
        v.type = tc.FixedArrayOf(tc.ast.voidtype, 0, line);
        return v;
    }
    for (auto &e : elems) {
        auto ev = tc.CheckValue(e, elem);
        if (!elem) elem = ev.type;
    }
    if (elem->kind == TY_VOID) tc.Error(this, "cannot infer array element type");
    if (wantcount >= 0 && (int64_t)elems.size() != wantcount)
        tc.Error(this, cat((int64_t)elems.size(), " element(s) do not fill ",
                           tc.TypeStr(expected)));
    if (expected && (expected->kind == TY_ARRAY || expected->kind == TY_SLICE)) {
        // A literal in slice position materializes a temporary fixed array.
        v.type = expected->kind == TY_ARRAY
                     ? expected : tc.FixedArrayOf(elem, (int64_t)elems.size(), line);
        v.root = expected->kind == TY_SLICE ? tc.temproot : nullptr;
        return v;
    }
    v.type = tc.FixedArrayOf(elem, (int64_t)elems.size(), line);
    return v;
}

inline Val StructLit::Check(TypeCheck &tc, TypeExpr *expected) {
    fieldindices.clear();
    sinst = nullptr;
    einst = nullptr;
    variant = nullptr;
    auto t = tc.Subst(type);
    if (t->kind == TY_VARIANT) {
        if (t->var->adt->kind != TY_ENUM)
            tc.Error(this, cat("variant literal of non-ADT type ", tc.TypeStr(t->var->adt)));
        auto ei = tc.GetEnumInst(t->var->adt);
        auto var = t->var->variant;
        auto vi = tc.VariantIndex(ei->en, var);
        einst = ei;
        variant = var;
        Val v;
        // The mode comes from the receiving declaration (§3.5); a literal
        // adapts to either, or stands as a first-class variant value (§8.2).
        // Which it is decides what `self` inside it denotes, so settle the
        // type before the initializers are checked.
        v.type = expected && expected->kind == TY_ENUM && expected->enu->en == ei->en &&
                         tc.TypeArgsEq(expected->enu->args, t->var->adt->enu->args)
                     ? expected : t;
        tc.CheckInits(this, var->fields, ei->vftypes[vi], ei->en->name, v.type);
        return v;
    }
    if (t->kind == TY_STRUCT) {
        auto st = t->struc->st;
        if (t->struc->args.empty() && !st->generics.empty()) {
            // Infer the struct's generics from the field initializers.
            vector<pair<string_view, TypeExpr *>> b;
            auto named = !inits.empty() && !inits[0].name.empty();
            auto pos = 0;
            for (auto &fi : inits) {
                Field *field = nullptr;
                if (named) {
                    for (auto &f2 : st->fields)
                        if (!f2.ispad && f2.name == fi.name) { field = &f2; break; }
                } else {
                    while (pos < (int)st->fields.size() && st->fields[pos].ispad) pos++;
                    if (pos < (int)st->fields.size()) field = &st->fields[pos++];
                }
                if (!field) continue;  // Reported properly below.
                // `self` says nothing about the arguments (its type is the
                // literal's own), and has no meaning outside CheckInits.
                if (Is<SelfRef>(fi.val)) continue;
                auto av = tc.CheckV(fi.val, nullptr);
                fi.val->exprtype = av.type;
                auto nt = tc.NaturalType(av);
                if (nt) tc.BindTypes(field->type, nt, b);
            }
            auto nt2 = tc.ast.NewType(TY_STRUCT, line);
            nt2->struc = tc.ast.NewDetail<TypeStruct>();
            nt2->struc->st = st;
            for (auto &g : st->generics) {
                TypeExpr *bound = nullptr;
                for (auto &[n, bt] : b) if (n == g.name) bound = bt;
                if (!bound)
                    tc.Error(this, cat("cannot infer generic parameter ", g.name, " of ",
                                       st->name, "; use ", st->name, "<...> { }"));
                nt2->struc->args.push_back(bound);
            }
            t = nt2;
        }
        auto inst = tc.GetStructInst(t);
        sinst = inst;
        tc.CheckInits(this, st->fields, inst->ftypes, st->name, t);
        Val v;
        v.type = t;
        return v;
    }
    tc.Error(this, cat("cannot construct a value of type ", tc.TypeStr(t), " with a literal"));
}

inline Val Unary::Check(TypeCheck &tc, TypeExpr *) {
    if (op == T_BITAND) return tc.CheckRefOf(this);
    auto v = tc.Operand(child);
    auto t = tc.LoadType(v.type);
    Val r;
    switch (op) {
        case T_MINUS:
            if (tc.IsIntT(t)) {
                if (v.ck == CK_INT) {
                    // -(2^63) is exactly i64.min; any other u64-range value
                    // cannot be negated.
                    if (v.uns && v.ival != INT64_MIN)
                        tc.Error(this, "negated constant too large for i64");
                    r.type = tc.ast.inttypes[IS_I64];
                    r.ck = CK_INT;
                    r.ival = v.uns ? INT64_MIN : -v.ival;
                    child->exprtype = r.type;
                    return r;
                }
                if (IsUnsigned(t->intstorage))
                    tc.Error(this, cat("cannot negate a value of unsigned type ",
                                       tc.TypeStr(t), " (convert with `as`)"));
                r.type = t;
            } else if (t->kind == TY_FLT) {
                r.type = t;
                if (v.ck == CK_FLT) { r.ck = CK_FLT; r.fval = -v.fval; }
            } else {
                tc.Error(this, cat("cannot negate a value of type ", tc.TypeStr(t)));
            }
            return r;
        case T_NOT:
            // Optionals are testable like conditions (§3.8 truthiness).
            if (t->kind != TY_BOOL && !tc.IsOptional(t))
                tc.Error(this, cat("! requires bool, got ", tc.TypeStr(t)));
            r.type = tc.ast.booltype;
            return r;
        case T_BITNOT:
            if (!tc.IsIntT(t))
                tc.Error(this, cat("~ requires an integer, got ", tc.TypeStr(t)));
            if (v.ck == CK_INT) {
                r.type = tc.ast.inttypes[v.uns ? IS_U64 : IS_I64];
                r.ck = CK_INT;
                r.ival = ~v.ival;
                r.uns = v.uns && r.ival < 0;
                child->exprtype = r.type;
                return r;
            }
            r.type = t;
            return r;
        default:
            assert(false);
            return tc.VoidVal();
    }
}

inline Val Binary::Check(TypeCheck &tc, TypeExpr *) {
    if (op == T_ANDAND || op == T_OROR) {
        tc.CheckCond(left);
        auto snap = tc.SaveFlow();
        tc.NarrowCond(left, op == T_ANDAND);
        tc.CheckCond(right);
        tc.RestoreFlow(snap);
        Val v;
        v.type = tc.ast.booltype;
        return v;
    }
    auto lv = tc.Operand(left);
    auto rv = tc.Operand(right);
    auto lt = tc.LoadType(lv.type), rt = tc.LoadType(rv.type);
    Val v;
    switch (op) {
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ: {
            auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt, true);
            if (!ct)
                tc.Error(this, cat("ordering comparison requires numeric operands, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            tc.RetypeOperands(left, right, lv, rv, ct);
            v.type = tc.ast.booltype;
            return v;
        }
        case T_EQ: case T_NEQ: {
            // null tests: the other side must be an optional (an already
            // narrowed optional variable still counts).
            if (lv.isnull || rv.isnull) {
                auto othernode = lv.isnull ? right : left;
                auto &other = lv.isnull ? rt : lt;
                auto oid = Is<Ident>(othernode);
                auto narrowedopt = oid && oid->vdef && tc.IsOptional(oid->vdef->type);
                if (!tc.IsOptional(other) && !narrowedopt && !(lv.isnull && rv.isnull))
                    tc.Error(this, cat("only optionals compare against null, not ",
                                       tc.TypeStr(other)));
                v.type = tc.ast.booltype;
                return v;
            }
            if (auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt, true)) {
                tc.RetypeOperands(left, right, lv, rv, ct);
                v.type = tc.ast.booltype;
                return v;
            }
            if (lt->kind == TY_FN || lt->kind == TY_VOID)
                tc.Error(this, "these values cannot be compared");
            if (!tc.TypeEq(lt, rt))
                tc.Error(this, cat("== requires operands of the same type, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            v.type = tc.ast.booltype;
            return v;
        }
        case T_SHL: case T_SHR: {
            // Shifts: the left operand's type is the result type; the count
            // may be any integer type and is masked to the width (§6.2).
            if (!tc.IsIntT(lt) || !tc.IsIntT(rt))
                tc.Error(this, cat("shift requires integer operands, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            if (lv.ck == CK_INT)
                v.type = tc.ast.inttypes[lv.uns ? IS_U64 : IS_I64];
            else
                v.type = lt;
            left->exprtype = v.type;
            lv.type = v.type;
            tc.FoldInt(op, lv, rv, v, this);
            return v;
        }
        case T_BITAND: case T_BITOR: case T_XOR: {
            if (!tc.IsIntT(lt) || !tc.IsIntT(rt))
                tc.Error(this, cat("bitwise operator requires integer operands, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt);
            tc.RetypeOperands(left, right, lv, rv, ct);
            v.type = ct;
            tc.FoldInt(op, lv, rv, v, this);
            return v;
        }
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            if ((tc.IsIntT(lt) && tc.IsIntT(rt)) ||
                (lt->kind == TY_FLT && rt->kind == TY_FLT)) {
                auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt);
                tc.RetypeOperands(left, right, lv, rv, ct);
                v.type = ct;
                if (ct->kind == TY_INT) {
                    tc.FoldInt(op, lv, rv, v, this);
                } else if (lv.ck == CK_FLT && rv.ck == CK_FLT && op != T_MOD) {
                    // % (fmod) is left to the runtime.
                    auto a = lv.fval, b = rv.fval;
                    if (tc.IsF32(ct)) { a = (float)a; b = (float)b; }
                    v.ck = CK_FLT;
                    switch (op) {
                        case T_PLUS:  v.fval = a + b; break;
                        case T_MINUS: v.fval = a - b; break;
                        case T_MUL:   v.fval = a * b; break;
                        default:      v.fval = b != 0 ? a / b : 0; break;
                    }
                    if (tc.IsF32(ct)) v.fval = (double)(float)v.fval;
                }
                return v;
            }
            // Elementwise math on identical struct / fixed array types whose
            // scalar leaves are uniformly int or float (§6.1).
            if (tc.TypeEq(lt, rt) && tc.ElementwiseOK(lt)) {
                v.type = lt;
                return v;
            }
            tc.Error(this, cat("operator ", TName(op), " cannot be applied to ",
                               tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
        }
        default:
            assert(false);
            return tc.VoidVal();
    }
}

inline Val Dot::Check(TypeCheck &tc, TypeExpr *) {
    // EnumName.Variant: a payload-less variant constant (§3.5).
    if (auto id = Is<Ident>(obj)) {
        if (!tc.LookupVar(id->name) && !tc.LookupFnVal(id->name)) {
            auto eit = tc.ast.enummap.find(id->name);
            if (eit != tc.ast.enummap.end()) return tc.CheckVariantConst(this, eit->second);
        }
    }
    auto ov = tc.CheckV(obj, nullptr);
    obj->exprtype = ov.type;
    auto t = ov.type;
    if (t->kind == TY_REF) {
        if (t->ref->optional)
            tc.Error(this, "optional value must be narrowed (if/guard/assert) before use");
        t = t->ref->sub;  // Auto-deref; ov.root is already the pointee's owner.
    }
    // Builtin properties (.len/.cap) from the table.
    if (auto bd = LookupBuiltin(name); bd && (bd->flags & BF_PROPERTY)) {
        auto got = 0;
        if (t->kind == TY_ARRAY) {
            switch (t->arr->akind) {
                case A_FIXED:      got = BR_FIXED; break;
                case A_VAR:        got = BR_VAR; break;
                case A_LIMITED:    got = BR_LIMITED; break;
                case A_GROW:       got = BR_GROW; break;
                case A_GROWSHRINK: got = BR_GROWSHRINK; break;
            }
        } else if (t->kind == TY_SLICE) {
            got = BR_SLICE;
        }
        if (got & bd->recv) {
            member = bd->kind;
            Val v;
            assert(bd->rets[0] == 'i');
            v.type = tc.ast.inttypes[IS_I64];
            v.nonneg = true;   // A length or capacity is in [0, 2^48] (§10.4).
            return v;
        }
        if (t->kind == TY_ARRAY || t->kind == TY_SLICE)
            tc.Error(this, cat(".", name, " is not available on ", tc.TypeStr(t)));
    }
    TypeCheck::LVal lv;
    lv.type = t;
    lv.SetProv(ov);
    tc.ResolveMemberLValue(lv, this);
    return tc.ContainerRead(lv);
}

inline Val Call::Check(TypeCheck &tc, TypeExpr *) { return tc.CheckCall(this); }

inline Val Index::Check(TypeCheck &tc, TypeExpr *) {
    return tc.ContainerRead(tc.CheckLValue(this));
}

inline Val SliceExpr::Check(TypeCheck &tc, TypeExpr *) {
    auto lv = tc.LValueBase(obj);
    tc.DerefLValue(lv, obj);
    tc.SliceProvenance(lv, obj);
    TypeExpr *elem;
    if (lv.type->kind == TY_SLICE) {
        elem = lv.type->sub;
        if (tc.ClassOf(elem) != SC_FIXED && (lo || hi))
            tc.Error(this, "slices of variable-size elements can only be re-sliced "
                           "whole ([..])");
    } else if (lv.type->kind == TY_ARRAY) {
        elem = lv.type->arr->sub;
        if (tc.ClassOf(elem) != SC_FIXED && (lo || hi))
            tc.Error(this, "arrays of variable-size elements can only be sliced "
                           "whole ([..])");
    } else {
        tc.Error(this, cat("cannot slice a value of type ", tc.TypeStr(lv.type)));
    }
    if (lo) tc.CheckIntAny(lo);
    if (hi) tc.CheckIntAny(hi);
    Val v;
    v.type = tc.SliceOf(elem, line);
    v.SetProv(lv);
    v.reusable = false;   // A view of a pool is not the pool.
    return v;
}

inline Val AsCast::Check(TypeCheck &tc, TypeExpr *) {
    auto cv = tc.Operand(child);
    auto st = tc.LoadType(cv.type);
    if (!tc.IsIntT(st) && st->kind != TY_FLT)
        tc.Error(this, cat("as requires a numeric source, got ", tc.TypeStr(st)));
    auto tt = tc.Subst(type);
    Val v;
    if (tt->kind == TY_INT) {
        if (tt->intstorage == IS_VARINT)
            tc.Error(this, "cannot cast to varint (varints are written at construction only)");
        v.type = tt;
        return v;
    }
    if (tt->kind == TY_FLT) {
        v.type = tt;
        return v;
    }
    tc.Error(this, cat("as can only convert between numeric types, not to ", tc.TypeStr(tt)));
}

inline Val RangeExpr::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "range expressions are only valid in for headers");
}

inline Val Block::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckBlockVal(this, expected, true, TypeCheck::SK_PLAIN);
}

inline Val IfExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckIf(this, expected, true);
}

inline Val MatchExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckMatch(this, expected, true);
}

inline Val EarlyBlock::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckEarlyBlock(this, expected, true);
}

inline Val While::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckWhile(this);
    return tc.VoidVal();
}

inline Val LoopExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckLoop(this, expected, true);
}

inline Val ForLoop::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckFor(this);
    return tc.VoidVal();
}

inline Val Guard::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckGuard(this);
    return tc.VoidVal();
}

inline Val Return::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckReturn(this);
    return tc.VoidVal();
}

inline Val Break::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckBreak(this);
    return tc.VoidVal();
}

inline Val Continue::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckContinue(this);
    return tc.VoidVal();
}

inline Val FunVal::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "function values can only be passed to calls (§7.6)");
}

inline Val VarDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val Assign::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "an assignment is a statement, not a value");
}

inline Val IncDec::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "an increment is a statement, not a value");
}

inline Val FnDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val StructDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val EnumDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val AliasDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val InlineBlock::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "internal: optimizer node reached the typechecker");
}

}  // namespace goose
