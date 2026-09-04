// Goose compiler — codegen's construction (definitions of CodeGen members,
// codegen.h): values written front-to-back at a data stack's top (§4.2,
// §4.3), relative-reference stores (§3.9), and the literals.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Construction (§4.2/§4.3): writes a value front-to-back at a stack's
// top, bumping it. All branches of value-producing control constructs
// construct to the same destination; calls pass the stack down.

inline void CodeGen::EmitValStore(const string &stk, TypeExpr *t, const string &x) {
    auto sz = FixedSize(t);
    if (sz == 0) { L("(void)(", x, ");"); return; }
    L("*(", CT(t), " *)", Top(stk), " = ", x, ";");
    Bump(stk, cat(sz));
}

inline void CodeGen::EmitLenStore(const string &stk, IntStorage ls, const string &n) {
    if (ls == IS_VARINT) {
        Bump(stk, cat("gs_uleb_write(", Top(stk), ", (uint64_t)(", n, "))"));
    } else {
        L("*(", IntCT(ls), " *)", Top(stk), " = (", IntCT(ls), ")(", n, ");");
        Bump(stk, cat(IntSize(ls)));
    }
}

// The range check a store of `off` into a relative slot of width `w`
// needs (§3.9). The self-relative form is signed and bounded by the span
// of the root array; the `in pool` form is unsigned and bounded by the
// pool, so its width covers 2^N - 1 bytes of one. Either way a root on a
// data stack is inside one reservation of GS_STACK_RESERVE bytes (guard
// region behind it, so nothing past it is ever written), and a fixed root
// is at most `relrootmax`. The check is emitted only where one of those
// exceeds the width; GS_STACK_RESERVE is a C macro, so that half of the
// test is left to the C preprocessor.
//
// `inroot` says the slot address is its real one inside the root array,
// which holds everywhere except the literal-temp path in StructLit::CgX.
inline void CodeGen::EmitRelRangeCheck(TypeExpr *rt, const string &off, Line ln, bool inroot) {
    auto w = (IntStorage)rt->ref->lenstorage;
    auto bits = IntSize(w) * 8;
    if (bits >= 64) return;
    if (rt->ref->pool) {
        L("#if GS_STACK_RESERVE >= (1ull << ", bits, ")");
        L("if ((uint64_t)", off, " > ", (1ull << bits) - 1, "ull) gs_abort(GS_E_RELOFF, ",
          LocArgs(ln), ");");
        L("#endif");
        return;
    }
    auto guarded = inroot && relrootmax <= (1ll << (bits - 1));
    if (guarded) L("#if GS_STACK_RESERVE > (1ull << ", bits - 1, ")");
    L("if (", off, " < -(1LL << ", bits - 1, ") || ", off, " >= (1LL << ",
      bits - 1, ")) gs_abort(GS_E_RELOFF, ",
      LocArgs(ln), ");");
    if (guarded) L("#endif");
}

// Writes a plain-reference value into the relative-reference slot at
// `fa` (§3.9), range-checked. Fixed widths only; the varint form exists
// only in the stack-top variant below.
inline void CodeGen::EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln,
                                    bool inroot) {
    auto w = (IntStorage)rt->ref->lenstorage;
    assert(w != IS_VARINT);
    assert(!IsResz(rt->ref->sub));
    auto addr = cat("(uint8_t *)(", rv, ")");
    auto org = RelOrigin(rt, fa);
    auto off = T();
    if (rt->ref->optional)
        L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - (", org, ")) : 0;");
    else
        L("int64_t ", off, " = (int64_t)(", addr, " - (", org, "));");
    EmitRelRangeCheck(rt, off, ln, inroot);
    L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", off, ";");
}

inline void CodeGen::EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln) {
    auto w = (IntStorage)rt->ref->lenstorage;
    auto fa = T();
    L("uint8_t *", fa, " = ", Top(stk), ";");
    if (w == IS_VARINT) {
        assert(!IsResz(rt->ref->sub));
        auto addr = cat("(uint8_t *)(", rv, ")");
        auto org = RelOrigin(rt, fa);
        auto off = T();
        if (rt->ref->optional)
            L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - (", org, ")) : 0;");
        else
            L("int64_t ", off, " = (int64_t)(", addr, " - (", org, "));");
        Bump(stk, rt->ref->pool ? cat("gs_uleb_write(", fa, ", (uint64_t)", off, ")")
                                : cat("gs_zig_write(", fa, ", ", off, ")"));
    } else {
        EmitRelStoreAt(fa, rt, rv, ln, true);
        Bump(stk, cat(IntSize(w)));
    }
}

// `self` in a literal field (§3.9): the slot at `fa` gets the offset back
// to the start of the value under construction. For a self-relative field
// that is minus its own byte offset in the value, the same wherever the
// value lives, so unlike other relative references it survives being
// built in a temp and copied into place. For an `in pool` field it is the
// value's own position in the pool, which the checker admits only where
// the literal is being built inside that pool.
inline void CodeGen::EmitRelSelfAt(const string &fa, TypeExpr *rt, int64_t fieldoff, Line ln,
                                   bool inroot) {
    auto w = (IntStorage)rt->ref->lenstorage;
    assert(w != IS_VARINT);
    auto bits = IntSize(w) * 8;
    if (rt->ref->pool) {
        if (!inroot)
            Fail(ln, cat("self in a ", IntStorageName(w), " in ", rt->ref->pool->name,
                         " field needs the value's final address, which this literal is "
                         "not being built at"));
        auto off = T();
        L("int64_t ", off, " = (int64_t)((", fa, ") - ", fieldoff, " - (",
          RelOrigin(rt, fa), "));");
        EmitRelRangeCheck(rt, off, ln, true);
        L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", off, ";");
        return;
    }
    if (bits < 64 && fieldoff > (1LL << (bits - 1)))
        Fail(ln, cat("self at byte offset ", fieldoff, " does not fit a ",
                     IntStorageName(w), "-width relative reference"));
    L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", -fieldoff, ";");
}

inline void CodeGen::EmitRelSelfStore(const string &stk, TypeExpr *rt, int64_t fieldoff, Line ln) {
    EmitRelSelfAt(Top(stk), rt, fieldoff, ln);
    Bump(stk, cat(IntSize((IntStorage)rt->ref->lenstorage)));
}

// Does a fixed type contain relative references at any depth? Literals of
// such types must construct in their final location, not via a temp.
inline bool CodeGen::HasRelRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0;
        case TY_STRUCT: {
            auto si = SI(t);
            for (size_t i = 0; i < si->st->fields.size(); i++)
                if (!si->st->fields[i].ispad && HasRelRef(si->ftypes[i])) return true;
            return false;
        }
        case TY_ENUM: {
            if (t->enu->varmode) return false;
            auto ei = EIOf(t);
            for (size_t vi = 0; vi < ei->en->variants.size(); vi++)
                if (HasRelRef(VariantType(t, (int)vi))) return true;
            return false;
        }
        case TY_VARIANT: {
            auto ei = EIVar(t);
            auto vi = VarIdx(ei->en, t->var->variant);
            for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++)
                if (!ei->en->variants[vi].fields[i].ispad &&
                    HasRelRef(ei->vftypes[vi][i])) return true;
            return false;
        }
        case TY_ARRAY:
            return (t->arr->akind == A_FIXED || t->arr->akind == A_LIMITED) &&
                   HasRelRef(t->arr->sub);
        default: return false;
    }
}

inline void CodeGen::ComputeRelRootMax() {
    for (auto vd : ast.vardefs) {
        if (!vd->type || !IsFix(vd->type) || !HasRelRef(vd->type)) continue;
        relrootmax = std::max(relrootmax, FixedSize(vd->type));
    }
}

// Copies `n` elements of type `elem` from `src` (typed or byte pointer)
// to the stack top.
inline void CodeGen::EmitCopyElems(const string &stk, TypeExpr *elem, const string &src,
                                   const string &n) {
    if (IsFix(elem)) {
        auto esz = FixedSize(elem);
        L("memcpy(", Top(stk), ", ", src, ", (size_t)((", n, ") * ", esz, "));");
        Bump(stk, cat("(", n, ") * ", esz));
    } else {
        auto p = T(), iv = T();
        L("const uint8_t *", p, " = (const uint8_t *)(", src, ");");
        L("for (int64_t ", iv, " = 0; ", iv, " < (", n, "); ", iv, "++) {");
        ind++;
        auto sz = T();
        L("int64_t ", sz, " = ", SizeX(elem, p), ";");
        L("memcpy(", Top(stk), ", ", p, ", (size_t)", sz, ");");
        Bump(stk, sz);
        L(p, " += ", sz, ";");
        ind--;
        L("}");
    }
}

inline CodeGen::SrcElems CodeGen::GenSrcElems(Node *n) {
    SrcElems r;
    if (auto s = Is<StrLit>(n)) {
        r.elems = StrRaw(s->val);
        r.n = cat(s->val.size());
        return r;
    }
    auto t = n->exprtype;
    if (t->kind == TY_SLICE) {
        auto x = GenPure(n);
        r.elems = cat(x, ".data");
        r.n = cat(x, ".len");
        return r;
    }
    assert(t->kind == TY_ARRAY);
    auto lv = GenLoc(n);
    if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
    if (lv.t->kind == TY_SLICE) {
        r.elems = cat(lv.s, ".data");
        r.n = cat(lv.s, ".len");
        return r;
    }
    auto v = ArrayView(lv, n->line);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    r.elems = v.elems;
    r.n = nn;
    return r;
}

// Constructs n's value at stk's top. For resizable-class values, lenlv
// names the receiving header's length lvalue: elements are written and
// the count assigned there (§7.3's metadata-outside-the-data form).
inline void CodeGen::GenConstruct(Node *n, const string &stk, TypeExpr *want, const string &lenlv) {
    // An inlined body's named result reaching the destination it was
    // bound to (OpenIbNrvo): the elements are in place, so all that is
    // left is the count or the reserved prefix. Any other use of that
    // local constructs a copy elsewhere and takes the normal path.
    if (auto id = Is<Ident>(n); id && id->vdef) {
        auto it = nrvo.find(id->vdef);
        if (it != nrvo.end() && it->second.inlined && it->second.stk == stk &&
            it->second.lenlv == lenlv) {
            EmitNrvoFinish(it->second);
            return;
        }
    }
    auto et = n->exprtype;
    // A variable array landing in a slot of another length storage takes
    // the slot's layout: the prefix written here is the destination's,
    // whatever the expression's own type says.
    if (want && et && want->kind == TY_ARRAY && want->arr->akind == A_VAR &&
        et->kind == TY_ARRAY && et->arr->akind == A_VAR &&
        LenStore(want->arr) != LenStore(et->arr) && TEq(want->arr->sub, et->arr->sub))
        et = want;
    // A resizable value landing in a variable-array slot (an element, a
    // field) takes the slot's layout: its elements behind a length prefix.
    if (want && et && lenlv.empty() && want->kind == TY_ARRAY && want->arr->akind == A_VAR &&
        et->kind == TY_ARRAY && IsResz(et) && TEq(want->arr->sub, et->arr->sub))
        et = want;
    if (want && NeedsDeref(n->exprtype, want)) {
        // A spliced reference in a decayed slot: copy the pointee.
        auto sub = n->exprtype->ref->sub;
        if (IsFix(sub)) {
            EmitValStore(stk, want, GenXD(n, want));
        } else if (IsResz(sub)) {
            EmitRzCopy(FatRefLoc(GenX(n), sub), sub, stk, lenlv, n->line);
        } else {
            auto x = GenX(n);
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(sub, x), ";");
            L("memcpy(", Top(stk), ", ", x, ", (size_t)", sz, ");");
            Bump(stk, sz);
        }
        return;
    }
    if (IsCtl(n)) { GenAny(n, Dst { DK_STACK, stk, want, lenlv }); return; }
    if (auto c = Is<Call>(n); c && c->builtin == B_COPY) {
        // copy(x): the stored value's bytes, as an implicit copy once was.
        GenConstruct(c->args[0], stk, want, lenlv);
        return;
    }
    if (auto c = Is<Call>(n)) {
        auto rets = EmitCall(c, Dst { DK_STACK, stk, want, lenlv });
        if (rets.empty() || IsVoidT(et)) return;
        // A resizable result with no receiving header was built behind a
        // temporary one: copy it into the slot as the slot's array kind.
        auto rt0 = c->rettypes.empty() ? nullptr : c->rettypes[0];
        if (rt0 && IsResz(rt0) && rt0->kind == TY_ARRAY && lenlv.empty() &&
            et->kind == TY_ARRAY && !rets[0].empty()) {
            Loc lv;
            lv.t = rt0;
            lv.s = cat(rets[0], ".base");
            lv.lenlv = cat(rets[0], ".len");
            GenArrayFromLoc(lv, et, stk, n->line, lenlv);
            return;
        }
        // A reference-returning call decayed to a value here: the callee
        // did not construct at the destination; copy the pointee.
        auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
        if (rt && rt->kind == TY_REF && et->kind != TY_REF && IsBytesT(rt->ref->sub)) {
            if (IsResz(rt->ref->sub)) {
                EmitRzCopy(FatRefLoc(rets[0], rt->ref->sub), et, stk, lenlv, n->line);
            } else {
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(et, rets[0]), ";");
                L("memcpy(", Top(stk), ", ", rets[0], ", (size_t)", sz, ");");
                Bump(stk, sz);
            }
            return;
        }
        // A fixed-size result (a reference-returning call's pointee
        // included) is a C value: it lands at the top like any other.
        if (!IsBytesT(et)) EmitValStore(stk, et, CallVal0(c, rets[0]));
        return;
    }
    if (!IsBytesT(et)) {
        // A reference landing in a relative-reference slot -- an element
        // of a `(T&<w>)[>..]`, say -- stores the offset from that slot,
        // not the pointer (§3.9). Reference values always reach here as
        // plain pointers, relative ones having been decoded on the read.
        if (want && want->kind == TY_REF && want->ref->lenstorage >= 0 &&
            et->kind == TY_REF && !Is<NullLit>(n)) {
            EmitRelStore(stk, want, GenX(n), n->line);
            return;
        }
        // Fixed values normally construct as C values; ones containing
        // relative references must be built at their final address.
        if ((Is<StructLit>(n) || Is<ArrayLit>(n)) && HasRelRef(et)) {
            FixedLitAtStk(n, stk);
            return;
        }
        EmitValStore(stk, et, GenX(n));
        return;
    }
    if (Is<NullLit>(n)) {
        // The zero value of an optional field.
        assert(et->kind == TY_REF);
        if (et->ref->lenstorage == IS_VARINT) {
            L("*", Top(stk), " = 0;");
            Bump(stk, "1");
        } else {
            L("memset(", Top(stk), ", 0, ", FixedSize(et), ");");
            Bump(stk, cat(FixedSize(et)));
        }
        return;
    }
    if (auto s2 = Is<StrLit>(n); s2 && (IsResz(et) || !lenlv.empty())) {
        // A string literal building a resizable string: raw elements,
        // count into the receiving header.
        assert(!lenlv.empty());
        L(lenlv, " = ", s2->val.size(), ";");
        if (!s2->val.empty()) {
            L("memcpy(", Top(stk), ", ", StrRaw(s2->val), ", ", s2->val.size(), ");");
            Bump(stk, cat(s2->val.size()));
        }
        return;
    }
    if (auto s = Is<StrLit>(n)) {
        assert(et->kind == TY_ARRAY && et->arr->akind == A_VAR && lenlv.empty());
        EmitLenStore(stk, LenStore(et->arr), cat(s->val.size()));
        if (!s->val.empty()) {
            L("memcpy(", Top(stk), ", ", StrRaw(s->val), ", ", s->val.size(), ");");
            Bump(stk, cat(s->val.size()));
        }
        return;
    }
    if (auto al = Is<ArrayLit>(n)) { GenArrayLit(al, stk, lenlv); return; }
    if (auto sl = Is<StructLit>(n)) { GenStructLit(sl, stk, lenlv); return; }
    if (auto d = Is<Dot>(n); d && d->variantconst) {
        // A payload-less variant constant in variable mode: just the tag.
        assert(et->kind == TY_ENUM && et->enu->varmode);
        auto ei = EIOf(et);
        EmitValStoreTag(stk, TagStore(ei->en),
                        TagConst(ei, VarIdx(ei->en, d->variantconst)));
        if (!lenlv.empty()) L(lenlv, " = 0;");
        return;
    }
    if (auto se = Is<SliceExpr>(n); se && et->kind == TY_ARRAY) {
        // A slice expression constructing an array copies the range.
        Loc slv;
        slv.val = true;
        slv.s = GenSlice(se);
        slv.t = MakeSliceT(et->arr->sub, n->line);
        GenArrayFromLoc(slv, et, stk, n->line, lenlv);
        return;
    }
    // Remaining nodes denote existing values: resolve the location, then
    // either copy identical layouts wholesale or adapt (slice/array kind
    // changes, ADT mode changes) element/field-wise.
    auto lv = GenLoc(n);
    if (lv.t->kind == TY_REF && et->kind != TY_REF) DerefLoc(lv, n->line);
    if (IsResz(et)) {
        if (lv.t->kind == TY_SLICE && et->kind == TY_ARRAY) {
            GenArrayFromLoc(lv, et, stk, n->line, lenlv);
            return;
        }
        if (lv.t->kind == TY_ARRAY || TEq(lv.t, et)) {
            if (TEq(lv.t, et) && et->kind != TY_ARRAY) {
                EmitRzCopy(lv, et, stk, lenlv, n->line);
            } else {
                GenArrayFromLoc(lv, et, stk, n->line, lenlv);
            }
            return;
        }
        Fail(n->line, cat("unsupported resizable construction from ", Mangle(lv.t)));
    }
    if (!lenlv.empty() && et->kind == TY_ARRAY) {
        // Element-run destination: elements only, count into lenlv.
        GenArrayFromLoc(lv, et, stk, n->line, lenlv);
        return;
    }
    if (TEq(lv.t, et)) {
        assert(!lv.val);
        auto sz = T();
        L("int64_t ", sz, " = ", SizeX(et, lv.s), ";");
        L("memcpy(", Top(stk), ", ", lv.s, ", (size_t)", sz, ");");
        Bump(stk, sz);
        return;
    }
    if (et->kind == TY_ARRAY) { GenArrayFromLoc(lv, et, stk, n->line, lenlv); return; }
    if (et->kind == TY_ENUM && et->enu->varmode) {
        GenVarEnumFromLoc(lv, et, stk);
        return;
    }
    Fail(n->line, cat("unsupported construction adaptation to ", Mangle(et)));
}

inline void CodeGen::GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln,
                                     const string &lenlv) {
    auto v = ArrayView(lv, ln);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    switch (et->arr->akind) {
        case A_VAR:
            if (!lenlv.empty()) L(lenlv, " = ", nn, ";");
            else EmitLenStore(stk, LenStore(et->arr), nn);
            break;
        case A_LIMITED:   // Runtime capacity: chosen as the initial length (v1).
            L("*(uint32_t *)", Top(stk), " = (uint32_t)", nn, ";");
            L("*(uint32_t *)(", Top(stk), " + 4) = (uint32_t)", nn, ";");
            Bump(stk, "8");
            break;
        default:
            assert(!lenlv.empty());
            L(lenlv, " = ", nn, ";");
            break;
    }
    EmitCopyElems(stk, et->arr->sub, v.elems, nn);
}

// Copies an existing resizable value (source location) to the stack top:
// the static prefix of any resizable-tailed structs, then the tail's
// elements; the count goes to the receiving header.
inline void CodeGen::EmitRzCopy(Loc lv, TypeExpr *et, const string &stk, const string &lenlv,
                                Line ln) {
    assert(!lenlv.empty());
    if (IsFrameObj(et)) {
        // The frame object copies as a C value; the innermost tail's
        // elements follow it onto the stack behind a fresh base.
        assert(lv.val);
        auto src = T();
        L(CT(et), " ", src, " = ", lv.s, ";");
        L(lenlv, " = ", src, ";");
        auto dh = FoTailHdr(et, lenlv), sh = FoTailHdr(et, src);
        L(dh, ".base = ", Top(stk), ";");
        EmitCopyElems(stk, FoTailArr(et)->arr->sub, cat(sh, ".base"), cat(sh, ".len"));
        return;
    }
    int64_t prefix = 0;
    TypeExpr *elem = nullptr;
    if (!RzShape(et, prefix, elem))
        Fail(ln, "copying this resizable value is unsupported (variable-size prefix)");
    auto len = T();
    L("int64_t ", len, " = ", lv.lenlv, ";");
    if (prefix) {
        L("memcpy(", Top(stk), ", ", lv.s, ", ", prefix, ");");
        Bump(stk, cat(prefix));
    }
    EmitCopyElems(stk, elem, cat("(", lv.s, ") + ", prefix), len);
    L(lenlv, " = ", len, ";");
}

// The static byte size before a resizable value's tail elements, plus the
// tail's element type. False when the prefix is not statically sized.
inline bool CodeGen::RzShape(TypeExpr *t, int64_t &prefix, TypeExpr *&elem) {
    if (t->kind == TY_ARRAY) {
        elem = t->arr->sub;
        return true;
    }
    if (t->kind == TY_STRUCT) {
        auto si = SI(t);
        auto last = -1;
        for (auto i = 0; i < (int)si->st->fields.size(); i++)
            if (!si->st->fields[i].ispad) last = i;
        assert(last >= 0);
        vector<Field> pre(si->st->fields.begin(), si->st->fields.begin() + last);
        vector<TypeExpr *> pret(si->ftypes.begin(), si->ftypes.begin() + last);
        for (auto ft : pret) if (ft && !IsFix(ft)) return false;
        auto lo = LayoutFields(pre, pret);
        int64_t sub = 0;
        if (!RzShape(si->ftypes[last], sub, elem)) return false;
        prefix += lo.size + sub;
        return true;
    }
    return false;   // Resizable variable-mode ADTs: unsupported copies.
}

inline void CodeGen::GenVarEnumFromLoc(Loc lv, TypeExpr *et, const string &stk) {
    auto ei = EIOf(et);
    auto ts = TagStore(ei->en);
    if (lv.t->kind == TY_VARIANT) {
        auto vi = VarIdx(ei->en, lv.t->var->variant);
        EmitValStoreTag(stk, ts, TagConst(ei, vi));
        if (!lv.val) {
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(lv.t, lv.s), ";");
            L("memcpy(", Top(stk), ", ", lv.s, ", (size_t)", sz, ");");
            Bump(stk, sz);
        } else {
            EmitValStore(stk, lv.t, lv.s);
        }
        return;
    }
    assert(lv.t->kind == TY_ENUM && !lv.t->enu->varmode);
    auto sv = T();
    L(CT(lv.t), " ", sv, " = ", lv.s, ";");
    EmitValStoreTag(stk, ts, cat("(int64_t)", sv, ".tag"));
    L("switch (", sv, ".tag) {");
    for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
        auto psz = VariantLayout(ei, (int)vi).size;
        L("case ", TagConst(ei, (int)vi), ":");
        ind++;
        if (psz) {
            L("memcpy(", Top(stk), ", &", sv, ".u.v_",
              Sanitize(ei->en->variants[vi].name), ", ", psz, ");");
            Bump(stk, cat(psz));
        }
        L("break;");
        ind--;
    }
    L("}");
}

// A fixed struct/array literal built directly at the stack top, so its
// relative references measure offsets from their real addresses. Layout
// gaps (pads, ADT payload padding) are zero-filled to keep sizes exact.
inline void CodeGen::FixedLitAtStk(Node *n, const string &stk) {
    auto et = n->exprtype;
    auto Gap = [&](int64_t bytes) {
        if (bytes <= 0) return;
        L("memset(", Top(stk), ", 0, ", bytes, ");");
        Bump(stk, cat(bytes));
    };
    // `fieldoff` is the field's byte offset within the value, which is
    // what a `self` initializer stores the negation of.
    auto EmitF = [&](Node *init, TypeExpr *ft, int64_t fieldoff = 0) {
        if (!init) {   // Omitted optional: null.
            Gap(FixedSize(ft));
            return;
        }
        if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
            if (Is<SelfRef>(init)) EmitRelSelfStore(stk, ft, fieldoff, n->line);
            else EmitRelStore(stk, ft, GenX(init), n->line);
            return;
        }
        if ((Is<StructLit>(init) || Is<ArrayLit>(init)) && HasRelRef(ft)) {
            FixedLitAtStk(init, stk);
            return;
        }
        EmitValStore(stk, ft, GenX(init));
    };
    if (auto al = Is<ArrayLit>(n)) {
        auto elem = et->arr->sub;
        if (et->arr->akind == A_LIMITED) {
            EmitLenStore(stk, LenStore(et->arr),
                         cat(al->fillval ? ((IntLit *)al->fillcount)->val
                                         : (int64_t)al->elems.size()));
        }
        if (al->fillval) {
            auto fc = Is<IntLit>(al->fillcount);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", fc->val, "; ", iv, "++) {");
            ind++;
            EmitF(al->fillval, elem);
            ind--;
            L("}");
        } else {
            for (auto e : al->elems) EmitF(e, elem);
        }
        if (et->arr->akind == A_LIMITED) {
            auto filled = al->fillval ? ((IntLit *)al->fillcount)->val
                                      : (int64_t)al->elems.size();
            Gap((ArrSize(et->arr) - filled) * FixedSize(elem));
        }
        return;
    }
    auto sl = Is<StructLit>(n);
    assert(sl);
    // `base` is where the fields run starts within the value: past the tag
    // for a literal constructing its enum, zero otherwise.
    auto emitfields = [&](const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                          const vector<Node *> &defaults, const Layout &lo, int64_t total,
                          int64_t base) {
        int64_t cur = 0;
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            Gap(lo.offs[i] - cur);
            cur = lo.offs[i];
            Node *init = nullptr;
            for (size_t k = 0; k < sl->fieldindices.size(); k++)
                if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
            if (!init && i < defaults.size() && defaults[i]) init = defaults[i];
            EmitF(init, ftypes[i], base + lo.offs[i]);
            cur += FixedSize(ftypes[i]);
        }
        Gap(total - cur);
    };
    if (et->kind == TY_STRUCT) {
        auto si = SI(et);
        auto &lo = StructLayout(si);
        emitfields(si->st->fields, si->ftypes, si->defaults, lo, lo.size, 0);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = VarIdx(ei->en, et->var->variant);
        auto &lo = VariantLayout(ei, vi);
        emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi], lo,
                   lo.size, 0);
        return;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
    auto ei = EIOf(et);
    auto vi = VarIdx(ei->en, sl->variant);
    EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
    auto &lo = VariantLayout(ei, vi);
    emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi], lo,
               lo.size, TagSize(ei->en));
    Gap(FixedSize(et) - TagSize(ei->en) - lo.size);
}

// The same construct-in-place rule as FixedLitAtStk, for a literal whose
// destination is a C lvalue rather than a stack top: the relative
// references measure from where the value stays, not from a temporary
// that is then copied over. `dst` is named once, through a pointer, so an
// indexed destination evaluates (and bounds-checks) once.
inline void CodeGen::FixedLitAt(Node *n, const string &dst) {
    auto p = T();
    L(CT(n->exprtype), " *", p, " = &", dst, ";");
    FixedLitAtLv(n, cat("(*", p, ")"), true);
}

// As above, for a destination cheap enough to name per field.
inline void CodeGen::FixedLitAtLv(Node *n, const string &base, bool inroot) {
    if (auto al = Is<ArrayLit>(n)) { FixedArrayLitAt(al, base, inroot); return; }
    auto sl = Is<StructLit>(n);
    assert(sl);
    StructLitAt(sl, base, inroot);
}

inline void CodeGen::FixedArrayLitAt(ArrayLit *al, const string &base, bool inroot) {
    auto et = al->exprtype;
    assert(et->kind == TY_ARRAY);
    auto elem = et->arr->sub;
    auto rel = elem->kind == TY_REF && elem->ref->lenstorage >= 0;
    auto emitelem = [&](Node *e, const string &path) {
        if (rel) EmitRelStoreAt(cat("(uint8_t *)&", path), elem, GenX(e), al->line, inroot);
        else GenAny(e, Dst { DK_LVALUE, path });
    };
    if (et->arr->akind == A_LIMITED) {
        auto count = al->fillval ? ((IntLit *)al->fillcount)->val
                                 : (int64_t)al->elems.size();
        L(base, ".len = ", count, ";");
    }
    if (al->fillval) {
        auto fc = Is<IntLit>(al->fillcount);
        assert(fc);
        // Elements holding relative references are built one by one, each
        // against its own address; any other fill value is evaluated once.
        auto perelem = HasRelRef(elem);
        auto fv = perelem ? string() : GenPure(al->fillval);
        auto iv = T();
        L("for (int64_t ", iv, " = 0; ", iv, " < ", fc->val, "; ", iv, "++) {");
        ind++;
        auto path = cat(base, ".e[", iv, "]");
        if (perelem) emitelem(al->fillval, path);
        else L(path, " = ", fv, ";");
        ind--;
        L("}");
        return;
    }
    for (size_t i = 0; i < al->elems.size(); i++)
        emitelem(al->elems[i], cat(base, ".e[", i, "]"));
}

// `inroot` says `base` is the value's real address inside its root (see
// EmitRelStoreAt); a temporary that is copied afterwards is not.
inline void CodeGen::StructLitAt(StructLit *sl, const string &base, bool inroot) {
    auto et = sl->exprtype;
    // `baseoff` is where the fields run starts within the whole value, so a
    // `self` field can store its (constant) offset back to it.
    auto fieldset = [&](const string &b, const vector<Field> &fields,
                        const vector<TypeExpr *> &ftypes, const vector<Node *> &defaults,
                        const Layout &lo, int64_t baseoff) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            Node *init = nullptr;
            for (size_t k = 0; k < sl->fieldindices.size(); k++)
                if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
            if (!init && i < defaults.size() && defaults[i]) init = defaults[i];
            auto ft = ftypes[i];
            auto path = cat(b, ".", Sanitize(fields[i].name));
            if (!init) {   // Omitted optional: null.
                assert(ft->kind == TY_REF);
                L("memset(&", path, ", 0, sizeof(", path, "));");
                continue;
            }
            if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
                if (Is<SelfRef>(init))
                    EmitRelSelfAt(cat("(uint8_t *)&", path), ft, baseoff + lo.offs[i],
                                  sl->line, inroot);
                else
                    EmitRelStoreAt(cat("(uint8_t *)&", path), ft, GenX(init), sl->line, inroot);
                continue;
            }
            GenAny(init, Dst { DK_LVALUE, path });
        }
    };
    if (et->kind == TY_STRUCT) {
        auto si = SI(et);
        fieldset(base, si->st->fields, si->ftypes, si->defaults, StructLayout(si), 0);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = VarIdx(ei->en, et->var->variant);
        if (ei->en->variants[vi].fields.empty())
            L("memset(&", base, ", 0, sizeof(", base, "));");
        fieldset(base, ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi],
                 VariantLayout(ei, vi), 0);
        return;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
    auto ei = EIOf(et);
    auto vi = VarIdx(ei->en, sl->variant);
    L(base, ".tag = ", TagConst(ei, vi), ";");
    if (!ei->en->variants[vi].fields.empty())
        fieldset(cat(base, ".u.v_", Sanitize(ei->en->variants[vi].name)),
                 ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi],
                 VariantLayout(ei, vi), TagSize(ei->en));
}

inline void CodeGen::EmitValStoreTag(const string &stk, IntStorage ts, const string &x) {
    L("*(", IntCT(ts), " *)", Top(stk), " = (", IntCT(ts), ")(", x, ");");
    Bump(stk, cat(IntSize(ts)));
}

inline void CodeGen::GenArrayLit(ArrayLit *al, const string &stk, const string &lenlv) {
    auto et = al->exprtype;
    assert(et->kind == TY_ARRAY);
    auto elem = et->arr->sub;
    if (al->capexpr) {
        // [..cap]: capacity + zero length; the slots stay uninitialized
        // and the runtime commits the skipped range on first touch (C.4).
        assert(et->arr->akind == A_LIMITED);
        auto cv = GenX(al->capexpr);
        auto t = T();
        L("int64_t ", t, " = ", cv, ";");
        L("if (", t, " < 0 || ", t, " > (int64_t)UINT32_MAX) "
          "gs_abort(GS_E_CAPRANGE, ", LocArgs(al->line), ");");
        L("*(uint32_t *)", Top(stk), " = (uint32_t)", t, ";");
        L("*(uint32_t *)(", Top(stk), " + 4) = 0;");
        Bump(stk, cat("8 + ", t, " * ", FixedSize(elem)));
        return;
    }
    int64_t count = al->fillval ? -1 : (int64_t)al->elems.size();
    string cn;
    if (al->fillval) {
        // The fill count is a constant expression (§4.2).
        auto fc = Is<IntLit>(al->fillcount);
        assert(fc);
        count = fc->val;
    }
    cn = cat(count);
    switch (et->arr->akind) {
        case A_VAR:
            if (!lenlv.empty()) L(lenlv, " = ", cn, ";");
            else EmitLenStore(stk, LenStore(et->arr), cn);
            break;
        case A_LIMITED:
            L("*(uint32_t *)", Top(stk), " = ", count, ";");
            L("*(uint32_t *)(", Top(stk), " + 4) = ", count, ";");
            Bump(stk, "8");
            break;
        default:
            assert(!lenlv.empty());
            L(lenlv, " = ", cn, ";");
            break;
    }
    if (al->fillval) {
        if (IsBytesT(elem)) {
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            GenConstruct(al->fillval, stk);
            ind--;
            L("}");
        } else {
            auto fv = GenPure(al->fillval);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            EmitValStore(stk, elem, fv);
            ind--;
            L("}");
        }
        return;
    }
    for (auto e : al->elems) GenConstruct(e, stk);
}

// Does this literal name `self` directly? Such a literal has no static
// layout here, so the address it constructs at must be captured before
// any of its bytes are written.
inline bool CodeGen::HasSelfInit(StructLit *sl) {
    for (auto &fi : sl->inits) if (Is<SelfRef>(fi.val)) return true;
    return false;
}

// Struct/variant literal into a bytes destination: fields in layout
// order, defaults (or a zero optional) for omitted ones. Named inits out
// of declaration order still construct in layout order (a note against
// §2's left-to-right rule; flagged for the spec).
inline void CodeGen::GenStructLit(StructLit *sl, const string &stk, const string &lenlv) {
    auto et = sl->exprtype;
    string selfbase;
    if (HasSelfInit(sl)) {
        selfbase = T();
        L("uint8_t *", selfbase, " = ", Top(stk), ";");
    }
    if (et->kind == TY_ENUM) {
        assert(et->enu->varmode && sl->variant);
        auto ei = EIOf(et);
        auto vi = VarIdx(ei->en, sl->variant);
        EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
        // A resizable-class ADT: variants without a resizable tail leave
        // the receiving header length zero.
        if (!lenlv.empty() && IsFix(VariantType(et, vi))) L(lenlv, " = 0;");
        GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi],
                      ei->vdefaults[vi], stk, lenlv, selfbase);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = VarIdx(ei->en, et->var->variant);
        GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi],
                      ei->vdefaults[vi], stk, lenlv, selfbase);
        return;
    }
    assert(et->kind == TY_STRUCT);
    auto si = SI(et);
    if (IsFrameObj(et)) {
        if (!selfbase.empty()) Fail(sl->line, "self references in a frame object are unsupported");
        GenFrameObjLit(sl, si, stk, lenlv);
        return;
    }
    GenFieldInits(sl, si->st->fields, si->ftypes, si->defaults, stk, lenlv, selfbase);
}

// A frame object literal: fixed fields as C members of the receiving
// object `obj`, the tail's header pointed at the stack top before its
// elements are built there.
inline void CodeGen::GenFrameObjLit(StructLit *sl, StructInst *si, const string &stk,
                                    const string &obj) {
    assert(!obj.empty());
    auto &fields = si->st->fields;
    for (size_t i = 0; i < fields.size(); i++) {
        if (fields[i].ispad) continue;
        Node *init = nullptr;
        for (size_t k = 0; k < sl->fieldindices.size(); k++)
            if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
        auto ft = si->ftypes[i];
        if (!init && i < si->defaults.size()) init = si->defaults[i];
        auto flv = cat(obj, ".", Sanitize(fields[i].name));
        if (IsResz(ft)) {
            assert(init);
            if (IsFrameObj(ft)) {
                GenConstruct(init, stk, ft, flv);
            } else {
                L(flv, ".base = ", Top(stk), ";");
                L(flv, ".len = 0;");
                GenConstruct(init, stk, ft, cat(flv, ".len"));
            }
            continue;
        }
        if (!init || Is<NullLit>(init)) {
            assert(ft->kind == TY_REF && ft->ref->optional);
            L("memset(&", flv, ", 0, sizeof(", flv, "));");
            continue;
        }
        GenAny(init, Dst { DK_LVALUE, flv, ft });
    }
}

inline void CodeGen::GenFieldInits(StructLit *sl, const vector<Field> &fields,
                                   const vector<TypeExpr *> &ftypes, const vector<Node *> &defaults,
                                   const string &stk, const string &lenlv, const string &selfbase) {
    for (size_t i = 0; i < fields.size(); i++) {
        if (fields[i].ispad) {
            auto n = fields[i].padsize > 0 ? fields[i].padsize : 0;
            if (n) { L("memset(", Top(stk), ", 0, ", n, ");"); Bump(stk, cat(n)); }
            continue;
        }
        Node *init = nullptr;
        for (size_t k = 0; k < sl->fieldindices.size(); k++)
            if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
        auto ft = ftypes[i];
        if (!init && i < defaults.size()) init = defaults[i];
        if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
            // Relative-reference slot: store from the plain reference.
            if (!init) {
                if (ft->ref->lenstorage == IS_VARINT) { L("*", Top(stk), " = 0;"); Bump(stk, "1"); }
                else { L("memset(", Top(stk), ", 0, ", IntSize((IntStorage)ft->ref->lenstorage), ");");
                       Bump(stk, cat(IntSize((IntStorage)ft->ref->lenstorage))); }
                continue;
            }
            // A `self` field points at the value this literal is building,
            // whose start was captured above.
            auto rv = Is<SelfRef>(init) ? selfbase : GenX(init);
            EmitRelStore(stk, ft, rv, sl->line);
            continue;
        }
        if (!init) {
            // Omitted optional: null (checked by TC that a default exists otherwise).
            assert(ft->kind == TY_REF && ft->ref->optional);
            L("memset(", Top(stk), ", 0, ", FixedSize(ft), ");");
            Bump(stk, cat(FixedSize(ft)));
            continue;
        }
        if (ft->kind == TY_INT && ft->intstorage == IS_VARINT) {
            auto x = GenXD(init, ast.inttypes[IS_I64]);
            Bump(stk, cat("gs_zig_write(", Top(stk), ", ", x, ")"));
            continue;
        }
        // The resizable tail (if any) receives the enclosing header's
        // length; every other field constructs plainly.
        GenConstruct(init, stk, ft, IsResz(ft) ? lenlv : "");
    }
}

}  // namespace goose
