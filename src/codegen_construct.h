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
    StoreWhole(Top(stk), t, x);
    Bump(stk, cat(sz));
}

// Stores the fixed value x of type t at the byte address p. An aggregate of
// 2, 4 or 8 bytes goes out as one store of that width: the C compiler
// otherwise writes it field by field, and a load of the whole value soon
// after -- a heap's sift reading the element just pushed, a pop -- cannot be
// forwarded from several narrower stores and waits for them to reach the
// cache. Copying through an integer of the value's size is what makes the
// C compiler assemble it in a register first.
inline void CodeGen::StoreWhole(const string &p, TypeExpr *t, const string &x) {
    auto sz = FixedSize(t);
    auto agg = t->kind == TY_STRUCT || t->kind == TY_VARIANT || t->kind == TY_ARRAY ||
               (t->kind == TY_ENUM && !t->enu->varmode);
    if (!agg || (sz != 2 && sz != 4 && sz != 8)) {
        L("*(", CT(t), " *)", p, " = ", x, ";");
        return;
    }
    auto v = x;
    auto simple = !v.empty() && !isdigit((unsigned char)v[0]);
    for (auto c : v) simple &= isalnum((unsigned char)c) || c == '_';
    if (!simple) {
        v = T();
        FixedLocal(t, v, x);
    }
    auto w = T();
    L(sz == 8 ? "uint64_t " : sz == 4 ? "uint32_t " : "uint16_t ", w, ";");
    L("memcpy(&", w, ", &", v, ", ", sz, ");");
    L("memcpy(", p, ", &", w, ", ", sz, ");");
}

inline void CodeGen::EmitLenCheck(IntStorage ls, const string &n) {
    if (ls == IS_VARINT || IntSize(ls) == 8) return;
    auto max = (1ull << (IntSize(ls) * 8)) - 1;
    L("if ((uint64_t)(", n, ") > ", max,
      "ull) gs_panic(\"array length exceeds storage range\");");
}

inline void CodeGen::EmitLenStore(const string &stk, IntStorage ls, const string &n) {
    EmitLenCheck(ls, n);
    if (ls == IS_VARINT) {
        Bump(stk, cat("gs_uleb_write(", Top(stk), ", (uint64_t)(", n, "))"));
    } else {
        L("*(", IntCT(ls), " *)", Top(stk), " = (", IntCT(ls), ")(", n, ");");
        Bump(stk, cat(IntSize(ls)));
    }
}

// A varint field or element (§3.6): the i64 value `x`, zigzag-encoded.
inline void CodeGen::EmitVarintStore(const string &stk, const string &x) {
    Bump(stk, cat("gs_zig_write(", Top(stk), ", ", x, ")"));
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

// A reference of a non-optional type is never null (§3.8): the value of
// a node typed so, of a call declared to return one, and of a block or an
// if whose every value is one. (A node delivering to a relative slot may
// carry the slot's own type, optional or not.)
static bool NonNullRefValue(Node *v) {
    auto nonnull = [](TypeExpr *t) { return t && t->kind == TY_REF && !t->ref->optional; };
    if (!v) return false;
    if (nonnull(v->exprtype)) return true;
    if (auto c = Is<Call>(v)) return !c->rettypes.empty() && nonnull(c->rettypes[0]);
    if (auto b = Is<Block>(v)) return NonNullRefValue(b->tail);
    if (auto i = Is<IfExpr>(v))
        return i->elseb && NonNullRefValue(i->thenb) && NonNullRefValue(i->elseb);
    return false;
}

inline RelFacts CodeGen::RelValue(Node *v) {
    RelFacts f;
    f.nonnull = NonNullRefValue(v);
    return f;
}

// Whether a self-relative slot of type `rt`, a field of a fixed struct of
// type `holder` at byte `off`, can never have its own address as a target.
// It cannot where `off` is not 0 and the pointee is no reference: a target
// starting at the slot would share the slot's bytes with the holder, so one
// of the two values would contain the other. The holder starts before the
// slot, so it would be the target that lies inside the holder, as a part
// starting where the slot does -- and the only such part is the slot
// itself, a reference.
inline bool CodeGen::RelSlotApart(TypeExpr *rt, TypeExpr *holder, int64_t off) {
    return off > 0 && holder && holder->kind == TY_STRUCT && IsFix(holder) &&
           rt->ref->sub->kind != TY_REF;
}

// The offset a plain reference value stores as, in a local: its distance
// from the origin the width's form measures against (RelOrigin), and zero
// for the null of an optional one, whose slot cannot hold a real offset of
// zero (§3.9). A self-relative one could be zero for a target at the slot
// itself, which aborts, unless the facts rule that target out.
inline string CodeGen::RelOffset(TypeExpr *rt, const string &org, const string &rv, Line ln,
                                 RelFacts f) {
    auto addr = cat("(uint8_t *)(", rv, ")");
    auto off = T();
    if (rt->ref->optional && !f.nonnull)
        L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - (", org, ")) : 0;");
    else
        L("int64_t ", off, " = (int64_t)(", addr, " - (", org, "));");
    if (rt->ref->optional && !rt->ref->pool && !f.apart)
        L("if (", f.nonnull ? string() : cat(addr, " && "), "!", off, ") gs_abort(GS_E_RELNULL, ",
          LocArgs(ln), ");");
    return off;
}

// Writes a plain-reference value into the relative-reference slot at
// `fa` (§3.9), range-checked. Fixed widths only; the varint form exists
// only in the stack-top variant below.
inline void CodeGen::EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln,
                                    bool inroot, RelFacts f) {
    assert(rt->ref->lenstorage != IS_VARINT);
    assert(!IsResz(rt->ref->sub));
    auto off = RelOffset(rt, RelOrigin(rt, fa), rv, ln, f);
    EmitRelRangeCheck(rt, off, ln, inroot);
    L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", off, ";");
}

inline void CodeGen::EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln,
                                  RelFacts f) {
    auto w = (IntStorage)rt->ref->lenstorage;
    auto fa = T();
    L("uint8_t *", fa, " = ", Top(stk), ";");
    if (w == IS_VARINT) {
        assert(!IsResz(rt->ref->sub));
        auto off = RelOffset(rt, RelOrigin(rt, fa), rv, ln, f);
        Bump(stk, rt->ref->pool ? cat("gs_uleb_write(", fa, ", (uint64_t)", off, ")")
                                : cat("gs_zig_write(", fa, ", ", off, ")"));
    } else {
        EmitRelStoreAt(fa, rt, rv, ln, true, f);
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

// Whether a fixed value of type t can hold bytes nothing ever wrote: the
// unused slots of a limited array (§5.3), at any depth, or the one element
// C gives a zero-length array. A C temporary of such a type is
// zero-initialized before a literal fills it, since copying a struct with
// indeterminate bytes is what MSVC 19.51 exploits to miscompile the reads of
// the bytes that were written, and copying one nothing wrote at all is
// undefined.
inline bool CodeGen::HasUninitSlots(TypeExpr *t) {
    switch (t->kind) {
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            if (t->kind == TY_ENUM && t->enu->varmode) return false;
            return AnyField(t, [&](TypeExpr *ft) { return HasUninitSlots(ft); });
        case TY_ARRAY:
            if (t->arr->akind == A_LIMITED) return true;
            return t->arr->akind == A_FIXED &&
                   (ArrSize(t->arr) == 0 || HasUninitSlots(t->arr->sub));
        default: return false;
    }
}

// Does a fixed type contain relative references at any depth? Literals of
// such types must construct in their final location, not via a temp.
inline bool CodeGen::HasRelRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            if (t->kind == TY_ENUM && t->enu->varmode) return false;
            return AnyField(t, [&](TypeExpr *ft) { return HasRelRef(ft); });
        case TY_ARRAY:
            return (t->arr->akind == A_FIXED || t->arr->akind == A_LIMITED) &&
                   HasRelRef(t->arr->sub);
        default: return false;
    }
}

// Does a value of type t hold a self-relative reference by value, at any
// depth, variable-size parts included? Its offsets depend on where it sits.
inline bool CodeGen::HasSelfRelRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0 && !t->ref->pool;
        case TY_ARRAY: return HasSelfRelRef(t->arr->sub);
        default: return AnyField(t, [&](TypeExpr *ft) { return HasSelfRelRef(ft); });
    }
}

// An exit's value that is a local holding self-relative references reaches
// its destination only as the named result built there (§3.9, §7.3): the
// checker allows nothing else (TypeCheck::AllowNamedResult), and a copy
// would keep offsets measured from the local.
inline void CodeGen::NoSelfRelCopy(Node *val) {
    auto id = Is<Ident>(val);
    if (id && id->vdef && id->vdef->type && HasSelfRelRef(id->vdef->type))
        Fail(val->line, cat("internal error: ", id->vdef->name, " holds self-relative "
                            "references and is not built where it is returned"));
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
        L(CopyFn(), "(", Top(stk), ", ", src, ", (size_t)((", n, ") * ", esz, "));");
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
    // An array, or a reference to an array or slice (an append's source
    // keeps its reference type): the elements where they are stored.
    assert(t->kind == TY_ARRAY || IsPlainRef(t));
    auto lv = GenLoc(n);
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    if (lv.t->kind == TY_SLICE) {
        r.elems = cat(lv.s, ".data");
        r.n = cat(lv.s, ".len");
        return r;
    }
    auto v = ArrayView(lv);
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
    if (auto it = fillvalues.find(n); it != fillvalues.end()) {
        auto target = want ? want : it->second.t;
        if (target->kind == TY_REF && target->ref->lenstorage >= 0)
            EmitRelStore(stk, target, GenX(n), n->line, RelValue(n));
        else if (IsVarintT(target)) EmitVarintStore(stk, GenXD(n, ast.inttypes[IS_I64]));
        else if (IsBytesT(target) || IsResz(target))
            ConstructFromLoc(it->second, target, stk, lenlv, n->line);
        else EmitValStore(stk, target, GenXD(n, target));
        return;
    }
    // An inlined body's named result reaching the destination it was
    // bound to (OpenIbNrvo): the elements are in place, so all that is
    // left is the count or the reserved prefix. Any other use of that
    // local constructs a copy elsewhere and takes the normal path.
    if (auto nd = NrvoBoundAt(n, stk, lenlv)) {
        EmitNrvoFinish(*nd);
        return;
    }
    auto et = n->exprtype;
    // A varint slot's value is an i64, like every varint read, whatever
    // produces it: an if or an inlined body computes it into a temporary
    // (CtlValX), and only then is it encoded at stk, so an exit taken while
    // it is computed leaves nothing there.
    if (IsVarintT(want ? want : et)) {
        EmitVarintStore(stk, GenXD(n, ast.inttypes[IS_I64]));
        return;
    }
    // An array that is not fixed-size landing in a slot of another such array
    // type (an inlined callee's result reaching the caller's destination,
    // typed as the callee's, say) takes the slot's layout (§4.2): the
    // length prefix, capacity or receiving count written here is the
    // destination's, whatever the expression's own type says.
    if (want && et && want->kind == TY_ARRAY && IsBytesT(want) && et->kind == TY_ARRAY &&
        IsBytesT(et) && !TEq(want, et) && TEq(want->arr->sub, et->arr->sub))
        et = want;
    // Any array or slice of the element type landing in a static-capacity
    // limited slot is copied into the slot's C value (§4.2).
    if (want && et && IsStaticLimited(want) && (et->kind == TY_ARRAY || et->kind == TY_SLICE) &&
        !TEq(et, want))
        et = want;
    if (want && NeedsDeref(n->exprtype, want)) {
        // A spliced reference in a decayed slot: the pointee, constructed
        // as the slot's type from where it lies.
        if (IsBytesT(want)) ConstructFromLoc(GenLoc(n), want, stk, lenlv, n->line);
        else EmitValStore(stk, want, GenXD(n, want));
        return;
    }
    if (IsCtl(n)) { GenAny(n, Dst { DK_STACK, stk, want, lenlv }); return; }
    // What this places at stk while its parts are built sits in front of
    // any exit taken inside one of them (ExitStart).
    OpenAt open(*this, stk);
    if (auto c = Is<Call>(n); c && c->builtin == B_COPY) {
        // Keep the source's storage type and adapt into the destination.
        auto target = want ? want : et;
        auto lv = GenLoc(c->FirstArg());
        if (IsBytesT(target)) ConstructFromLoc(lv, target, stk, lenlv, n->line);
        else EmitValStore(stk, target, LoadLoc(lv, target, n->line));
        return;
    }
    // A fixed-size array or slice landing in a slot of an array type that is
    // not -- an inlined callee's result again -- is taken as its own type
    // first, a C value, which checks a limited array's capacity as the
    // callee's return would, and constructed as the slot's from there.
    if (want && et && IsFix(et) && (et->kind == TY_ARRAY || et->kind == TY_SLICE) &&
        want->kind == TY_ARRAY && IsBytesT(want)) {
        Loc lv;
        lv.t = et;
        lv.val = true;
        lv.s = Snapshot(et, GenXD(n, et));
        ConstructFromLoc(lv, want, stk, lenlv, n->line);
        return;
    }
    if (auto c = Is<Call>(n)) {
        if (auto from = AdtFrom(c)) {
            GenAdtAdapted(from, et, Dst { DK_STACK, stk, et, lenlv }, n->line,
                          [&](const Dst &d) { GenCallAs(c, from, d); });
            return;
        }
        ConstructCall(c, et, stk, want, lenlv);
        return;
    }
    // A reference landing in a relative-reference slot -- an element of a
    // `(T&<w>)[>..]`, a variable of a varint width, say -- stores the offset
    // from that slot, not the pointer (§3.9), and null the optional's zero.
    // Reference values always reach here as plain pointers, relative ones
    // having been decoded on the read.
    if (want && want->kind == TY_REF && want->ref->lenstorage >= 0 && et->kind == TY_REF) {
        EmitRelStore(stk, want, GenX(n), n->line, RelValue(n));
        return;
    }
    if (!IsBytesT(et)) {
        // Fixed values normally construct as C values; ones containing
        // relative references must be built at their final address.
        if ((Is<StructLit>(n) || Is<ArrayLit>(n)) && HasRelRef(et)) {
            FixedLitAtStk(n, stk);
            return;
        }
        EmitValStore(stk, et, GenXD(n, et));
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
        assert(et->kind == TY_ARRAY && lenlv.empty());
        if (et->arr->akind == A_LIMITED) {
            // Runtime capacity: chosen as the literal's length (v1).
            L("*(uint32_t *)", Top(stk), " = ", s->val.size(), ";");
            L("*(uint32_t *)(", Top(stk), " + 4) = ", s->val.size(), ";");
            Bump(stk, "8");
        } else {
            assert(et->arr->akind == A_VAR);
            EmitLenStore(stk, LenStore(et->arr), cat(s->val.size()));
        }
        if (!s->val.empty()) {
            L("memcpy(", Top(stk), ", ", StrRaw(s->val), ", ", s->val.size(), ");");
            Bump(stk, cat(s->val.size()));
        }
        return;
    }
    if (auto al = Is<ArrayLit>(n)) {
        // A `[..cap]` literal taking another array type is empty there: built
        // as its own, which checks the capacity's range, and copied below.
        if (!al->capexpr || TEq(et, al->exprtype)) {
            GenArrayLit(al, stk, lenlv, et);
            return;
        }
    }
    if (auto sl = Is<StructLit>(n)) { GenStructLit(sl, stk, lenlv); return; }
    if (auto d = Is<Dot>(n); d && d->variantconst) {
        // A payload-less variant constant in variable mode: just the tag.
        assert(et->kind == TY_ENUM && et->enu->varmode);
        auto ei = EIOf(et);
        EmitValStoreTag(stk, TagStore(ei->en),
                        TagConst(ei, ei->en->VariantIndex(d->variantconst)));
        if (!lenlv.empty()) L(lenlv, " = 0;");
        return;
    }
    if (auto se = Is<SliceExpr>(n); se && et->kind == TY_ARRAY) {
        // A slice expression constructing an array copies the range.
        Loc slv;
        slv.val = true;
        slv.s = GenSlice(se);
        slv.t = ast.SliceOf(et->arr->sub, n->line);
        GenArrayFromLoc(slv, et, stk, n->line, lenlv);
        return;
    }
    // Remaining nodes denote existing values: resolve the location, then
    // either copy identical layouts wholesale or adapt (slice/array kind
    // changes, ADT mode changes) element/field-wise.
    ConstructFromLoc(GenLoc(n), et, stk, lenlv, n->line);
}

// Constructs the value at an existing location as a bytes-class et at stk's
// top (see GenConstruct).
inline void CodeGen::ConstructFromLoc(Loc lv, TypeExpr *et, const string &stk,
                                      const string &lenlv, Line ln) {
    if (lv.t->kind == TY_REF && et->kind != TY_REF) DerefLoc(lv);
    if (IsResz(et)) {
        if (lv.t->kind == TY_SLICE && et->kind == TY_ARRAY) {
            GenArrayFromLoc(lv, et, stk, ln, lenlv);
            return;
        }
        if (lv.t->kind == TY_ARRAY || TEq(lv.t, et)) {
            if (TEq(lv.t, et) && et->kind != TY_ARRAY) {
                EmitRzCopy(lv, et, stk, lenlv, ln);
            } else {
                GenArrayFromLoc(lv, et, stk, ln, lenlv);
            }
            return;
        }
        // A variant without a resizable tail: the ADT's tail stays empty.
        if (et->kind == TY_ENUM && !IsResz(lv.t)) {
            GenVarEnumFromLoc(lv, et, stk);
            if (!lenlv.empty()) L(lenlv, " = 0;");
            return;
        }
        Fail(ln, cat("unsupported resizable construction from ", Mangle(lv.t)));
    }
    if (!lenlv.empty() && et->kind == TY_ARRAY) {
        // Element-run destination: elements only, count into lenlv.
        GenArrayFromLoc(lv, et, stk, ln, lenlv);
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
    if (et->kind == TY_ARRAY) { GenArrayFromLoc(lv, et, stk, ln, lenlv); return; }
    if (et->kind == TY_ENUM && et->enu->varmode) {
        GenVarEnumFromLoc(lv, et, stk);
        return;
    }
    Fail(ln, cat("unsupported construction adaptation to ", Mangle(et)));
}

// Whether call c, handed a stack slot of type et that counts into lenlv
// where that is set, builds its bytes-class result of type rt there: in its
// own layout, which is the slot's where the two are one type, or as the
// call's convention retargets it -- a resizable's or a variable array's
// elements as a run counted into lenlv, a variable array behind the slot's
// length prefix (EmitReprefix), a resizable function result behind a header
// of its own for the caller to copy, a builtin's behind a variable array's
// prefix (OpenRzDest, EmitStr). Any other array result (a runtime-capacity
// limited one, say, or a variable one for such a slot) has a layout the
// slot does not share.
inline bool CodeGen::CallBuildsAt(Call *c, TypeExpr *rt, TypeExpr *et, const string &lenlv) {
    if (rt->kind != TY_ARRAY || et->kind != TY_ARRAY || TEq(rt, et)) return true;
    auto var = [](TypeExpr *t) { return t->arr->akind == A_VAR; };
    if (!lenlv.empty()) return IsResz(rt) || var(rt);
    if (IsResz(rt)) return c->builtin < 0 || var(et);
    return var(rt) && var(et);
}

// A call's first result constructed as et at stk's top (see GenConstruct).
inline void CodeGen::ConstructCall(Call *c, TypeExpr *et, const string &stk, TypeExpr *want,
                                   const string &lenlv) {
    auto rt0 = c->rettypes.empty() ? nullptr : c->rettypes[0];
    // A bytes-class result feeding a fixed-class slot (an array
    // constructing a static-capacity limited one, §4.2), or a slot of an
    // array kind it cannot be built in (CallBuildsAt), is built on a
    // temporary of its own and copied into the slot below, so the call
    // is not handed the slot's stack to build on.
    auto own = rt0 && IsBytesT(rt0) && (!IsBytesT(et) || !CallBuildsAt(c, rt0, et, lenlv));
    auto rets = EmitCall(c, own ? Dst {} : Dst { DK_STACK, stk, want, lenlv });
    if (rets.empty() || IsVoidT(et)) return;
    // A returned reference landing in a relative-reference slot stores the
    // offset, as any other reference value does there (§3.9).
    auto slot = want ? want : et;
    if (slot->kind == TY_REF && slot->ref->lenstorage >= 0) {
        EmitRelStore(stk, slot, CallVal0(c, rets[0], slot), c->line);
        return;
    }
    // A resizable result with no receiving header was built behind a
    // temporary one: copy it into the slot as the slot's array kind.
    if (rt0 && IsResz(rt0) && rt0->kind == TY_ARRAY && lenlv.empty() &&
        et->kind == TY_ARRAY && !rets[0].empty()) {
        Loc lv;
        lv.t = rt0;
        lv.s = cat(rets[0], ".base");
        lv.lenlv = cat(rets[0], ".len");
        GenArrayFromLoc(lv, et, stk, c->line, lenlv);
        return;
    }
    // A reference-returning call decayed to a value here, or an array or
    // slice result of another kind than the slot's -- a fixed-size one, which
    // arrives as a C value, or one built on its own temporary above: the
    // callee did not construct at the destination, so the value is
    // constructed as the slot's type from where it lies (§4.2).
    auto fixarr = rt0 && IsFix(rt0) && (rt0->kind == TY_ARRAY || rt0->kind == TY_SLICE);
    if (rt0 && IsBytesT(et) && (IsPlainRef(rt0) || fixarr || own)) {
        ConstructFromLoc(CallResLoc(c, rets[0]), et, stk, lenlv, c->line);
        return;
    }
    // A fixed-size result (a reference-returning call's pointee
    // included) is a C value: it lands at the top like any other.
    if (!IsBytesT(et)) EmitValStore(stk, et, CallVal0(c, rets[0], et));
}

// The type a call or an inlined call body delivers its value as, where the
// checker adapted that value to an ADT it does not arrive as (FitsAt: a
// variant to its ADT, or the ADT to its other mode, §3.5); null elsewhere.
// A reference result counts as its pointee, which the receiver loads.
inline TypeExpr *CodeGen::AdtFrom(Node *n) {
    auto et = n->exprtype;
    if (!et || et->kind != TY_ENUM) return nullptr;
    TypeExpr *from = nullptr;
    if (auto c = Is<Call>(n)) {
        if (c->fvbody) from = c->fvbody->exprtype;
        else if (c->builtin != B_COPY && !c->rettypes.empty()) from = c->rettypes[0];
    } else if (auto ib = Is<InlineBlock>(n)) {
        if (ib->spec && !ib->spec->rets.empty()) from = ib->spec->rets[0];
    }
    if (from && IsPlainRef(from)) from = from->ref->sub;
    if (!from || (from->kind != TY_VARIANT && from->kind != TY_ENUM) || TEq(from, et))
        return nullptr;
    return from;
}

// A value produced as `from` reaching d as the ADT `to` it was adapted to
// (AdtFrom): `gen` builds the value, as `from`, into the destination it is
// handed. A variant constructing a variable-mode ADT needs only its tag in
// front of it, so it builds in place; every other adaptation builds into a
// temporary and converts from there.
inline void CodeGen::GenAdtAdapted(TypeExpr *from, TypeExpr *to, const Dst &d, Line ln,
                                   const function<void(const Dst &)> &gen) {
    if (d.k == DK_DISCARD) { gen(d); return; }
    if (to->enu->varmode) {
        assert(d.k == DK_STACK);
        if (from->kind == TY_VARIANT) {
            gen(VariantBehindTag(from, to, d));
            return;
        }
        // The fixed-mode ADT, whose variants are all fixed-size.
        Loc lv;
        lv.t = from;
        lv.val = true;
        lv.s = T();
        FixedLocal(from, lv.s);
        gen(Dst { DK_LVALUE, lv.s, from });
        GenVarEnumFromLoc(lv, to, d.s);
        if (!d.lenlv.empty()) L(d.lenlv, " = 0;");
        return;
    }
    // The fixed-mode ADT, from a variant (fixed-size, as the mode requires)
    // or from the variable-mode ADT.
    Loc lv;
    lv.t = from;
    if (IsBytesT(from)) {
        string stk;
        lv.s = BytesTemp(stk);
        lv.stk = stk;
        gen(Dst { DK_STACK, stk, from });
    } else {
        lv.val = true;
        lv.s = T();
        FixedLocal(from, lv.s);
        gen(Dst { DK_LVALUE, lv.s, from });
    }
    auto x = AdaptToFixed(lv, to, ln);
    if (d.k == DK_LVALUE) L(d.s, " = ", x, ";");
    else EmitValStore(d.s, to, x);
}

// The variant `from` building the variable-mode ADT `to` at d, a stack: the
// ADT's tag goes in first, and the variant builds behind it where the
// returned destination says. A resizable-class ADT's tail count is the
// variant's own, or zero when the variant has no tail.
inline Dst CodeGen::VariantBehindTag(TypeExpr *from, TypeExpr *to, const Dst &d) {
    assert(d.k == DK_STACK);
    auto ei = EIOf(to);
    EmitValStoreTag(d.s, TagStore(ei->en), TagConst(ei, ei->en->VariantIndex(from->var->variant)));
    auto rz = IsResz(from);
    if (!d.lenlv.empty() && !rz) L(d.lenlv, " = 0;");
    return Dst { DK_STACK, d.s, from, rz ? d.lenlv : "" };
}

// A call's first result, of the type t it arrives as, into d.
inline void CodeGen::GenCallAs(Call *c, TypeExpr *t, const Dst &d) {
    if (d.k == DK_STACK) {
        ConstructCall(c, t, d.s, d.t, d.lenlv);
        return;
    }
    auto rets = EmitCall(c, d);
    if (d.k != DK_LVALUE || rets.empty()) return;
    auto r0 = CallVal0(c, rets[0], t);
    if (r0 != d.s) L(d.s, " = ", r0, ";");
}

inline void CodeGen::GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln,
                                     const string &lenlv) {
    if (IsStaticLimited(et)) {
        // A static-capacity limited array is a C value (§4.2): its length
        // and slots land together.
        assert(lenlv.empty());
        EmitValStore(stk, et, AdaptToFixed(lv, et, ln));
        return;
    }
    auto v = ArrayView(lv);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    switch (et->arr->akind) {
        case A_VAR:
            if (!lenlv.empty()) L(lenlv, " = ", nn, ";");
            else EmitLenStore(stk, LenStore(et->arr), nn);
            break;
        case A_LIMITED:   // Runtime capacity: chosen as the initial length (v1).
            EmitLenCheck(IS_U32, nn);
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
        if (!lv.val) lv = FoView(lv);
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

// Frame object `obj`'s fixed fields, at every nesting level, against the
// bytes layout the same type takes at `p` as the tail of a value that is not
// a frame object (C.2): stored there, explicit pads zeroed as GenFieldInits
// zeroes them, or (`tobytes` false) loaded back.
inline void CodeGen::FoBytes(TypeExpr *t, const string &obj, const string &p, bool tobytes) {
    auto si = SI(t);
    auto &fields = si->st->fields;
    int64_t off = 0;
    for (size_t i = 0; i < fields.size(); i++) {
        auto at = off ? cat("(", p, " + ", off, ")") : p;
        if (fields[i].ispad) {
            if (fields[i].padsize <= 0) continue;
            if (tobytes) L("memset(", at, ", 0, ", fields[i].padsize, ");");
            off += fields[i].padsize;
            continue;
        }
        auto ft = si->ftypes[i];
        auto flv = cat(obj, ".", Sanitize(fields[i].name));
        if (IsResz(ft)) {
            if (IsFrameObj(ft)) FoBytes(ft, flv, at, tobytes);
            return;
        }
        if (tobytes) L("*(", CT(ft), " *)", at, " = ", flv, ";");
        else L(flv, " = *(", CT(ft), " *)", at, ";");
        off += FixedSize(ft);
    }
}

// A frame object laid out as bytes (FoBytes) as the C frame object a whole
// read of it takes: its fixed fields loaded, its innermost tail's header
// pointing at the elements where they lie.
inline CodeGen::Loc CodeGen::FoView(const Loc &lv) {
    assert(!lv.val && !lv.lenlv.empty());
    Loc r;
    r.t = lv.t;
    r.val = true;
    r.s = T();
    L(CT(lv.t), " ", r.s, ";");
    FoBytes(lv.t, r.s, lv.s, false);
    auto th = FoTailHdr(lv.t, r.s);
    L(th, ".base = (", lv.s, ") + ", FoBytesPrefix(lv.t), ";");
    L(th, ".len = ", lv.lenlv, ";");
    return r;
}

// Constructs n's value, of frame object type t, at stk's top as the tail of
// a value that is not a frame object: its fixed fields as bytes (FoBytes),
// its innermost tail's count into the enclosing header's `lenlv`. A literal
// builds that way directly. Anything else is built as the frame object it is
// everywhere else, its elements landing behind room left for the fixed
// fields, which are stored there after it.
inline void CodeGen::GenFoAsBytes(Node *n, const string &stk, TypeExpr *t, const string &lenlv) {
    OpenAt open(*this, stk);
    if (auto sl = Is<StructLit>(n); sl && TEq(sl->exprtype, t) && !fillvalues.count(n)) {
        auto si = SI(t);
        GenFieldInits(sl, si->st->fields, si->ftypes, stk, lenlv);
        return;
    }
    auto pre = FoBytesPrefix(t);
    string p;
    if (pre) {
        p = T();
        L("uint8_t *", p, " = ", Top(stk), ";");
        Bump(stk, cat(pre));
    }
    auto h = T();
    L(CT(t), " ", h, ";");
    GenConstruct(n, stk, t, h);
    if (pre) FoBytes(t, h, p, true);
    L(lenlv, " = ", FoTailHdr(t, h), ".len;");
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
        auto last = LastRealField(si->st->fields);
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
        auto vi = ei->en->VariantIndex(lv.t->var->variant);
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
    FixedLocal(lv.t, sv, lv.s);
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

// Capture a fill's value before repeating its construction. Relative links
// retain absolute targets until the destination is known; `self` remains
// bound to each constructed element. Capturing the literal's leaves avoids
// trying to encode a narrow relative link in a distant temporary first.
inline void CodeGen::FreezeFill(Node *n, TypeExpr *t, vector<Node *> &added) {
    if (!n || Is<SelfRef>(n) || fillvalues.count(n)) return;
    if (HasRelRefAny(t)) {
        if (auto sl = Is<StructLit>(n)) {
            const vector<TypeExpr *> *fields;
            if (t->kind == TY_STRUCT) fields = &SI(t)->ftypes;
            else {
                auto ei = t->kind == TY_VARIANT ? EIVar(t) : EIOf(t);
                auto vi = ei->en->VariantIndex(t->kind == TY_VARIANT ? t->var->variant : sl->variant);
                fields = &ei->vftypes[vi];
            }
            for (size_t i = 0; i < sl->inits.size(); i++)
                FreezeFill(sl->inits[i].val, (*fields)[sl->fieldindices[i]], added);
            return;
        }
        if (auto al = Is<ArrayLit>(n); al && t->kind == TY_ARRAY) {
            if (al->capexpr) FreezeFill(al->capexpr, ast.inttypes[IS_I64], added);
            if (al->fillval) FreezeFill(al->fillval, t->arr->sub, added);
            else for (auto e : al->elems) FreezeFill(e, t->arr->sub, added);
            return;
        }
    }
    if (IsVarintT(t)) t = ast.inttypes[IS_I64];
    if (t->kind == TY_REF && t->ref->lenstorage >= 0)
        t = ast.RefTo(t->ref->sub, n->line, t->ref->optional);
    Loc lv;
    lv.t = t;
    if (IsResz(t)) {
        auto h = RzTemp(t, lv.stk);
        GenAny(n, Dst { DK_STACK, lv.stk, t, RzLenLv(t, h) });
        lv = RzTempLoc(t, h, lv.stk);
    } else if (IsBytesT(t)) {
        lv.s = BytesTemp(lv.stk);
        GenConstruct(n, lv.stk, t);
    } else {
        lv.val = true;
        lv.s = Snapshot(t, GenXD(n, t));
    }
    fillvalues.emplace(n, lv);
    added.push_back(n);
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
            if (Is<SelfRef>(init)) {
                EmitRelSelfStore(stk, ft, fieldoff, n->line);
            } else {
                auto f = RelValue(init);
                f.apart = RelSlotApart(ft, et, fieldoff);
                EmitRelStore(stk, ft, GenX(init), n->line, f);
            }
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
            FillScope fill(*this, al->fillval, elem);
            auto fc = Is<IntLit>(al->fillcount);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", fc->val, "; ", iv, "++) {");
            ind++;
            PushSc(SC_PLAIN);
            EmitF(al->fillval, elem);
            PopSc();
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
                          const Layout &lo, int64_t total, int64_t base) {
        int64_t cur = 0;
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            Gap(lo.offs[i] - cur);
            cur = lo.offs[i];
            EmitF(sl->InitFor((int)i), ftypes[i], base + lo.offs[i]);
            cur += FixedSize(ftypes[i]);
        }
        Gap(total - cur);
    };
    if (et->kind == TY_STRUCT) {
        auto si = SI(et);
        auto &lo = StructLayout(si);
        emitfields(si->st->fields, si->ftypes, lo, lo.size, 0);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = ei->en->VariantIndex(et->var->variant);
        auto &lo = VariantLayout(ei, vi);
        emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], lo, lo.size, 0);
        return;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
    auto ei = EIOf(et);
    auto vi = ei->en->VariantIndex(sl->variant);
    EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
    auto &lo = VariantLayout(ei, vi);
    emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], lo, lo.size, TagSize(ei->en));
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
        if (rel) EmitRelStoreAt(cat("(uint8_t *)&", path), elem, GenX(e), al->line, inroot,
                                RelValue(e));
        else GenAny(e, Dst { DK_LVALUE, path, elem });
    };
    if (et->arr->akind == A_LIMITED) {
        auto count = al->fillval ? ((IntLit *)al->fillcount)->val
                                 : (int64_t)al->elems.size();
        L(base, ".len = ", count, ";");
    }
    if (al->fillval) {
        auto fc = Is<IntLit>(al->fillcount);
        assert(fc);
        FillScope fill(*this, al->fillval, elem);
        // Captured values repeat; relative offsets are encoded per slot.
        auto perelem = HasRelRef(elem);
        auto fv = perelem ? string() : GenXD(al->fillval, elem);
        auto iv = T();
        L("for (int64_t ", iv, " = 0; ", iv, " < ", fc->val, "; ", iv, "++) {");
        ind++;
        auto path = cat(base, ".e[", iv, "]");
        PushSc(SC_PLAIN);
        if (perelem) emitelem(al->fillval, path);
        else L(path, " = ", fv, ";");
        PopSc();
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
                        const vector<TypeExpr *> &ftypes, const Layout &lo, int64_t baseoff) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            auto init = sl->InitFor((int)i);
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
                else {
                    auto f = RelValue(init);
                    f.apart = RelSlotApart(ft, et, baseoff + lo.offs[i]);
                    EmitRelStoreAt(cat("(uint8_t *)&", path), ft, GenX(init), sl->line, inroot, f);
                }
                continue;
            }
            GenAny(init, Dst { DK_LVALUE, path, ft });
        }
    };
    if (et->kind == TY_STRUCT) {
        auto si = SI(et);
        // Nothing writes the C object of a struct or a variant without
        // fields, and copying one nothing wrote is undefined.
        if (LastRealField(si->st->fields) < 0) L("memset(&", base, ", 0, sizeof(", base, "));");
        fieldset(base, si->st->fields, si->ftypes, StructLayout(si), 0);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = ei->en->VariantIndex(et->var->variant);
        if (LastRealField(ei->en->variants[vi].fields) < 0)
            L("memset(&", base, ", 0, sizeof(", base, "));");
        fieldset(base, ei->en->variants[vi].fields, ei->vftypes[vi], VariantLayout(ei, vi), 0);
        return;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
    auto ei = EIOf(et);
    auto vi = ei->en->VariantIndex(sl->variant);
    L(base, ".tag = ", TagConst(ei, vi), ";");
    if (!EmptyLayout(ei->en->variants[vi].fields))
        fieldset(cat(base, ".u.v_", Sanitize(ei->en->variants[vi].name)),
                 ei->en->variants[vi].fields, ei->vftypes[vi], VariantLayout(ei, vi),
                 TagSize(ei->en));
}

inline void CodeGen::EmitValStoreTag(const string &stk, IntStorage ts, const string &x) {
    L("*(", IntCT(ts), " *)", Top(stk), " = (", IntCT(ts), ")(", x, ");");
    Bump(stk, cat(IntSize(ts)));
}

// The literal built as `as` where that is given (GenConstruct's retarget),
// as its own type otherwise.
inline void CodeGen::GenArrayLit(ArrayLit *al, const string &stk, const string &lenlv,
                                TypeExpr *as) {
    auto et = as ? as : al->exprtype;
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
        FillScope fill(*this, al->fillval, elem);
        if (IsVarintT(elem)) {
            // One i64, evaluated once as a fixed-size fill value is.
            auto i64 = ast.inttypes[IS_I64];
            auto fv = Snapshot(i64, GenXD(al->fillval, i64));
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            EmitVarintStore(stk, fv);
            ind--;
            L("}");
        } else if (IsBytesT(elem) || HasRelRef(elem)) {
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            PushSc(SC_PLAIN);
            GenConstruct(al->fillval, stk, elem);
            PopSc();
            ind--;
            L("}");
        } else if (elem->kind == TY_REF && elem->ref->lenstorage >= 0) {
            // Each relative slot stores its own offset to the one plain
            // reference, or the optional's zero for null (§3.9).
            auto p = T();
            L("uint8_t *", p, " = (uint8_t *)(", GenX(al->fillval), ");");
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            EmitRelStore(stk, elem, p, al->line);
            ind--;
            L("}");
        } else {
            auto fv = GenXD(al->fillval, elem);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            EmitValStore(stk, elem, fv);
            ind--;
            L("}");
        }
        return;
    }
    for (auto e : al->elems) GenConstruct(e, stk, elem);
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
        auto vi = ei->en->VariantIndex(sl->variant);
        EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
        // A resizable-class ADT: variants without a resizable tail leave
        // the receiving header length zero.
        if (!lenlv.empty() && IsFix(VariantType(et, vi))) L(lenlv, " = 0;");
        GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi], stk, lenlv, selfbase);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = ei->en->VariantIndex(et->var->variant);
        GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi], stk, lenlv, selfbase);
        return;
    }
    assert(et->kind == TY_STRUCT);
    auto si = SI(et);
    if (IsFrameObj(et)) {
        if (!selfbase.empty()) Fail(sl->line, "self references in a frame object are unsupported");
        GenFrameObjLit(sl, si, stk, lenlv);
        return;
    }
    GenFieldInits(sl, si->st->fields, si->ftypes, stk, lenlv, selfbase);
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
        auto init = sl->InitFor((int)i);
        auto ft = si->ftypes[i];
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
        // Only the `in pool` form can be a frame object's field (C.2), so
        // the offset does not depend on where the object is built.
        if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
            EmitRelStoreAt(cat("(uint8_t *)&", flv), ft, GenX(init), sl->line, true,
                           RelValue(init));
            continue;
        }
        GenAny(init, Dst { DK_LVALUE, flv, ft });
    }
}

inline void CodeGen::GenFieldInits(StructLit *sl, const vector<Field> &fields,
                                   const vector<TypeExpr *> &ftypes, const string &stk,
                                   const string &lenlv, const string &selfbase) {
    for (size_t i = 0; i < fields.size(); i++) {
        if (fields[i].ispad) {
            auto n = fields[i].padsize > 0 ? fields[i].padsize : 0;
            if (n) { L("memset(", Top(stk), ", 0, ", n, ");"); Bump(stk, cat(n)); }
            continue;
        }
        auto init = sl->InitFor((int)i);
        auto ft = ftypes[i];
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
            EmitRelStore(stk, ft, rv, sl->line, RelValue(init));
            continue;
        }
        if (!init) {
            // Omitted optional: null (checked by TC that a default exists otherwise).
            assert(ft->kind == TY_REF && ft->ref->optional);
            L("memset(", Top(stk), ", 0, ", FixedSize(ft), ");");
            Bump(stk, cat(FixedSize(ft)));
            continue;
        }
        // The resizable tail (if any) receives the enclosing header's
        // length, a frame object one laid out as bytes like the rest of this
        // value; every other field constructs plainly.
        if (IsResz(ft) && IsFrameObj(ft)) GenFoAsBytes(init, stk, ft, lenlv);
        else GenConstruct(init, stk, ft, IsResz(ft) ? lenlv : "");
    }
}

}  // namespace goose
