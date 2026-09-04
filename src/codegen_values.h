// Goose compiler — codegen's values (definitions of CodeGen members,
// codegen.h): locations (paths resolved to C lvalues or byte pointers, with
// their data stacks), loop-invariant array views, expression values, string
// literals, and the binary operators (§6.1, §4.5).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Locations: an assignable/addressable path resolved to either a typed C
// lvalue (val) or a byte pointer (bytes values, packed dynamic layouts).
// The root's data stack (and pool freelist) ride along for growth ops.

// Reference variables with reusable-pool provenance are represented as
// gs_pref so pool operations stay available through them (§5.4). This is
// a property of the VarDef (provenance), shared by caller and callee.
inline bool CodeGen::PrefVar(const VarDef *vd) {
    auto t = vd->type;
    return t && t->kind == TY_REF && t->ref->lenstorage < 0 && !t->ref->optional &&
           t->ref->sub->kind == TY_ARRAY && t->ref->sub->arr->akind == A_GROW &&
           vd->ref.reusable;
}

// Optimizer splices can leave a reference-typed tree in a slot whose
// checked type already decayed; Dst::t says what the receiver wants, and
// the pointee load then happens at the leaf against it.

// Whether a value of type `have` needs a pointee load to serve as `want`.
inline bool CodeGen::NeedsDeref(TypeExpr *have, TypeExpr *want) {
    return have && want && have->kind == TY_REF && !have->ref->optional &&
           have->ref->lenstorage < 0 && want->kind != TY_REF && want->kind != TY_VOID;
}

// The pointee of a fat reference `x` as a location.
inline CodeGen::Loc CodeGen::FatRefLoc(const string &x, TypeExpr *sub) {
    Loc lv;
    lv.t = sub;
    lv.stk = cat(x, ".stk");
    if (IsFrameObj(sub)) {
        lv.val = true;
        lv.s = cat("(*(", CT(sub), " *)", x, ".hdr)");
        lv.hdr = lv.s;
    } else {
        lv.s = cat(x, ".hdr->base");
        lv.lenlv = cat(x, ".hdr->len");
        lv.hdr = cat("(*", x, ".hdr)");
    }
    return lv;
}

inline CodeGen::Loc CodeGen::BytesLoc(const string &ptr, TypeExpr *t, const Loc &from) {
    Loc l;
    l.t = t;
    l.stk = from.stk;
    l.lenlv = from.lenlv;
    l.fl = from.fl;
    l.flstk = from.flstk;
    if (IsFix(t)) {
        l.val = true;
        l.s = cat("(*(", CT(t), " *)(", ptr, "))");
    } else {
        l.val = false;
        l.s = ptr;
    }
    return l;
}

// The slot's own address and the offset it holds, for a relative
// reference loc. The stored offset is signed for the self-relative form
// and unsigned for the `in pool` one, despite both widths being spelled
// unsigned (§3.9); an int64 holds either.
inline void CodeGen::RelParts(const Loc &lv, string &faddr, string &off) {
    auto &r = *lv.t->ref;
    if (r.lenstorage == IS_VARINT) {
        assert(!lv.val);
        faddr = lv.s;
        off = cat(r.pool ? "gs_uleb_read(" : "gs_zig_read(", lv.s, ")");
    } else if (lv.val) {
        faddr = cat("((uint8_t *)&", lv.s, ")");
        off = cat("(int64_t)(", RelCT(lv.t), ")", lv.s);
    } else {
        faddr = lv.s;
        off = cat("(int64_t)*(", RelCT(lv.t), " *)(", lv.s, ")");
    }
}

// What a relative offset is measured from (§3.9): the offset field's own
// address, or, for an `in pool` reference, the pool's base biased by one
// so that a target at the base still encodes non-zero and 0 stays null.
inline string CodeGen::RelOrigin(TypeExpr *rt, const string &faddr) {
    if (!rt->ref->pool) return faddr;
    return cat("(", PoolBase(rt->ref->pool), " - 1)");
}

// Loads the value of a loc holding a (plain or relative) reference and
// steps to the pointee. Optional locs never get here (narrowing).
inline void CodeGen::DerefLoc(Loc &lv, Line ln) {
    assert(lv.t->kind == TY_REF);
    auto &r = *lv.t->ref;
    if (r.lenstorage >= 0) {
        string faddr, off;
        RelParts(lv, faddr, off);
        auto p = T();
        L("uint8_t *", p, " = ", RelOrigin(lv.t, faddr), " + ", off, ";");
        auto nl = BytesLoc(p, r.sub, lv);
        lv = nl;
        return;
    }
    auto rv = lv.s;   // The lvalue text reads as the reference value.
    if (IsResz(r.sub) && IsFrameObj(r.sub)) {
        lv = FatRefLoc(rv, r.sub);
        return;
    }
    if (IsResz(r.sub)) {
        Loc nl;
        nl.t = r.sub;
        nl.val = false;
        nl.hdr = cat("(*", rv, ".hdr)");
        nl.s = lv.hbase.empty() ? cat(rv, ".hdr->base") : lv.hbase;
        // Growth writes the header even where the reads come from a hoist.
        nl.lenlv = cat(rv, ".hdr->len");
        nl.hlen = lv.hlen;
        nl.stk = cat(rv, ".stk");
        // A pool reference (gs_pref) has the same leading members plus the
        // freelist; keep the companions reachable for pool operations.
        nl.fl = lv.ispref ? cat("(*", rv, ".fl)") : lv.fl;
        nl.flstk = lv.ispref ? cat(rv, ".flstk") : lv.flstk;
        lv = nl;
        return;
    }
    if (IsBytesT(r.sub)) {
        Loc nl;
        nl.t = r.sub;
        nl.val = false;
        nl.s = rv;
        nl.hlen = lv.hlen;
        lv = nl;
        return;
    }
    Loc nl;
    nl.t = r.sub;
    nl.val = true;
    nl.s = cat("(*", rv, ")");
    nl.hlen = lv.hlen;
    lv = nl;
    (void)ln;
}

inline CodeGen::Loc CodeGen::VarLoc(VarDef *vd) {
    Loc l;
    l.t = vd->type;
    auto name = VName(vd);
    if (auto hit = views.find(vd); hit != views.end()) {
        l.hbase = hit->second.first;
        l.hlen = hit->second.second;
    }
    if (vd->reusable) {
        // A reusable pool: locals/globals have gs_rhdr companions; a
        // captured pool arrives as a gs_pref.
        auto pit = vpool.find(vd);
        auto git = gpools.find(vd);
        if (pit != vpool.end() || git != gpools.end()) {
            auto &p = pit != vpool.end() ? pit->second : git->second;
            auto hdr = fvptr.count(vd) ? cat("(*", name, ")") : name;
            l.val = false;
            l.s = PoolBaseOr(vd, cat(hdr, ".base"));
            l.lenlv = cat(hdr, ".len");
            l.stk = VStkOf(vd);
            l.fl = p.first;
            l.flstk = p.second;
        } else {
            l.val = false;
            l.s = cat(name, ".hdr->base");
            l.lenlv = cat(name, ".hdr->len");
            l.stk = cat(name, ".stk");
            l.fl = cat("(*", name, ".fl)");
            l.flstk = cat(name, ".flstk");
        }
        return l;
    }
    if (IsResz(vd->type)) {
        // A gs_rhdr or frame object in the frame (or a pointer to one,
        // when captured).
        auto hdr = fvptr.count(vd) ? cat("(*", name, ")") : name;
        l.hdr = hdr;
        l.stk = VStkOf(vd);
        if (IsFrameObj(vd->type)) {
            l.val = true;
            l.s = hdr;
            return l;
        }
        l.val = false;
        l.s = PoolBaseOr(vd, cat(hdr, ".base"));
        l.lenlv = cat(hdr, ".len");
        return l;
    }
    if (IsBytesT(vd->type)) {
        l.val = false;
        l.s = name;
        l.stk = VStkOf(vd);
        return l;
    }
    l.val = true;
    l.s = fvptr.count(vd) ? cat("(*", name, ")") : name;
    if (PrefVar(vd)) {
        l.ispref = true;
        l.fl = cat("(*", l.s, ".fl)");
        l.flstk = cat(l.s, ".flstk");
    }
    return l;
}

// The gs_rhdr lvalue of a resizable variable (frame header form, C.2).
inline string CodeGen::HdrLv(VarDef *vd) {
    auto name = VName(vd);
    return fvptr.count(vd) ? cat("(*", name, ")") : name;
}

// The C type a variable's storage uses (gs_pref for pool references).
inline string CodeGen::VarCT(const VarDef *vd) {
    if (PrefVar(vd)) { EmitCoreTypes(); return "gs_pref"; }
    return CT(vd->type);
}

// The data stack a nonfixed variable's value lives on, if known here.
inline string CodeGen::VStkOf(const VarDef *vd) {
    auto it = vstk.find(vd);
    if (it != vstk.end()) return it->second;
    auto git = gstks.find(vd);
    if (git != gstks.end()) return git->second;
    return "";
}

// A byte-pointer expression for field `fieldidx` within the bytes value at
// `base` (fields/ftypes from a struct instance or a variant). Emits cursor
// statements when a dynamic-size field precedes the target.
inline string CodeGen::FieldPtr(const string &base, const vector<Field> &fields,
                                const vector<TypeExpr *> &ftypes, int fieldidx) {
    int64_t off = 0;
    string cur;   // Cursor variable once dynamic fields intervene.
    for (auto i = 0; i < fieldidx; i++) {
        auto &f = fields[i];
        if (f.ispad) { off += f.padsize > 0 ? f.padsize : 0; continue; }
        if (IsFix(ftypes[i])) { off += FixedSize(ftypes[i]); continue; }
        if (cur.empty()) {
            cur = T();
            L("uint8_t *", cur, " = ", base, " + ", off, ";");
        } else if (off) {
            L(cur, " += ", off, ";");
        }
        off = 0;
        L(cur, " += ", SizeX(ftypes[i], cur), ";");
    }
    if (cur.empty()) return off ? cat(base, " + ", off) : base;
    return off ? cat(cur, " + ", off) : cur;
}

// Reads of the length come from a loop-hoisted local where there is one;
// `lenlv`, which the operations that change the length write, does not.
inline CodeGen::ArrView CodeGen::ArrayView(const Loc &lv, Line ln) {
    auto v = RawArrayView(lv, ln);
    if (!lv.hlen.empty()) v.len = lv.hlen;
    return v;
}

inline CodeGen::ArrView CodeGen::RawArrayView(const Loc &lv, Line ln) {
    auto t = lv.t;
    ArrView v;
    if (t->kind == TY_SLICE) {
        v.elem = t->sub;
        v.elems = cat(lv.s, ".data");
        v.len = cat(lv.s, ".len");
        v.typedelems = !IsBytesT(t->sub);
        return v;
    }
    assert(t->kind == TY_ARRAY);
    auto &a = *t->arr;
    v.elem = a.sub;
    switch (a.akind) {
        case A_FIXED:
            assert(lv.val);
            v.elems = cat(lv.s, ".e");
            v.len = cat(ArrSize(t->arr));
            v.typedelems = true;
            return v;
        case A_LIMITED:
            if (lv.val) {
                v.elems = cat(lv.s, ".e");
                v.len = cat("(int64_t)", lv.s, ".len");
                v.lenlv = cat(lv.s, ".len");
                v.typedelems = true;
            } else {
                v.elems = cat("((", lv.s, ") + 8)");
                v.len = cat("(int64_t)*(uint32_t *)((", lv.s, ") + 4)");
                v.lenlv = cat("(*(uint32_t *)((", lv.s, ") + 4))");
            }
            return v;
        case A_VAR: {
            assert(!lv.val);
            auto ls = LenStore(t->arr);
            if (ls == IS_VARINT) {
                // Sequential unless elements are fixed; length prefix is
                // dynamic, so materialize both once.
                auto p = T();
                L("uint8_t *", p, " = (", lv.s, ") + GS_ULEB_SIZE(", lv.s, ");");
                v.elems = p;
                v.len = cat("GS_ULEB_READ(", lv.s, ")");
            } else {
                v.elems = cat("((", lv.s, ") + ", IntSize(ls), ")");
                v.len = cat("(int64_t)*(", IntCT(ls), " *)(", lv.s, ")");
            }
            return v;
        }
        default:   // Grow / grow-shrink: elements at base, length in the header.
            assert(!lv.val && !lv.lenlv.empty());
            v.elems = cat("(", lv.s, ")");
            v.len = cat("(", lv.lenlv, ")");
            v.lenlv = lv.lenlv;
            return v;
    }
    (void)ln;
}

// base, length.

inline bool CodeGen::AddView(VarDef *vd, Line ln) {
    auto t = vd->type;
    if (views.count(vd) || !t || t->kind != TY_REF) return false;
    if (t->ref->optional || t->ref->lenstorage >= 0) return false;
    auto s = t->ref->sub;
    if (s->kind != TY_ARRAY || s->arr->akind == A_FIXED) return false;
    // A varint length prefix is not a plain load: its view emits statements.
    if (s->arr->akind == A_VAR && LenStore(s->arr) == IS_VARINT) return false;
    // Not named yet means bound inside the loop (a for/match binder, or a
    // declaration the body repeats), which has no view to read out here.
    if (!vnames.count(vd) && !gnames.count(vd)) return false;
    auto lv = VarLoc(vd);
    DerefLoc(lv, ln);
    auto v = ArrayView(lv, ln);
    // Only a fat reference keeps the elements pointer in memory too; the
    // other forms reach them by offsetting the reference itself.
    string nb;
    if (IsResz(s)) {
        nb = T();
        L("uint8_t *", nb, " = ", v.elems, ";");
    }
    auto nl = T();
    L("int64_t ", nl, " = ", v.len, ";");
    views[vd] = { nb, nl };
    return true;
}

// A side-effect-free C expression for a node: literals and plain scalar
// variables pass through, anything else lands in a temp first.
inline string CodeGen::GenPure(Node *n) {
    if (auto i = Is<IntLit>(n)) return IntStr(i->val);
    if (auto id = Is<Ident>(n)) {
        if (id->vdef && !id->vdef->captured && !IsBytesT(id->vdef->type) &&
            id->vdef->type->kind != TY_REF && !fvptr.count(id->vdef))
            return GenX(n);
    }
    auto x = GenX(n);
    // Temps generated by GenX are already single identifiers.
    auto simple = !x.empty() && (isalpha((uint8_t)x[0]) || x[0] == '_');
    for (auto c : x) simple = simple && (isalnum((uint8_t)c) || c == '_');
    if (simple) return x;
    auto t = T();
    L(CT(n->exprtype), " ", t, " = ", x, ";");
    return t;
}

// The two minima are written as a subtraction: C has no negative decimal
// constants, and MSVC's C front end types both magnitudes as unsigned
// (C90 literal rules), which the negation would carry through.
inline string CodeGen::IntStr(int64_t v) {
    if (v == INT64_MIN) return "(-9223372036854775807LL - 1)";
    if (v == INT32_MIN) return "(-2147483647 - 1)";
    if (v > INT32_MAX || v < INT32_MIN) return cat(v, "LL");
    return cat(v);
}

inline string CodeGen::FltStr(double v, bool f32) {
    // A folded NaN or infinity has no literal spelling; math.h's macros.
    if (v != v) return "NAN";
    if (std::isinf(v)) return v > 0 ? "INFINITY" : "(-INFINITY)";
    string s;
    CatOne(s, v);
    if (s.find('.') == string::npos && s.find('e') == string::npos &&
        s.find("inf") == string::npos && s.find("nan") == string::npos)
        s += ".0";
    if (f32) s += "f";
    return s;
}

// Indexed element location with bounds check (elided when the BCE pass
// proved it redundant, or for provably in-range constant indices into
// fixed arrays).
inline CodeGen::Loc CodeGen::IndexLoc(Loc lv, Node *idxnode, Line ln, bool nobc) {
    if (lv.t->kind == TY_REF) DerefLoc(lv, ln);
    auto v = ArrayView(lv, ln);
    auto idx = GenPure(idxnode);
    string ix;
    auto il = Is<IntLit>(idxnode);
    auto statlen = lv.t->kind == TY_ARRAY && lv.t->arr->akind == A_FIXED
                       ? ArrSize(lv.t->arr) : -1;
    if (nobc || (il && statlen >= 0 && il->val >= 0 && il->val < statlen)) ix = idx;
    else ix = cat("GS_IDX((int64_t)(", idx, "), ", v.len, ", ", LocArgs(ln), ")");
    if (v.typedelems) {
        Loc r;
        r.t = v.elem;
        r.val = true;
        r.s = cat(v.elems, "[", ix, "]");
        r.stk = lv.stk;
        r.fl = lv.fl;
        r.flstk = lv.flstk;
        return r;
    }
    assert(IsFix(v.elem));   // Variable elements are never indexed (TC).
    return BytesLoc(cat("(", v.elems, " + ", ix, " * ", FixedSize(v.elem), ")"), v.elem,
                    lv);
}

inline CodeGen::Loc CodeGen::GenLoc(Node *n) {
    if (auto id = Is<Ident>(n)) {
        assert(id->vdef);
        return VarLoc(id->vdef);
    }
    if (auto d = Is<Dot>(n)) {
        auto lv = GenLoc(d->obj);
        if (lv.t->kind == TY_REF) DerefLoc(lv, d->line);
        return MemberLoc(lv, d);
    }
    if (auto ix = Is<Index>(n)) {
        auto lv = GenLoc(ix->obj);
        if (lv.t->kind == TY_REF) DerefLoc(lv, ix->line);
        return IndexLoc(lv, ix->idx, ix->line, ix->nobc);
    }
    // `&path` addressed as a location is the path itself: going through a
    // reference temporary would spell the value's stack a second way,
    // behind the top the function may be keeping in a local (see the
    // note on data-stack top caching).
    if (auto u = Is<Unary>(n); u && u->op == T_BITAND &&
        (Is<Ident>(u->child) || Is<Dot>(u->child) || Is<Index>(u->child)) &&
        u->child->exprtype && u->child->exprtype->kind != TY_REF)
        return GenLoc(u->child);
    // Any other expression: an addressed temporary (TC's LValueBase).
    Loc l;
    l.t = n->exprtype;
    if (IsResz(n->exprtype)) return GenRzTmp(n);
    if (IsBytesT(n->exprtype)) {
        string stk;
        l.s = GenPtr(n, &stk);
        l.stk = stk;
        l.val = false;
    } else {
        auto t = T();
        L(CT(n->exprtype), " ", t, " = ", GenX(n), ";");
        l.s = t;
        l.val = true;
    }
    return l;
}

// A fresh statement-scoped stack for a bytes-class temporary, returned in
// `stk`; the value's base (also the watermark to restore) is the result.
inline string CodeGen::BytesTemp(string &stk) {
    stk = AllocStk(false);
    auto base = T();
    L("uint8_t *", base, " = ", Top(stk), ";");
    SaveBase(false, stk, base);
    return base;
}

// A fresh header -- or frame object -- for a resizable value of type t,
// named by the result, on a fresh statement-scoped stack returned in
// `stk`; the elements are built there next.
inline string CodeGen::RzTemp(TypeExpr *t, string &stk) {
    EmitCoreTypes();
    stk = AllocStk(false);
    auto h = T();
    if (IsFrameObj(t)) {
        L(CT(t), " ", h, ";");
        SaveBase(false, stk, cat(FoTailHdr(t, h), ".base"));
    } else {
        L("gs_rhdr ", h, " = { ", Top(stk), ", 0 };");
        SaveBase(false, stk, cat(h, ".base"));
    }
    return h;
}

// What a construction of type t behind header h writes its count to: the
// whole frame object, or the header's count.
inline string CodeGen::RzLenLv(TypeExpr *t, const string &h) {
    return IsFrameObj(t) ? h : cat(h, ".len");
}

// The value behind such a header as a location.
inline CodeGen::Loc CodeGen::RzTempLoc(TypeExpr *t, const string &h, const string &stk) {
    Loc l;
    l.t = t;
    l.stk = stk;
    l.hdr = h;
    if (IsFrameObj(t)) {
        l.val = true;
        l.s = h;
    } else {
        l.s = cat(h, ".base");
        l.lenlv = cat(h, ".len");
    }
    return l;
}

// A resizable-valued temporary (a call or control result): elements on a
// fresh statement-scoped stack, header in a temp.
inline CodeGen::Loc CodeGen::GenRzTmp(Node *n) {
    auto t = n->exprtype;
    string stk;
    auto h = RzTemp(t, stk);
    GenAny(n, Dst { DK_STACK, stk, t, RzLenLv(t, h) });
    return RzTempLoc(t, h, stk);
}

inline CodeGen::Loc CodeGen::MemberLoc(Loc lv, Dot *d) {
    assert(d->fieldidx >= 0);
    return FieldLocAt(lv, d->fieldidx);
}

inline CodeGen::Loc CodeGen::FieldLocAt(Loc lv, int fieldidx) {
    if (lv.t->kind == TY_STRUCT) {
        auto si = SI(lv.t);
        auto ft = si->ftypes[fieldidx];
        if (lv.val) {
            Loc r = lv;
            r.t = ft;
            r.s = cat(lv.s, ".", Sanitize(si->st->fields[fieldidx].name));
            r.hbase.clear();
            r.hlen.clear();
            r.lenlv.clear();
            if (IsResz(ft)) {
                // A frame object's tail: its own header (or a nested
                // frame object) is the member.
                r.hdr = r.s;
                if (!IsFrameObj(ft)) {
                    r.val = false;
                    r.lenlv = cat(r.s, ".len");
                    r.s = cat(r.s, ".base");
                }
            }
            return r;
        }
        return BytesLoc(FieldPtr(lv.s, si->st->fields, si->ftypes, fieldidx), ft, lv);
    }
    if (lv.t->kind != TY_VARIANT)
        Fail(Line {}, cat("internal: field of a non-struct location of type ", Mangle(lv.t)));
    auto ei = EIVar(lv.t);
    auto vi = VarIdx(ei->en, lv.t->var->variant);
    auto ft = ei->vftypes[vi][fieldidx];
    if (lv.val) {
        Loc r = lv;
        r.t = ft;
        r.s = cat(lv.s, ".", Sanitize(ei->en->variants[vi].fields[fieldidx].name));
        return r;
    }
    return BytesLoc(FieldPtr(lv.s, ei->en->variants[vi].fields, ei->vftypes[vi],
                             fieldidx), ft, lv);
}

// ------------------------------------------------------------------
// Expression values. GenX produces a C expression for fixed-class values
// (possibly after emitting statements); GenPtr produces a byte pointer to
// a bytes-class value. Both follow the node's checked exprtype, which
// already encodes operand unification and reference decay.

inline bool CodeGen::IsCtl(Node *n) {
    return Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<Block>(n) || Is<EarlyBlock>(n) ||
           Is<LoopExpr>(n) || Is<InlineBlock>(n);
}

// The value of a location, as the given expression type expects: storage
// widening is implicit in C, varints decode, relative references load,
// and a reference loc decays to its pointee when the exprtype says so.
inline string CodeGen::LoadLoc(Loc lv, TypeExpr *et, Line ln) {
    if (lv.t->kind == TY_REF && et->kind != TY_REF &&
        !(IsOpt(lv.t) && et->kind == TY_REF)) {
        DerefLoc(lv, ln);
        return LoadLoc(lv, et, ln);
    }
    if (lv.t->kind == TY_REF && lv.t->ref->lenstorage >= 0) {
        // Relative reference load: an ordinary (possibly fat) reference;
        // a zero offset is the null of the optional form (§3.9).
        auto &r = *lv.t->ref;
        string faddr, off;
        RelParts(lv, faddr, off);
        auto org = RelOrigin(lv.t, faddr);
        auto ov = T(), p = T();
        L("int64_t ", ov, " = ", off, ";");
        // Offset 0 is null only for the optional spelling (§3.9); a
        // non-optional slot holds a reference, so the address is the sum.
        if (r.optional) L("uint8_t *", p, " = ", ov, " ? ", org, " + ", ov, " : NULL;");
        else L("uint8_t *", p, " = ", org, " + ", ov, ";");
        // Relative references never point at resizables: pool elements
        // are at most variable-class (§3.3).
        assert(!IsResz(r.sub));
        if (IsBytesT(r.sub)) return p;
        return cat("((", CT(r.sub), " *)", p, ")");
    }
    if (et && et->kind == TY_SLICE && lv.t->kind == TY_ARRAY) {
        // Whole-array argument to a slice parameter (§3.10), any loc form
        // (fixed value or bytes/resizable pointer).
        auto v = ArrayView(lv, ln);
        auto t = T();
        auto dp = v.typedelems && IsBytesT(et->sub)
                      ? cat("(uint8_t *)(", v.elems, ")") : string(v.elems);
        if (!v.typedelems && !IsBytesT(et->sub))
            dp = cat("(", CT(et->sub), " *)(", v.elems, ")");
        L(CT(et), " ", t, " = { ", dp, ", ", v.len, " };");
        return t;
    }
    if (!lv.val) {
        assert(IsBytesT(lv.t));
        if (lv.t->kind == TY_INT)   // varint field read: decode to i64.
            return cat("gs_zig_read(", lv.s, ")");
        if (et && IsFix(et) && !TEq(lv.t, et)) return AdaptToFixed(lv, et, ln);
        return lv.s;   // Bytes value: the pointer is the currency.
    }
    if (et && IsFix(et) && lv.val && lv.t->kind == TY_SLICE && et->kind == TY_ARRAY)
        return AdaptToFixed(lv, et, ln);
    if (lv.ispref && et && et->kind == TY_REF) {
        // A pool reference read as a plain reference drops the freelist.
        auto t = T();
        L("gs_rref ", t, " = { ", lv.s, ".hdr, ", lv.s, ".stk };");
        return t;
    }
    return lv.s;
}

// A fixed C value built from a differently-represented source: a
// variable-mode ADT read into a fixed-mode context, or an array/slice
// into a static-capacity limited array.
inline string CodeGen::AdaptToFixed(Loc lv, TypeExpr *et, Line ln) {
    if (et->kind == TY_ENUM) {
        auto ei = EIOf(et);
        auto ts = TagSize(ei->en);
        auto tv = T();
        L(CT(et), " ", tv, ";");
        L(tv, ".tag = *(", IntCT(TagStore(ei->en)), " *)(", lv.s, ")", ";");
        L("switch (", tv, ".tag) {");
        for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
            auto psz = VariantLayout(ei, (int)vi).size;
            L("case ", TagConst(ei, (int)vi), ":");
            ind++;
            if (psz)
                L("memcpy(&", tv, ".u.v_", Sanitize(ei->en->variants[vi].name), ", ",
                  lv.s, " + ", ts, ", ", psz, ");");
            L("break;");
            ind--;
        }
        L("}");
        return tv;
    }
    assert(et->kind == TY_ARRAY && et->arr->akind == A_LIMITED);
    auto v = ArrayView(lv, ln);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    L("if (", nn, " > ", ArrSize(et->arr),
      ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
    auto tv = T();
    L(CT(et), " ", tv, ";");
    L(tv, ".len = (", IntCT(LenStore(et->arr)), ")", nn, ";");
    L("memcpy(", tv, ".e, ", v.elems, ", (size_t)(", nn, " * ", FixedSize(et->arr->sub),
      "));");
    return tv;
}

inline string CodeGen::BytesAddrOf(const Loc &lv) {
    return lv.val ? cat("(uint8_t *)&", lv.s) : lv.s;
}

// &lvalue (§3.8): the reference value. On a location holding a reference,
// yields the stored reference.
inline string CodeGen::GenRefVal(Node *child, Line ln) {
    // A reference to a resizable value points at its frame header (C.2),
    // so it can only be taken of a whole variable.
    if (auto id = Is<Ident>(child); id && id->vdef && IsResz(id->vdef->type)) {
        auto stk = VStkOf(id->vdef);
        assert(!stk.empty());
        auto t = T();
        L("gs_rref ", t, " = { (gs_rhdr *)&", HdrLv(id->vdef), ", ", stk, " };");
        return t;
    }
    auto lv = GenLoc(child);
    if (lv.t->kind == TY_REF) {
        if (lv.t->ref->lenstorage >= 0) return LoadLoc(lv, nullptr, ln);
        return lv.s;
    }
    if (IsResz(lv.t)) {
        // A frame object's tail (or a nested frame object) has a header
        // of its own to point at.
        if (lv.hdr.empty() || lv.stk.empty())
            Fail(ln, "a reference to a resizable value nested in a variable-size "
                     "prefix or an ADT payload is unsupported; reference the owning "
                     "variable");
        auto t = T();
        L("gs_rref ", t, " = { (gs_rhdr *)&", lv.hdr, ", ", lv.stk, " };");
        return t;
    }
    if (!lv.val) return lv.s;
    return cat("&", lv.s);
}

// GenX, loading the pointee when an optimizer splice left a reference
// where the context's checked type had already decayed.
inline string CodeGen::GenXD(Node *n, TypeExpr *want) {
    if (NeedsDeref(n->exprtype, want)) {
        auto sub = n->exprtype->ref->sub;
        if (want->kind == TY_SLICE && sub->kind == TY_ARRAY) {
            // The pointee array sliced whole (§3.10).
            auto lv = GenLoc(n);
            if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
            return LoadLoc(lv, want, n->line);
        }
        auto x = GenX(n);
        if (IsResz(sub) || IsBytesT(sub)) return x;   // Byte-pointer currency.
        return cat("(*", x, ")");
    }
    return GenX(n);
}

inline string CodeGen::GenTruth(Node *n) {
    auto x = GenX(n);
    auto t = n->exprtype;
    if (t->kind == TY_REF && IsResz(t->ref->sub)) return cat("(", x, ".hdr != 0)");
    return x;
}

// A control construct used as a fixed-class value: route it into a temp.
// A varint-typed value (an inlined call initializing a varint field) is
// held in its decoded i64 form, like every other varint read.
inline string CodeGen::CtlValX(Node *n) {
    auto t = T();
    auto vt = n->exprtype;
    if (vt->kind == TY_INT && vt->intstorage == IS_VARINT) vt = ast.inttypes[IS_I64];
    L(CT(vt), " ", t, ";");
    // The type rides along: a spliced body may deliver a reference where
    // the checked value had already decayed (NeedsDeref).
    GenAny(n, Dst { DK_LVALUE, t, vt });
    return t;
}

// A non-control node's value routed to a destination.
inline void CodeGen::LeafAny(Node *n, const Dst &d) {
    if (d.k == DK_STACK) { GenConstruct(n, d.s, d.t, d.lenlv); return; }
    if (d.k == DK_LVALUE) {
        // A literal holding relative references builds at the destination;
        // assigning it from a temporary would copy the temporary's offsets.
        if ((Is<StructLit>(n) || Is<ArrayLit>(n)) && HasRelRef(n->exprtype))
            FixedLitAt(n, d.s);
        else L(d.s, " = ", GenXD(n, d.t), ";");
        return;
    }
    if (IsVoidT(n->exprtype)) { Fail(n->line, "internal: valueless leaf"); }
    L("(void)(", GenVal(n), ");");
}

inline string CodeGen::GenFixedArrayLit(ArrayLit *al) {
    auto et = al->exprtype;
    if (et->kind == TY_SLICE) {
        // A literal in slice position: a temporary fixed array, sliced
        // whole (§3.10; rooted as a temporary by the checker).
        auto count = al->fillval ? ((IntLit *)al->fillcount)->val
                                 : (int64_t)al->elems.size();
        auto at = ast.NewType(TY_ARRAY, al->line);
        at->arr = ast.NewDetail<TypeArray>();
        at->arr->sub = et->sub;
        at->arr->akind = A_FIXED;
        at->arr->size = count;
        auto save = al->exprtype;
        al->exprtype = at;
        auto av = GenFixedArrayLit(al);
        al->exprtype = save;
        auto t = T();
        L(CT(et), " ", t, " = { ", av, ".e, ", count, " };");
        return t;
    }
    assert(et->kind == TY_ARRAY);
    auto tv = T();
    L(CT(et), " ", tv, ";");
    FixedArrayLitAt(al, tv, false);
    return tv;
}

// Bytes-class value of an expression; optionally reports the data stack
// it lives on (temporaries land on a fresh statement-scoped stack).
// Resizable values carry a header and go through GenLoc/GenRzTmp instead.
inline string CodeGen::GenPtr(Node *n, string *stkout) {
    assert(!IsResz(n->exprtype));
    if (stkout) stkout->clear();
    if (auto id = Is<Ident>(n)) {
        auto lv = VarLoc(id->vdef);
        if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
        if (stkout) *stkout = lv.stk;
        return lv.s;
    }
    if (Is<Dot>(n) || Is<Index>(n)) {
        auto lv = GenLoc(n);
        if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
        if (stkout) *stkout = lv.stk;
        assert(!lv.val);
        return lv.s;
    }
    if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
        // A reference decayed back to a bytes pointee.
        auto lv = GenLoc(u->child);
        if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
        if (stkout) *stkout = lv.stk;
        return lv.s;
    }
    if (auto s = Is<StrLit>(n)) return GenStrBytes(s);
    // Everything else constructs on a fresh temp stack.
    string stk;
    auto base = BytesTemp(stk);
    GenAny(n, Dst { DK_STACK, stk });
    if (stkout) *stkout = stk;
    return base;
}

// mangle+text -> value name.

inline string CodeGen::StrRaw(const string &v) {
    auto it = strdata.find(v);
    if (it != strdata.end()) return it->second;
    auto name = Unique(cat("gs_str", strdata.size()));
    Append(pdata, "static uint8_t ", name, "[", v.size() ? v.size() : 1, "] = ", CStr(v),
           ";\n");
    strdata[v] = name;
    return name;
}

inline string CodeGen::GenStrBytes(StrLit *s) {
    auto et = s->exprtype;
    assert(et->kind == TY_ARRAY);
    auto key = cat(Mangle(et), "|", s->val);
    auto it = strval.find(key);
    if (it != strval.end()) return it->second;
    auto ls = LenStore(et->arr);
    auto name = Unique(cat("gs_strv", strval.size()));
    if (ls == IS_VARINT) {
        uint8_t lenbuf[10];
        uint64_t v = s->val.size();
        string enc;
        auto nn = 0;
        do {
            lenbuf[nn] = v & 0x7f;
            v >>= 7;
            if (v) lenbuf[nn] |= 0x80;
            nn++;
        } while (v);
        enc.assign((char *)lenbuf, nn);
        enc += s->val;
        Append(pdata, "static uint8_t ", name, "[", enc.size(), "] = ", CStr(enc),
               ";\n");
    } else {
        Append(pdata, "static struct { ", IntCT(ls), " len; uint8_t s[",
               s->val.size() ? s->val.size() : 1, "]; } ", name, " = { ",
               s->val.size(), ", ", CStr(s->val), " };\n");
    }
    auto expr = cat("((uint8_t *)&", name, ")");
    strval[key] = expr;
    return expr;
}

// ------------------------------------------------------------------
// Binary operators. Operand exprtypes are already decayed and unified.

// Operand exprtype as an operator sees it: a spliced reference reads as
// its pointee.
inline TypeExpr *CodeGen::OperandT(TypeExpr *t) {
    if (t && t->kind == TY_REF && !t->ref->optional && t->ref->lenstorage < 0)
        return t->ref->sub;
    return t;
}

// GenX or GenPtr per the operand's rep; operators see spliced references
// as their pointees.
inline string CodeGen::GenVal(Node *n) {
    auto et = OperandT(n->exprtype);
    if (et != n->exprtype) return GenXD(n, et);   // Bytes pointees share the currency.
    return IsBytesT(et) ? GenPtr(n) : GenX(n);
}

inline string CodeGen::GenPureVal(Node *n) {
    auto et = OperandT(n->exprtype);
    if (et != n->exprtype) return GenXD(n, et);
    if (IsBytesT(et)) return GenPtr(n);
    return GenPure(n);
}

inline bool CodeGen::HasStmts(Node *n) {
    if (!n) return false;
    if (Is<Call>(n) || IsCtl(n) || Is<StrLit>(n)) return true;
    auto found = false;
    n->Children([&](Node *c) { found = found || HasStmts(c); });
    return found;
}

// Equality of two direct operands (§4.5): a slice compares length then
// elements here, where as a member it compares by identity (EqX).
inline string CodeGen::GenEquality(TypeExpr *lt, const string &l, const string &r) {
    if (lt->kind == TY_SLICE) return GenSliceEq(lt, l, r);
    return EqX(lt, l, r);
}

inline string CodeGen::GenSliceEq(TypeExpr *st, const string &l, const string &r) {
    return GenRangeEq(st->sub, cat(l, ".data"), cat(l, ".len"), cat(r, ".data"),
                      cat(r, ".len"));
}

// Structural equality of two element ranges (§4.5): length then elements.
inline string CodeGen::GenRangeEq(TypeExpr *elem, const string &ae, const string &an,
                                  const string &be, const string &bn) {
    auto t = T();
    L("uint8_t ", t, " = ", an, " == ", bn, ";");
    L("if (", t, ") {");
    ind++;
    if (ScalarEq(elem) && GapFree(elem)) {
        L(t, " = memcmp(", ae, ", ", be, ", (size_t)((", an, ") * ", FixedSize(elem),
          ")) == 0;");
    } else if (IsFix(elem)) {
        auto pa = T(), pb = T(), iv = T();
        L("const ", CT(elem), " *", pa, " = (const ", CT(elem), " *)(", ae, ");");
        L("const ", CT(elem), " *", pb, " = (const ", CT(elem), " *)(", be, ");");
        L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < (", an, "); ", iv, "++)");
        L("    ", t, " = ", EqX(elem, cat(pa, "[", iv, "]"), cat(pb, "[", iv, "]")),
          ";");
    } else {
        // Variable elements: parallel cursor walk.
        auto pa = T(), pb = T(), iv = T();
        L("const uint8_t *", pa, " = (const uint8_t *)(", ae, "), *", pb,
          " = (const uint8_t *)(", be, ");");
        L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < (", an, "); ", iv, "++) {");
        ind++;
        L(t, " = ", EqFn(elem), "(", pa, ", ", pb, ");");
        L(pa, " += ", SizeX(elem, pa), "; ", pb, " += ", SizeX(elem, pb), ";");
        ind--;
        L("}");
    }
    ind--;
    L("}");
    return t;
}

// Elementwise arithmetic (§6.1), expanded member by member with direct
// loads and stores — no whole-struct temporaries, which backends fail to
// scalarize. Member i of the result depends only on member i of each
// operand, so writing members straight into `dst` is exact even when it
// aliases an operand (the p.vel = p.vel + g shape).
inline void CodeGen::GenElemwiseInto(Binary *b, const string &l, const string &r,
                                     const string &dst) {
    function<void(TypeExpr *, const string &)> rec = [&](TypeExpr *tt, const string &path) {
        switch (tt->kind) {
            case TY_INT: case TY_FLT: {
                auto a = cat("(", l, ")", path), c = cat("(", r, ")", path);
                string x;
                if (tt->kind == TY_FLT) {
                    switch (b->op) {
                        case T_PLUS:  x = cat("(", a, " + ", c, ")"); break;
                        case T_MINUS: x = cat("(", a, " - ", c, ")"); break;
                        case T_MUL:   x = cat("(", a, " * ", c, ")"); break;
                        case T_DIV:   x = cat("(", a, " / ", c, ")"); break;
                        default:      x = cat((tt->fltstorage == FS_F32 ? "fmodf(" : "fmod("),
                                              a, ", ", c, ")"); break;
                    }
                } else {
                    auto sfx = IntSfx(tt->intstorage);
                    switch (b->op) {
                        case T_PLUS:  x = cat("gs_add_", sfx, "(", a, ", ", c, ")"); break;
                        case T_MINUS: x = cat("gs_sub_", sfx, "(", a, ", ", c, ")"); break;
                        case T_MUL:   x = cat("gs_mul_", sfx, "(", a, ", ", c, ")"); break;
                        case T_DIV:   x = cat("gs_div_", sfx, "(", a, ", ", c, ", ",
                                              LocArgs(b->line), ")"); break;
                        default:      x = cat("gs_mod_", sfx, "(", a, ", ", c, ", ",
                                              LocArgs(b->line), ")"); break;
                    }
                }
                L(dst, path, " = ", x, ";");
                return;
            }
            case TY_STRUCT: {
                auto si = SI(tt);
                for (size_t i = 0; i < si->st->fields.size(); i++)
                    if (!si->st->fields[i].ispad)
                        rec(si->ftypes[i],
                            cat(path, ".", Sanitize(si->st->fields[i].name)));
                return;
            }
            case TY_ARRAY: {
                auto iv = T();
                L("for (int64_t ", iv, " = 0; ", iv, " < ", ArrSize(tt->arr), "; ", iv,
                  "++) {");
                ind++;
                rec(tt->arr->sub, cat(path, ".e[", iv, "]"));
                ind--;
                L("}");
                return;
            }
            default: assert(false); return;
        }
    };
    // Struct field paths start at the value; array paths at .e — handled
    // uniformly since the outer type is one of the two.
    rec(b->exprtype, "");
}

// Left-to-right evaluation (§2): when the right operand needs statements,
// the left's value must be snapshotted before they run.
inline void CodeGen::ElemwiseOperands(Binary *b, string &l, string &r) {
    if (HasStmts(b->right)) {
        l = GenVal(b->left);
        auto lt = T();
        L(CT(b->exprtype), " ", lt, " = ", l, ";");
        l = lt;
        r = GenVal(b->right);
    } else {
        l = GenVal(b->left);
        r = GenVal(b->right);
    }
}

inline string CodeGen::GenElemwise(Binary *b, const string &l, const string &r) {
    auto tv = T();
    L(CT(b->exprtype), " ", tv, ";");
    GenElemwiseInto(b, l, r, tv);
    return tv;
}

inline string CodeGen::GenSlice(SliceExpr *se) {
    auto lv = GenLoc(se->obj);
    if (lv.t->kind == TY_REF) DerefLoc(lv, se->line);
    auto v = ArrayView(lv, se->line);
    auto lenv = T();
    L("int64_t ", lenv, " = ", v.len, ";");
    auto bound = [&](Node *b, bool fromend, const char *dflt) -> string {
        if (!b) return dflt;
        auto x = GenX(b);
        if (fromend) return cat("(", lenv, " - (", x, "))");
        return x;
    };
    auto lo = bound(se->lo, se->lo_from_end, "0");
    auto hi = bound(se->hi, se->hi_from_end, lenv.c_str());
    auto lov = T(), hiv = T();
    L("int64_t ", lov, " = ", lo, ", ", hiv, " = ", hi, ";");
    if (!se->nobc)
        L("if (", lov, " < 0 || ", hiv, " < ", lov, " || ", hiv, " > ", lenv,
          ") gs_abort(GS_E_SLICE, ", LocArgs(se->line), ");");
    auto t = T();
    // Adapted contexts (slice constructing an array) leave an array
    // exprtype on the node; the slice value's own type comes from the
    // source element.
    auto et = se->exprtype->kind == TY_SLICE ? se->exprtype : MakeSliceT(v.elem, se->line);
    if (IsFix(v.elem)) {
        auto dp = v.typedelems ? v.elems
                               : cat("(", CT(v.elem), " *)(", v.elems, ")");
        L(CT(et), " ", t, " = { ", dp, " + ", lov, ", ", hiv, " - ", lov, " };");
    } else {
        // Whole-array slices only (TC): elements are sequential bytes.
        L(CT(et), " ", t, " = { ", v.elems, ", ", lenv, " };");
    }
    return t;
}

}  // namespace goose
