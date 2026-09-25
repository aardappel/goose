// Goose compiler — the typechecker's builtins (definitions of TypeCheck
// members, typecheck.h): the builtin functions and array members (§3.3,
// §3.7, §5.4, §11.2), text rendering through user `format` overloads, and
// the shrink rules of §5.1 and §5.2.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

// One entry for every builtin (builtins.h), for both spellings — f(a, b)
// and a.f(b) arrive with a uniform argument list (receiver first). The
// table drives arity, receiver kind/provenance, and simple signatures;
// BF_CUSTOM entries get dedicated code below.
inline Val TypeCheck::CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv) {
    c->builtin = d.kind;
    if (c->trailing) Error(c, cat(d.name, " takes no function value"));
    if (!(d.flags & BF_TYARGS) && !c->tyargs.empty())
        Error(c, cat(d.name, " takes no type arguments"));
    if ((int)args.size() < d.minargs || (int)args.size() > d.maxargs)
        Error(c, cat(d.name, " takes ", (int64_t)d.minargs,
                     d.minargs == d.maxargs ? string() : cat("-", (int64_t)d.maxargs),
                     " argument(s), ", (int64_t)args.size(), " given"));
    // The fully custom builtins first.
    switch (d.kind) {
        case B_PRINT:
            for (size_t i = 0; i < args.size(); i++) CheckPrintable(c, d.name, args, i);
            return VoidVal();
        case B_STR: {
            // str(a, b, ...): a fresh u8[>..] holding the arguments' text,
            // built at the destination like any resizable result (§7.3).
            for (size_t i = 0; i < args.size(); i++) CheckPrintable(c, d.name, args, i);
            auto t = GrowU8Array(c->line);
            c->rettypes.push_back(t);
            Val v;
            v.type = t;
            v.Set(TempRoot(), false);
            return v;
        }
        case B_ASSERT:
            CheckCond(args[0]);
            NarrowCond(args[0], true);  // assert(r) narrows onwards (§3.8).
            return VoidVal();
        case B_ABORT: case B_EXIT:
            // Both end the program (§9.3), so the code after them is
            // unreachable: this is what lets a `guard ... else` diverge
            // with an abort (§6.4).
            if (d.kind == B_ABORT) CheckArg(args[0], u8slice);
            else CheckIntAny(args[0]);
            reachable = false;
            return VoidVal();
        case B_THREAD_SPAWN: {
            auto wid = Is<Ident>(args[0]);
            SFunction *wsf = nullptr;
            if (wid)
                for (auto sf : ast.LookupFunctions(wid->name, wid->ns))
                    if (sf->isthread) wsf = sf;
            if (!wsf) Error(c, "thread_spawn's first argument names a thread_fn");
            wid->fnref = wsf;
            wid->exprtype = fntype;
            auto spec = EnsureThreadSpec(wsf, c->line);
            if (args.size() != 1 + spec->argtypes.size())
                Error(c, cat("thread_spawn(", wsf->name, ", ...) takes ",
                             (int64_t)spec->argtypes.size(), " worker argument(s)"));
            {
                DestScope ds(*this, Dest {});
                for (size_t i = 0; i < spec->argtypes.size(); i++)
                    CheckArg(args[1 + i], spec->argtypes[i]);
            }
            c->spec = spec;
            Val v;
            v.type = ast.inttypes[IS_I64];
            return v;
        }
        case B_QPUT: {
            auto av = CheckValue(args[0], nullptr);
            if (av.type->kind == TY_VOID || av.type->kind == TY_FN || !IsFlat(av.type))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(av.type)));
            return VoidVal();
        }
        case B_QGET: case B_QPOLL: {
            if (c->tyargs.size() != 1)
                Error(c, cat(d.name, "<T>() needs exactly one explicit type argument"));
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            if (!IsFlat(t))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(t)));
            c->rettypes.push_back(t);
            Val first;
            first.type = t;
            first.Set(TempRoot(), false);
            lastcallrets.clear();
            lastcallrets.push_back(first);
            if (d.kind == B_QPOLL) {
                Val b2;
                b2.type = ast.booltype;
                c->rettypes.push_back(ast.booltype);
                lastcallrets.push_back(b2);
            }
            return first;
        }
        case B_EMBED_SHADER: {
            // The shader compiled now, and the result a read-only view of
            // static data, like a string literal's (stdlib/gfx.goose).
            c->shaderblob = EmbedShader(c, args);
            c->rettypes.push_back(cu8slice);
            Val v;
            v.type = cu8slice;
            v.Set(nullptr, true);   // Static data owns what it holds.
            v.writable = false;
            return v;
        }
        case B_COPY: {
            // copy(x): a fresh value from stored one (§4.1), for the
            // destinations that never copy implicitly.
            auto av = CheckV(args[0], nullptr);
            args[0]->exprtype = av.type;
            auto v = DecayRef(av);
            if (v.type->kind == TY_SLICE)
                Error(c, "copy takes a value or a reference, not a slice");
            if (!av.lvalue && !IsPlainRef(av.type))
                Error(c, "copy of a temporary: the value is fresh already");
            if ((v.type->kind == TY_ENUM || v.type->kind == TY_VARIANT) &&
                ClassOf(v.type) == SC_RESIZABLE)
                Error(c, "copying a resizable ADT or variant is not supported by the "
                         "C backend yet; construct a fresh value or pass the owning "
                         "value by reference");
            v.lvalue = false;
            // Keep the copy node and its own storage root through every
            // argument check. Its contents still borrow from the source.
            if (!v.holderset && HoldsPlainRef(v.type)) {
                v.contents = v;
                v.contents.Weaken();
                v.holderfrom = IsTemp(v.Root()) ? nullptr : v.Root();
                v.holderset = true;
            }
            v.Set(TempRoot(), true);
            v.writable = false;
            c->rettypes.push_back(v.type);
            return v;
        }
        case B_FROM_BYTES: {
            // from_bytes<T[>..]>(bytes): the image verified and copied into
            // a fresh array (docs/design/serialization.md §4). The result is
            // rooted at its own variable like any resizable one, so nothing
            // in §9 has to know it came from outside; the bool is the
            // verifier's verdict, and a rejected image leaves the array empty.
            if (c->tyargs.size() != 1)
                Error(c, "from_bytes<T[>..]>(bytes) needs exactly one explicit type argument");
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            // The kinds whose contents are exactly an element run plus a
            // count: a resizable's count lives in its header, a variable
            // array's in a length prefix the construction writes. A fixed or
            // limited array would need the count to match a capacity the
            // image does not carry, so those are rejected.
            auto ak = t->kind == TY_ARRAY ? t->arr->akind : A_FIXED;
            if (t->kind != TY_ARRAY || (ak != A_GROW && ak != A_GROWSHRINK && ak != A_VAR))
                Error(c, cat("from_bytes builds a resizable or variable array "
                             "(T[>..], T[>..<], T[]), not ", TypeStr(t)));
            auto el = t->arr->sub;
            if (el->kind == TY_VOID) Error(c, "from_bytes needs a known element type");
            string why;
            if (!VerifiableElem(el, el, why))
                Error(c, cat("from_bytes<", TypeStr(t), "> has no verifier: ", why));
            CheckArg(args[0], u8slice);
            c->rettypes.push_back(t);
            c->rettypes.push_back(ast.booltype);
            Val first;
            first.type = t;
            first.Set(TempRoot(), false);
            Val ok;
            ok.type = ast.booltype;
            lastcallrets.clear();
            lastcallrets.push_back(first);
            lastcallrets.push_back(ok);
            return first;
        }
        case B_DEFAULT: {
            // default<T>(): the value a T has before anything is written
            // to it, declared field defaults applied (§4.2).
            if (c->tyargs.size() != 1)
                Error(c, "default<T>() needs exactly one explicit type argument");
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            if (ClassOf(t) != SC_FIXED)
                Error(c, cat("default<T>() needs a fixed-size type, not ", TypeStr(t)));
            string why;
            if (!HasDefault(t, why))
                Error(c, cat("default<", TypeStr(t), ">() does not exist: ", why));
            c->rettypes.push_back(t);
            if (!c->defaultinit) {
                if (t->kind == TY_STRUCT || t->kind == TY_ENUM || t->kind == TY_VARIANT) {
                    auto st = t->kind == TY_ENUM ? VariantTypeOf(t, &t->enu->en->variants[0], c->line) : t;
                    auto sl = ast.New<StructLit>(c->line, st);
                    sl->defaultall = true;
                    c->defaultinit = sl;
                } else if (t->kind == TY_ARRAY && t->arr->akind == A_FIXED && ArraySize(t->arr))
                    c->defaultinit = DefaultCall(t->arr->sub, c->line);
            }
            if (c->defaultinit) {
                auto v = CheckValue(c->defaultinit, t->kind == TY_ARRAY ? t->arr->sub : t);
                v.type = t;
                // An array is filled with its element's default in a
                // temporary of its own, as a literal is built (§9.2).
                if (t->kind == TY_ARRAY) v = TempCopy(v);
                lastcallrets = { v };
                return v;
            }
            Val v;
            v.type = t;
            v.Set(nullptr, true);   // A null optional or an empty slice: static.
            v.writable = true;      // And nothing to write, so it fits any slot (§9.5).
            return v;
        }
        default: break;
    }
    // Member receiver validation, from the table.
    Val rv;
    TypeExpr *elem = nullptr;
    auto ak = A_FIXED;
    if (d.flags & BF_MEMBER) {
        if (precv) {
            rv = *precv;
        } else {
            rv = CheckV(args[0], nullptr);
            args[0]->exprtype = rv.type;
        }
        auto rt = rv.type;
        if (IsPlainRef(rt)) {
            rt = rt->ref->sub;
            // As DerefLValue.
            rv.ClearSlotRead();
            rv.reached = LoadType(rt);
        }
        if (rt->kind == TY_ARRAY) {
            ak = rt->arr->akind;
            elem = rt->arr->sub;
        } else if (rt->kind == TY_SLICE) {
            elem = rt->sub;
        }
        if (!(RecvKindOf(rt) & d.recv))
            Error(c, cat(".", d.name, " is not available on ", TypeStr(rv.type)));
        if ((d.flags & BF_WRITE) && !rv.writable)
            Error(c, cat("cannot .", d.name, " through a non-writable value "
                         "(let, const, or a read-only instantiation, §9.5)"));
        if ((d.flags & BF_REUSABLE) && !(rv.reusable & RU_SLOTS))
            Error(c, cat(".", d.name, " exists on reusable pools only",
                         rv.reusable ? ", not on the slice pools of reusable[]" : "", " (§5.4)"));
        if ((d.flags & BF_SLICEPOOL) && !(rv.reusable & RU_SLICES))
            Error(c, cat(".", d.name, " exists on reusable[] pools only",
                         rv.reusable ? ", not on the slot pools of reusable" : "", " (§5.4)"));
    }
    // A pending `var x = []` receiver learns its element type from what
    // is first pushed or appended into it (§4.2).
    if (elem && elem->kind == TY_VOID) {
        auto rt = rv.type;
        if (IsPlainRef(rt)) rt = rt->ref->sub;
        if (d.kind == B_PUSH || d.kind == B_ALLOC_INDEX || d.kind == B_ALLOC_REF) {
            auto av = DecayRef(CheckV(args[1], nullptr));
            CompletePending(rt, PendingElemFrom(av, args[1]), c->line);
        } else if (d.kind == B_APPEND) {
            auto av = DecayRef(CheckV(args[1], nullptr));
            CompletePending(rt, PendingElemFromSeq(av, c), c->line);
        } else if (d.kind == B_FORMAT) {
            CompletePending(rt, ast.inttypes[IS_U8], c->line);
        } else {
            RequireComplete(rt, c->line);
        }
        elem = rt->arr->sub;
    }
    // The receiver grows (§1.3(4)): logged ahead of the arguments, so that
    // a value built in place among them is checked against the growths
    // within it alone.
    if (d.kind == B_PUSH || d.kind == B_APPEND || d.kind == B_ALLOC_INDEX ||
        d.kind == B_ALLOC_REF || d.kind == B_ALLOC_SLICE || d.kind == B_REALLOC_SLICE ||
        d.kind == B_FORMAT || d.kind == B_RESIZE) {
        auto how = d.kind == B_PUSH ? "push into " : d.kind == B_APPEND ? "append to "
                 : d.kind == B_FORMAT ? "format into " : d.kind == B_RESIZE ? "resize "
                 : "allocate in ";
        NoteGrow(c, rv, cat(how, ExprStr(args[0])));
    }
    // The serialization pair (docs/design/serialization.md §4). to_bytes
    // builds the image -- a varint byte count then the element region --
    // either as a fresh u8[>..] or appended to a builder the caller owns, so
    // its own header can go in front. bytes_of is the element region alone,
    // as a view: no copy, and no framing of its own.
    if (d.kind == B_TO_BYTES || d.kind == B_BYTES_OF) {
        string why;
        if (!ImageSafe(elem, why))
            Error(c, cat(d.name, " cannot write ", TypeStr(rv.type), " out: ", why));
        if (d.kind == B_BYTES_OF) {
            NoTemporaryLiteral(args[0], rv.type);
            Val v;
            v.type = cu8slice;   // A read-only view, in its type too (§9.5).
            v.SetProv(rv);
            // The bytes of a live structure: reading them is what they are
            // for, and writing them would forge the relative references the
            // checker otherwise proves (§3.9), so the view is never writable.
            v.writable = false;
            v.reusable = false;
            v.byteview = true;
            c->rettypes.push_back(v.type);
            return v;
        }
        if (args.size() == 2) {
            auto ov = CheckV(args[1], nullptr);
            args[1]->exprtype = ov.type;
            auto ot = ov.type;
            if (IsPlainRef(ot)) ot = ot->ref->sub;
            auto ok = ot->kind == TY_ARRAY && IsU8(ot->arr->sub) &&
                      (ot->arr->akind == A_GROW || ot->arr->akind == A_GROWSHRINK ||
                       ot->arr->akind == A_LIMITED);
            if (!ok)
                Error(c, cat("to_bytes(a, out) appends to a growable u8 array, not ",
                             TypeStr(ov.type)));
            if (!ov.writable)
                Error(c, "cannot append through a non-writable value (let, or "
                         "non-writable provenance, §9.5)");
            NoteGrow(c, ov, cat("append to ", ExprStr(args[1])));
            return VoidVal();
        }
        auto t = GrowU8Array(c->line);
        c->rettypes.push_back(t);
        Val v;
        v.type = t;
        v.Set(TempRoot(), false);
        return v;
    }
    // format(out, a, b, ...): the arguments' text appended to a growable
    // u8 array (§3.7).
    if (d.kind == B_FORMAT) {
        if (!IsU8(elem))
            Error(c, cat(".format appends text to u8 arrays, not ", TypeStr(rv.type)));
        for (size_t i = 1; i < args.size(); i++) CheckPrintable(c, d.name, args, i, &rv);
        return VoidVal();
    }
    // A grow-only array shrinks only where nothing can still be rooted in
    // it (§5.1); pop and resize also need an element the shrink can find,
    // which a sequential array has not got.
    if (ak == A_GROW && (d.kind == B_POP || d.kind == B_RESIZE || d.kind == B_CLEAR)) {
        CheckGrowShrink(c, c->standalone, d.name, args[0], rv);
        if (d.kind != B_CLEAR && ClassOf(elem) != SC_FIXED)
            Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
    }
    // A grow-shrink array shrinks from anywhere, provided nothing in scope
    // refers into it (§5.2).
    if (ak == A_GROWSHRINK && (d.kind == B_POP || d.kind == B_RESIZE || d.kind == B_CLEAR))
        ShrinkThrough(c, c->standalone, d.name, ExprStr(args[0]), rv,
                      IsPlainRef(rv.type) ? rv.type->ref->sub : rv.type,
                      d.kind == B_RESIZE && ResizesToMark(args[0], args[1]) ? SB_BALANCED
                                                                            : SB_UNBALANCED);
    // resize has two forms (§3.3); a target below zero is caught at runtime.
    if (d.kind == B_RESIZE) {
        CheckIntAny(args[1]);
        if (args.size() == 3) {
            ElemArg(args[2], elem, rv);
            // The fill value is built once and copied into every slot the
            // resize adds, so not even a literal is built in place (§3.9).
            if (HasRelRefT(elem) && (Is<StructLit>(args[2]) || Is<ArrayLit>(args[2])))
                Error(args[2], cat(".resize copies its fill value into every slot it adds: "
                                   "copying a value of type ", TypeStr(elem), ", which "
                                   "contains self-relative references, is not supported; push "
                                   "the elements, which constructs each in place"));
        }
        return VoidVal();
    }
    // index_of recovers the element index a reference stands for (§3.3).
    // The reference must be an element of this very array, which is what
    // an exact root at the receiver says (§9.2); the distance is then a
    // whole number of elements and inside the length, so the division is
    // exact and nothing has to be bounds-checked.
    if (d.kind == B_INDEX_OF) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, cat(".index_of needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
        auto av = CheckV(args[1], nullptr);
        if (av.lvalue && av.type->kind != TY_REF && TypeEq(av.type, elem))
            args[1] = AutoRef(args[1], av);
        else if (UserRefOf(args[1]))
            Warn(args[1], cat("redundant &: ", ExprStr(Is<Unary>(args[1])->child),
                              " is passed by reference without it (§4.1)"));
        args[1]->exprtype = av.type;
        if (!IsPlainRef(av.type) || !TypeEq(av.type->ref->sub, elem))
            Error(c, cat(".index_of takes a reference to an element of ", TypeStr(rv.type),
                         ", got ", TypeStr(av.type)));
        CheckRootedAtReceiver(c, d.name, rv, av, "a reference", "§3.3");
    }
    // A slice pool's operations (§5.4). A slice handed back must be one of
    // the pool's own, by the same exact root index_of needs, so that the
    // position it starts at is a whole index inside the length; the
    // elements an operation adds are default values.
    if (d.kind == B_ALLOC_SLICE || d.kind == B_REALLOC_SLICE || d.kind == B_FREE_SLICE) {
        if (d.kind != B_ALLOC_SLICE) {
            auto sv = CheckV(args[1], nullptr);
            args[1]->exprtype = sv.type;
            if (sv.type->kind != TY_SLICE || !TypeEq(sv.type->sub, elem))
                Error(c, cat(".", d.name, " takes a slice of ", TypeStr(rv.type), ", got ",
                             TypeStr(sv.type)));
            if (!RootedAtReceiver(rv, sv)) {
                // Globals and this function's own variables are separate storage,
                // so a slice exactly rooted at another one is not the pool's.
                // Any other slice may be, and is checked when the call runs.
                auto own = [&](VarDef *r) {
                    return r && (r->isglobal || r->ownerspec == CurRealFrame().spec);
                };
                if (sv.Exact() && own(sv.Root()) && own(rv.Root()))
                    Error(c, cat(".", d.name, " needs a slice of the pool it is called on (§5.4); "
                                 "this one is rooted at ", sv.Root()->name));
                c->poolcheck = true;
            }
        }
        if (d.kind != B_FREE_SLICE) {
            CheckIntAny(args.back());
            string why;
            if (!HasDefault(elem, why))
                Error(c, cat(".", d.name, " fills the elements it adds with default values, "
                             "and ", TypeStr(elem), " has none: ", why, " (§5.4)"));
            if (!c->defaultinit) c->defaultinit = DefaultCall(elem, c->line);
            auto logbase = growlog.size();
            ElemArg(c->defaultinit, elem, rv);
            CheckGrowsSince(logbase, rv, "the default elements allocated in a slice pool");
        }
        // Growing a slice may move it, and a moved element's self-relative
        // offsets would still measure from where it was.
        if (d.kind == B_REALLOC_SLICE && HasRelRefT(elem))
            Error(c, cat(".realloc_slice may move the slice, which a value of type ",
                         TypeStr(elem), " cannot survive: it contains self-relative "
                         "references (§3.9)"));
    }
    // Signature-driven arguments.
    auto base = (d.flags & BF_MEMBER) ? 1 : 0;
    for (auto i = 0; d.args[i]; i++) {
        auto &an = args[base + i];
        switch (d.args[i]) {
            case 'i': CheckIntAny(an); break;
            case 'f': CheckValue(an, ast.flttypes[FS_F64]); break;
            case 'b': CheckValue(an, ast.booltype); break;
            case 'e': {
                // An element built in place is under construction while
                // its expression runs (§1.3(4)).
                auto logbase = growlog.size();
                ElemArg(an, elem, rv);
                if (BuiltInPlace(elem))
                    CheckGrowsSince(logbase, rv,
                                    cat("the element ",
                                        d.kind == B_PUSH ? "pushed into " : "allocated in ",
                                        ExprStr(args[0])));
                break;
            }
            case 'a': {  // An array/slice of the receiver's element type.
                auto logbase = growlog.size();
                // An array literal is the run appended: its elements are the
                // receiver's, constructed into its storage (§4.2).
                auto al = Is<ArrayLit>(an);
                if (al && al->capexpr) al = nullptr;
                auto av = al ? CheckValueAt(an, AppendedRun(elem, al), Dest(rv, false, rv.reached))
                             : CheckV(an, nullptr);
                an->exprtype = av.type;
                auto t2 = av.type;
                if (IsPlainRef(t2)) t2 = t2->ref->sub;
                TypeExpr *selem = nullptr;
                if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
                if (t2->kind == TY_SLICE) selem = t2->sub;
                if (av.strlit) selem = ast.inttypes[IS_U8];
                if (!selem || !TypeEq(selem, elem))
                    Error(c, cat(".", d.name, " takes an array or slice of ",
                                 TypeStr(elem), ", got ", TypeStr(av.type)));
                if (!al) AppendedCopies(an, av, elem, rv);
                // A call's array result is built at the receiver's top
                // (§7.3), and a literal's run is built in place where its
                // elements are not fixed-size or hold relative references of
                // either form (at the top, or in a limited array's free
                // slots): under construction while the call or the elements
                // run (§1.3(4)).
                auto inplace = al ? ClassOf(elem) != SC_FIXED || HasRelRefT(elem, true)
                                  : Is<Call>(an) && ak != A_LIMITED && ClassOf(t2) != SC_FIXED;
                if (inplace)
                    CheckGrowsSince(logbase, rv, cat("the run appended to ", ExprStr(args[0])));
                break;
            }
            default: assert(false);
        }
    }
    // Returns, from the table.
    Val v = VoidVal();
    switch (d.rets[0]) {
        case 0: break;
        case 'i': v.type = ast.inttypes[IS_I64]; break;
        case 'b': v.type = ast.booltype; break;
        case 'e':
            v.type = LoadType(elem);
            v.Set(TempRoot(), false);
            if (HoldsPlainRef(v.type)) {
                // The element leaves as a temporary, holding what it held in
                // the receiver, as an element read would (ContainerRead).
                v.contents = rv;
                v.contents.Weaken();
                v.holderset = true;
                v.holderfrom = rv.Root();
            }
            // What an adapting receiver (the element's ADT, say) constructs from.
            c->rettypes.push_back(v.type);
            break;
        case 'r':
            v.type = RefTo(elem, c->line);
            v.TakeAlts(rv);
            v.ClearSlotRead();
            v.writable = rv.writable;
            // What a receiver that decays the reference loads through.
            c->rettypes.push_back(v.type);
            break;
        case 's':
            v.type = SliceOf(elem, c->line);
            v.TakeAlts(rv);
            v.ClearSlotRead();
            v.writable = rv.writable;
            // What an adapting receiver (a limited array) constructs from.
            c->rettypes.push_back(v.type);
            break;
        default: assert(false);
    }
    return v;
}

// An argument of print/str/format (§3.7): every value type has a text
// form -- scalars and bool as text, u8 arrays and slices as their bytes
// (quoted inside an aggregate), other arrays as [a, b], structs and
// variants as their positional literal, references as their pointee,
// null as null. A user overload fn format(out: u8[>..]&, v: T) renders a
// T instead wherever one occurs; its specialization is recorded on the
// call for codegen. The arguments are evaluated and rendered in order, each
// just before its text (EmitFormatInto, EmitStr), so the ones after
// argument i use what they name after whatever it shrinks.
inline void TypeCheck::CheckPrintable(Call *c, const char *what, vector<Node *> &args, size_t i,
                                      const Val *out) {
    auto &a = args[i];
    auto av = CheckValue(a, nullptr);
    // Rendered now (HeldOperands).
    auto saverender = tuple(renderarg, renderwhere, rendering);
    renderarg = a;
    renderwhere = nullptr;
    rendering = what;
    struct Restore {
        TypeCheck &tc;
        tuple<Node *, Node *, const char *> saved;
        ~Restore() {
            tc.renderarg = get<0>(saved);
            tc.renderwhere = get<1>(saved);
            tc.rendering = get<2>(saved);
        }
    } restore { *this, saverender };
    Val builder;
    builder.Set(TempRoot(), true);
    builder.writable = true;
    if (out && ClassOf(DecayRef(*out).type) == SC_RESIZABLE) builder = *out;
    auto context = ast.New<Call>(c->line, c->callee);
    context->standalone = c->standalone;
    vector<TypeExpr *> seen;
    CheckRenderable(context, what, av.type, a, seen, av, builder);
    c->fmtcontexts.push_back(context);
    c->fmtspecs.insert(c->fmtspecs.end(), context->fmtspecs.begin(), context->fmtspecs.end());
}

inline void TypeCheck::CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at,
                                       vector<TypeExpr *> &seen, Val value, const Val &out) {
    for (auto s : seen) if (TypeEq(s, t)) return;   // Recursion through references.
    seen.push_back(t);
    struct PopSeen { vector<TypeExpr *> &types; ~PopSeen() { types.pop_back(); } } pop { seen };
    value.type = t;
    value.writable &= !t->cq;
    if (UserFormat(c, t, value, out)) return;
    // The argument itself, unless an overload takes it whole, is read where
    // it lies, around the overloads its parts run, so that storage stays in
    // use meanwhile (HeldOperands, the argument being rendered).
    if (seen.size() == 1 && (t->kind == TY_STRUCT || t->kind == TY_ENUM ||
                             t->kind == TY_VARIANT || t->kind == TY_ARRAY))
        renderwhere = at;
    auto child = [&](TypeExpr *ft, bool throughref) {
        auto v = value;
        if (throughref) {
            ReadBack contents;
            auto hascontents = TempContents(value, contents);
            v.TakeAlts(ReadBackRoot(ft, value, value.byteview, hascontents ? &contents : nullptr));
        }
        CheckRenderable(c, what, ft, at, seen, v, out);
    };
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: return;
        case TY_ARRAY: child(t->arr->sub, IsRefOrSlice(t->arr->sub)); return;
        case TY_SLICE: child(t->sub, IsRefOrSlice(t->sub)); return;
        case TY_REF: child(t->ref->sub, false); return;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            EachField(t, [&](TypeExpr *ft) { child(ft, IsRefOrSlice(ft)); });
            return;
        default:
            Error(at, cat(what, " cannot render a value of type ", TypeStr(t)));
    }
}

// The user's `format` overload for t, instantiated for a builder rooted
// anywhere and a T by value or by reference (the two parameter shapes
// such an overload takes), once per print call. The overloads tried are
// those of the type's own namespace, then the global ones: rendering
// follows the type, not the namespace of whoever prints it
// (docs/design/namespaces.md).
inline FnSpec *TypeCheck::UserFormat(Call *c, TypeExpr *t, const Val &value, const Val &out) {
    auto tns = NominalNs(t);
    if (auto sp = UserFormatIn(c, t, tns, value, out)) return sp;
    return tns.empty() ? nullptr : UserFormatIn(c, t, {}, value, out);
}

inline FnSpec *TypeCheck::UserFormatIn(Call *c, TypeExpr *t, string_view ns,
                                      const Val &value, const Val &out) {
    auto n = ast.FindNS(ns);
    if (!n) return nullptr;
    auto fit = n->functionmap.find("format");
    if (fit == n->functionmap.end()) return nullptr;
    for (auto sf : fit->second) {
        if (sf->params.size() != 2 || !sf->params[0].type || !sf->params[1].type ||
            !sf->generics.empty() || sf->isnested)
            continue;
        auto pt1 = Subst(sf->params[1].type);
        auto p1 = IsPlainRef(pt1) ? pt1->ref->sub : pt1;
        if (!TypeEq(p1, t)) continue;
        auto pt0 = Subst(sf->params[0].type);
        if (!IsPlainRef(pt0) || !IsArrayKind(pt0->ref->sub, A_GROW) ||
            !IsU8(pt0->ref->sub->arr->sub))
            continue;
        vector<Val> argvals(2);
        argvals[0] = out;
        argvals[0].type = pt0;
        argvals[1] = value;
        argvals[1].type = pt1;
        if (!IsRefOrSlice(pt1)) argvals[1].writable = true; // The hook's own value copy.
        MatchInfo mi;
        mi.sf = sf;
        mi.env = nullptr;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why)) continue;
        if (sf->isextern && IsRefOrSlice(pt1) && !pt1->cq && !argvals[1].writable)
            Error(c, "extern format hook needs a writable value or a const parameter");
        auto sp = GetOrCreateSpec(mi, argvals, c);
        ApplyCalleeShrinks(c, sp, argvals, "format");
        ApplyCalleeRebinds(sp);
        ApplyCalleeGrows(c, sp, argvals, "format");
        c->fmtspecs.push_back({ t, sp });
        return sp;
    }
    return nullptr;
}

// The literal a compile-time string argument stands for: a string literal,
// or a let or const global initialized with one, named directly or through
// other such globals -- a named constant, as an array size may use (§11.1).
// Null for anything else, a local of the name included.
inline StrLit *TypeCheck::ConstStrLit(Node *n) {
    if (auto s = Is<StrLit>(n)) return s;
    auto id = Is<Ident>(n);
    if (!id) return nullptr;
    if (auto vd = LookupVar(id->name, id->ns); vd && !vd->isglobal) return nullptr;
    set<VarDecl *> visiting;
    for (;;) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (!g || g->isvar || g->inits.size() != 1 || !visiting.insert(g).second) return nullptr;
        if (auto s = Is<StrLit>(g->inits[0])) return s;
        if (!(id = Is<Ident>(g->inits[0]))) return nullptr;
    }
}

// The blob of the shader an embed_shader call names, compiled here once per
// distinct shader: embed_shader("x.frag") names a file, relative to the file
// with the call; embed_shader("frag", source, ...) gives the GLSL, its parts
// joined as lines, with #include relative to that file. The shader compiler's
// errors are errors of the call, and one at a line of a """ source is
// reported at that line of the program.
inline const string *TypeCheck::EmbedShader(Call *c, vector<Node *> &args) {
    auto &callfile = ast.sources[c->line.fileidx].first;
    vector<StrLit *> lits;
    vector<bool> named;
    for (auto &a : args) {
        auto lit = ConstStrLit(a);
        if (!lit)
            Error(a, "embed_shader takes string literals, and let or const globals "
                     "initialized with one");
        // The literal stands in for a global naming it: the call reads nothing
        // at run time.
        named.push_back(lit != a);
        if (lit != a) a = ast.New<StrLit>(lit->line, lit->val, lit->multiline);
        a->exprtype = cu8slice;
        lits.push_back(lit);
    }
    if (lits.size() == 1) {
        if (lits[0]->multiline || lits[0]->val.find('\n') != string::npos)
            Error(c, "embed_shader(\"x.frag\") takes a shader file's path; shader source "
                     "follows its stage: embed_shader(\"frag\", source)");
        auto path = EmbeddedShaderPath(callfile, lits[0]->val);
        auto it = ast.shaders.find(path);
        if (it == ast.shaders.end()) {
            try {
                it = ast.shaders.emplace(path, CompileShaderFile(path)).first;
            } catch (CompileError &e) {
                Error(c, cat("embed_shader: ", e.msg));
            }
        }
        return &it->second;
    }
    auto stage = ShaderStageNamed(lits[0]->val);
    if (stage < 0)
        Error(args[0], cat("embed_shader: the stage is \"vert\", \"frag\" or \"comp\", not \"",
                           lits[0]->val, "\""));
    // The source, and the line of it each part starts at.
    string source;
    vector<int> starts;
    for (size_t i = 1; i < lits.size(); i++) {
        if (i > 1) source += '\n';
        starts.push_back(1 + (int)count(source.begin(), source.end(), '\n'));
        source += lits[i]->val;
    }
    auto key = cat(callfile, "\n", lits[0]->val, "\n", source);
    auto it = ast.shaders.find(key);
    if (it != ast.shaders.end()) return &it->second;
    try {
        it = ast.shaders.emplace(key, CompileShader(source, callfile, stage)).first;
    } catch (CompileError &e) {
        int line;
        string msg;
        if (!ShaderMessageAt(e.msg, callfile, line, msg)) Error(c, cat("embed_shader: ", e.msg));
        if (!line) Error(c, cat("embed_shader: ", msg));
        auto part = upper_bound(starts.begin(), starts.end(), line) - starts.begin() - 1;
        auto lit = lits[part + 1];
        auto at = lit->line;
        if (lit->multiline) {
            auto lines = (int)count(lit->val.begin(), lit->val.end(), '\n') + 1;
            at.line += min(line - starts[part] + 1, lines);
        }
        Error(at, cat("embed_shader: ", msg,
                      named[part + 1] ? cat(" (in the shader embedded at ", Where(c->line), ")")
                                      : string()));
    }
    return &it->second;
}

// A shrink (`pop`, `resize` down, `clear`) of a grow-only array (§5.1).
// Everything below the stack top belongs to the array's elements for as
// long as it lives, so handing part of the region back is safe exactly
// when nothing can still point into it: the call stands on its own (a
// statement, an initializer, or the right-hand side of an assignment to a
// variable), so no reference taken earlier in the same expression outlives
// it; no reference or slice variable in scope points into the array; and
// no container in scope had a reference into it stored, which every store
// the checker has seen is on record for (storeevents). The receiver is
// named directly, or through a reference variable or parameter, in which
// case the array behind the reference is what shrinks.
inline void TypeCheck::CheckGrowShrink(Node *at, bool standalone, const char *op, Node *recv,
                                       const Val &rv) {
    auto id = Is<Ident>(recv);
    auto vd = id ? id->vdef : nullptr;
    auto at_type = rv.type->kind == TY_REF ? rv.type->ref->sub : rv.type;
    if (!vd || at_type->kind != TY_ARRAY)
        Error(at, cat(op, " on a grow-only array names the array's variable, or a "
                      "reference to it, not an element of another value (§5.1)"));
    // Through a reference variable or parameter: the array it points at.
    auto viaref = vd->type && vd->type->kind == TY_REF;
    auto roots = viaref ? RefRootsOf(vd) : RootsOf(vd);
    if (roots.Any([&](const RootAlt &a) { return !a.root || IsTemp(a.root); }))
        Error(at, cat(op, " through a reference whose array is not known (§5.1)"));
    ShrinkThrough(at, standalone, op, ExprStr(recv), roots, at_type);
}

// Whether a reference to `of`, or a byte view, may point into what a shrink
// at root frees: the resizable part of root's storage, or for a bound, an
// array of that type.
inline bool TypeCheck::ShrinkMayFree(VarDef *root, TypeExpr *bound, bool growonly,
                                     TypeExpr *of, bool byteview) {
    if (byteview) return bound ? Viewable(bound) : MayBeViewed(root);
    if (bound) return !of || (growonly ? CanContain(bound, of) : GrowShrinkContains(bound, of));
    if (growonly) return !of || !root->type || CanContain(LoadType(root->type), of);
    return GrowShrinkCanHold(root, of);
}

// Unnamed locations and views retained by an enclosing operation are live
// just like named references. This also covers a reference assignment's
// standalone RHS, where §5.1's syntax restriction alone is insufficient.
inline void TypeCheck::CheckHeldShrinks(Node *at, const string &op, VarDef *root,
                                        const string &what, bool growonly, TypeExpr *bound) {
    HeldOperands([&](const Held &h) {
        auto &[node, v, location, render] = h;
        auto path = v.type->kind == TY_REF && ClassOf(v.type->ref->sub) == SC_RESIZABLE;
        auto held = !path && ShrinkMayFree(root, bound, growonly, PointeeOf(v.type), v.byteview) &&
                    v.Any([&](const RootAlt &a) {
                        // A slot read never points into a grow-shrink array
                        // (§5.2), though it may into a grow-only one. An
                        // inexact root bounds the lifetime: it may name any
                        // outer owner, not just another at that depth.
                        if (!growonly && a.slotread) return false;
                        return a.root == root || (!a.exact && Depth(a.root) >= Depth(root));
                    });
        // A reference to a slice also reaches where the slice points, and,
        // for a grow-only array, one to anything holding references what
        // those point at: only a variable holds a slice into a grow-shrink
        // array. An assignment's location is overwritten before it is read
        // again.
        if (!held && !location && v.type->kind == TY_REF &&
            (growonly ? HoldsPlainRef(v.type->ref->sub) : v.type->ref->sub->kind == TY_SLICE))
            held = HeldRefsMayPointInto(nullptr, v, v.type, root, bound, growonly);
        if (!held) return;
        auto sec = growonly ? " (§5.1)" : " (§5.2)";
        if (render) {
            // A §5.1 scan's op names no array.
            auto arg = ExprStr(node);
            if (arg.find('\n') != string::npos) arg = "its argument";
            Error(at, cat("cannot ", op, growonly ? cat(" ", what) : string(), ": ", render,
                          " runs it in the middle of rendering ", arg, ", which may refer into ",
                          what, sec));
        }
        Error(at, cat("cannot ", op, ": an earlier expression value at ", Where(node->line),
                      " may still refer into ", what, sec));
    });
}

// A grow-only array shrinks wherever nothing can still point into it: a
// local of this function, a caller's array reached through a reference
// parameter, a global, or an enclosing function's local. Everything in
// scope is scanned; a shrink through a parameter or of a global is also
// recorded for the callers, whose own scopes are scanned at the call.
inline void TypeCheck::GrowOnlyShrinkAt(Node *c, bool standalone, const string &op, VarDef *vd,
                                        const string &what, TypeExpr *bound) {
    if (!frames.back().spec)
        Error(c, cat("cannot ", op, " ", what, " in a global initializer (§5.1)"));
    if (vd->reusable)
        Error(c, cat("cannot ", op, " reusable pool ", what,
                     ": its slots stay live for the freelist (§5.4)"));
    if (!standalone)
        Error(c, cat("cannot ", op, " ", what,
                     " inside a larger expression: a reference taken earlier in it may "
                     "still be live, so bind the result first (§5.1)"));
    if (invalue)
        Error(c, cat("cannot ", op, " ", what,
                     " inside a value-producing expression: references taken earlier in "
                     "it may still be live (§5.1)"));
    // The elements freed: of the array itself, or for a bound, of an array
    // of its type. A parameter class's storage is not known here.
    auto arrtype = bound ? bound : vd->type ? LoadType(vd->type) : nullptr;
    CheckHeldShrinks(c, op, vd, what, true, bound);
    for (auto v : vars) {
        if (v == vd || !v->type) continue;
        auto t = v->type;
        if (IsRefOrSlice(t)) {
            // A recorded root is exact only while the variable keeps its
            // first binding: a `var` may since have been rebound to any
            // root at the same depth, and one not bound yet can still
            // commit to this array further down a loop body. A pointee
            // the array cannot contain by value rules the variable out,
            // and so does a reference to a whole resizable value, which
            // is the path to an array rather than a pointer into one.
            // A bytes_of view is over the element region itself, so it
            // survives this filter however unrelated its pointee looks.
            auto path = t->kind == TY_REF && ClassOf(t->ref->sub) == SC_RESIZABLE;
            auto into = !path && ShrinkMayFree(vd, bound, true, PointeeOf(t), v->ref.byteview) &&
                        RefMayPointInto(v, vd);
            // A reference to a slice or to a value holding references, the
            // path to an array included, also reaches what those point at.
            auto via = !into && t->kind == TY_REF && HoldsPlainRef(t->ref->sub) &&
                       HeldRefsMayPointInto(v, v->ref, t, vd, bound, true);
            if ((!into && !via) || !UsedAfter(v)) continue;
            if (via)
                Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is still used: ",
                             t->ref->sub->kind == TY_SLICE
                                 ? "the slice it refers to may point into it"
                                 : "what it refers to may hold a reference or slice into it",
                             " (§5.1)"));
        } else {
            // Any other value holds references only where a store put
            // them, and every store this function can see is on record
            // (§9.2); a type without a plain reference or slice in it
            // (flat, or linked by relative references only) has no room
            // for one.
            if (!HoldsPlainRef(t)) continue;
            Line where;
            size_t hit = 0;
            if (!HolderMayPointInto(v, vd, arrtype, LiveEventBase(v), &where, &hit) ||
                !UsedAfter(v))
                continue;
            // A store later in the loop body than the shrink, which the
            // next iteration reaches (a pass before this one recorded it).
            if (CarriedEvent(hit))
                Error(c, cat("cannot ", op, " ", what, " while ", v->name,
                             " is in scope: a reference into it is stored there at ",
                             Where(where), ", which the next iteration reaches (§5.1)"));
            Error(c, cat("cannot ", op, " ", what, " while ", v->name,
                         " is still used: a reference into it was stored there at ",
                         Where(where), " (§5.1)"));
        }
        Error(c, cat("cannot ", op, " ", what, " while ", v->name,
                     " is still used: it may hold a reference or slice into it (§5.1)"));
    }
    if (vd->isglobal) {
        // What other globals hold cannot be enumerated from here: any one
        // whose type can hold a reference to something this array can
        // contain counts as holding one.
        for (auto g : ast.globals) {
            for (auto gd : g->defs) {
                if (gd == vd || !gd->type || !HoldsPlainRef(gd->type)) continue;
                vector<TypeExpr *> ps;
                RefPointees(gd->type, ps);
                for (auto pt : ps)
                    if ((IsU8(pt) && Viewable(arrtype)) || CanContain(arrtype, pt))
                        Error(c, cat("cannot ", op, " ", what, ": global ", gd->name,
                                     " may hold a reference into it (§5.1)"));
            }
        }
    }
    NoteLiveViews(c, cat("cannot ", op, " ", what), vd, what, true, bound);
    NoteShrink(vd, bound, SB_UNBALANCED, true);
}

// A field or element of a literal that is a reference, slice or holder:
// its root joins the literal's. It is storage wherever the literal lands,
// so it never points into a grow-shrink array (§5.2), even where FitsAt has
// no destination to check it against: in an argument, which passes none
// since parameters die before their arguments' roots, or in a result.
inline void TypeCheck::NoteLitElem(LitDeep &deep, Node *at, const Val &v, TypeExpr *t) {
    if (!t) return;
    auto isrs = IsRefOrSlice(t);
    if (!isrs && !HoldsPlainRef(t)) return;
    if (v.isnull) return;
    const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
    if (auto gs = StoredIntoGrowShrink(v, roots, t, !isrs))
        Error(at, NeverStoredError(gs, MayPointWording(roots, gs)));
    deep.roots.Add(roots);
    deep.byteview = deep.byteview || v.byteview;
}

inline void TypeCheck::HolderFromLit(Val &v, const LitDeep &deep) {
    if (!v.type || !HoldsPlainRef(v.type)) return;
    v.holderset = true;
    v.contents = deep.roots;
    v.byteview = deep.byteview;
}

// One store on record: program-wide, and on the specialization as well when
// the container is storage of the caller's, which a parameter's class root
// stands for -- the call sites map those back (§3.5).
inline void TypeCheck::AddStoreEvent(const StoreEvent &e) {
    storeevents.push_back(e);
    if (e.container->type || e.container->isglobal) return;
    auto spec = CurRealFrame().spec;
    if (!spec) return;
    // A cycle's rounds map a back edge's record onto the same class roots
    // again: one entry per fact.
    for (auto &o : spec->classevents)
        if (o.container == e.container && o.root == e.root && o.src == e.src &&
            o.exact == e.exact && o.byteview == e.byteview && o.bound == e.bound &&
            !o.pointee == !e.pointee && (!o.pointee || TypeEq(o.pointee, e.pointee)) &&
            !o.reached == !e.reached && (!o.reached || TypeEq(o.reached, e.reached)))
            return;
    spec->classevents.push_back(e);
}

// One store on record per place the value may point (§9.2). The container's
// contents grow by the same: a container that is itself a reference or a
// slice has none -- what it points at is its binding -- and neither has a
// parameter's class root, which stands for storage of the caller's, whose
// contents are the caller's to know.
inline void TypeCheck::RecordStore(VarDef *container, const Roots &roots, bool byteview,
                                    TypeExpr *pointee, VarDef *src, TypeExpr *reached,
                                    bool bound) {
    if (!container) return;
    // Putting a container's own read-back contents back into it adds no
    // incoming lifetime. Keep this distinction before discarding src.
    if (src == container) return;
    auto holds = container->type && !IsRefOrSlice(container->type);
    container->contentbyteview |= byteview;
    if (holds && roots.Unknown()) container->contents.unknown = true;
    for (auto &a : roots.alts) {
        if (!a.exact && a.from == container) continue;
        StoreEvent e;
        e.container = container;
        e.root = a.root;
        // A temporary was filled by whatever made it, not by stores on
        // record, so it is never the source: the value's own root bounds
        // what it holds.
        e.src = IsTemp(src) ? nullptr : src;
        // A reference read back out of a container inexactly (§9.5) points
        // at whatever was stored into that container: its stores are the
        // precise answer, where a bound would implicate every sibling.
        if (!e.src && !a.exact && a.from && a.from != container && !IsTemp(a.from))
            e.src = a.from;
        e.exact = a.exact;
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.reached = reached;
        e.bound = bound;
        if (fitnode) e.at = fitnode->line;
        AddStoreEvent(e);
        if (holds && container->contents.Add({ a.root, a.exact, a.from })) NoteFact(container);
    }
}

// The places a parameter's class stands for at a call: a reference or
// slice argument's own, and for a by-value holder those its references
// point into, which is what the class is keyed by (GetOrCreateSpec). What
// the callee's summary records against the class -- a store into it, a
// shrink or a growth of it -- happened to that storage, never to the
// holder, which the callee received a copy of.
inline Roots TypeCheck::ClassArgRoots(TypeExpr *pt, const Val &v) {
    return IsRefOrSlice(pt) ? v.AsRoots() : ContentsOf(v);
}

// What the callee stored into the caller's containers, as the caller's
// own events: a store through reference parameter p, or through the
// references by-value holder p holds, into something rooted at parameter q
// becomes a store into what argument p's class stands for (ClassArgRoot) of
// a value rooted at argument q's. Where that argument's root only bounds
// the storage, or the store went into storage the class only leads to, it
// is a store into each storage there that can hold what the store reached
// (StoreEvent::reached, ShrinkTargets), which the value must outlive (§9.2):
// the body saw only the bound. A callee still being checked (a back edge)
// may have stored any reference argument into any container argument.
inline void TypeCheck::ApplyCalleeStores(FnSpec *spec, vector<Val> &argvals, Node *at) {
    auto argroots = [&](size_t q) { return ClassArgRoots(spec->argtypes[q], argvals[q]); };
    auto paramof = [&](VarDef *cr) -> int {
        for (size_t p = 0; p < spec->params.size() && p < argvals.size(); p++)
            if (cr && spec->params[p]->ref.Root() == cr) return (int)p;
        return -1;
    };
    auto push = [&](VarDef *container, const RootAlt &a, TypeExpr *pointee, VarDef *src,
                    bool byteview, TypeExpr *reached, bool bound) {
        if (!container || src == container) return;
        StoreEvent e;
        e.container = container;
        e.root = a.root;
        e.src = src;
        e.exact = a.exact;
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.at = at->line;
        e.reached = reached;
        e.bound = bound;
        container->contentbyteview |= byteview;
        if (container->type && !IsRefOrSlice(container->type))
            container->contents.Add({ a.root, a.exact, a.from });
        AddStoreEvent(e);
    };
    // A class root of the callee, as seen from here: the argument's roots.
    auto mapped = [&](VarDef *cr, bool exact) -> Roots {
        auto q = paramof(cr);
        Roots r;
        if (q < 0) {
            r.Set(cr, exact);
            return r;
        }
        r = argroots((size_t)q);
        if (!exact) r.Weaken();
        return r;
    };
    // The record read: none in a cycle's first round (RecordOf), whose back
    // edge stores nothing yet.
    auto rec = RecordOf(spec);
    if (!rec) return;
    // A function value's body, checked inside the callee, stores values
    // rooted at the callee's parameters into its own lexical containers:
    // those roots are this call's arguments, one event per place.
    auto end = min(rec->eventend, storeevents.size());
    for (auto i = rec->eventstart; i < end; i++) {
        auto e = storeevents[i];
        auto r = mapped(e.root, e.exact);
        auto src = mapped(e.src, true).Root();
        for (size_t k = 0; k < r.alts.size(); k++) {
            auto &a = r.alts[k];
            e.root = a.root;
            e.exact = a.exact;
            e.src = src;
            if (k == 0) storeevents[i] = e;
            else storeevents.push_back(e);
        }
    }
    // A slice writes the caller's elements just as an array reference does.
    // Only a read-back from the same container preserves its existing
    // contents provenance (for example a permutation).
    for (auto &e : rec->classevents) {
        auto p = paramof(e.container);
        if (p < 0 || e.src == e.container) continue;
        auto r = mapped(e.root, e.exact);
        auto src = mapped(e.src, true).Root();
        auto cr = argroots((size_t)p);
        // Where the argument's root only bounds the storage, or the store
        // went into storage the class only leads to, the store lands in
        // every storage there may be behind it.
        auto widen = e.bound || !cr.Exact();
        if (e.bound) cr.Weaken();
        for (auto &t : ShrinkTargets(cr, e.reached)) {
            for (auto &a : r.alts) {
                if (widen && Depth(a.root) > Depth(t.root)) {
                    auto rname = a.root ? a.root->name : string_view("static data");
                    auto pname = spec->sf->params[(size_t)p].name;
                    Error(at, cat("call ", spec->sf->name, " stores a reference rooted at ",
                                  rname,
                                  e.bound ? cat(" into what its parameter ", pname,
                                                " leads to, which may be ")
                                          : cat(" through its parameter ", pname,
                                                ", whose argument may point into "),
                                  TargetStr(t), ", which ", rname, " does not outlive (§9.2)"));
                }
                push(t.root, a, e.pointee, src, e.byteview, e.reached, t.bound);
            }
        }
    }
}

// Whether a store into `holder`, from event `from` on, may have put a
// reference into `arr` there: one rooted at it exactly, or one bounded by
// a root the array outlives whose pointee the array's elements can hold.
// `arrtype` is the type of the array whose elements are in question, null
// where it is not known (a parameter class), which lets any pointee in.
inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype,
                                          size_t from, Line *where, size_t *hit) {
    set<VarDef *> seen;
    return HolderMayPointInto(holder, arr, arrtype, from, where, seen, hit);
}

inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype,
                                          size_t from, Line *where, set<VarDef *> &seen,
                                          size_t *hitat) {
    if (!seen.insert(holder).second) return false;
    auto contains = [&](TypeExpr *pt) { return !arrtype || CanContain(arrtype, pt); };
    for (auto i = from; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        if (e.container != holder) continue;
        auto hit = false;
        if (e.src && e.src->isglobal) {
            // A global's stores may come from functions not checked yet, so
            // its type decides: any reference it can hold to something the
            // array can contain.
            vector<TypeExpr *> ps;
            if (e.src->type) RefPointees(e.src->type, ps);
            for (auto pt : ps)
                hit |= (IsU8(pt) && (!arrtype || Viewable(arrtype))) || contains(pt);
        } else if (e.src && !e.src->type) {
            // Read out of a parameter's class: storage of the caller's,
            // whose stores this function cannot see, so the class bounds
            // what was read, as an inexact root would.
            hit = Depth(arr) <= Depth(e.src) && (!e.pointee || contains(e.pointee));
            if (hit) *where = e.at;
        } else if (e.src) {
            // A copy of another container's contents: whatever that one
            // holds, from its own first event on.
            hit = HolderMayPointInto(e.src, arr, arrtype, 0, where, seen, nullptr);
        } else if (e.exact) {
            hit = e.root == arr;
        } else if (e.root) {
            hit = Depth(arr) <= Depth(e.root) && (!e.pointee || contains(e.pointee));
        }
        if (hit) {
            if (!e.src) *where = e.at;
            if (hitat) *hitat = i;
            return true;
        }
    }
    return false;
}

// The pointee types of the plain references and slices a value of type t
// can hold: what a reference into some other array would be a reference
// to. Relative references point into their own root or a named pool.
inline void TypeCheck::RefPointees(TypeExpr *t, vector<TypeExpr *> &out) {
    switch (t->kind) {
        case TY_REF:
            if (t->ref->lenstorage < 0) out.push_back(LoadType(t->ref->sub));
            return;
        case TY_SLICE: out.push_back(t->sub); return;
        case TY_ARRAY: RefPointees(t->arr->sub, out); return;
        default: EachField(t, [&](TypeExpr *ft) { RefPointees(ft, out); }); return;
    }
}

// Whether what a shrink of storage of type t frees is a grow-only array's:
// t is one, or holds one as its tail (§3.4).
inline bool TypeCheck::GrowOnlyTail(TypeExpr *t) {
    auto arr = ResizableArrayIn(t);
    return arr && arr->arr->akind == A_GROW;
}

// Whether root r is (or stands for a call-site root that is) a grow-only
// array, or holds one: the receiver of a §5.1 shrink rather than a §5.2 one.
inline bool TypeCheck::IsGrowOnlyRootVar(VarDef *r) {
    auto v = r;
    while (v && !v->type && v->classfrom) v = v->classfrom;
    return v && v->type && GrowOnlyTail(LoadType(v->type));
}

// A shrink of the grow-shrink array rooted at root (§5.2): no variable in
// scope may refer into it. Such references are held only by variables
// (they cannot be stored), so the scan is exact, up to a `var` reference
// the same-depth rebinding rule could have retargeted into it.
inline void TypeCheck::CheckShrinkHolders(Node *at, const string &op, VarDef *root,
                                          const string &what, TypeExpr *bound) {
    CheckHeldShrinks(at, op, root, what, false, bound);
    VisibleVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        if (!IsRefOrSlice(v->type)) return;
        // A reference to the whole array (or the value holding it) is the
        // path to it, not something a shrink invalidates.
        if (v->type->kind == TY_REF && ContainsGrowShrink(v->type->ref->sub)) return;
        // Nor is one whose pointee the array's elements cannot contain: a
        // slice of text rooted at a dictionary keyed by slices points at
        // the text, whatever else it might be rebound to.
        // A bytes_of view is over the element region itself, so the
        // pointee-type filter would dismiss exactly the case it is for.
        // Where every binding on record is a slot read, which never points
        // into a grow-shrink array, only a binding the record does not show
        // can.
        auto into = ShrinkMayFree(root, bound, false, PointeeOf(v->type), v->ref.byteview) &&
                    (v->ref.Any([&](const RootAlt &a) {
                         return !a.slotread && (a.root == root ||
                                                (!a.exact && Depth(a.root) >= Depth(root)));
                     }) ||
                     RefMayRetarget(v, root));
        // A reference to a slice also reaches where the slice points: it may
        // name a variable holding one into the array, which is where such
        // slices are kept.
        auto via = !into && v->type->kind == TY_REF && v->type->ref->sub->kind == TY_SLICE &&
                   HeldRefsMayPointInto(v, v->ref, v->type, root, bound, false);
        if ((!into && !via) || !UsedAfter(v)) return;
        Error(at, cat("cannot ", op, " while ", v->name, " (bound at ", Where(v->line),
                      ") is still used: ", via ? "the slice it refers to may point into "
                                               : "it may refer into ",
                      what, " (§5.2)"));
    });
}

template <typename P, typename X>
inline void TypeCheck::NoteRootEvent(VarDef *root, P param, X external) {
    auto current = CurRealFrame().spec;
    if (!current) return;
    if (root->type) {
        if (root->isglobal || root->ownerspec != current) external(current, root);
        return;
    }
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        auto spec = frames[fi].spec;
        if (!spec) continue;
        auto found = false;
        for (size_t i = 0; i < spec->params.size(); i++) {
            if (RefRootOf(spec->params[i]) != root) continue;
            param(spec, (int)i);
            found = true;
        }
        if (found) {
            if (spec != current) external(current, root);
            return;
        }
    }
}

// Records a shrink for callers (§5.2), a bound's with the type of the array
// its storage leads to. An array stays balanced only while every shrink of
// it is.
inline void TypeCheck::NoteShrink(VarDef *root, TypeExpr *bound, ShrinkBalance balance,
                                  bool growonly) {
    auto note = [&](auto &bounds, auto key) {
        for (auto &b : bounds)
            if (b.key == key && TypeEq(b.type, bound)) {
                b.balance = std::max(b.balance, balance);
                return;
            }
        bounds.push_back({ key, bound, balance });
    };
    auto mark = [&](auto &entries, auto key) {
        auto [it, fresh] = entries.try_emplace(key, balance);
        if (!fresh) it->second = std::max(it->second, balance);
    };
    auto noted = [&](FnSpec *s) {
        if (balance == SB_UNBALANCED && !growonly) s->unbalancedshrink = true;
    };
    NoteRootEvent(root,
                  [&](FnSpec *s, int i) {
                      if (bound) note(s->shrinkparambounds, i);
                      else mark(s->shrinkparams, i);
                      noted(s);
                  },
                  [&](FnSpec *s, VarDef *r) {
                      if (bound) note(s->shrinkexternalbounds, r);
                      else mark(s->shrinkexternals, r);
                      noted(s);
                  });
}

inline void TypeCheck::ShrinkGrowShrink(Node *at, const string &op, VarDef *root,
                                        const string &what, TypeExpr *bound,
                                        ShrinkBalance balance) {
    if (!root) return;
    CheckShrinkHolders(at, op, root, what, bound);
    NoteLiveViews(at, cat("cannot ", op), root, what, false, bound);
    NoteShrink(root, bound, balance);
}

// Whether `recv.resize(len)` is a balanced shrink (§5.2): len names a `let`
// of this activation initialized to exactly `recv.len`, with recv the same
// path there, so the resize goes back to a length recv has had since the
// activation began. While every shrink of it is such a resize or a balanced
// call, no such length is shorter than the one it began with.
inline bool TypeCheck::ResizesToMark(Node *recv, Node *len) {
    auto id = Is<Ident>(len);
    auto m = id ? LookupVar(id->name, id->ns) : nullptr;
    auto spec = CurRealFrame().spec;
    return m && m->markof && !m->isvar && spec && m->ownerspec == spec &&
           SamePath(m->markof, recv);
}

// Whether two checked paths name the same storage whenever both run under
// one binding of the variable they start from: the same variable, which no
// rebinding can move (a `var` reference can be rebound), then the same
// fields, none of them a reference or slice, which could be moved.
inline bool TypeCheck::SamePath(Node *a, Node *b) {
    auto da = Is<Dot>(a), db = Is<Dot>(b);
    if (da || db) {
        auto field = [](Dot *d) {
            return d && d->fieldidx >= 0 && d->exprtype && !IsRefOrSlice(d->exprtype);
        };
        return field(da) && field(db) && da->fieldidx == db->fieldidx && da->name == db->name &&
               SamePath(da->obj, db->obj);
    }
    auto ia = Is<Ident>(a), ib = Is<Ident>(b);
    if (!ia || !ib || !ia->vdef || ia->vdef != ib->vdef) return false;
    auto v = ia->vdef;
    return v->type && !(v->isvar && IsRefOrSlice(v->type));
}

// The arrays a shrink of an `arr` rooted at root may free. An exact root
// owns the array. An inexact one only bounds its lifetime (§9.2): the array
// may be any `arr` owned at the root's depth or outside it, which the
// read-back candidates for that depth stand for (RootCandidates), the
// caller's storage behind a parameter as a bound. The root itself comes
// first, and is a bound too where its own storage cannot hold an `arr`: it
// was read out of something whose references lead to the array. A null
// root is static data where exact, and where not, the globals. The same
// storage is where a store rooted there into a slot inside an `arr` may land
// (FitsAt, ApplyCalleeStores).
inline vector<TypeCheck::ShrinkTarget> TypeCheck::ShrinkTargets(const Roots &roots,
                                                                 TypeExpr *arr) {
    vector<ShrinkTarget> out;
    auto add = [&](VarDef *r, bool bound) {
        for (auto &t : out)
            if (t.root == r) {
                t.bound = t.bound && bound;
                return;
            }
        out.push_back({ r, bound });
    };
    for (auto &a : roots.alts) {
        auto root = a.root;
        if (a.exact) {
            if (root) add(root, false);
            continue;
        }
        auto cands = RootCandidates(arr, Depth(root), false, true);
        if (root) {
            auto isbound = false;
            if (!IsTemp(root)) {
                isbound = true;
                for (auto &c : cands.alts) if (c.root == root && c.exact) isbound = false;
            }
            add(root, isbound);
        }
        for (auto &c : cands.alts)
            if (c.root && c.root != root) add(c.root, !c.exact);
    }
    return out;
}

// One of those as a diagnostic names it: a bound by what it leads to, the
// caller's storage for a parameter's class.
inline string TypeCheck::TargetStr(const ShrinkTarget &t) {
    if (!t.bound) return string(t.root->name);
    if (!t.root->type) return cat("the caller's storage behind ", t.root->name);
    return cat("what ", t.root->name, "'s references lead to");
}

// A shrink of the `arr` rooted at root, spelled `verb` on the receiver
// `recv` (§5.1, §5.2): of every array it may free (ShrinkTargets). The
// diagnostics name the root's array as the receiver does, and any other by
// its own name and the receiver's.
inline void TypeCheck::ShrinkThrough(Node *at, bool standalone, const string &verb,
                                     const string &recv, const Roots &roots, TypeExpr *arr,
                                     ShrinkBalance balance) {
    auto root = roots.Root();
    auto growonly = GrowOnlyTail(arr);
    for (auto &t : ShrinkTargets(roots, arr)) {
        auto bound = t.bound ? arr : nullptr;
        if (growonly) {
            auto what = t.root == root ? string(t.root->name)
                                       : cat(t.root->name, " (which ", recv, " may point at)");
            GrowOnlyShrinkAt(at, standalone, verb, t.root, what, bound);
        } else {
            auto what = t.root == root ? recv : cat(t.root->name, ", which ", recv,
                                                    " may point at");
            ShrinkGrowShrink(at, cat(verb, " ", recv), t.root, what, bound, balance);
        }
    }
}

// Whether a view rooted at r, still used after a shrink of root, is one
// only the callers can tell apart from the array: one of the two is a
// parameter's class. The scans judge every other view.
inline bool TypeCheck::CallersJudge(VarDef *r, VarDef *root) {
    return r && r != root && !IsTemp(r) && (IsClassRoot(r) || IsClassRoot(root));
}

// What the activation still uses after a shrink of root that only its
// callers can tell apart from the array (§5.1, §5.2): a view rooted at a
// parameter's class, whose argument may point into the array, or any view
// while the array is itself a parameter's, which the argument may make the
// array the view points into. Kept for the call sites (NoteLiveShrink).
// `prefix` names the shrink as the scans' errors do; `bound` is the type of
// the array where root only bounds it.
inline void TypeCheck::NoteLiveViews(Node *at, const string &prefix, VarDef *root,
                                     const string &what, bool growonly, TypeExpr *bound) {
    auto current = CurRealFrame().spec;
    // Neither a class nor a view rooted outside the activation can be in an
    // array the activation owns, and every array a local of it only bounds
    // is a shrink target of its own (ShrinkTargets).
    if (!current || (root->type && !root->isglobal && root->ownerspec == current)) return;
    struct View {
        Prov p;
        TypeExpr *pointee;
    };
    // What a reference or slice of type t with provenance p may point into:
    // its pointee, unless it is the path to a whole resizable value, and,
    // for a reference to a slice or (§5.1) to anything else holding
    // references, what those point into: where the slice points (SlotView,
    // without noting a slice variable's root as read), or what the holder's
    // root bounds. An assignment's location is overwritten before it is read.
    auto views = [&](const Prov &p, TypeExpr *t, bool location) {
        vector<View> out;
        if (t->kind != TY_REF || ClassOf(t->ref->sub) != SC_RESIZABLE)
            out.push_back({ p, PointeeOf(t) });
        if (location || t->kind != TY_REF) return out;
        auto sub = t->ref->sub;
        auto r = p.Root();
        if (sub->kind == TY_SLICE) {
            auto sv = p;
            if (r && p.Exact() && r->type && IsRefOrSlice(r->type)) {
                sv = r->ref;
                if (!r->refrootknown) {
                    if (UnboundIsBottom()) sv.Clear();
                    else sv.Set(temproot, false);
                }
            } else {
                sv = SlotView(p, sub);
            }
            out.push_back({ sv, sub->sub });
        } else if (growonly && HoldsPlainRef(sub)) {
            auto hv = p;
            hv.Weaken();
            hv.byteview = hv.byteview || p.Any([](const RootAlt &a) {
                return a.root && a.root->contentbyteview;
            });
            vector<TypeExpr *> pointees;
            RefPointees(sub, pointees);
            for (auto pt : pointees) out.push_back({ hv, pt });
        }
        return out;
    };
    // Every place a view may point that the callers judge, as a pair each.
    auto note = [&](const View &w, const string &name) {
        for (auto &a : w.p.alts) {
            if (!CallersJudge(a.root, root)) continue;
            LiveShrink ls { .shrunk = root, .shrunkexact = !bound, .bound = bound,
                            .live = a.root, .liveexact = a.exact, .pointee = w.pointee,
                            .byteview = w.p.byteview, .growonly = growonly, .name = name };
            if (NoteLiveShrink(ls, current) < 0)
                Error(at, cat(prefix, " while ", name, " is still used: it may refer into ", what,
                              growonly ? " (§5.1)" : " (§5.2)"));
        }
    };
    auto judged = [&](vector<View> &vs) {
        vs.erase(std::remove_if(vs.begin(), vs.end(),
                                [&](const View &w) {
                                    return !w.p.Any([&](const RootAlt &a) {
                                        return CallersJudge(a.root, root);
                                    });
                                }),
                 vs.end());
        return !vs.empty();
    };
    HeldOperands([&](const Held &h) {
        auto vs = views(h.v, h.v.type, h.location);
        if (!judged(vs)) return;
        auto name = ExprStr(h.node);
        if (name.find('\n') != string::npos) name = "an earlier expression value";
        for (auto &w : vs) note(w, name);
    });
    VisibleVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        auto t = v->type;
        vector<View> vs;
        if (IsRefOrSlice(t)) {
            if (!v->refrootknown) return;
            vs = views(v->ref, t, false);
        } else if (growonly && HoldsPlainRef(t)) {
            // A grow-only array's views may be stored (§5.1): the holder's
            // store record says where its references lead.
            EachHolderRoot(v, LiveEventBase(v), [&](const StoreEvent &e) {
                Prov p;
                p.Set(e.root, e.exact);
                p.byteview = e.byteview;
                vs.push_back({ p, e.pointee });
            });
        }
        if (!judged(vs) || !UsedAfter(v)) return;
        for (auto &w : vs) note(w, string(v->name));
    });
}

// What the stores into holder from event `from` on put there, following
// copies of other containers' contents to those containers' own stores.
// A copy of a global's contents counts as a reference bounded by the
// global: stores into a global may come from functions not checked yet.
template<typename F> void TypeCheck::EachHolderRoot(VarDef *holder, size_t from, F f) {
    set<VarDef *> seen;
    function<void(VarDef *, size_t)> walk = [&](VarDef *h, size_t start) {
        if (!seen.insert(h).second) return;
        for (auto i = start; i < storeevents.size(); i++) {
            auto e = storeevents[i];
            if (e.container != h) continue;
            if (e.src && !e.src->isglobal && e.src->type) {
                walk(e.src, 0);
                continue;
            }
            // A copy out of a global or a parameter's class is bounded by
            // it: a global's stores may come from functions not checked yet,
            // a class's are the caller's.
            if (e.src) {
                e.root = e.src;
                e.exact = false;
                if (e.src->isglobal) e.pointee = nullptr;
            }
            if (e.root) f(e);
        }
    };
    walk(holder, from);
}

// A shrink of ls.shrunk while what ls.live roots is still used, both as the
// activation of `current` names them. Where they may be one array as only
// its callers can tell -- one is a parameter's class, and neither is storage
// the activation owns -- the pair is kept on its record for them. Returns -1
// where nothing can tell the two apart, 1 where the record grew, else 0.
inline int TypeCheck::NoteLiveShrink(LiveShrink ls, FnSpec *current) {
    auto s = ls.shrunk, l = ls.live;
    if (!s || !l || IsTemp(s) || IsTemp(l)) return 0;
    if (!ShrinkMayFree(s, ls.bound, ls.growonly, ls.pointee, ls.byteview)) return 0;
    // A view into storage that cannot hold an array of the bound's type is
    // not in the array freed.
    if (ls.bound && l->type && ls.liveexact && !CanContain(LoadType(l->type), ls.bound))
        return 0;
    if (MayAliasRoots(l, ls.liveexact, s, ls.shrunkexact, current) == AL_NO) return 0;
    auto outside = [&](VarDef *v) {
        return IsClassRoot(v) || (v->type && (v->isglobal || v->ownerspec != current));
    };
    if (!current || s == l || (!IsClassRoot(s) && !IsClassRoot(l)) || !outside(s) || !outside(l))
        return -1;
    for (auto &e : current->liveshrinks) {
        if (e.shrunk != s || e.live != l || !e.bound != !ls.bound) continue;
        if (e.bound && !TypeEq(e.bound, ls.bound)) continue;
        auto was = e;
        e.shrunkexact = e.shrunkexact && ls.shrunkexact;
        e.liveexact = e.liveexact && ls.liveexact;
        if (e.pointee && (!ls.pointee || !TypeEq(e.pointee, ls.pointee))) e.pointee = nullptr;
        e.byteview = e.byteview || ls.byteview;
        return e.shrunkexact != was.shrunkexact || e.liveexact != was.liveexact ||
               e.pointee != was.pointee || e.byteview != was.byteview;
    }
    current->liveshrinks.push_back(ls);
    return 1;
}

// The callee's shrinks of arrays something it still uses may point into
// (FnSpec::liveshrinks), mapped onto this call's arguments: a parameter's
// class becomes the root of the argument passed for it (ClassArgRoot). Two
// arrays the caller cannot tell apart are an error here; two it can only as
// its own callers can are kept for them in turn. A callee in a recursive
// cycle still being checked has not recorded all of them yet, so the call
// is mapped again once the cycle is (ResolveCycleSites).
inline void TypeCheck::ApplyCalleeLiveShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                              string_view name) {
    CycleSite site { at, CurRealFrame().spec, RecordOf(spec), {}, string(name) };
    if (!site.callee) return;   // A cycle's first round: no record yet.
    for (size_t q = 0; q < spec->argtypes.size() && q < argvals.size(); q++)
        site.args.push_back(ClassArgRoots(spec->argtypes[q], argvals[q]));
    MapLiveShrinks(site);
}

// Maps the callee's pairs through one call's arguments into the caller's
// record; whether that record grew.
inline bool TypeCheck::MapLiveShrinks(const CycleSite &site) {
    auto spec = site.callee;
    // A class root of the callee, as seen from here: every place the
    // argument may point.
    auto mapped = [&](VarDef *r, bool exact) -> Roots {
        for (size_t p = 0; p < spec->params.size() && p < site.args.size(); p++) {
            if (!r || spec->params[p]->ref.Root() != r) continue;
            auto m = site.args[p];
            if (!exact) m.Weaken();
            return m;
        }
        Roots one;
        one.Set(r, exact);
        return one;
    };
    auto grew = false;
    // The caller may be the callee, whose record then grows underneath.
    for (size_t k = 0; k < spec->liveshrinks.size(); k++) {
        auto orig = spec->liveshrinks[k];
        auto name = orig.name;
        auto shrunk = mapped(orig.shrunk, orig.shrunkexact);
        auto live = mapped(orig.live, orig.liveexact);
        for (auto &sa : shrunk.alts) {
            for (auto &la : live.alts) {
                auto ls = orig;
                ls.shrunk = sa.root;
                ls.shrunkexact = sa.exact;
                ls.live = la.root;
                ls.liveexact = la.exact;
                if (!ls.shrunk || !ls.live) continue;
                // Passed on from the caller's parameter: that is what its
                // callers see used.
                if (ls.live != orig.live && IsClassRoot(ls.live)) ls.name = string(ls.live->name);
                auto r = NoteLiveShrink(ls, site.caller);
                if (r < 0) {
                    // The array as the caller names it: the shrunk argument's
                    // root, or where that only bounds the array, the view's
                    // own, or else the first array it bounds that the view
                    // may point into.
                    auto arr = ls.shrunkexact || !ls.liveexact ? ls.shrunk : ls.live;
                    if (!ls.shrunkexact && !ls.liveexact && !ls.bound && ls.shrunk->type &&
                        site.caller == CurRealFrame().spec) {
                        Roots one;
                        one.Set(ls.shrunk, false);
                        for (auto &t : ShrinkTargets(one, LoadType(ls.shrunk->type))) {
                            if (t.bound ||
                                MayAliasRoots(ls.live, false, t.root, true, site.caller) == AL_NO)
                                continue;
                            arr = t.root;
                            break;
                        }
                    }
                    auto what = ls.bound ? cat("an array ", ls.shrunk->name, " leads to")
                                         : string(arr->name);
                    Error(site.at, cat("cannot call ", site.name, ": it ",
                                       ls.bound || !shrunk.Exact() ? "may shrink " : "shrinks ",
                                       what, " while ", name, " is still used, and ", name,
                                       " may refer into ", ls.bound ? "it" : what,
                                       ls.growonly ? " (§5.1)" : " (§5.2)"));
                }
                grew = grew || r > 0;
            }
        }
    }
    return grew;
}

// The callee's shrinks of grow-shrink arrays (§5.2) are the caller's:
// nothing in scope may refer into an argument it shrinks through or a
// external owner it shrinks, and both are recorded for the caller's callers.
// A balanced shrink (NoteShrink) frees nothing a view the caller holds can
// reach, provided no other shrink of the call may be of the same array. A
// back edge's summary is incomplete, so it counts as shrinking every
// grow-shrink array it can reach, balanced as long as nothing the callee has
// done so far says otherwise (SettleAssumedShrinks).
inline void TypeCheck::ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                          string_view name) {
    auto standalone = Is<Call>(at) && Is<Call>(at)->standalone;
    // A shrink of an array the call may free: a grow-only array takes the
    // §5.1 scan (variables and recorded stores), a grow-shrink one the §5.2
    // scan (variables only) and the pairs its callers judge (NoteLiveViews),
    // unless it is judged balanced.
    struct Hit {
        VarDef *root;
        TypeExpr *bound;
        bool growonly;
        ShrinkBalance balance;
        const char *how;
    };
    vector<Hit> hits;
    auto apply = [&](const Hit &h, ShrinkBalance judged) {
        auto what = cat("call ", name, ", which ", h.how);
        auto op = cat(what, " ", h.root->name);
        if (h.growonly) {
            GrowOnlyShrinkAt(at, standalone, what, h.root, string(h.root->name), h.bound);
        } else if (judged == SB_UNBALANCED) {
            ShrinkGrowShrink(at, op, h.root, string(h.root->name), h.bound);
        } else {
            NoteShrink(h.root, h.bound, judged);
        }
    };
    // A shrink of the `arr` at root, and, where root is inexact or only
    // bounds it, of every other array it may be (ShrinkTargets). An
    // external's type is its root's own, which a parameter class takes from
    // its call site. A callee's are applied once all of them are known.
    auto shrink = [&](const Roots &roots, TypeExpr *arr, const char *how,
                      ShrinkBalance balance) {
        auto growonly = arr ? GrowOnlyTail(arr) : IsGrowOnlyRootVar(roots.Root());
        for (auto &t : ShrinkTargets(roots, arr))
            hits.push_back({ t.root, t.bound ? arr : nullptr, growonly,
                             growonly ? SB_UNBALANCED : balance,
                             roots.Exact() && t.root == roots.Root() ? how : "may shrink" });
    };
    ApplyCalleeStores(spec, argvals, at);
    // The record read: none in a cycle's first round (RecordOf), whose back
    // edge shrinks nothing yet.
    auto rec = RecordOf(spec);
    if (!rec) return;
    for (size_t i = 0; i < argvals.size() && i < spec->argtypes.size(); i++) {
        auto pt = spec->argtypes[i];
        auto entry = rec->shrinkparams.find((int)i);
        auto recorded = entry != rec->shrinkparams.end();
        if (!recorded) continue;
        // A shrink recorded against a by-value holder parameter is of the
        // array its references point into (ClassArgRoot), not of the holder,
        // which the callee received a copy of. Only a holder whose class is
        // that one array exactly has such an entry (RootArg::heldexact, or a
        // class shared with a reference); an inexact one's are bounds, below.
        if (!IsRefOrSlice(pt)) {
            shrink(ClassArgRoots(pt, argvals[i]), nullptr, "shrinks", entry->second);
            continue;
        }
        // What shrinks through a parameter is its pointee: a resizable one,
        // which every other parameter in its class points into.
        if (!argvals[i].Root() || pt->kind != TY_REF || ClassOf(pt->ref->sub) != SC_RESIZABLE)
            continue;
        shrink(argvals[i], LoadType(pt->ref->sub), "shrinks", entry->second);
    }
    // An array only reached through the references an argument holds or
    // points at: any of its type that the argument's roots, or its contents'
    // for a by-value holder, bound.
    for (auto &b : rec->shrinkparambounds) {
        if (b.key >= (int)argvals.size()) continue;
        auto roots = ClassArgRoots(spec->argtypes[b.key], argvals[b.key]);
        roots.Weaken();
        shrink(roots, b.type, "may shrink", b.balance);
    }
    for (auto &[vd, balance] : rec->shrinkexternals)
        shrink(RootsOf(vd), nullptr, "shrinks", balance);
    for (auto &b : rec->shrinkexternalbounds) {
        Roots one;
        one.Set(b.key, false);
        shrink(one, b.type, "may shrink", b.balance);
    }
    // A balanced shrink frees nothing a view of the array taken before
    // the call points into, unless the call may also shrink that array
    // unbalanced, under its root or another that may name it: an inexact
    // or bound one, or a parameter class, which may be a global, a
    // captured variable or another class (MayAliasRoots; two classes of
    // one activation only the call sites could tell apart).
    auto judge = [&](const Hit &h) {
        if (h.growonly || h.balance == SB_UNBALANCED) return SB_UNBALANCED;
        auto judged = h.balance;
        for (auto &u : hits) {
            if (u.growonly || MayAliasRoots(h.root, !h.bound, u.root, !u.bound) == AL_NO)
                continue;
            if (u.balance == SB_UNBALANCED) return SB_UNBALANCED;
            judged = std::max(judged, u.balance);
        }
        return judged;
    };
    for (auto &h : hits) apply(h, judge(h));
    ApplyCalleeLiveShrinks(at, spec, argvals, name);
}

inline bool TypeCheck::BuiltInPlace(TypeExpr *elem) {
    return ClassOf(elem) != SC_FIXED || (elem->kind != TY_REF && HasRelRefT(elem, true));
}

// A T[k] of the receiver's elements, or a T[] where they are not
// fixed-size, as only variable and grow-only arrays hold those (§3.3).
inline TypeExpr *TypeCheck::AppendedRun(TypeExpr *elem, ArrayLit *al) {
    if (ClassOf(elem) != SC_FIXED) {
        auto t = ast.NewType(TY_ARRAY, al->line);
        t->arr = ast.NewDetail<TypeArray>();
        t->arr->sub = elem;
        t->arr->akind = A_VAR;
        return t;
    }
    // A negative fill count is the literal's own error to report.
    auto n = al->fillval ? ConstIntOrError(al->fillcount, "array fill count")
                         : (int64_t)al->elems.size();
    return FixedArrayOf(elem, std::max<int64_t>(n, 0), al->line);
}

// A copied element would carry self-relative offsets still measured from
// the source (§3.9), and the references it holds are stored into the
// receiver (§9.2): bounded as they are in a copy of a whole array, or, out
// of the storage a slice or reference views, as reading each element out of
// it would bound them (§9.5).
inline void TypeCheck::AppendedCopies(Node *an, const Val &av, TypeExpr *elem, const Val &rv) {
    if (HasRelRefT(elem))
        Error(an, cat(".append copies the elements of ", ExprStr(an), ": copying a value of "
                      "type ", TypeStr(elem), ", which contains self-relative references, is "
                      "not supported; append an array literal, which constructs them in place"));
    if (!HoldsPlainRef(elem)) return;
    auto ev = av;
    if (IsRefOrSlice(av.type)) {
        LVal lv;
        lv.SetProv(av);
        lv.type = elem;
        lv.fromstorage = true;
        ev = ContainerRead(lv);
    }
    DestScope ds(*this, Dest(rv, false, rv.reached));
    MustFit(ev, an, ev.type, false);
}

inline TypeCheck::Alias TypeCheck::MayAliasRoots(VarDef *a, bool aexact, VarDef *b,
                                                 bool bexact) {
    return MayAliasRoots(a, aexact, b, bexact, CurRealFrame().spec);
}

// As the activation of `current` names the two roots.
inline TypeCheck::Alias TypeCheck::MayAliasRoots(VarDef *a, bool aexact, VarDef *b,
                                                 bool bexact, FnSpec *current) {
    if (!a || !b || IsTemp(a) || IsTemp(b)) return AL_NO;
    if (a == b) return AL_YES;
    auto isclass = [](VarDef *v) { return !v->type; };
    auto own = [&](VarDef *v, bool exact) {
        return exact && v->type && !v->isglobal && v->ownerspec == current;
    };
    if (isclass(a) || isclass(b)) {
        // A class stands for arrays outside this activation: a variable of
        // the activation, named exactly, is another array; a global or a
        // captured variable may be what a caller passed; two classes are
        // one array where some call site passes it to both.
        if (own(a, aexact) || own(b, bexact)) return AL_NO;
        return isclass(a) && isclass(b) ? AL_DEFER : AL_YES;
    }
    // An inexact root bounds the lifetime: it may name any owner at least
    // as outer (CheckHeldShrinks).
    if (!aexact && Depth(a) >= Depth(b)) return AL_YES;
    if (!bexact && Depth(b) >= Depth(a)) return AL_YES;
    return AL_NO;
}

inline void TypeCheck::NoteGrow(Node *at, const Roots &roots, const string &what) {
    for (auto &a : roots.alts) {
        auto root = a.root;
        if (!root || IsTemp(root)) continue;
        growlog.push_back({ at, root, a.exact, what });
        NoteRootEvent(root, [](FnSpec *s, int i) { s->growparams.insert(i); },
                      [](FnSpec *s, VarDef *r) { s->growexternals.insert(r); });
    }
}

// The value built at the top or in a slot of the array `built` may be, by
// the expression checked since growlog was `base` long: none of the growths
// logged meanwhile may have been of that array, or it would have landed
// inside the value.
inline void TypeCheck::CheckGrowsSince(size_t base, const Roots &built, const string &what) {
    for (auto i = base; i < growlog.size(); i++) {
        auto e = growlog[i];
        for (auto &b : built.alts) {
            auto may = MayAliasRoots(e.root, e.exact, b.root, b.exact);
            if (may == AL_NO) continue;
            auto msg = cat("cannot ", e.what, ": ", what, " is still under construction, and "
                           "the growth would land inside it (§1.3)");
            if (may == AL_YES) Error(e.at, msg);
            growconflicts.push_back({ e.at, CurRealFrame().spec, e.root, b.root, msg });
        }
    }
}

// The callee's growths are the caller's (§1.3(4)): an argument it grows
// through, and a global or captured owner it grows, are logged here for
// the values under construction around the call and for the caller's
// callers. A back edge's summary is incomplete, so what the callee's text
// grows stands in for it. A C function appends to every builder it is
// handed (§7.10).
inline void TypeCheck::ApplyCalleeGrows(Node *at, FnSpec *spec, vector<Val> &argvals,
                                        string_view name) {
    // A record notes a growth of a parameter's class (ClassArgRoot).
    auto grows = [&](size_t i, const char *how) {
        if (i >= argvals.size()) return;
        auto roots = ClassArgRoots(spec->argtypes[i], argvals[i]);
        auto root = roots.Root();
        if (!root || IsTemp(root)) return;
        NoteGrow(at, roots, cat("call ", name, ", which ", how, " ", root->name));
    };
    if (spec->sf->isextern) {
        for (size_t i = 0; i < spec->argtypes.size(); i++) {
            auto pt = spec->argtypes[i];
            if (IsPlainRef(pt) && ClassOf(pt->ref->sub) == SC_RESIZABLE) grows(i, "may grow");
        }
        return;
    }
    // The record read: none in a cycle's first round (RecordOf), whose back
    // edge grows nothing yet.
    auto rec = RecordOf(spec);
    if (!rec) return;
    for (auto pi : rec->growparams) grows((size_t)pi, "grows");
    for (auto vd : rec->growexternals)
        NoteGrow(at, RootsOf(vd), cat("call ", name, ", which grows ", vd->name));
}

// Where naming v leads, if that can be the array under construction at
// `built`, of type arr: v is the array's variable or the value holding it,
// or a reference to either. A slice, or a reference to anything smaller,
// points into the old elements, which the shrink the assignment starts
// with keeps from being used (§5.1).
inline TypeCheck::Alias TypeCheck::ReachesBuilt(VarDef *v, VarDef *built, bool exact,
                                                TypeExpr *arr, VarDef *&root) {
    auto t = v->type;
    if (!t || t->kind == TY_SLICE || (t->kind == TY_REF && t->ref->lenstorage >= 0))
        return AL_NO;
    auto isref = t->kind == TY_REF;
    if (!CanContain(isref ? t->ref->sub : t, arr)) return AL_NO;
    auto roots = isref ? RefRootsOf(v) : RootsOf(v);
    auto worst = AL_NO;
    for (auto &a : roots.alts) {
        auto may = MayAliasRoots(a.root, a.exact, built, exact);
        if (may == AL_NO) continue;
        if (worst == AL_NO || may == AL_YES) root = a.root;
        worst = may == AL_YES ? AL_YES : worst == AL_YES ? AL_YES : AL_DEFER;
    }
    return worst;
}

// The array a whole assignment replaces has no contents while the
// right-hand side runs: the old ones are gone, and the new ones are built
// over them (§4.4). Nothing the right-hand side runs may use it, then:
// name the array or a reference to it, itself or in a function it calls,
// which reaches the caller's arrays only through its arguments and what it
// names outside its own activation. A use the checker cannot show to be of
// a different array is an error, as a growth is (CheckGrowsSince).
inline void TypeCheck::CheckBuiltUses(Node *rhs, Node *lval, const Roots &built,
                                      TypeExpr *arr) {
    if (!built.Root() || IsTemp(built.Root())) return;
    auto what = ExprStr(lval);
    auto base = lval;
    while (auto d = Is<Dot>(base)) base = d->obj;
    auto lvvar = Is<Ident>(base) ? Is<Ident>(base)->vdef : nullptr;
    // `via` says which function names v, when the right-hand side's own
    // text does not.
    auto check = [&](Node *at, VarDef *v, const string &how, const string &via) {
        for (auto &b : built.alts) {
            if (!b.root || IsTemp(b.root)) continue;
            VarDef *root = nullptr;
            auto may = ReachesBuilt(v, b.root, b.exact, arr, root);
            if (may == AL_NO) continue;
            // The reference the assignment writes through needs no mention.
            auto refers = v->type->kind == TY_REF && v != lvvar;
            auto why = via.empty() ? (refers ? cat(v->name, " may refer to ", what) : string())
                                   : cat(via, refers ? cat(", which may refer to ", what) : "");
            auto msg = cat("cannot ", how, " in the value assigned to ", what, ": ",
                           why.empty() ? string() : cat(why, ", and "),
                           "that value is built over the old contents of ", what,
                           " (§4.4); build it in a variable of its own, and assign copy() of "
                           "that");
            if (may == AL_YES) Error(at, msg);
            growconflicts.push_back({ at, CurRealFrame().spec, root, b.root, msg });
        }
    };
    EachUse(rhs,
            [&](Ident *id, Node *path) {
                if (!FieldsApart(path, lval))
                    check(id, id->vdef, cat("use ", ExprStr(path)), string());
            },
            [&](Call *c, FnSpec *sp) {
                vector<VarDef *> named;
                auto pending = NamedOutside(sp, named);
                for (auto v : named)
                    check(c, v, cat("call ", sp->sf->name),
                          cat(sp->sf->name, pending ? ", still being checked, may use "
                                                    : " uses ", v->name));
            });
}

// The code under n that CheckBuiltUses judges: `named` gets each Ident
// naming a variable, with the path of fields it is read through, which
// may lead away from what the variable holds; `called` gets each call
// with each specialization it may run. A rebind's target is not named:
// moving a reference reaches nothing it points at.
template<typename F, typename G> void TypeCheck::EachUse(Node *n, F named, G called) {
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN && Is<Ident>(a->lval)) {
            walk(a->rhs);
            return;
        }
        auto base = n;
        for (auto d = Is<Dot>(base); d && d->fieldidx >= 0; d = Is<Dot>(base)) base = d->obj;
        if (auto id = Is<Ident>(base); id && id->vdef) {
            named(id, n);
            if (base != n) return;
        }
        if (auto c = Is<Call>(n)) {
            if (c->spec) called(c, c->spec);
            for (auto sp : c->dispatch) called(c, sp);
            for (auto &fs : c->fmtspecs) called(c, fs.second);
        }
        RunChildren(n, walk);
    };
    walk(n);
}

// Whether field paths `use` and `lval` start at one variable and part at
// some field, what `use` reads holding no reference that could lead back
// to what `lval` names.
inline bool TypeCheck::FieldsApart(Node *use, Node *lval) {
    auto path = [](Node *n, vector<Dot *> &fields) -> VarDef * {
        for (auto d = Is<Dot>(n); d; d = Is<Dot>(n)) {
            if (d->fieldidx < 0) return nullptr;
            fields.push_back(d);
            n = d->obj;
        }
        auto id = Is<Ident>(n);
        return id ? id->vdef : nullptr;
    };
    vector<Dot *> uf, lf;
    auto var = path(use, uf);
    if (!var || var != path(lval, lf)) return false;
    for (size_t i = 1; i <= uf.size() && i <= lf.size(); i++) {
        if (uf[uf.size() - i]->fieldidx == lf[lf.size() - i]->fieldidx) continue;
        auto ot = uf[0]->obj->exprtype;
        if (ot && ot->kind == TY_REF) ot = ot->ref->sub;
        auto runs = ot ? FieldRuns(ot) : vector<FieldRun>();
        return runs.size() == 1 && IsFlat((*runs[0].ftypes)[uf[0]->fieldidx]);
    }
    return false;
}

// What a specialization's body names outside its own activation, itself or
// through the functions it calls: globals, and variables of its lexical
// parents or of the bodies that wrote the function values it runs -- all it
// reaches of a caller's storage besides its arguments. A body still being
// checked (a recursive cycle) is not known in full: it counts as naming
// every global and every variable in scope on the path being checked, and
// the result says so.
inline bool TypeCheck::NamedOutside(FnSpec *spec, vector<VarDef *> &out) {
    if (auto it = namedoutside.find(spec); it != namedoutside.end()) {
        out = it->second;
        return false;
    }
    set<FnSpec *> walked;
    vector<VarDef *> named;
    auto pending = false;
    function<void(FnSpec *)> visit = [&](FnSpec *sp) {
        if (sp->sf->isextern || !walked.insert(sp).second) return;
            // A callee still being checked: what the round before saw it use
        // (RecordOf), or in a cycle's first round anything at all.
        auto rec = RecordOf(sp);
        if (!rec) {
            pending = true;
            return;
        }
        EachUse(rec->body, [&](Ident *id, Node *) { named.push_back(id->vdef); },
                [&](Call *, FnSpec *callee) { visit(callee); });
    };
    visit(spec);
    set<VarDef *> seen;
    auto add = [&](VarDef *v) {
        if (seen.insert(v).second) out.push_back(v);
    };
    // A variable of an activation the call runs is created by it.
    for (auto v : named)
        if (v->isglobal || !walked.count(v->ownerspec)) add(v);
    if (pending) {
        for (auto g : ast.globals)
            for (auto vd : g->defs) add(vd);
        for (auto vd : vars) add(vd);
    } else {
        namedoutside[spec] = out;
    }
    return pending;
}

// Element construction targets the array's storage (relative references
// in the element must derive from the same root, §3.9).
inline void TypeCheck::ElemArg(Node *&n, TypeExpr *elem, Val &rv) {
    SlotScope ss(*this, true);
    CheckValueAt(n, elem, Dest(rv, false, rv.reached), true);
}

}  // namespace goose
