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
// table drives arity, receiver kind/provenance, simple signatures and the
// growths and shrinks; what it cannot express gets dedicated code below.
inline Val TypeCheck::CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv) {
    c->builtin = d.kind;
    if (c->trailing) Error(c, cat(d.name, " takes no function value"));
    if (!(d.flags & BF_TYARGS) && !c->tyargs.empty())
        Error(c, cat(d.name, " takes no type arguments"));
    if ((int)args.size() < d.minargs || (int)args.size() > d.maxargs)
        Error(c, cat(d.name, " takes ", (int64_t)d.minargs,
                     d.minargs == d.maxargs ? string() : cat("-", (int64_t)d.maxargs),
                     " argument(s), ", (int64_t)args.size(), " given"));
    if (d.kind >= B_SHARED_ALLOC && d.kind <= B_MEMORY_COPY) {
        auto captype = [&](NativeKind k) {
            auto t = ast.NewType(TY_NATIVE, c->line); t->nativekind = k; return t;
        };
        auto cap = [&](size_t i, NativeKind k) {
            auto v = CheckValue(args[i], nullptr);
            if (v.type->kind != TY_NATIVE || v.type->nativekind != k)
                Error(c, cat(d.name, " requires ", NativeName(k), " for argument ", i + 1));
        };
        auto number = [&](size_t i) { CheckArg(args[i], ast.inttypes[IS_U64]); };
        auto result = [&](TypeExpr *t, bool ok = false) {
            Val v; v.type = t; c->rettypes.push_back(t);
            if (ok) {
                Val b; b.type = ast.booltype;
                c->rettypes.push_back(b.type); lastcallrets = {v, b};
            }
            return v;
        };
        switch (d.kind) {
            case B_SHARED_ALLOC:
                number(0); return result(captype(NK_OWNER), true);
            case B_SHARED_VIEW:
                cap(0, NK_OWNER); number(1); number(2); return result(captype(NK_VIEW));
            case B_SHARED_CLOSE:
                cap(0, NK_OWNER); return result(ast.booltype);
            case B_SHARED_SIZE: {
                auto v = CheckValue(args[0], nullptr);
                if (v.type->kind != TY_NATIVE || v.type->nativekind > NK_ACCESS)
                    Error(c, "shared_size requires an owner, view, or access");
                return result(ast.inttypes[IS_U64]);
            }
            case B_SHARED_BORROW:
                cap(0, NK_VIEW); return result(captype(NK_ACCESS));
            case B_SHARED_RELEASE:
                cap(0, NK_ACCESS); return VoidVal();
            case B_MEMORY_READ: case B_MEMORY_WRITE: {
                if (c->tyargs.size() != 1) Error(c, "memory scalar access needs exactly one type argument");
                auto t = Subst(c->tyargs[0]);
                if (!IsIntT(t) || t->ix) Error(c, "memory scalar type must be i/u8/16/32/64");
                c->qtype = t;
                cap(0, NK_ACCESS); number(1);
                if (d.kind == B_MEMORY_WRITE) CheckArg(args[2], t);
                CheckArg(args[d.kind == B_MEMORY_READ ? 2 : 3], ast.booltype);
                return d.kind == B_MEMORY_READ ? result(t) : VoidVal();
            }
            case B_MEMORY_COPY:
                cap(0, NK_ACCESS); number(1); cap(2, NK_ACCESS); number(3); number(4);
                return VoidVal();
            default: assert(false);
        }
    }
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
            // unreachable.
            if (d.kind == B_ABORT) CheckArg(args[0], u8slice);
            else CheckIntAny(args[0]);
            reachable = false;
            return VoidVal();
        case B_THREAD_SPAWN: case B_THREAD_SPAWN_CONTAINED: {
            auto wid = Is<Ident>(args[0]);
            vector<SFunction *> workers;
            // A variable in scope, a type parameter or a nested function of the
            // name hides it, as at a call (§11.1).
            auto wvd = wid ? LookupVar(wid->name, wid->ns) : nullptr;
            if (wid && !(wvd && !wvd->isglobal) && !ScopeNameKind(wid->name))
                for (auto sf : ast.LookupFunctions(wid->name, wid->ns))
                    if (sf->isthread) workers.push_back(sf);
            // A function parameter bound to a thread_fn's name names it too
            // (§7.6).
            const FnValBind *fvb = nullptr;
            if (wid && !(wvd && !wvd->isglobal) &&
                !LookupTypeParam(wid->name, fvb) && fvb && fvb->named) {
                if (fvb->set) {
                    for (auto sf : *fvb->set) if (sf->isthread) workers.push_back(sf);
                } else if (fvb->named->isthread) {
                    workers.push_back(fvb->named);
                }
            }
            // A spawn names one worker (§11.2): its arguments do not choose
            // among thread_fns of one name, as a call's do among overloads.
            if (workers.size() > 1) {
                string which;
                for (auto sf : workers)
                    Append(which, "\n  candidate thread_fn ", sf->name, " at ", Where(sf->line));
                Error(c, cat("ambiguous ", d.name, " of ", wid->name, ": more than one thread_fn "
                             "has that name (§11.2)", which));
            }
            auto wsf = workers.empty() ? nullptr : workers[0];
            if (!wsf && fvb)
                Error(c, cat(d.name, "'s first argument names a thread_fn: ", wid->name,
                             " is bound to ", fvb->named ? cat(fvb->named->name, ", which is not one")
                                                         : string("a block")));
            if (!wsf) Error(c, cat(d.name, "'s first argument names a thread_fn"));
            wid->fnref = wsf;
            wid->exprtype = fntype;
            auto spec = EnsureThreadSpec(wsf, c->line);
            if (args.size() != 1 + spec->argtypes.size())
                Error(c, cat(d.name, "(", wsf->name, ", ...) takes ",
                             (int64_t)spec->argtypes.size(), " worker argument(s)"));
            {
                DestScope ds(*this, Dest {});
                for (size_t i = 0; i < spec->argtypes.size(); i++) {
                    if (HoldsIndexT(spec->argtypes[i])) ThreadIndexError(c, spec->argtypes[i]);
                    CheckArg(args[1 + i], spec->argtypes[i]);
                }
            }
            c->spec = spec;
            Val v;
            v.type = ast.inttypes[IS_I64];
            return v;
        }
        case B_THREAD_WAIT: {
            // thread_wait(id, msg) appends a contained worker's abort
            // message to msg (§11.2).
            CheckIntAny(args[0]);
            if (args.size() == 2) CheckByteOut(c, args[1], "thread_wait(id, msg)");
            Val v;
            v.type = ast.booltype;
            return v;
        }
        case B_QPUT: {
            auto av = CheckValue(args[0], nullptr);
            NoUntypedEmptyArray(av, args[0], d.name);
            if (av.type->kind == TY_VOID || av.type->kind == TY_FN || !IsFlat(av.type))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(av.type)));
            if (HoldsIndexT(av.type)) ThreadIndexError(c, av.type);
            // false when the queue is full (§11.2); a call statement drops it.
            Val ok;
            ok.type = ast.booltype;
            return ok;
        }
        case B_QGET: case B_QPOLL: case B_QLIMIT: {
            if (c->tyargs.size() != 1)
                Error(c, cat(d.name,
                             d.kind == B_QLIMIT ? "<T>(n)" : args.empty() ? "<T>()" : "<T>(ms)",
                             " needs exactly one explicit type argument"));
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            if (!IsFlat(t))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(t)));
            if (HoldsIndexT(t)) ThreadIndexError(c, t);
            // A limit, or a wait's milliseconds.
            if (!args.empty()) CheckArg(args[0], ast.inttypes[IS_I64]);
            if (d.kind == B_QLIMIT) {
                c->qtype = t;
                return VoidVal();
            }
            c->rettypes.push_back(t);
            Val first;
            first.type = t;
            first.Set(TempRoot(), false);
            lastcallrets.clear();
            lastcallrets.push_back(first);
            if (d.kind == B_QPOLL || !args.empty()) {
                Val b2;
                b2.type = ast.booltype;
                c->rettypes.push_back(ast.booltype);
                lastcallrets.push_back(b2);
            }
            return first;
        }
        case B_EMBED_SHADER:
        case B_EMBED_FILE: {
            // The shader compiled, or the file read, now, and the result a
            // read-only view of static data, like a string literal's (the
            // gfx package's gfx.goose, github.com/aardappel/goose_gfx, and
            // docs/design/packages.md §6.5).
            c->shaderblob = c->builtin == B_EMBED_FILE ? EmbedFile(c, args) : EmbedShader(c, args);
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
            if (ClassOf(v.type) == SC_ROOT)
                Error(c, "root owners cannot be copied; bind a reference instead");
            if (v.type->kind == TY_SLICE)
                Error(c, "copy takes a value or a reference, not a slice");
            if (!av.lvalue && !IsPlainRef(av.type))
                Error(c, "copy of a temporary: the value is fresh already");
            NoRelRefCopy(args[0], v.type);
            if ((v.type->kind == TY_ENUM || v.type->kind == TY_VARIANT) &&
                ClassOf(v.type) == SC_RESIZABLE)
                Error(c, "copying a resizable ADT or variant is not supported by the "
                         "C backend yet; construct a fresh value or pass the owning "
                         "value by reference");
            v.lvalue = false;
            // Keep the copy node and its own storage root through every
            // argument check. Its contents still borrow from the source.
            if (!v.holderset && HoldsPlainRef(v.type)) {
                v.contents = Bounds(v);
                v.holderfrom = HolderSource(v);
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
            // A relative reference's is the null of its zero offset, whatever
            // the width, and as a value the plain reference a load gives.
            auto vt = ValueType(t);
            if (ClassOf(vt) != SC_FIXED)
                Error(c, cat("default<T>() needs a fixed-size type, not ", TypeStr(t)));
            string why;
            if (!HasDefault(t, why))
                Error(c, cat("default<", TypeStr(t), ">() does not exist: ", why));
            c->rettypes.push_back(vt);
            if (!c->defaultinit) {
                if (t->kind == TY_STRUCT || t->kind == TY_ENUM || t->kind == TY_VARIANT) {
                    auto st = t->kind == TY_ENUM ? ast.VariantTypeOf(t, &t->enu->en->variants[0], c->line) : t;
                    auto sl = ast.New<StructLit>(c->line, st);
                    sl->defaultall = true;
                    sl->implicit = true;
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
                return v;
            }
            Val v;
            v.type = vt;
            v.Set(nullptr, true);   // A null optional or an empty slice: static.
            v.writable = true;      // And nothing to write, so it fits any slot (§9.5).
            // Any other value -- a scalar's, an empty limited array's -- is
            // built in a temporary, as a literal is (§9.2).
            if (!IsRefOrSlice(vt)) v = TempCopy(v);
            return v;
        }
        default: break;
    }
    // Member receiver validation, from the table.
    Val rv;
    TypeExpr *elem = nullptr;
    TypeExpr *recvarr = nullptr;   // The receiver's array type.
    auto ak = A_FIXED;
    if (d.flags & BF_MEMBER) {
        if (precv) {
            rv = *precv;
        } else {
            rv = CheckV(args[0], nullptr);
            args[0]->exprtype = rv.type;
        }
        NoUntypedEmptyArray(rv, args[0], d.name);
        auto rt = rv.type;
        if (IsPlainRef(rt)) {
            rt = rt->ref->sub;
            if (rt->kind == TY_SLICE) {
                // A slice is the one its slot holds, as DecayRef loads it.
                auto sv = SlotView(rv, rt);
                sv.reached = nullptr;
                rv.SetProv(sv);
            } else {
                // As DerefLValue.
                rv.ClearReads();
                rv.reached = LoadType(rt);
            }
        }
        if (rt->kind == TY_ARRAY) {
            ak = rt->arr->akind;
            elem = rt->arr->sub;
            recvarr = rt;
        } else if (rt->kind == TY_SLICE) {
            elem = rt->sub;
        }
        if (!(RecvKindOf(rt) & d.recv))
            Error(c, cat(".", d.name, " is not available on ", TypeStr(rv.type)));
        if ((d.flags & BF_WRITE) && !rv.writable)
            Error(c, cat("cannot .", d.name, " through a non-writable value "
                         "(let, const, or a read-only instantiation, §9.5)"));
        // A reference that may point at such a pool, but is not a pool
        // reference, says so.
        auto notpoolref = [&](int ru) -> string {
            if (rv.reusable) return "";
            for (auto &a : rv.alts)
                if (a.root && (a.root->reusable & ru))
                    return cat(": ", ExprStr(args[0]), " may point at ", a.root->name,
                               ", but is not a pool reference");
            return "";
        };
        if ((d.flags & BF_REUSABLE) && !(rv.reusable & RU_SLOTS))
            Error(c, cat(".", d.name, " exists on reusable pools only",
                         rv.reusable ? ", not on the slice pools of reusable[]" : "",
                         notpoolref(RU_SLOTS), " (§5.4)"));
        if ((d.flags & BF_SLICEPOOL) && !(rv.reusable & RU_SLICES))
            Error(c, cat(".", d.name, " exists on reusable[] pools only",
                         rv.reusable ? ", not on the slot pools of reusable" : "",
                         notpoolref(RU_SLICES), " (§5.4)"));
    }
    if (d.kind == B_MOVE_FROM) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, ".move_from requires fixed-size elements");
        auto sv = CheckV(args[1], nullptr);
        args[1]->exprtype = sv.type;
        if (sv.type->kind != TY_SLICE || !TypeEq(sv.type->sub, elem))
            Error(c, cat(".move_from takes a slice of ", TypeStr(elem), ", got ", TypeStr(sv.type)));
        if (HasRelRefT(elem))
            Error(c, ".move_from cannot copy elements containing self-relative references");
        AppendedCopies(args[1], sv, elem, rv);
        return VoidVal();
    }
    // A pending `var x = []` receiver learns its element type from what
    // is first pushed or appended into it (§4.2).
    if (elem && elem->kind == TY_VOID) {
        auto rt = rv.type;
        if (IsPlainRef(rt)) rt = rt->ref->sub;
        if (d.args[0] == 'e') {
            auto av = DecayRef(CheckV(args[1], nullptr));
            // The element's own type is what completes the array (JudgeCastAt).
            JudgeCastAt(args[1], nullptr, av);
            CompletePending(rt, PendingElemFrom(av, args[1]), c->line);
        } else if (d.args[0] == 'a') {
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
    if (*d.grows && !(d.flags & BF_OUTARG))
        NoteGrow(c, rv, cat(d.grows, " ", ExprStr(args[0])), recvarr);
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
            v.freshview = true;
            c->rettypes.push_back(v.type);
            return v;
        }
        if (args.size() == 2) {
            CheckByteOut(c, args[1], "to_bytes(a, out)");
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
    // it (§5.1); pop, pop_n and resize also need an element the shrink can
    // find, which a sequential array has not got.
    auto shrink = (d.flags & BF_SHRINKS) != 0;
    if (ak == A_GROW && shrink) {
        CheckGrowShrink(c, d.name, args[0], rv);
        if (d.kind != B_CLEAR && ClassOf(elem) != SC_FIXED)
            Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
    }
    // A grow-shrink array shrinks from anywhere, provided nothing in scope
    // refers into it (§5.2).
    if (ak == A_GROWSHRINK && shrink)
        ShrinkThrough(c, d.name, ExprStr(args[0]), rv,
                      IsPlainRef(rv.type) ? rv.type->ref->sub : rv.type,
                      d.kind == B_RESIZE && ResizesToMark(args[0], args[1]) ? SB_BALANCED
                                                                            : SB_UNBALANCED);
    // resize has two forms (§3.3); a target below zero is caught at runtime.
    if (d.kind == B_RESIZE) {
        CheckIntAny(args[1]);
        if (args.size() == 3) {
            ElemArg(args[2], elem, rv, true);
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
        if (av.lvalue && av.type->kind != TY_REF && TypeEq(StorageType(av), elem))
            args[1] = AutoRef(args[1], av, false);
        else if (UserRefOf(args[1]))
            Warn(args[1], cat("redundant &: ", ExprStr(Is<Unary>(args[1])->child),
                              " is passed by reference without it (§4.1)"));
        args[1]->exprtype = av.type;
        if (!IsPlainRef(av.type) || !TypeEq(av.type->ref->sub, elem))
            Error(c, cat(".index_of takes a reference to an element of ", TypeStr(rv.type),
                         ", got ", TypeStr(StorageType(av))));
        CheckRootedAtReceiver(c, d.name, rv, av, elem, "a reference", "§3.3");
    }
    if (d.kind == B_RANGE_INDEX || d.kind == B_CHECK_LENGTH) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
        if (d.kind == B_RANGE_INDEX) {
            auto sv = CheckV(args[1], nullptr);
            args[1]->exprtype = sv.type;
            if (sv.type->kind != TY_SLICE || !TypeEq(sv.type->sub, elem))
                Error(c, cat(".range_index takes a slice of ", TypeStr(rv.type),
                             ", got ", TypeStr(sv.type)));
            // Empty slices from any storage (including default/null) yield
            // zero. Other origins are checked at runtime unless proven here.
            c->poolcheck = !RootedAtReceiver(rv, sv, elem);
        }
        // What a failed check aborts with in place of the runtime's message,
        // which a library words in its own terms (§3.3).
        if (args.size() == 3) {
            CheckArg(args[2], u8slice);
            if (!Is<StrLit>(args[2]))
                Error(args[2], cat(".", d.name, " takes its abort message as a string literal"));
        }
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
            if (!RootedAtReceiver(rv, sv, elem)) {
                // Globals and this function's own variables are separate storage,
                // so a slice exactly rooted at one is not the pool's when every
                // pool the receiver may be (a branch's value may choose among
                // several) is another. Any other slice may be, and is checked
                // when the call runs.
                auto own = [&](VarDef *r) {
                    return r && (r->isglobal || r->ownerspec == CurRealFrame().spec);
                };
                if (sv.Exact() && own(sv.Root()) && !rv.None() && !rv.Has(sv.Root()) &&
                    rv.All([&](const RootAlt &a) { return own(a.root); }) && !VerdictDeferred())
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
            auto logbase = cur.growlog.size();
            ElemArg(c->defaultinit, elem, rv);
            CheckGrowsSince(logbase, rv, "the default elements allocated in a slice pool",
                            recvarr);
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
            case 'e': {
                // An element built in place is under construction while
                // its expression runs (§1.3(4)).
                auto logbase = cur.growlog.size();
                ElemArg(an, elem, rv);
                if (BuiltInPlace(elem))
                    CheckGrowsSince(logbase, rv,
                                    cat("the element ",
                                        d.kind == B_PUSH ? "pushed into " : "allocated in ",
                                        ExprStr(args[0])),
                                    recvarr);
                break;
            }
            case 'a': {  // An array/slice of the receiver's element type.
                auto logbase = cur.growlog.size();
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
                    CheckGrowsSince(logbase, rv, cat("the run appended to ", ExprStr(args[0])),
                                    recvarr);
                break;
            }
            default: assert(false);
        }
    }
    // Returns, from the table.
    Val v = VoidVal();
    // A reference or slice element read out as itself: it points where the
    // element does and is as writable as its slot says, as an element read
    // is (ContainerRead).
    auto heldread = [&] {
        LVal lv;
        lv.SetProv(rv);
        lv.type = elem;
        lv.fromstorage = true;
        lv.isslot = true;
        v.type = LoadType(elem);
        v.SetProv(ContainerRead(lv));
        c->rettypes.push_back(v.type);
    };
    switch (d.rets[0]) {
        case 0: break;
        case 'i': v.type = ast.inttypes[IS_I64]; break;
        case 'e':
            // A popped reference or slice leaves as itself: what a temporary
            // holds is not rooted at the temporary (§9.2).
            if (IsRefOrSlice(elem)) {
                heldread();
                break;
            }
            v.type = LoadType(elem);
            v.Set(TempRoot(), false);
            if (HoldsPlainRef(v.type)) {
                // The element leaves as a temporary, holding what it held in
                // the receiver, as an element read would (ContainerRead).
                v.contents = HeldAt(rv);
                for (auto &a : v.contents.alts) a.slotread = true;
                v.holderset = true;
                v.holderfrom = HolderSource(rv);
                MarkClassCopy(v);
            }
            // What an adapting receiver (the element's ADT, say) constructs from.
            c->rettypes.push_back(v.type);
            break;
        case 'r':
            // An element that is itself a reference gives the one it holds,
            // loaded if relative, as `&` of it does: there are no references
            // to references (§3.8).
            if (elem->kind == TY_REF) {
                heldread();
                break;
            }
            v.type = ast.RefTo(elem, c->line);
            v.TakeAlts(rv);
            v.ClearReads();
            v.writable = rv.writable;
            // What a receiver that decays the reference loads through.
            c->rettypes.push_back(v.type);
            break;
        case 's':
            v.type = ast.SliceOf(elem, c->line);
            v.TakeAlts(rv);
            v.ClearReads();
            v.writable = rv.writable;
            // What an adapting receiver (a limited array) constructs from.
            c->rettypes.push_back(v.type);
            break;
        default: assert(false);
    }
    return v;
}

// The growable u8 array a builtin's `out` argument appends to (BF_OUTARG),
// as `form` names the call: to_bytes(a, out), thread_wait(id, msg).
inline void TypeCheck::CheckByteOut(Call *c, Node *out, const string &form) {
    auto ov = CheckV(out, nullptr);
    out->exprtype = ov.type;
    auto ot = ov.type;
    if (IsPlainRef(ot)) ot = ot->ref->sub;
    auto ok = ot->kind == TY_ARRAY && IsU8(ot->arr->sub) &&
              (ot->arr->akind == A_GROW || ot->arr->akind == A_GROWSHRINK ||
               ot->arr->akind == A_LIMITED);
    if (!ok) Error(c, cat(form, " appends to a growable u8 array, not ", TypeStr(ov.type)));
    if (!ov.writable)
        Error(c, "cannot append through a non-writable value (let, or "
                 "non-writable provenance, §9.5)");
    NoteGrow(c, ov, cat(BuiltinByKind(c->builtin).grows, " ", ExprStr(out)), ot);
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
    NoUntypedEmptyArray(av, a, what);
    // A reference is rendered as its pointee, so the links of a copy would
    // be followed from the copy.
    if (!av.lvalue && !av.pointee) NoRelRefTempRead(a, av.type, av.type);
    // A value that is no storage -- neither a variable, field or element nor
    // what a reference names -- is rendered from a temporary codegen puts it
    // in (GenLoc), which a format hook taking it by reference is handed,
    // read-only (§3.7). A call's result, a struct or array literal and a
    // control construct's value are rooted at one already; any other, such
    // as an operator's result, a literal, a cast, a variant constant or a
    // literal parameter, has no roots, which every rule would take for
    // static data, and is rooted at one here, still holding what it held. A
    // reference or slice keeps where it points (a slice's slot is below).
    if (!av.lvalue && !av.pointee && av.None() && !IsRefOrSlice(av.type)) {
        if (HoldsPlainRef(av.type)) {
            av.contents = ContentsOf(av);
            av.holderset = true;
        }
        av.Set(TempRoot(), true);
    }
    // A slice that is no storage is rendered from a temporary, one the
    // optimizer keeps where it reduces the argument to storage (OptRendered),
    // which is the slot a format hook taking it by reference is given
    // (UserFormatIn), and read-only, as a view into a temporary is (§9.5). So
    // is the value of a control construct or of a function value's call, a
    // copy of what its branch gives even where that names a variable (§4.1),
    // which keeps none of that branch's slot (TempCopy).
    if (av.type->kind == TY_SLICE && !av.hasslot) {
        av.slot = Prov {};
        av.slot.Set(TempRoot(), true);
        av.hasslot = true;
    }
    // An i64 read out of varint storage lies in no i64 storage (§3.6): it is
    // rendered from a temporary holding the i64 the varint decodes to, which
    // is what a format hook taking an i64 by reference is given
    // (EmitUserFormat), read-only.
    if (av.isvarint) {
        Val tv;
        tv.type = av.type;
        tv.Set(TempRoot(), true);
        av = tv;
    }
    // Rendered now (HeldOperands).
    auto saverender = tuple(cur.renderarg, cur.renderwhere, cur.rendering,
                            std::move(cur.renderwalks));
    cur.renderarg = a;
    cur.renderwhere = nullptr;
    cur.rendering = what;
    cur.renderwalks.clear();
    struct Restore {
        TypeCheck &tc;
        tuple<Node *, Node *, const char *, vector<Val>> saved;
        ~Restore() {
            tc.cur.renderarg = get<0>(saved);
            tc.cur.renderwhere = get<1>(saved);
            tc.cur.rendering = get<2>(saved);
            tc.cur.renderwalks = std::move(get<3>(saved));
        }
    } restore { *this, std::move(saverender) };
    // The buffer print and str render into is a string of its own.
    auto buffer = TempRoot();
    buffer->onearray = { ast.inttypes[IS_U8] };
    Val builder;
    builder.Set(buffer, true);
    builder.writable = true;
    if (out && ClassOf(DecayedType(*out)) == SC_RESIZABLE) builder = *out;
    auto context = ast.New<Call>(c->line, c->callee);
    RenderSeen seen;
    CheckRenderable(context, what, av.type, a, seen, av, builder);
    // A type that reaches itself through references is rendered below its
    // first level by a function of its own, which runs no format overload
    // (RenderFn): an overload for a part of it needs one for the type.
    for (auto t : seen.recurring) {
        vector<TypeExpr *> walked;
        if (auto part = OverloadedPart(context, t, walked))
            Error(a, cat(what, " cannot render ", TypeStr(t), ", which reaches itself through "
                         "references, around the format overload for ", TypeStr(part),
                         ": give ", TypeStr(t), " a format overload of its own (§3.7)"));
    }
    c->fmtcontexts.push_back(context);
    c->fmtspecs.insert(c->fmtspecs.end(), context->fmtspecs.begin(), context->fmtspecs.end());
}

// The first type rendering a value of type t meets that one of context c's
// format overloads renders: t itself, or a part at any depth its references
// reach. An ADT's payload is rendered as its variant's literal, whatever
// overload a value of the variant type has (RenderLoc). `seen` holds the
// types walked.
inline TypeExpr *TypeCheck::OverloadedPart(Call *c, TypeExpr *t, vector<TypeExpr *> &seen) {
    for (auto s : seen) if (TypeEq(s, t)) return nullptr;
    seen.push_back(t);
    for (auto &fs : c->fmtspecs) if (TypeEq(fs.first, t)) return t;
    switch (t->kind) {
        case TY_ARRAY: return OverloadedPart(c, t->arr->sub, seen);
        case TY_SLICE: return OverloadedPart(c, t->sub, seen);
        case TY_REF: return OverloadedPart(c, t->ref->sub, seen);
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT: {
            TypeExpr *part = nullptr;
            EachField(t, [&](TypeExpr *ft) { if (!part) part = OverloadedPart(c, ft, seen); });
            return part;
        }
        default: return nullptr;
    }
}

inline void TypeCheck::CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at,
                                       RenderSeen &seen, Val value, const Val &out) {
    // A type met again on the path reaches itself through references. The
    // levels below render as this one does, from the struct, variant or ADT
    // a reference or slice leads to on by a function that runs no format
    // overload (RenderFn), which CheckPrintable holds that type to.
    for (auto s : seen.path) {
        if (!TypeEq(s, t)) continue;
        while (t->kind == TY_REF || t->kind == TY_SLICE || t->kind == TY_ARRAY)
            t = t->kind == TY_REF ? t->ref->sub : t->kind == TY_SLICE ? t->sub : t->arr->sub;
        for (auto r : seen.recurring) if (TypeEq(r, t)) return;
        seen.recurring.push_back(t);
        return;
    }
    seen.path.push_back(t);
    struct PopSeen { vector<TypeExpr *> &types; ~PopSeen() { types.pop_back(); } } pop { seen.path };
    value.type = t;
    value.writable &= !t->cq;
    if (UserFormat(c, t, value, out, seen.path.size() == 1 ? at : nullptr)) return;
    // The argument itself, unless an overload takes it whole, is read where
    // it lies, around the overloads its parts run, so that storage stays in
    // use meanwhile (HeldOperands, the argument being rendered).
    if (seen.path.size() == 1 && (t->kind == TY_STRUCT || t->kind == TY_ENUM ||
                                  t->kind == TY_VARIANT || t->kind == TY_ARRAY))
        cur.renderwhere = at;
    // A variable-mode ADT's tag, or the count of an array of a size not
    // fixed, is read once, and the parts it covers are rendered where they
    // lie after it (RenderLoc): where it lies stays held while their
    // overloads run (HeldOperands).
    auto walk =(t->kind == TY_ENUM && t->enu->varmode) ||
                (t->kind == TY_ARRAY && ClassOf(t) != SC_FIXED);
    if (walk) cur.renderwalks.push_back(value);
    struct PopWalk {
        TypeCheck &tc;
        bool walk;
        ~PopWalk() { if (walk) tc.cur.renderwalks.pop_back(); }
    } popwalk { *this, walk };
    // A part lies in the value's storage, or where the value points: a slice
    // part's slot is the value, as writable as that is (UserFormatIn).
    auto child = [&](TypeExpr *ft, bool throughref, int component = -1) {
        auto v = value;
        if (component >= 0) v.TakeAlts(ProjectRoots(value, t, component));
        v.hasslot = ft->kind == TY_SLICE;
        if (v.hasslot) v.slot = v;
        if (throughref) {
            ReadBack contents;
            auto hascontents = TempContents(value, contents);
            v.TakeAlts(ReadBackRoot(ft, v, value.byteview, hascontents ? &contents : nullptr));
            v.writable = SlotLoadWritable(ft, value.writable);
        }
        CheckRenderable(c, what, ft, at, seen, v, out);
    };
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: return;
        case TY_ARRAY: child(t->arr->sub, IsRefOrSlice(t->arr->sub)); return;
        case TY_SLICE: child(t->sub, IsRefOrSlice(t->sub)); return;
        case TY_REF: child(t->ref->sub, false); return;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            if (ClassOf(t) == SC_ROOT) {
                auto si = GetStructInst(t);
                for (size_t i = 0; i < si->ftypes.size(); i++)
                    if (auto ft = si->ftypes[i]) child(ft, IsRefOrSlice(ft), (int)i);
                return;
            }
            // An overload rendering one part of a fixed-mode value may
            // overwrite it with another variant, and nothing may refer into
            // its payload meanwhile (§3.5): the parts lie in a copy codegen
            // takes once the tag is read (RenderLoc), a read-only temporary,
            // as a construct's value is.
            if (t->kind == TY_ENUM && !t->enu->varmode) value = TempCopy(value);
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
// (docs/design/namespaces.md). `arg` is the argument where the value is all
// of it, else null.
inline FnSpec *TypeCheck::UserFormat(Call *c, TypeExpr *t, const Val &value, const Val &out,
                                     Node *arg) {
    auto tns = NominalNs(t);
    if (auto sp = UserFormatIn(c, t, tns, value, out, arg)) return sp;
    return tns.empty() ? nullptr : UserFormatIn(c, t, {}, value, out, arg);
}

inline FnSpec *TypeCheck::UserFormatIn(Call *c, TypeExpr *t, string_view ns,
                                      const Val &value, const Val &out, Node *arg) {
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
        // A reference to a slice takes the slot the slice lies in, which
        // codegen passes (EmitUserFormat), as a reference parameter takes a
        // slice lvalue (RefSliceArgs): rooted there, as writable as that is.
        if (IsPlainRef(pt1) && t->kind == TY_SLICE) {
            SlotRoots(argvals[1]);
            NoteHeld(argvals[1], t);
        }
        argvals[1].type = pt1;
        if (!IsRefOrSlice(pt1)) argvals[1].writable = true; // The hook's own value copy.
        MatchInfo mi;
        mi.sf = sf;
        mi.env = nullptr;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why)) continue;
        if (sf->isextern && IsRefOrSlice(pt1) && !pt1->cq && !argvals[1].writable)
            Error(c, "extern format hook needs a writable value or a const parameter");
        // Given a variable argument by a writable reference, the overload may
        // write it as a reference parameter may, a `let` included (§4.4).
        if (auto id = Is<Ident>(arg); id && id->vdef && value.lvalue && IsPlainRef(pt1) &&
                                      !pt1->cq && argvals[1].writable)
            NoteWritableRef(id->vdef, arg);
        // Rendering a T is no copy(x), and a T that is not fixed-size
        // reaches a value parameter only as one or as an rvalue (§4.1).
        if (!IsRefOrSlice(pt1) && ClassOf(pt1) != SC_FIXED)
            Error(c, cat(TypeStr(t), " is not fixed-size and is not copied implicitly into the "
                         "format overload taking it by value (§4.1): take it by reference"));
        // The hook's copy would still measure its self-relative references
        // from where the value lies (§3.9).
        if (!IsRefOrSlice(pt1) && HasRelRefT(pt1))
            Error(c, cat("copying a value of type ", TypeStr(t), ", which contains "
                         "self-relative references, into the format overload taking it by "
                         "value is not supported; take it by reference"));
        auto sp = GetOrCreateSpec(mi, argvals, c);
        // A kill in the value empties the argument's part, where it is all
        // of the value; the builder has no node here, and holds no references.
        ApplyCalleeShrinks(c, sp, argvals, "format", { nullptr, arg });
        ApplyCalleeRebinds(sp);
        ApplyCalleeGrows(c, sp, argvals, "format");
        CheckfreeCallee(c, sp->sf);
        c->fmtspecs.push_back({ t, sp });
        return sp;
    }
    return nullptr;
}

// The literal a compile-time string argument stands for: a string literal,
// or a let or const global initialized with one, named directly or through
// other such globals -- a named constant, as an array size may use (§11.1).
// Null for anything else, a local, type parameter or nested function of the
// name included, which hides the global. Unlike an integer one
// (RelyOnConstant), such a global keeps its initializer's value without a
// mark: the slice in its slot is a literal's, read-only, so a reference to
// the slot is read-only too (§9.5).
inline StrLit *TypeCheck::ConstStrLit(Node *n) {
    if (auto s = Is<StrLit>(n)) return s;
    auto id = Is<Ident>(n);
    if (!id) return nullptr;
    if (auto vd = LookupVar(id->name, id->ns); !vd || !vd->isglobal) return nullptr;
    set<VarDecl *> visiting;
    for (;;) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (!g || g->isvar || g->inits.size() != 1 || !visiting.insert(g).second) return nullptr;
        if (auto s = Is<StrLit>(g->inits[0])) return s;
        if (!(id = Is<Ident>(g->inits[0]))) return nullptr;
    }
}

// The bytes of the file an embed_file call names, read here once per
// distinct file, resolved relative to the file with the call as a shader
// file is. The argument is a string literal, or a let or const global
// initialized with one, which stands in for it: the call reads nothing at
// run time.
inline const string *TypeCheck::EmbedFile(Call *c, vector<Node *> &args) {
    auto &callfile = ast.sources[c->line.fileidx].first;
    auto lit = ConstStrLit(args[0]);
    if (!lit)
        Error(args[0], "embed_file takes a string literal, or a let or const global initialized "
                       "with one");
    if (lit != args[0]) args[0] = ast.New<StrLit>(lit->line, lit->val, lit->multiline);
    args[0]->exprtype = cu8slice;
    auto path = EmbeddedShaderPath(callfile, lit->val);
    auto key = cat("file\n", path);
    auto it = ast.shaders.find(key);
    if (it == ast.shaders.end()) {
        string bytes;
        if (!LoadFile(path, bytes)) Error(c, cat("embed_file: cannot open ", path));
        it = ast.shaders.emplace(key, std::move(bytes)).first;
    }
    return &it->second;
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
// when nothing can still point into it: no value its statement evaluated
// earlier and still uses refers into the array; no reference or slice
// variable in scope points into it; and no container in scope had a
// reference into it stored, which every store the checker has seen is on
// record for (storeevents). The receiver is named directly, or through a
// reference variable or parameter, in which case the array behind the
// reference is what shrinks, or it is a struct's tail field, which shrinks
// the storage the struct lies in, as a reference to the field would.
inline void TypeCheck::CheckGrowShrink(Node *at, const char *op, Node *recv, const Val &rv) {
    auto id = Is<Ident>(recv);
    auto vd = id ? id->vdef : nullptr;
    auto at_type = rv.type->kind == TY_REF ? rv.type->ref->sub : rv.type;
    assert(at_type->kind == TY_ARRAY);
    Roots roots;
    if (vd) {
        // Through a reference variable or parameter: the array it points at.
        auto viaref = vd->type && vd->type->kind == TY_REF;
        roots = viaref ? RefRootsOf(vd) : RootsOf(vd);
    } else {
        roots = rv.AsRoots();
    }
    if (!ShrinkRootsKnown(roots))
        Error(at, cat(op, " through a reference whose array is not known (§5.1)"));
    ShrinkThrough(at, op, ExprStr(recv), roots, at_type);
}

// A whole assignment of a grow-only array, or of a value holding one as its
// tail (`whole` false), shrinks it (§4.4) where a `clear` of its location
// would: the array's variable, a reference to it or to the value holding it,
// or a struct's field on such a path, whose roots are those of the storage
// it lies in (§5.1). CheckAssignedValue scans the shrink.
inline void TypeCheck::GrowOnlyAssignKnown(Assign *a, const Roots &built, bool whole) {
    if (!ShrinkRootsKnown(built))
        Error(a, cat("cannot assign ", whole ? "a grow-only array" : "a value holding a "
                     "grow-only array", " through a reference whose array is not known (§5.1)"));
}

// Whether a reference to `of`, or a byte view, may point into what a shrink
// at root frees: the resizable part of root's storage, or for a bound, an
// array of that type.
inline bool TypeCheck::ShrinkMayFree(VarDef *root, TypeExpr *bound, bool growonly,
                                     TypeExpr *of, bool byteview) {
    if (byteview) return bound ? Viewable(bound) : MayBeViewed(root);
    if (bound) return !of || (growonly ? CanContain(bound, of) : GrowShrinkContains(bound, of));
    if (growonly) {
        auto st = ShrunkType(root, nullptr);
        return !of || !st || CanContain(st, of);
    }
    return GrowShrinkCanHold(root, of);
}

// Whether a shrink at root, of storage of type `bound` where root only bounds
// it, may relay out or free a `t` a rendering walks in place (renderwalks):
// the storage holds a t, which a whole assignment rebuilds, or the t holds
// the array shrunk.
inline bool TypeCheck::ShrinkMayMove(VarDef *root, TypeExpr *bound, TypeExpr *t) {
    if (!t) return true;
    if (bound) return CanContain(bound, t) || CanContain(t, bound);
    return !root->type || CanContain(LoadType(root->type), t);
}

// Unnamed locations and views retained by an enclosing operation are live
// just like named references: the values the statement evaluated before the
// shrink and uses after it (HeldOperands).
inline void TypeCheck::CheckHeldShrinks(Node *at, const string &op, VarDef *root,
                                        const string &what, bool growonly, TypeExpr *bound) {
    HeldOperands([&](const Held &h) {
        auto &[node, v, location, render, elems, loop, reread, inplace] = h;
        // A variable of the loop's body is made after its sequence and gone
        // before it, so neither the sequence nor what it holds can lie in
        // or refer into that variable's storage (§9.2).
        if (loop && loop->vdef && root && !root->isglobal && !IsClassRoot(root) &&
            Depth(root) >= Depth(loop->vdef))
            return;
        auto path = !inplace && v.type->kind == TY_REF &&
                    ClassOf(v.type->ref->sub) == SC_RESIZABLE;
        auto mayfree = inplace
                           ? ShrinkMayMove(root, bound, v.type->ref->sub)
                           : ShrinkMayFree(root, bound, growonly, PointeeOf(v.type), v.byteview);
        auto held = !path && mayfree &&
                    v.Any([&](const RootAlt &a) {
                        // A slot read never points into a grow-shrink array
                        // (§5.2), though it may into a grow-only one, where
                        // a view the storage of a parameter's class held
                        // points where that storage's views do; a walk's may
                        // have been reached through a slot's reference to a
                        // whole resizable of either kind. An inexact root
                        // bounds the lifetime: it may name any outer owner,
                        // not just another at that depth.
                        if (!growonly && !inplace && a.slotread) return false;
                        if ((growonly || inplace) && a.classread)
                            return ClassReadMayPointInto(a.root, root, bound, inplace);
                        return a.root == root || (!a.exact && Depth(a.root) >= Depth(root));
                    });
        // A reference to a slice also reaches where the slice points, and,
        // for a grow-only array, one to anything holding references, or a
        // slice of such values, what those point at: only a variable holds a
        // slice into a grow-shrink array. An assignment's location is
        // overwritten before it is read again, and a slot a for loop reads
        // again holds the sequence it walks, which is held itself.
        auto reaches = v.type->kind == TY_REF
                           ? growonly ? HoldsPlainRef(v.type->ref->sub)
                                      : v.type->ref->sub->kind == TY_SLICE
                           : growonly && v.type->kind == TY_SLICE && HoldsPlainRef(v.type->sub);
        auto elemsrefer = false;
        if (!held && !location && !reread && reaches) {
            held = HeldRefsMayPointInto(nullptr, v, v.type, root, bound, growonly);
            elemsrefer = held && v.type->kind == TY_SLICE;
        }
        if (!held) return;
        auto sec = growonly ? " (§5.1)" : " (§5.2)";
        // A §5.1 scan's op names no array.
        auto shrink = cat("cannot ", op, growonly ? cat(" ", what) : string(), ": ");
        auto expr = ExprStr(node);
        if (render) {
            if (expr.find('\n') != string::npos) expr = "its argument";
            Error(at, cat(shrink, render, " runs it in the middle of rendering ", expr,
                          ", which may refer into ", what, sec));
        }
        if (loop) {
            if (expr.find('\n') != string::npos)
                expr = reread ? "a reference or slice on the path to its sequence"
                              : "its sequence";
            Error(at, cat(shrink, "the for loop at ", Where(loop->line),
                          reread ? cat(" reads ", expr, " again on every iteration, which may lie in ")
                          : elemsrefer ? cat(" iterates ", expr, ", whose elements may refer into ")
                          : IsRefOrSlice(node->exprtype)
                              ? cat(" iterates ", expr, ", which may refer into ")
                              : cat(" iterates ", expr, " in place, which may lie in "),
                          what, sec));
        }
        if (expr.find('\n') != string::npos) expr = "the value";
        if (node->line.line != at->line.line || node->line.fileidx != at->line.fileidx)
            expr = cat(expr, " (at ", Where(node->line), ")");
        if (location)
            Error(at, cat(shrink, expr, ", which the assignment writes after it, may be in ",
                          what, sec));
        if (elems)
            Error(at, cat(shrink, "the elements of ", expr, ", which the statement reads after it, ",
                          elemsrefer ? "may refer into " : "may be in ", what, sec));
        Error(at, cat(shrink, expr, ", evaluated earlier in the statement, may still refer into ",
                      what, sec));
    });
}

// The part of a holder checked location path n names (Kill): a variable,
// whose own storage it is, then the fields stepped into in it, or the
// pointee of a `let` reference or a parameter, which no rebinding moves,
// then its fields. A path through an element, a slice or any other
// reference names no such part.
inline bool TypeCheck::KillPath(Node *n, VarDef *&base, string &path) {
    if (auto id = Is<Ident>(n)) {
        auto vd = id->vdef;
        if (!vd || vd->isglobal || vd->istemp) return false;
        if (IsRefOrSlice(vd->type) && (vd->type->kind != TY_REF || vd->isvar)) return false;
        base = vd;
        path.clear();
        return true;
    }
    auto d = Is<Dot>(n);
    if (!d || !d->IsField() || d->fieldidx < 0 || !d->obj->exprtype) return false;
    auto ot = d->obj->exprtype;
    if (ot->kind == TY_REF) {
        auto id = Is<Ident>(d->obj);
        if (!id || !KillPath(id, base, path)) return false;
    } else if (ot->kind != TY_STRUCT || !KillPath(d->obj, base, path)) {
        return false;
    }
    Append(path, ".", d->name);
    return true;
}

// A whole assignment to the location `lval` names, which lies where `loc`
// says, of a value v holding no references into storage -- none at all, or
// nulls and string literals, which are static data: the part of the holder
// it names, a slice's or a reference's own slot included, holds nothing from
// here on, which the stores into it before event `at`, the one at which the
// value began to be checked, then did not put there. Only the part of
// storage one known container holds is so emptied.
inline void TypeCheck::NoteKill(Node *lval, const Roots &loc, TypeExpr *target, const Val &v,
                                size_t at) {
    if (!target || !HoldsPlainRef(target)) return;
    auto held = ContentsOf(v);
    if (!held.OnlyStatic() || held.unknown || !loc.Exact()) return;
    auto container = loc.Root();
    if (!container || container->isglobal || IsTemp(container)) return;
    VarDef *base = nullptr;
    string path;
    if (!KillPath(lval, base, path)) return;
    if (!IsRefOrSlice(base->type) && base != container) return;
    erase_if(cur.kills, [&](const Kill &k) {
        return k.container == container && k.base == base && k.path == path;
    });
    cur.kills.push_back({ container, base, path, at });
}

// Whether the kills in force empty every slot of a `t` lying at `base` in
// container c that can hold a view into the elements of an `arr` (null: of
// any array), at any remove through its references: each such slot lies in
// a part a kill names, the container's whole storage included, a struct's
// fields taken one by one. A slot a reference leads to lies in other
// storage, which no kill of this one empties. `since` gets the earliest kill
// relied on; with none relied on, nothing is said.
inline bool TypeCheck::KilledFor(VarDef *c, VarDef *base, TypeExpr *t, TypeExpr *arr,
                                 size_t &since) {
    since = SIZE_MAX;
    if (cur.kills.empty()) return false;
    // A reference into c whose pointee is of c's own type points at the
    // whole of it: no value holds one of its own type by value.
    auto whole = [&](VarDef *b) {
        if (b == c || !b->type || b->type->kind != TY_REF || !c->type || IsRefOrSlice(c->type))
            return b;
        return TypeEq(LoadType(b->type->ref->sub), LoadType(c->type)) ? c : b;
    };
    base = whole(base);
    function<bool(TypeExpr *, const string &)> emptied = [&](TypeExpr *ft, const string &path) {
        for (auto &k : cur.kills) {
            if (k.container != c) continue;
            auto kb = whole(k.base);
            if (!(kb == c && k.path.empty()) && (kb != base || k.path != path)) continue;
            since = std::min(since, k.at);
            return true;
        }
        if (!CanHoldViewInto(ft, arr)) return true;
        if (ft->kind != TY_STRUCT) return false;
        auto inst = GetStructInst(ft);
        auto &fields = inst->st->fields;
        for (size_t i = 0; i < fields.size() && i < inst->ftypes.size(); i++) {
            if (fields[i].ispad || !inst->ftypes[i]) continue;
            if (!emptied(inst->ftypes[i], cat(path, ".", fields[i].name))) return false;
        }
        return true;
    };
    return emptied(t, "") && since != SIZE_MAX;
}

// The type of the part kill k emptied, null where its path names no field.
inline TypeExpr *TypeCheck::KillPartType(const Kill &k) {
    auto t = k.base->type;
    if (!t) return nullptr;
    if (t->kind == TY_REF) t = t->ref->sub;
    t = LoadType(t);
    size_t at = 0;
    while (at < k.path.size()) {
        auto end = k.path.find('.', at + 1);
        if (end == string::npos) end = k.path.size();
        auto name = string_view(k.path).substr(at + 1, end - at - 1);
        at = end;
        if (t->kind != TY_STRUCT) return nullptr;
        auto inst = GetStructInst(t);
        TypeExpr *ft = nullptr;
        for (size_t i = 0; i < inst->st->fields.size() && i < inst->ftypes.size(); i++)
            if (!inst->st->fields[i].ispad && inst->st->fields[i].name == name) ft = inst->ftypes[i];
        if (!ft) return nullptr;
        t = LoadType(ft);
    }
    return t;
}

// Whether a call may write into the part kill k emptied, which only a
// reference to storage holding the part, or to storage lying in it, can: an
// argument pointing at storage that may be the container, whose pointee may
// hold the part's value or lie in it, or any reference an argument leads to
// through its pointee's or its own references, whatever its root, whose
// pointee may (MayHoldValueOf either way). A callee that can name the
// caller's variables -- a nested function, one handed a function value --
// may write through one bound into the part, and is taken to.
inline bool TypeCheck::CallMayWrite(FnSpec *spec, const vector<Val> &argvals, const Kill &k) {
    auto part = KillPartType(k);
    if (!part || spec->sf->isnested || spec->isfunval) return true;
    for (auto &[name, fb] : spec->fnvals) {
        if (fb.fv) return true;
        if (fb.set) {
            for (auto sf : *fb.set)
                if (sf->isnested) return true;
        } else if (fb.named && fb.named->isnested) {
            return true;
        }
    }
    auto overlaps = [&](TypeExpr *t) {
        return t && (MayHoldValueOf(t, part) || MayHoldValueOf(part, t));
    };
    for (size_t i = 0; i < spec->argtypes.size(); i++) {
        auto pt = spec->argtypes[i];
        if (pt->kind == TY_FN) return true;
        TypeExpr *reach = pt;
        if (IsRefOrSlice(pt)) {
            reach = PointeeOf(pt);
            auto mayname = i >= argvals.size() || argvals[i].Unknown() ||
                           argvals[i].Any([&](const RootAlt &a) {
                               return !a.root ||
                                      MayAliasRoots(a.root, a.exact, k.container, true) != AL_NO;
                           });
            if (mayname && overlaps(reach)) return true;
        }
        vector<TypeExpr *> reached;
        ReachedThroughRefs(reach, reached);
        for (auto t : reached)
            if (overlaps(t)) return true;
    }
    return false;
}

// A grow-only array shrinks wherever nothing can still point into it: a
// local of this function, a caller's array reached through a reference
// parameter, a global, or an enclosing function's local. Everything in
// scope is scanned: the body's variables and its lexical parents', and in a
// function value's body those of the function running it (ShrinkScanVars).
// A shrink through a parameter or of a global is also recorded for the
// callers, and each caller's variables are scanned at its call, from that
// record -- the first caller's too, though this body is checked inside its
// check -- where the rest of the caller's statement is in view.
inline void TypeCheck::GrowOnlyShrinkAt(Node *c, const string &op, VarDef *vd,
                                        const string &what, TypeExpr *bound, TypeExpr *arr,
                                        bool viaclass) {
    struct ClassType {
        TypeCheck &tc;
        VarDef *cls;
        TypeExpr *type;
        ClassType(TypeCheck &t) : tc(t), cls(t.shrunkclass), type(t.shrunkclasstype) {}
        ~ClassType() {
            tc.shrunkclass = cls;
            tc.shrunkclasstype = type;
        }
    } classtype(*this);
    if (!bound && !vd->type && arr) {
        shrunkclass = vd;
        shrunkclasstype = arr;
    }
    if (!frames.back().spec)
        Error(c, cat("cannot ", op, " ", what, " in a global initializer (§5.1)"));
    if (vd->reusable)
        Error(c, cat("cannot ", op, " reusable pool ", what,
                     ": its slots stay live for the freelist (§5.4)"));
    // An index type relies on its array's length never going down, which
    // any index held anywhere may still name (§3.11). Where the root only
    // bounds the storage, any such array of the type may be the one.
    for (auto ia : indexarrays) {
        if (ia == vd || (bound && !vd->isglobal && ia->type && TypeEq(LoadType(ia->type), bound)))
            Error(c, cat("cannot ", op, " ", what, ia == vd ? "" : cat(", which may be ", ia->name),
                         ": an index type names ", ia->name, ", so its length never goes "
                         "down (§3.11)"));
    }
    // The elements freed: of the array itself, or for a bound, of an array
    // of its type. A parameter class's storage is not known here.
    auto arrtype = ShrunkType(vd, bound);
    CheckHeldShrinks(c, op, vd, what, true, bound);
    ShrinkScanVars([&](VarDef *v) {
        // The array's own holder is scanned as any other where the fields
        // before its tail may hold references, which may point into the tail.
        if (!v->type || (v == vd && (bound || !RefsBesideTail(arrtype)))) return;
        PathsScope ps(*this, rebuilding && ExposedLater(v));
        auto t = v->type;
        if (IsRefOrSlice(t) && !v->componentowner) {
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
                        RefMayPointInto(v, vd, true, bound);
            // A reference to a slice or to a value holding references, the
            // path to an array included, also reaches what those point at,
            // and so does a slice of such values.
            auto held = t->kind == TY_REF ? t->ref->sub : t->sub;
            auto via = !into && HoldsPlainRef(held) &&
                       HeldRefsMayPointInto(v, v->ref, t, vd, bound, true);
            if ((!into && !via) || !UsedAfter(v)) return;
            if (via)
                Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is still used: ",
                             t->kind == TY_SLICE
                                 ? held->kind == TY_SLICE
                                       ? "the slices it views may point into it"
                                       : "what it views may hold a reference or slice into it"
                             : held->kind == TY_SLICE
                                 ? "the slice it refers to may point into it"
                                 : "what it refers to may hold a reference or slice into it",
                             " (§5.1)"));
        } else {
            // Any other value holds references only where a store put
            // them, and every store this function can see is on record
            // (§9.2); a type without a plain reference or slice in it
            // (flat, or linked by relative references only) has no room
            // for one.
            if (!HoldsPlainRef(t)) return;
            Line where;
            size_t hit = 0;
            TypeExpr *via = nullptr;
            // What a part a kill emptied held before it is gone, and so are
            // the views a path the holder keeps may have been followed to.
            auto from = LiveEventBase(v);
            size_t since;
            if (!pathslive && KilledFor(v, v, LoadType(t), arrtype, since))
                from = std::max(from, since);
            if (!HolderMayPointInto(v, vd, arrtype, from, &where, &hit, &via) || !UsedAfter(v))
                return;
            auto stored = !via ? "a reference into it"
                          : via->kind == TY_SLICE
                              ? "a reference to a slice that may point into it"
                              : "a reference to what may hold a reference or slice into it";
            // A store later in the loop body than the shrink, which the
            // next iteration reaches (a pass before this one recorded it).
            if (CarriedEvent(hit))
                Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is in scope: ",
                             stored, " is stored there at ", Where(where),
                             ", which the next iteration reaches (§5.1)"));
            Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is still used: ",
                         stored, " was stored there at ", Where(where), " (§5.1)"));
        }
        auto why = v->ref.Exact() ? string() : ReadBackWhy(v->ref);
        Error(c, cat("cannot ", op, " ", what, " while ", v->name,
                     " is still used: it may hold a reference or slice into it",
                     why.empty() ? "" : "; ", why, " (§5.1)"));
    });
    // An array only the storage behind a parameter's class leads to is the
    // callers' to name: the class's bound stands for it at every call, where
    // the argument says what that storage holds.
    if (viaclass) return;
    // What the other globals hold may yet be stored by functions not checked
    // so far: they are judged once every one has been.
    if (vd->isglobal) NoteGlobalShrink(c, cat("cannot ", op, " ", what), vd, bound);
    NoteLiveViews(c, cat("cannot ", op, " ", what), vd, what, true, bound);
    NoteShrink(vd, bound, SB_UNBALANCED);
    NoteShrinkOrder(vd, bound);
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
    auto reach = false;
    if (auto gs = StoredIntoGrowShrink(v, roots, t, !isrs, &reach))
        Error(at, NeverStoredError(gs, MayPointWording(roots, gs), reach));
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
    madestores.push_back(e);
    NoteSlotStore(e);
    if (e.container->type || e.container->isglobal) return;
    auto spec = CurRealFrame().spec;
    if (!spec) return;
    // The callee's class event a replayed store came from, for the entry it
    // lands in: the earliest of those, where any is (Replay).
    auto origin = [&](size_t m) {
        if (!replay.classorigins || m < replay.classbase) return;
        auto &os = *replay.classorigins;
        auto k = m - replay.classbase;
        if (k >= os.size()) {
            os.resize(k + 1, replay.origin);
            return;
        }
        os[k] = os[k] < 0 || replay.origin < 0 ? -1 : std::min(os[k], replay.origin);
    };
    // A cycle's rounds map a back edge's record onto the same class roots
    // again: one entry per fact, out of a class's storage alone only where
    // every store of it is.
    auto &evs = spec->record.classevents;
    for (size_t m = 0; m < evs.size(); m++) {
        auto &o = evs[m];
        if (o.container == e.container && o.root == e.root && o.src == e.src &&
            o.exact == e.exact && o.byteview == e.byteview && o.bound == e.bound &&
            o.slot == e.slot && o.sliceref == e.sliceref && !o.pointee == !e.pointee &&
            (!o.pointee || TypeEq(o.pointee, e.pointee)) &&
            !o.reached == !e.reached && (!o.reached || TypeEq(o.reached, e.reached))) {
            o.classread = o.classread && e.classread;
            origin(m);
            return;
        }
    }
    evs.push_back(e);
    origin(evs.size() - 1);
}

// A store that may write a parameter's view joins its aggregate contents
// (VarDef::heldholder), or its slice binding (VarDef::heldslice): a
// store into the slot's own class, or into any other class or bound, which
// may be that slot where some call site passes it to both (§3.4), unless it
// cannot hold a slice of the slot's type. The views are those of the bodies
// whose classes the code being checked can name: its own and its lexical
// parents', and those of the bodies its function values were written in.
// Only the activation's own stores name roots its body can bind the variable
// to: a store checked in a body nested in it, or in a function value it
// wrote, leaves an aggregate view holding the class's contents, which the
// caller judges by the storage's stores (RootAlt::classread), or a slice
// binding bounded by the slot's class. Its callers' calls map the stored
// roots as they reach the body.
inline void TypeCheck::NoteSlotStore(const StoreEvent &e) {
    auto x = e.container;
    if (x->type || x->isglobal || IsTemp(x)) return;
    auto own = CurRealFrame().spec;
    set<FnSpec *> seen;
    for (auto fi = (int)frames.size() - 1; fi >= RealFrameIndex(); fi--) {
        for (auto s = frames[fi].lexspec; s && seen.insert(s).second; s = s->lexparent) {
            for (auto slot : s->classroots) {
                if (slot && slot->heldholder) {
                    // Distinct parameter classes need not be disjoint.
                    // A store through any of them may fill this holder.
                    auto h = slot->heldholder;
                    RootAlt a { e.root, e.exact };
                    if (h->ownerspec != own) a = { slot, false, nullptr, false, true };
                    AddContents(h, a, e.classread, e.src);
                    if (e.byteview && !h->contentbyteview) {
                        h->contentbyteview = true;
                        NoteFact(h);
                    }
                }
                auto h = slot ? slot->heldslice : nullptr;
                if (!h) continue;
                if (x != slot && ((e.pointee && !TopConstEq(e.pointee, h->type->sub)) ||
                                  (e.reached && !CanContain(e.reached, h->type))))
                    continue;
                RootAlt a { e.root, e.exact };
                if (h->ownerspec != own) a = { slot, false };
                auto changed = h->ref.Add(a);
                if (e.byteview && !h->ref.byteview) {
                    h->ref.byteview = true;
                    changed = true;
                }
                if (changed) NoteFact(h);
            }
        }
    }
}

// One store on record per place the value may point (§9.2). The container's
// contents grow by the same: a container that is itself a reference or a
// slice has none -- what it points at is its binding -- and neither has a
// parameter's class root, which stands for storage of the caller's, whose
// contents are the caller's to know.
inline void TypeCheck::RecordStore(VarDef *container, const Roots &roots, bool byteview,
                                    TypeExpr *pointee, VarDef *src, TypeExpr *reached,
                                    bool bound, bool slot, bool sliceref) {
    if (!container) return;
    // Putting a container's own read-back contents back into it adds no
    // incoming lifetime. Keep this distinction before discarding src. It
    // may put them into a part a kill emptied, though, as may a store of a
    // value that points nowhere yet, neither on record.
    if (src == container) {
        DropKills(container);
        return;
    }
    auto holds = container->type &&
                 (!IsRefOrSlice(container->type) || container->componentowner);
    container->contentbyteview |= byteview;
    if (roots.Unknown()) DropKills(container);
    if (holds && roots.Unknown()) container->contents.unknown = true;
    for (auto &a : roots.alts) {
        // A view the storage of a parameter's class held was read out of
        // that storage (RootAlt::classread). Stored back into that storage
        // it adds nothing; into storage the container only leads to
        // (`bound`), which may outlive what the view points into, it is a
        // store like any other.
        auto from = a.classread ? a.root : a.from;
        if (!a.exact && from == container && !bound) {
            DropKills(container);
            continue;
        }
        StoreEvent e;
        e.container = container;
        e.root = a.root;
        // A temporary was filled by whatever made it, not by stores on
        // record, so it is never the source: the value's own root bounds
        // what it holds (StoreSource).
        auto copied = StoreSource(src);
        e.src = copied;
        // A reference read back out of a container inexactly (§9.5) points
        // at whatever was stored into that container: its stores are the
        // precise answer, where a bound would implicate every sibling.
        if (!e.src && !a.exact) e.src = StoreSource(from);
        // Out of a parameter's class, what was stored holds what its storage
        // holds only where it came out of that storage alone: a holder that
        // lies there alone (Val::holderfrom), or a view the read says it
        // held.
        e.classread = IsClassRoot(e.src) && (copied || a.classread);
        e.exact = a.exact;
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.reached = reached;
        e.bound = bound;
        e.slot = slot;
        e.sliceref = sliceref;
        if (fitnode) e.at = fitnode->line;
        AddStoreEvent(e);
        if (holds) AddContents(container, a, e.classread, e.src);
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
// a value rooted at argument q's, a view's class standing for the slice its
// argument's slot held (Val::held). Where that argument's root only bounds
// the storage, or the store went into storage the class only leads to, it
// is a store into each storage there that can hold what the store reached
// (StoreEvent::reached, ShrinkTargets), which the value must outlive (§9.2):
// the body saw only the bound. A store into the slot a reference to a slice
// names assigns the slice variable each such storage may be (StoreIntoSlot).
// A store into a holder grows its contents as the caller's own would, which
// a loop around the call feeds back (NoteFact).
inline void TypeCheck::ApplyCalleeStores(FnSpec *spec, vector<Val> &argvals, Node *at,
                                         vector<int> *origins) {
    auto base = storeevents.size();
    // The class event each store added from `from` on replays.
    auto tag = [&](size_t from, int origin) {
        if (!origins) return;
        origins->resize(storeevents.size() - base, -1);
        for (auto i = std::max(from, base); i < storeevents.size(); i++)
            (*origins)[i - base] = origin;
    };
    // What class root cr of the callee stands for here: the argument's roots,
    // or for a view the slice its slot held (Val::held).
    auto classat = [&](VarDef *cr, Roots &out) -> int {
        bool view = false;
        auto p = ClassParam(spec, cr, view);
        if (p >= 0 && p < (int)argvals.size()) out = MapClass(spec, cr, argvals);
        return p;
    };
    auto paramof = [&](VarDef *cr) -> int {
        for (size_t p = 0; p < spec->params.size() && p < argvals.size(); p++)
            if (cr && spec->params[p]->ref.Root() == cr) return (int)p;
        return -1;
    };
    auto push = [&](VarDef *container, const RootAlt &a, TypeExpr *pointee, VarDef *src,
                    bool classread, bool byteview, TypeExpr *reached, bool bound, bool slot,
                    bool sliceref) {
        // A view the storage of one of the caller's classes held, passed as
        // the argument: it holds what that storage holds, as the same value
        // stored here would (RecordStore).
        if (!src && !a.exact && a.classread && IsClassRoot(a.root)) {
            src = a.root;
            classread = true;
        }
        if (!container || (src == container && !bound)) return;
        StoreEvent e;
        e.container = container;
        e.root = a.root;
        e.src = src;
        e.classread = classread;
        e.exact = a.exact;
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.at = at->line;
        e.reached = reached;
        e.bound = bound;
        e.slot = slot;
        e.sliceref = sliceref;
        container->contentbyteview |= byteview;
        if (container->type && (!IsRefOrSlice(container->type) || container->componentowner))
            AddContents(container, a, classread, src);
        AddStoreEvent(e);
    };
    // A class root of the callee, as seen from here (classat), each root
    // only a bound where the callee's was (Bounds).
    auto mapped = [&](VarDef *cr, bool exact) -> Roots {
        Roots r;
        if (classat(cr, r) < 0) {
            r.Set(cr, exact);
            return r;
        }
        return exact ? r : Bounds(r);
    };
    // A class as the container a stored value was copied or read out of, as
    // seen from here: the one container the argument, or the slice a view's
    // slot held, names exactly, if it does (StoreSource). Anywhere else the
    // value's mapped roots bound it: an argument that may point at any of
    // several places, or only within one, or at a slice variable's slot,
    // whose binding says what it holds. What came out of a class's storage
    // alone (StoreEvent::classread) still did where that container is one
    // of the caller's classes.
    auto source = [&](VarDef *src) {
        Roots r;
        if (classat(src, r) < 0) return src;
        return r.Exact() ? StoreSource(r.alts[0].root) : nullptr;
    };
    NoteClassUses(spec, argvals);
    // The record read: none in a cycle's first round (RecordOf), whose back
    // edge stores nothing yet.
    auto rec = RecordOf(spec);
    if (!rec) return;
    // A function value's body, checked inside the callee, stores values
    // rooted at the callee's parameters into its own lexical containers, as
    // a nested function's body does into its parents' variables: those roots
    // are this call's arguments, one event per place. Such a variable holds
    // them (VarDef::contents) instead of the callee's classes, which mean
    // nothing once its activation has ended, and a loop around the call feeds
    // that back; the check then stands for this call alone (FnSpec::storesout).
    // A reference or slice variable such a body rebinds is bound to them the
    // same way, as the call's result would be (RetAltVal). A global is left
    // to the judgement of the globals, which follows its stores through the
    // calls that passed each class (CheckGlobalShrinks).
    // Cached callee locals retain the depth of their original check. A later
    // deeper call does not make them lexical variables of that caller.
    auto lexical_container = [&](VarDef *x) {
        auto named = false;
        EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
            named = named || vars[i] == x;
        });
        return named;
    };
    if (!spec->inprogress) {
        Roots unused;
        auto ofcallee = [&](const RootAlt &a) { return classat(a.root, unused) >= 0; };
        EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
            auto x = vars[i];
            if (!x->type || !IsRefOrSlice(x->type) || x->isglobal || !x->ref.Any(ofcallee))
                return;
            spec->storesout = true;
            Roots bound;
            for (auto &a : x->ref.alts)
                if (ofcallee(a)) bound.Add(RetAltVal(spec, a, argvals, x->type, at));
            erase_if(x->ref.alts, ofcallee);
            AddBinding(x, bound);
        });
    }
    auto end = min(rec->eventend, storeevents.size());
    for (auto i = rec->eventstart; i < end; i++) {
        auto e = storeevents[i];
        // A call back into a body still being checked leaves the events of
        // that body's own variables as they are: a cycle's rounds keep the
        // variables, so they are those of the activation being checked, which
        // this call's arguments do not describe.
        if (spec->inprogress && e.container && e.container->ownerspec == spec) continue;
        auto r = mapped(e.root, e.exact);
        auto src = source(e.src);
        e.classread = e.classread && IsClassRoot(src);
        Roots own;
        auto x = e.container;
        if (!spec->inprogress && classat(e.root, own) >= 0 && x && x->type &&
            !IsRefOrSlice(x->type) && !x->isglobal && !IsTemp(x) && lexical_container(x)) {
            spec->storesout = true;
            erase_if(x->contents.alts, [&](const RootAlt &c) { return c.root == e.root; });
            for (auto &a : r.alts) AddContents(x, a, e.classread, src);
        }
        for (size_t k = 0; k < r.alts.size(); k++) {
            auto &a = r.alts[k];
            e.root = a.root;
            e.exact = a.exact;
            e.src = src;
            if (k == 0) storeevents[i] = e;
            else storeevents.push_back(e);
        }
    }
    // What a store into a slot put there, as seen from here. A slice loaded
    // through a reference to a slice whose elements hold no references is
    // one of the slice the argument's slot holds (SlotView), which nothing
    // else of the slot's type lying behind the class can be; anything else
    // rooted at a class is bounded by the argument (mapped).
    auto slotval = [&](const StoreEvent &e) {
        Val v;
        v.type = e.reached;
        v.byteview = e.byteview;
        auto q = paramof(e.root);
        auto qt = q >= 0 ? spec->argtypes[(size_t)q] : nullptr;
        if (qt && qt->kind == TY_REF && qt->ref->sub->kind == TY_SLICE && !e.exact &&
            !IsRefOrSlice(qt->ref->sub->sub) && !HoldsPlainRef(qt->ref->sub->sub)) {
            auto held = SlotView(argvals[(size_t)q], qt->ref->sub);
            v.TakeAlts(held);
            v.byteview = v.byteview || held.byteview;
            v.freshview = held.freshview;
        } else {
            v.TakeAlts(mapped(e.root, e.exact));
        }
        return v;
    };
    struct SlotStore {
        VarDef *root;
        bool bound;
        StoreEvent e;
        string via;
    };
    vector<SlotStore> slotstores;
    // A view the storage of callee class cr held, as seen from here: for
    // each place the argument names exactly, what that place holds -- what
    // was stored into a local holder (HeldAt), a view the storage of a class
    // of the caller's holds -- and anywhere else whatever outlives the
    // storage, as `mapped` gives it.
    auto mappedheld = [&](VarDef *cr) -> Roots {
        Roots r, out;
        classat(cr, r);
        out.unknown = r.unknown;
        for (auto &a : r.alts) {
            Roots one;
            one.alts.push_back(a);
            if (a.exact && IsClassRoot(a.root)) {
                out.Add({ a.root, false, nullptr, a.slotread, true });
                continue;
            }
            out.Add(a.exact ? HeldAt(one) : Bounds(one));
        }
        return out;
    };
    // The places the stores of one event have gone to so far, to make each
    // store once (applyevent).
    struct Made {
        const StoreEvent *e;
        VarDef *target;
        VarDef *root;
    };
    vector<Made> made;
    // A slice writes the caller's elements just as an array reference does.
    // Only a read-back from the same container preserves its existing
    // contents provenance (for example a permutation).
    auto applyevent = [&](const StoreEvent &e, const Roots &mappedroots, bool first) {
        // A place the mapping names exactly whose storage cannot hold the
        // stored reference's pointee is none it points into: a view the
        // argument's storage holds that leads elsewhere (a `u8[>..]&` beside
        // the `Link[>..]&` a stored `Link&` came out of).
        Roots r = mappedroots;
        if (e.pointee && !e.byteview)
            erase_if(r.alts, [&](const RootAlt &a) {
                return a.exact && a.root && a.root->type &&
                       !MayHoldValueOf(LoadType(a.root->type), e.pointee);
            });
        Roots cr;
        auto p = classat(e.container, cr);
        auto src = source(e.src);
        auto classread = e.classread && IsClassRoot(src);
        auto once = [&](VarDef *target, VarDef *root) {
            for (auto &m : made)
                if (m.e == &e && m.target == target && m.root == root) return false;
            made.push_back({ &e, target, root });
            return true;
        };
        if (p < 0) {
            if (e.container && e.container->type && !e.container->isglobal &&
                !lexical_container(e.container)) return;
            // Not the callee's class but a lexical parent's, which a nested
            // function or a function value's body stored into: the storage
            // the parent's callers passed, whose record carries it to them.
            for (auto &a : r.alts)
                if (once(e.container, a.root))
                    push(e.container, a, e.pointee, src, classread, e.byteview, e.reached,
                         e.bound, e.slot, e.sliceref);
            return;
        }
        // Where the argument's root only bounds the storage, or the store
        // went into storage the class only leads to, the store lands in
        // every storage there may be behind it.
        auto widen = e.bound || !cr.Exact();
        if (e.bound) cr.Weaken();
        auto pname = spec->sf->params[(size_t)p].name;
        for (auto &t : ShrinkTargets(cr, e.reached, e.slot)) {
            for (auto &a : r.alts) {
                if (widen && Depth(a.root) > Depth(t.root)) {
                    auto rname = a.root ? a.root->name : string_view("static data");
                    Error(at, cat("call ", spec->sf->name, " stores a reference rooted at ",
                                  rname,
                                  e.bound ? cat(" into what its parameter ", pname,
                                                " leads to, which may be ")
                                          : cat(" through its parameter ", pname,
                                                ", whose argument may point into "),
                                  TargetStr(t), ", which ", rname, " does not outlive (§9.2)"));
                }
                if (once(t.root, a.root))
                    push(t.root, a, e.pointee, src, classread, e.byteview, e.reached, t.bound,
                         e.slot, e.sliceref);
            }
            if (e.slot && first)
                slotstores.push_back({ t.root, t.bound, e,
                                       cat(" through ", spec->sf->name, "'s parameter ", pname,
                                           widen ? ", which may name it" : "") });
        }
    };
    // A store of a view read out of the storage of one of the callee's
    // classes holds what that storage holds, which the call's other stores
    // may add to where they land in the same storage: those are applied
    // until what they store no longer grows.
    vector<pair<const StoreEvent *, Roots>> held;
    for (auto &e : rec->classevents) {
        if (e.src == e.container && !e.bound) continue;
        Roots unused;
        if (e.classread && e.src == e.root && !e.exact && classat(e.root, unused) >= 0) {
            held.push_back({ &e, Roots {} });
            continue;
        }
        auto from = storeevents.size();
        replay.origin = (int)(&e - rec->classevents.data());
        applyevent(e, mapped(e.root, e.exact), true);
        replay.origin = -1;
        tag(from, (int)(&e - rec->classevents.data()));
    }
    for (auto again = !held.empty(), first = true; again; first = false) {
        again = false;
        for (auto &[e, last] : held) {
            auto r = mappedheld(e->root);
            if (!first && r.Same(last)) continue;
            last = r;
            again = true;
            auto from = storeevents.size();
            replay.origin = (int)(e - rec->classevents.data());
            applyevent(*e, r, first);
            replay.origin = -1;
            tag(from, (int)(e - rec->classevents.data()));
        }
    }
    // A slice loaded through one slot may be stored into another, and the
    // record keeps no order: the re-bindings are applied until none changes
    // what a slot holds.
    for (auto again = !slotstores.empty(); again;) {
        again = false;
        for (auto &s : slotstores)
            again = StoreIntoSlot(at, s.root, s.bound, s.e.reached, slotval(s.e), s.via) || again;
    }
    tag(storeevents.size(), -1);
}

// What each parameter class of the callee stands for at this call: where
// its argument may point, or for a by-value holder where the references it
// holds may (ClassArgRoots), and the container the holder is a copy of; and
// what a view's class does, the slice its argument's slot held.
inline void TypeCheck::NoteClassUses(FnSpec *spec, const vector<Val> &argvals) {
    for (size_t p = 0; p < spec->params.size() && p < argvals.size() &&
                       p < spec->argtypes.size(); p++) {
        if (auto vr = ViewClassOf(spec, p)) {
            ClassUse u;
            u.roots = argvals[p].held;
            u.byteview = argvals[p].held.byteview;
            NoteClassUse(vr, u);
        }
        auto cr = spec->params[p]->ref.Root();
        if (!IsClassRoot(cr)) continue;
        auto pt = spec->argtypes[p];
        ClassUse u;
        u.roots = ClassArgRoots(pt, argvals[p]);
        if (!IsRefOrSlice(pt) && !IsTemp(argvals[p].holderfrom)) u.src = argvals[p].holderfrom;
        u.byteview = argvals[p].byteview;
        NoteClassUse(cr, u);
        for (auto c : cr->components) {
            ClassUse projected;
            projected.roots = ProjectRoots(u.roots, c->componentbase, c->componentfield);
            NoteClassUse(c, projected);
        }
    }
}

inline void TypeCheck::NoteClassUse(VarDef *cr, const ClassUse &u) {
    auto same = [&](const ClassUse &o) {
        return o.src == u.src && o.byteview == u.byteview &&
               o.roots.Same(u.roots, Roots::Compare::GlobalReach);
    };
    auto &uses = classuses[cr];
    if (none_of(uses.begin(), uses.end(), same)) uses.push_back(u);
}

// A binding of global reference or slice variable gd, which is no store for
// FitsAt: for the judgement of the globals it is one, of where it points.
inline void TypeCheck::NoteGlobalBinding(VarDef *gd, const Roots &roots, bool byteview,
                                         TypeExpr *pointee, Line at) {
    for (auto &a : roots.alts) {
        StoreEvent e;
        e.container = gd;
        e.root = a.root;
        e.exact = a.exact;
        if (!a.exact && a.from != gd) e.src = StoreSource(a.from);
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.at = at;
        madestores.push_back(e);
    }
}

// Each thread program has a copy of the globals of its own, which grow apart
// once it runs, so an index of one copy says nothing about another (§3.11).
inline void TypeCheck::ThreadIndexError(Node *at, TypeExpr *t) {
    Error(at, cat(TypeStr(t), " cannot pass between threads: it holds an index type, and each "
                  "thread program's copy of the array it names grows on its own (§3.11)"));
}

// A shrink of global array vd, or of what vd only bounds, for
// CheckGlobalShrinks. The first shrink checked stands for the others of the
// same array, which differ in nothing that judgement asks.
inline void TypeCheck::NoteGlobalShrink(Node *at, const string &prefix, VarDef *vd,
                                        TypeExpr *bound) {
    for (auto &gs : globalshrinks)
        if (gs.arr == vd && !gs.bound == !bound && (!bound || TypeEq(gs.bound, bound))) return;
    globalshrinks.push_back({ vd, bound, at->line, prefix, Chain({}) });
}

// A global may hold a reference into a global array only where a store, or
// the binding of a reference or slice global, put one there, and once every
// function has been checked every store is on record, however late the
// function making it was checked: a shrink of the array is an error where
// another global may hold one (§5.1). A bound stands for every array of its
// type the root's references may lead to, which for a global are globals.
inline void TypeCheck::CheckGlobalShrinks() {
    if (globalshrinks.empty()) return;
    map<VarDef *, vector<size_t>> bycontainer;
    for (size_t i = 0; i < madestores.size(); i++)
        bycontainer[madestores[i].container].push_back(i);
    for (auto &gs : globalshrinks) CheckItem([&] {
        vector<VarDef *> arrays;
        if (!gs.bound) arrays.push_back(gs.arr);
        else {
            auto consider = [&](VarDef *gd) {
                if (gd && gd->type && !IsRefOrSlice(gd->type) &&
                    CanContain(LoadType(gd->type), gs.bound))
                    arrays.push_back(gd);
            };
            for (auto g : ast.globals)
                for (auto gd : g->defs) {
                    consider(gd);
                    for (auto c : gd->components) consider(c);
                }
        }
        for (auto arr : arrays) {
            auto arrtype = gs.bound ? gs.bound : LoadType(arr->type);
            for (auto g : ast.globals) {
                for (auto gd : g->defs) {
                    if (gd == arr || !gd->type || !HoldsPlainRef(gd->type)) continue;
                    // A reference to nothing the array's storage can hold,
                    // nor a u8 that may be a byte view of it, is no concern.
                    vector<TypeExpr *> ps;
                    RefPointees(gd->type, ps);
                    auto fits = false;
                    for (auto pt : ps)
                        fits = fits || (IsU8(pt) && Viewable(arrtype)) || CanContain(arrtype, pt);
                    if (!fits) continue;
                    auto into = cat(gs.prefix, ": global ", gd->name,
                                    " may hold a reference into ",
                                    gs.bound ? string(arr->name) : string("it"));
                    GlobalReach reach { *this, arr, arrtype, bycontainer, {}, {} };
                    Line where;
                    if (!reach.Holds(gd, &where)) continue;
                    ErrorIn(LocOf(gs.at), cat(into, ", stored at ", Where(where), " (§5.1)"),
                            gs.chain);
                }
            }
        }
    });
}

// Whether a reference to a pointee of this type, or a byte view, can point
// into the array.
inline bool TypeCheck::GlobalReach::Fits(TypeExpr *pointee, bool byteview) {
    if (byteview) return tc.Viewable(arrtype);
    return !pointee || tc.CanContain(arrtype, pointee);
}

// A reference rooted at r. A parameter's class stands for what the calls
// passed. An exact root is its own storage. An inexact one read out of a
// container points where the stores into that container put, and any other
// only bounds the storage, which then may be the array wherever the pointee
// fits it: nothing outlives a global.
inline bool TypeCheck::GlobalReach::Root(VarDef *r, bool exact, VarDef *from, TypeExpr *pointee,
                                         bool byteview) {
    if (tc.IsClassRoot(r)) return Class(r, exact, pointee, byteview);
    if (exact) return r == arr;
    if (from && !IsTemp(from)) return Holds(from);
    return Fits(pointee, byteview);
}

// A reference rooted at a parameter's class: where the argument of each call
// that reached it may point, each only as a bound where the reference was
// one. A holder argument copied out of a container holds what that one
// does. A class no call reached stands for nothing.
inline bool TypeCheck::GlobalReach::Class(VarDef *cr, bool exact, TypeExpr *pointee,
                                          bool byteview) {
    if (!rooted.insert({ cr, exact }).second) return false;
    auto it = tc.classuses.find(cr);
    if (it == tc.classuses.end()) return false;
    for (auto &u : it->second) {
        if (u.src && Holds(u.src)) return true;
        for (auto &a : u.roots.alts) {
            if (u.src && Covered(u.src, a.root, a.exact)) continue;
            if (Root(a.root, a.exact && exact, a.from, pointee, byteview || u.byteview))
                return true;
        }
    }
    return false;
}

// What container x holds: what the stores into it put there, and for a
// reference or slice variable where its bindings point. A parameter's class
// stands for the caller's storage behind it: what each call's argument
// points at, anything where it only bounds that. `where` gets the line of
// the store into x that leads to the array.
inline bool TypeCheck::GlobalReach::Holds(VarDef *x, Line *where) {
    if (!x || !held.insert(x).second) return false;
    if (x->type && tc.ClassOf(x->type) == SC_ROOT)
        for (auto c : x->components) if (c && Holds(c, where)) return true;
    if (tc.IsClassRoot(x)) {
        auto it = tc.classuses.find(x);
        if (it == tc.classuses.end()) return false;
        for (auto &u : it->second)
            for (auto &a : u.roots.alts)
                if (!a.exact || (a.root && Holds(a.root))) return true;
        return false;
    }
    if (auto it = bycontainer.find(x); it != bycontainer.end()) {
        for (auto i : it->second) {
            auto &e = tc.madestores[i];
            if (!Event(e)) continue;
            if (where) *where = e.at;
            return true;
        }
    }
    if (x->type && IsRefOrSlice(x->type) && x->refrootknown) {
        for (auto &a : x->ref.alts) {
            if (!Root(a.root, a.exact, a.from, tc.PointeeOf(x->type), x->ref.byteview)) continue;
            if (where) *where = x->line;
            return true;
        }
    }
    return false;
}

// A store on record. A copy of a container's contents holds what that one
// does; its own roots add only what is not among those (Covered).
inline bool TypeCheck::GlobalReach::Event(const StoreEvent &e) {
    if (e.src) {
        if (Holds(e.src)) return true;
        if (Covered(e.src, e.root, e.exact)) return false;
    }
    return Root(e.root, e.exact, nullptr, e.pointee, e.byteview);
}

// Whether a root a copy of src's contents carries is judged by following
// src's stores: src itself or a global's static bound, which is how the
// copy of a container's or a global's contents is rooted, or one of the
// roots its stores put there, no more exact than there.
inline bool TypeCheck::GlobalReach::Covered(VarDef *src, VarDef *root, bool exact) {
    if (!exact && (root == src || (!root && src->isglobal))) return true;
    for (auto &c : src->contents.alts)
        if (c.root == root && (exact || !c.exact)) return true;
    return false;
}

// Whether a store into `holder`, from event `from` on, may have put a
// reference into `arr` there: one rooted at it exactly, or one bounded by
// a root the array outlives whose pointee the array's elements can hold,
// or one to a slot whose own references may point into it
// (StoredSlotMayPointInto). `arrtype` is the type of the array whose
// elements are in question (ShrunkType), null where nothing says (a
// parameter's class no record gives a type), which lets any pointee in. A holder whose type has no slot for a view
// into those elements holds none (CanHoldViewInto), but where a reference
// to the whole array is followed into it (pathslive).
inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype,
                                          size_t from, Line *where, size_t *hit,
                                          TypeExpr **via) {
    set<VarDef *> seen;
    return HolderMayPointInto(holder, arr, arrtype, from, where, seen, hit, via);
}

inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype,
                                          size_t from, Line *where, set<VarDef *> &seen,
                                          size_t *hitat, TypeExpr **via) {
    if (!seen.insert(holder).second) return false;
    if (holder->type && ClassOf(holder->type) == SC_ROOT) {
        for (auto c : holder->components) {
            if (!c) continue;
            auto start = from;
            size_t since;
            if (!pathslive && KilledFor(c, c, LoadType(c->type), arrtype, since))
                start = std::max(start, since);
            if (HolderMayPointInto(c, arr, arrtype, start, where, seen, hitat, via)) return true;
        }
    }
    // Whatever a store put there, or a view derived from it later, lies in a
    // slot of the holder's type.
    if (!pathslive && holder->type && !IsRefOrSlice(holder->type) &&
        !CanHoldViewInto(LoadType(holder->type), arrtype))
        return false;
    auto contains = [&](TypeExpr *pt) { return !arrtype || CanContain(arrtype, pt); };
    auto found = [&](size_t i, TypeExpr *through) {
        auto &e = storeevents[i];
        if (!e.src || through) *where = e.at;
        if (hitat) *hitat = i;
        if (via && through) *via = through;
        return true;
    };
    for (auto i = from; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        if (e.container != holder || StoredLater(i)) continue;
        auto hit = false;
        if (e.src && e.src->isglobal) {
            // A global's stores may come from functions not checked yet, so
            // its type decides: any reference it can hold to something the
            // array can contain.
            vector<TypeExpr *> ps;
            if (e.src->type) RefPointees(e.src->type, ps);
            for (auto pt : ps)
                hit |= (IsU8(pt) && (!arrtype || Viewable(arrtype))) || contains(pt);
        } else if (e.src && !e.src->type && e.classread) {
            // Out of the storage of a parameter's class alone: what the
            // activation stored there, from its first event on, the stores
            // its callers made there being theirs to judge (NoteLiveViews).
            hit = HolderMayPointInto(e.src, arr, arrtype, 0, where, seen, nullptr, via);
        } else if (e.src && !e.src->type) {
            // Read out of a parameter's class: storage of the caller's,
            // whose stores this function cannot see, so the class bounds
            // what was read, as an inexact root would.
            hit = Depth(arr) <= Depth(e.src) && (!e.pointee || contains(e.pointee));
            if (hit) *where = e.at;
        } else if (e.src) {
            // A copy of another container's contents: whatever that one
            // holds, from its own first event on.
            hit = HolderMayPointInto(e.src, arr, arrtype, 0, where, seen, nullptr, via);
        } else if (e.exact) {
            hit = e.root == arr;
        } else if (e.root) {
            hit = Depth(arr) <= Depth(e.root) && (!e.pointee || contains(e.pointee));
        }
        if (hit) return found(i, nullptr);
    }
    // What was stored may also be a reference to a slot holding references,
    // a slice's or a holder's, rooted where the event says or bounded by the
    // class it was read out of; a copy of another container's contents was
    // followed above, and a global's leads only to globals. What came out of
    // a class's storage alone leads where that storage's references do, as
    // one to the storage itself would: its own stores, followed above, and
    // for a slice's slot, which may be a slice variable, anything the class
    // outlives.
    for (auto i = from; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        if (e.container != holder || (e.src && (e.src->type || e.src->isglobal)) ||
            StoredLater(i))
            continue;
        auto through = e.src ? StoredSlotMayPointInto(holder, e.pointee, e.sliceref, e.src,
                                                      e.classread, arr, arrtype, where, seen)
                             : StoredSlotMayPointInto(holder, e.pointee, e.sliceref, e.root,
                                                      e.exact, arr, arrtype, where, seen);
        if (through) return found(i, through);
    }
    return false;
}

// Whether a reference stored into `holder`, rooted at r (exactly, or only
// bounded by it), to a slot of type `pointee` -- null where the record keeps
// none, as for a holder's copied contents, which may be any the holder's
// type refers to (RefSlots) -- leads into `arr` through what that slot
// holds: a slice's slot, or a holder (§5.1). `sliceref`: the stored
// reference is one to a slice. Returns the slot's type where it may. A
// holder named exactly holds what its own stores put there. So does a
// parameter's class named so, of what the activation stored there, where
// the slot is its contents -- the caller's holder, or an element of the
// caller's array: what the callers put there, each call judges by its
// record of the storage its argument names (the pair NoteLiveViews keeps,
// LiveShrink::contents). A slice's slot the class names may be a slice
// variable, whose binding no store record describes. That and any other
// slot -- a slice variable, a parameter's class standing for the caller's
// slice slot, whatever view of its slice the class has (VarDef::heldslice)
// -- is taken to hold anything that outlives it, an array at its depth or
// outside it. A global can hold a view of a global array only, and a shrink
// of one judges every global (CheckGlobalShrinks).
inline TypeExpr *TypeCheck::StoredSlotMayPointInto(VarDef *holder, TypeExpr *pointee,
                                                   bool sliceref, VarDef *r, bool exact,
                                                   VarDef *arr, TypeExpr *arrtype, Line *where,
                                                   set<VarDef *> &seen) {
    if (!r || r->isglobal) return nullptr;
    vector<pair<TypeExpr *, bool>> slots;
    if (pointee) slots.push_back({ pointee, sliceref });
    else if (holder->type) RefSlots(holder->type, slots);
    auto byteview = r->contentbyteview || (r->type && IsRefOrSlice(r->type) && r->ref.byteview);
    auto reaches = [&](TypeExpr *st) {
        if (!HoldsPlainRef(st)) return false;
        vector<TypeExpr *> ps;
        ReachedThroughRefs(st, ps);
        for (auto pt : ps)
            if (!arrtype || CanContain(arrtype, pt) ||
                (byteview && IsU8(pt) && Viewable(arrtype)))
                return true;
        return false;
    };
    // The first slot whose references may lead into arr, and the first of
    // each kind: a slice's slot, or contents.
    TypeExpr *via = nullptr, *slicevia = nullptr, *contentsvia = nullptr;
    for (auto [st, slice] : slots) {
        auto &kind = slice ? slicevia : contentsvia;
        if (kind || !reaches(st)) continue;
        kind = st;
        if (!via) via = st;
    }
    if (!via) return nullptr;
    if (exact && r->type && !IsRefOrSlice(r->type))
        return HolderMayPointInto(r, arr, arrtype, 0, where, seen, nullptr) ? via : nullptr;
    if (exact && IsClassRoot(r)) {
        if (slicevia && Depth(arr) <= Depth(r)) return slicevia;
        if (contentsvia && HolderMayPointInto(r, arr, arrtype, 0, where, seen, nullptr))
            return contentsvia;
        return nullptr;
    }
    return Depth(arr) <= Depth(r) ? via : nullptr;
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

// RefPointees, each with whether the reference is one to a slice: its root
// names the storage the slice's slot lies in, which may be a slice variable,
// whose binding says what the slot holds, rather than storage whose store
// record says it (StoredSlotMayPointInto).
inline void TypeCheck::RefSlots(TypeExpr *t, vector<pair<TypeExpr *, bool>> &out) {
    switch (t->kind) {
        case TY_REF:
            if (t->ref->lenstorage < 0) {
                auto st = LoadType(t->ref->sub);
                out.push_back({ st, st->kind == TY_SLICE });
            }
            return;
        case TY_SLICE: out.push_back({ t->sub, false }); return;
        case TY_ARRAY: RefSlots(t->arr->sub, out); return;
        default: EachField(t, [&](TypeExpr *ft) { RefSlots(ft, out); }); return;
    }
}

// Whether a value of type t holds plain references or slices outside the
// elements of its resizable array: in the fields before its tail, at any
// depth of nested tails (§3.4).
inline bool TypeCheck::RefsBesideTail(TypeExpr *t) {
    if (t->kind == TY_ARRAY) return false;
    return AnyField(t, [&](TypeExpr *ft) {
        return ResizableArrayIn(ft) ? RefsBesideTail(ft) : HoldsPlainRef(ft);
    });
}

// Whether storage of type t has a slot, at any remove through its
// references, for a reference or slice into the elements of an array of
// type arr (null: of any type), or for a byte view of one. A view derived
// from a reference stored there is stored in such a slot too, so a holder
// without one never refers into the array (§5.1). A reference to a whole
// resizable value is the path to it, which no array's elements hold (§3.3).
inline bool TypeCheck::CanHoldViewInto(TypeExpr *t, TypeExpr *arr) {
    vector<TypeExpr *> ps;
    ReachedThroughRefs(t, ps);
    for (auto pt : ps) {
        if (ClassOf(pt) == SC_RESIZABLE) continue;
        if (!arr || CanContain(arr, pt) || (IsU8(pt) && Viewable(arr))) return true;
    }
    return false;
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
    ShrinkScanVars([&](VarDef *v) {
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
                         return !DifferentComponents(a.root, a.exact || a.projected, root, !bound) &&
                                !a.slotread && (a.root == root ||
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
    // A projected class is a path-bearing effect. Keeping the path in the
    // external map lets call-site translation project each possible owner.
    if (root->componentowner && IsClassRoot(root)) {
        external(current, root);
        return;
    }
    if (root->type) {
        if (root->isglobal || root->ownerspec != current) external(current, root);
        return;
    }
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        auto spec = frames[fi].spec;
        if (!spec) continue;
        auto found = false;
        // A view's contents are a different effect target from the holder
        // or slice slot itself, even though one parameter reaches both.
        for (size_t i = 0; i < spec->params.size(); i++) {
            if (RefRootOf(spec->params[i]) == root) {
                param(spec, (int)i);
                found = true;
            }
            if (ViewClassOf(spec, i) == root) {
                param(spec, (int)(spec->params.size() + i));
                found = true;
            }
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
inline void TypeCheck::NoteShrink(VarDef *root, TypeExpr *bound, ShrinkBalance balance) {
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
    NoteRootEvent(root,
                  [&](FnSpec *s, int i) {
                      if (bound) note(s->record.shrinkparambounds, i);
                      else mark(s->record.shrinkparams, i);
                  },
                  [&](FnSpec *s, VarDef *r) {
                      if (bound) note(s->record.shrinkexternalbounds, r);
                      else mark(s->record.shrinkexternals, r);
                  });
}

// Records where a grow-only shrink stands in the body for its call sites
// (FnRecord::ShrinkOrder), beside NoteShrink's entry. Only the activation
// being checked knows its own kills and class events: a shrink recorded on
// a lexical parent's record, or in a body whose record a recursive cycle's
// rounds replace, counts every class event and no kill.
inline void TypeCheck::NoteShrinkOrder(VarDef *root, TypeExpr *bound) {
    auto current = CurRealFrame().spec;
    auto note = [&](FnSpec *s, int param, VarDef *external) {
        auto known = s == current && !s->isfunval && !s->incycle && !s->sf->isrec;
        FnRecord::ShrinkOrder o;
        o.param = param;
        o.external = external;
        o.bound = bound;
        o.before = known ? s->record.classevents.size() : SIZE_MAX;
        // A callee's shrink judged at a call stands where the callee's own
        // record puts it: after the class events the call added from the
        // callee's earlier ones, and before those from its later ones.
        if (known && callorder.classorigins) {
            auto &os = *callorder.classorigins;
            auto b = callorder.classbase;
            for (size_t k = 0; k < os.size(); k++)
                if (os[k] < 0 || (size_t)os[k] < callorder.before)
                    b = std::max(b, callorder.classbase + k + 1);
            if (o.before <= callorder.classbase + os.size()) o.before = std::min(o.before, b);
        }
        for (size_t p = 0; known && p < s->params.size() && p < s->argtypes.size(); p++) {
            if (!IsPlainRef(s->argtypes[p])) continue;
            for (auto &k : cur.kills)
                if (k.base == s->params[p] && k.container == RefRootOf(s->params[p]))
                    o.kills.push_back({ (int)p, k.path });
        }
        for (auto &e : s->record.shrinkorders) {
            if (e.param != param || e.external != external || !e.bound != !bound ||
                (bound && !TypeEq(e.bound, bound)))
                continue;
            e.before = std::max(e.before, o.before);
            erase_if(e.kills, [&](const pair<int, string> &k) {
                return find(o.kills.begin(), o.kills.end(), k) == o.kills.end();
            });
            return;
        }
        s->record.shrinkorders.push_back(o);
    };
    NoteRootEvent(root, [&](FnSpec *s, int i) { note(s, i, nullptr); },
                  [&](FnSpec *s, VarDef *r) { note(s, -1, r); });
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
// call, no such length is shorter than the one it began with. A writable
// reference bound to the `let` may have changed it (§4.4), which makes the
// resize an ordinary shrink, and one bound after the resize relied on it an
// error (NoteWritableRef), since the balance has been recorded by then.
inline bool TypeCheck::ResizesToMark(Node *recv, Node *len) {
    auto id = Is<Ident>(len);
    auto m = id ? LookupVar(id->name, id->ns) : nullptr;
    auto spec = CurRealFrame().spec;
    if (!m || !m->markof || m->isvar || !spec || m->ownerspec != spec || m->refwrite ||
        !SamePath(m->markof, recv))
        return false;
    if (!m->markuse) m->markuse = len;
    return true;
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
// (FitsAt, ApplyCalleeStores), and where the slot is one a reference to a
// slice names (`slots`), a slice variable may be it. With `classbound`, the
// arrays a bound that is a parameter's class of the activation stands for --
// what was read out of the storage behind it, or what that storage leads to
// -- are only where the class leads (ShrinkTarget::viaclass), the class
// itself coming first.
inline vector<TypeCheck::ShrinkTarget> TypeCheck::ShrinkTargets(const Roots &roots,
                                                                 TypeExpr *arr, bool slots,
                                                                 bool classbound) {
    vector<ShrinkTarget> out;
    auto add = [&](VarDef *r, bool bound, bool viaclass) {
        for (auto &t : out)
            if (t.root == r) {
                t.bound = t.bound && bound;
                t.viaclass = t.viaclass && viaclass;
                return;
            }
        out.push_back({ r, bound, viaclass });
    };
    auto own = classbound ? CurRealFrame().spec : nullptr;
    for (auto &a : roots.alts) {
        auto root = a.root;
        if (a.exact) {
            if (root) add(root, false, false);
            continue;
        }
        auto behind = own && IsClassRoot(root) && (!a.from || a.from == root) &&
                      find(own->classroots.begin(), own->classroots.end(), root) !=
                          own->classroots.end();
        auto cands = RootCandidates(arr, Depth(root), false, true, slots);
        if (root) {
            auto isbound = false;
            if (!IsTemp(root)) {
                isbound = true;
                for (auto &c : cands.alts) if (c.root == root && c.exact) isbound = false;
            }
            add(root, isbound, false);
        }
        for (auto &c : cands.alts)
            if (c.root && c.root != root) add(c.root, !c.exact, behind);
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
inline void TypeCheck::ShrinkThrough(Node *at, const string &verb, const string &recv,
                                     const Roots &roots, TypeExpr *arr, ShrinkBalance balance) {
    auto root = roots.Root();
    auto growonly = GrowOnlyTail(arr);
    for (auto &t : ShrinkTargets(roots, arr, false, growonly)) {
        auto bound = t.bound ? arr : nullptr;
        if (growonly) {
            auto what = t.root == root ? string(t.root->name)
                                       : cat(t.root->name, " (which ", recv, " may point at)");
            GrowOnlyShrinkAt(at, verb, t.root, what, bound, arr, t.viaclass);
        } else {
            auto what = t.root == root ? recv : cat(t.root->name, ", which ", recv,
                                                    " may point at");
            ShrinkGrowShrink(at, cat(verb, " ", recv), t.root, what, bound, balance);
        }
        NoteShrinkEvent(at, t.root, bound, cat(verb, " ", recv), arr);
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
    // What a reference or slice of type t with provenance p may point into:
    // its pointee, unless it is the path to a whole resizable value, and,
    // for a reference to a slice or (§5.1) to anything else holding
    // references, or a slice of those, what those point into: where the
    // slice points (SlotView, without noting a slice variable's root as
    // read), and for a slice of holders what those hold, or where the
    // holders' references may (HeldViews). A view the storage of a
    // parameter's class held, but in a `var`, is one of what that storage
    // holds (RootAlt::classread): the class's contents, as HeldViews takes
    // them. An assignment's location is overwritten before it is read, and a
    // slot a for loop reads again holds its sequence, which is held itself:
    // only the slot counts. What a rendering walks in place (`inplace`) is
    // its own pointee, a path to a resizable or not, which a slot may have
    // held whatever kind of array shrinks.
    auto arrtype = ShrunkType(root, bound);
    auto views = [&](const Prov &p, TypeExpr *t, bool slotonly, bool isvar, bool inplace,
                     VarDef *base) {
        vector<LiveView> out;
        if (inplace || t->kind != TY_REF || ClassOf(t->ref->sub) != SC_RESIZABLE) {
            auto own = p;
            if ((growonly || inplace) && !isvar) {
                Prov held = p;
                erase_if(held.alts, [](const RootAlt &a) { return !a.classread; });
                erase_if(own.alts, [](const RootAlt &a) { return a.classread; });
                auto first = out.size();
                HeldViews(held, t, false, out);
                for (auto i = first; i < out.size(); i++) out[i].inplace = inplace;
            }
            out.push_back({ own, PointeeOf(t), false, false, inplace });
        }
        if (slotonly || inplace) return out;
        if (t->kind == TY_SLICE) {
            if (growonly && HoldsPlainRef(t->sub)) HeldViews(p, t->sub, isvar, out);
            return out;
        }
        if (t->kind != TY_REF) return out;
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
            out.push_back({ sv, sub->sub, true });
            if (growonly && HoldsPlainRef(sub->sub)) HeldViews(sv, sub->sub, isvar, out);
        } else if (growonly && HoldsPlainRef(sub)) {
            HeldViews(p, sub, isvar, out, nullptr, base, arrtype);
        }
        return out;
    };
    // Whether the callers judge where a view may point by alternative a: a
    // slot read points into no grow-shrink array (§5.2), as the scans say,
    // though what a reference to a slice leads to may, and so may a walk's
    // path. What a class's storage holds is theirs to judge whatever array
    // shrinks, the class's own included.
    // What a class's storage holds never refers into the array where its
    // type has no slot for a view into the array's elements (CanHoldViewInto).
    auto judges = [&](const LiveView &w, const RootAlt &a) {
        if (growonly && !pathslive && w.contents && !w.inplace && w.pointee && arrtype &&
            !CanHoldViewInto(w.pointee, arrtype))
            return false;
        return (w.contents || CallersJudge(a.root, root)) &&
               (growonly || w.reached || w.inplace || !a.slotread);
    };
    // Every place a view may point that the callers judge, as a pair each.
    auto note = [&](const LiveView &w, const string &name) {
        for (auto &a : w.p.alts) {
            if (!judges(w, a)) continue;
            LiveShrink ls { .shrunk = root, .shrunkexact = !bound, .bound = bound,
                            .live = a.root, .liveexact = a.exact, .liveprojected = a.projected,
                            .pointee = w.pointee,
                            .byteview = w.p.byteview, .growonly = growonly, .name = name,
                            .contents = w.contents, .inplace = w.inplace };
            if (!NoteLiveShrink(ls, current))
                Error(at, cat(prefix, " while ", name, " is still used: it may refer into ", what,
                              growonly ? " (§5.1)" : " (§5.2)"));
        }
    };
    auto judged = [&](vector<LiveView> &vs) {
        vs.erase(std::remove_if(vs.begin(), vs.end(),
                                [&](const LiveView &w) {
                                    return !w.p.Any(
                                        [&](const RootAlt &a) { return judges(w, a); });
                                }),
                 vs.end());
        return !vs.empty();
    };
    HeldOperands([&](const Held &h) {
        auto vs = views(h.v, h.v.type, h.location || h.reread, false, h.inplace, nullptr);
        if (!judged(vs)) return;
        auto name = ExprStr(h.node);
        if (name.find('\n') != string::npos)
            name = h.loop ? "a for loop's sequence" : "an earlier expression value";
        for (auto &w : vs) note(w, name);
    });
    ShrinkScanVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        PathsScope ps(*this, rebuilding && ExposedLater(v));
        auto t = v->type;
        vector<LiveView> vs;
        if (IsRefOrSlice(t) && !v->componentowner) {
            if (!v->refrootknown) return;
            vs = views(v->ref, t, false, v->isvar, false, v);
        } else if (growonly && HoldsPlainRef(t)) {
            // A grow-only array's views may be stored (§5.1): the holder's
            // store record says where its references lead, since the kills
            // that emptied it.
            set<VarDef *> seen;
            auto from = LiveEventBase(v);
            size_t since;
            if (!pathslive && KilledFor(v, v, LoadType(t), arrtype, since))
                from = std::max(from, since);
            RecordedViews(v, from, t, vs, seen);
        }
        if (!judged(vs) || !UsedAfter(v)) return;
        for (auto &w : vs) note(w, string(v->name));
    });
}

// What the stores into holder h, of type ht, from event `from` on put there,
// as views (§5.1): each stored reference as StoredViews takes it, and what
// came out of the storage of a parameter's class alone (StoreEvent::
// classread) as what that storage holds, which is the callers' to judge as
// well: a view of the class's contents (LiveShrink::contents) of h's type,
// whose references lead wherever what came out of there does. `seen` holds
// the holders followed so far.
inline void TypeCheck::RecordedViews(VarDef *h, size_t from, TypeExpr *ht,
                                     vector<LiveView> &out, set<VarDef *> &seen) {
    if (!seen.insert(h).second) return;
    EachHolderRoot(h, from, [&](const StoreEvent &e) {
        if (e.classread) {
            Prov cv;
            cv.Set(e.src, true);
            cv.byteview = e.byteview || e.src->contentbyteview;
            out.push_back({ cv, ht, true, true });
            return;
        }
        StoredViews(e.root, e.exact, e.pointee, e.sliceref, e.byteview, ht, out, seen);
    });
}

// A reference stored into a holder of type ht, rooted at r (exactly, or only
// bounded by it), as views (§5.1): where it points, and, where it refers to
// a slot holding references -- a holder, an array's elements a slice views,
// a slice variable -- what that slot leads to, as the scan follows it
// (StoredSlotMayPointInto): a holder named exactly by its own record, a
// parameter's class named so, where the slot is its contents rather than a
// slice's slot, by the activation's own stores into it and, for what the
// callers put there, by the class's contents as a view of their own, which
// each call judges (LiveShrink::contents), anything else but a global by its
// root, as a bound. A stored value keeps no pointee where it is a copy of a
// holder's contents, which may refer to any slot ht can.
inline void TypeCheck::StoredViews(VarDef *r, bool exact, TypeExpr *pointee, bool sliceref,
                                   bool byteview, TypeExpr *ht, vector<LiveView> &out,
                                   set<VarDef *> &seen) {
    Prov ep;
    ep.Set(r, exact);
    ep.byteview = byteview;
    out.push_back({ ep, pointee, true });
    if (!r || r->isglobal) return;
    vector<pair<TypeExpr *, bool>> slots;
    if (pointee) slots.push_back({ pointee, sliceref });
    else if (ht) RefSlots(ht, slots);
    for (auto [st, slice] : slots) {
        if (!HoldsPlainRef(st)) continue;
        if (exact && r->type && !IsRefOrSlice(r->type))
            return RecordedViews(r, 0, r->type, out, seen);
        if (exact && IsClassRoot(r) && !slice) {
            RecordedViews(r, 0, st, out, seen);
            Prov cv;
            cv.Set(r, true);
            cv.byteview = byteview || r->contentbyteview;
            out.push_back({ cv, st, true, true });
            continue;
        }
        Prov bv;
        bv.Set(r, false);
        bv.byteview = r->contentbyteview || (r->type && IsRefOrSlice(r->type) && r->ref.byteview);
        vector<TypeExpr *> ps;
        ReachedThroughRefs(st, ps);
        for (auto pt : ps) out.push_back({ bv, pt, true });
    }
}

// Where the references in the holders of type `held` that p points at or
// views may point (§5.1): for a holder of this function or a parent's that
// the value names exactly, where its store record says, as for the holder
// itself. A parameter's class named exactly stands for storage of the
// caller's -- the holder a reference parameter names, the elements a slice
// parameter views: the activation's own stores into it are on record, and
// what the callers put there is a view of its contents, which each call
// judges by its record of the storage its argument names (LiveShrink::
// contents); so do a view that storage held and what it leads to
// (RootAlt::classread). There, a temporary the call is handed holds what
// `temps` says, which the callee cannot write. Anything else points
// anywhere its root bounds, as a `var` does, which a binding the record does
// not show yet may have moved to another holder at that depth
// (HeldRefsMayPointInto). A global holder is judged with the globals.
inline void TypeCheck::HeldViews(const Prov &p, TypeExpr *held, bool isvar,
                                 vector<LiveView> &out, const TempHolds *temps, VarDef *base,
                                 TypeExpr *arrtype) {
    vector<TypeExpr *> pointees;
    ReachedThroughRefs(held, pointees);
    for (auto &a : p.alts) {
        auto r = a.root;
        if (!r || r->isglobal) continue;
        set<VarDef *> seen;
        // Where kills emptied every slot of the holder a view into the array
        // could lie in, the stores since are all it holds, the callers' made
        // before them included (HeldRefsMayPointInto).
        size_t since = 0;
        auto emptied = base && a.exact && !isvar && !pathslive &&
                       KilledFor(r, base, held, arrtype, since);
        auto from = emptied ? std::max(LiveEventBase(r), since) : LiveEventBase(r);
        if (a.exact && r->type && !IsRefOrSlice(r->type) && !isvar) {
            RecordedViews(r, from, r->type, out, seen);
            continue;
        }
        if ((a.exact || a.classread) && IsClassRoot(r) && !isvar) {
            RecordedViews(r, from, held, out, seen);
            if (emptied) continue;
            Prov cv;
            cv.Set(r, true);
            cv.byteview = p.byteview || r->contentbyteview;
            out.push_back({ cv, held, true, true });
            continue;
        }
        const Roots *made = nullptr;
        if (temps && IsTemp(r))
            for (auto &[t, c] : *temps)
                if (t == r) made = &c;
        if (made) {
            for (auto &c : made->alts)
                if (c.root)
                    StoredViews(c.root, c.exact, nullptr, false, p.byteview, held, out, seen);
            continue;
        }
        Prov hv;
        hv.Set(r, false);
        hv.byteview = p.byteview || r->contentbyteview;
        for (auto pt : pointees) out.push_back({ hv, pt, true });
    }
}

// What the stores into holder from event `from` on put there, following
// copies of other containers' contents to those containers' own stores.
// A copy of a global's contents counts as a reference bounded by the
// global: stores into a global may come from functions not checked yet.
// What came out of the storage of a parameter's class alone (StoreEvent::
// classread) holds what the activation stored there, followed as a copy's
// source is, and what the callers did, which f takes the event for.
template<typename F> void TypeCheck::EachHolderRoot(VarDef *holder, size_t from, F f) {
    set<VarDef *> seen;
    function<void(VarDef *, size_t)> walk = [&](VarDef *h, size_t start) {
        if (!seen.insert(h).second) return;
        if (h->type && ClassOf(h->type) == SC_ROOT)
            for (auto c : h->components) if (c) walk(c, start);
        for (auto i = start; i < storeevents.size(); i++) {
            auto e = storeevents[i];
            if (e.container != h || StoredLater(i)) continue;
            if (e.src && !e.src->isglobal && e.src->type) {
                walk(e.src, 0);
                continue;
            }
            if (e.classread) {
                walk(e.src, 0);
                f(e);
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
// the activation owns -- the pair is kept on its record for them, as is one
// about what a class's storage holds (LiveShrink::contents) wherever the
// array is not the activation's own. False where nothing can tell the two
// apart.
inline bool TypeCheck::NoteLiveShrink(LiveShrink ls, FnSpec *current) {
    auto s = ls.shrunk, l = ls.live;
    if (!s || !l || IsTemp(s) || IsTemp(l)) return true;
    auto outside = [&](VarDef *v) {
        return IsClassRoot(v) || (v->type && (v->isglobal || v->ownerspec != current));
    };
    if (ls.contents) {
        // What the callers put in a class's storage outlives that storage,
        // so it points into nothing the activation owns; the activation's
        // own stores into it are judged where the array shrinks
        // (HeldRefsMayPointInto).
        if (!current) return false;
        if (!outside(s)) return true;
    } else {
        if (DifferentComponents(l, ls.liveexact || ls.liveprojected, s, ls.shrunkexact)) return true;
        // What a rendering walks in place goes with the storage it lies in,
        // a shrink of either kind relaying it out; its fixed-size parts, and
        // what they refer to, are judged as any view is.
        auto moves = ls.inplace && (!ls.pointee || ClassOf(ls.pointee) != SC_FIXED);
        auto mayfree = moves ? ShrinkMayMove(s, ls.bound, ls.pointee)
                             : ShrinkMayFree(s, ls.bound, ls.growonly, ls.pointee, ls.byteview);
        // A callee's shrink of a class, mapped onto an argument whose root
        // only bounds it: the array may be any of the shrink's kind in what
        // that root leads to (BoundReach).
        if (!mayfree && !ls.shrunkexact && !ls.bound) {
            vector<TypeExpr *> reach;
            BoundReach(s, reach);
            for (auto t : reach) {
                auto arr = ResizableArrayIn(t);
                if (!arr) continue;
                mayfree = mayfree ||
                          (moves ? ShrinkMayMove(s, t, ls.pointee)
                                 : GrowOnlyTail(arr) == ls.growonly &&
                                       ShrinkMayFree(s, arr, ls.growonly, ls.pointee, ls.byteview));
            }
        }
        if (!mayfree) return true;
        // A view into storage that cannot hold an array of the bound's type
        // is not in the array freed.
        if (ls.bound && l->type && ls.liveexact && !CanContain(LoadType(l->type), ls.bound))
            return true;
        if (MayAliasRoots(l, ls.liveexact, s, ls.shrunkexact, current) == AL_NO) return true;
        if (!current || s == l || (!IsClassRoot(s) && !IsClassRoot(l)) || !outside(s) ||
            !outside(l))
            return false;
    }
    for (auto &e : current->record.liveshrinks) {
        if (e.shrunk != s || e.live != l || !e.bound != !ls.bound || e.contents != ls.contents ||
            e.inplace != ls.inplace || e.growonly != ls.growonly)
            continue;
        if (e.bound && !TypeEq(e.bound, ls.bound)) continue;
        if (e.contents && !TypeEq(e.pointee, ls.pointee)) continue;
        e.shrunkexact = e.shrunkexact && ls.shrunkexact;
        e.liveexact = e.liveexact && ls.liveexact;
        e.liveprojected = e.liveprojected && ls.liveprojected;
        if (e.pointee && (!ls.pointee || !TypeEq(e.pointee, ls.pointee))) e.pointee = nullptr;
        e.byteview = e.byteview || ls.byteview;
        return true;
    }
    current->record.liveshrinks.push_back(ls);
    return true;
}

// The callee's shrinks of arrays something it still uses may point into
// (FnRecord::liveshrinks), mapped onto this call's arguments: a parameter's
// class becomes the root of the argument passed for it (ClassArgRoot), a
// view's the slice its argument's slot held (Val::held). Two arrays the
// caller cannot tell apart are an error here; two it can only as its own
// callers can are kept for them in turn. A callee in a recursive cycle still
// being checked has the pairs of the round before (RecordOf), and none in
// the cycle's first round.
inline void TypeCheck::ApplyCalleeLiveShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                              string_view name) {
    auto record = RecordOf(spec);
    if (!record) return;   // A cycle's first round: no record yet.
    auto caller = CurRealFrame().spec;
    // What each parameter's class stands for here, and each parameter's
    // view, the slice its slot held; what the temporaries among the
    // arguments hold.
    vector<Roots> args, views;
    TempHolds temps;
    args.reserve(argvals.size());
    views.reserve(argvals.size());
    for (size_t q = 0; q < spec->argtypes.size() && q < argvals.size(); q++) {
        args.push_back(ClassArgRoots(spec->argtypes[q], argvals[q]));
        views.push_back(ViewClassOf(spec, q) ? argvals[q].held.AsRoots() : Roots {});
        ReadBack held;
        if (TempContents(argvals[q], held)) temps.push_back({ argvals[q].Root(), held.roots });
    }
    // A class root of the callee, as seen from here: every place the
    // argument may point, or for a view every place its slot's slice may.
    auto mapped = [&](VarDef *r, bool exact, bool projected = false) -> Roots {
        bool view = false;
        auto p = ClassParam(spec, r, view);
        if (p >= 0 && p < (int)args.size()) {
            auto base = view ? ViewClassOf(spec, p) : spec->params[p]->ref.Root();
            auto m = ProjectClass(r, base, view ? views[p] : args[p]);
            if (!exact && !projected) m.Weaken();
            return m;
        }
        Roots one;
        one.Set(r, exact);
        one.alts[0].projected = projected;
        return one;
    };
    // The storage a reference parameter's class stands for, where it is a
    // resizable value, which a shrink of the class shrinks: the type an
    // argument's root that only bounds it leads to (ApplyCalleeShrinks).
    auto storage = [&](VarDef *r) -> TypeExpr * {
        if (r && r->componentbase)
            return LoadType(GetStructInst(r->componentbase)->ftypes[r->componentfield]);
        for (size_t p = 0; r && p < spec->params.size() && p < spec->argtypes.size(); p++) {
            auto pt = spec->argtypes[p];
            if (spec->params[p]->ref.Root() == r && pt->kind == TY_REF &&
                ClassOf(pt->ref->sub) == SC_RESIZABLE)
                return LoadType(pt->ref->sub);
        }
        return nullptr;
    };
    // The caller may be the callee, whose record then grows underneath.
    for (size_t k = 0; k < record->liveshrinks.size(); k++) {
        auto orig = record->liveshrinks[k];
        auto livename = orig.name;
        auto shrunk = mapped(orig.shrunk, orig.shrunkexact);
        auto live = mapped(orig.live, orig.liveexact, orig.liveprojected);
        auto shrunkat = orig.shrunkexact && !orig.bound ? storage(orig.shrunk) : nullptr;
        // What is still used is the argument passed for a parameter, itself:
        // one loaded out of a field, an element or a global points into no
        // grow-shrink array (§5.2), as the scans say of a slot read.
        auto liveparam = false;
        for (size_t p = 0; p < spec->params.size(); p++)
            liveparam = liveparam || spec->params[p]->ref.Root() == orig.live;
        auto slotsafe = liveparam && !orig.growonly && !orig.inplace && !orig.contents;
        for (auto &sa : shrunk.alts) {
            for (auto &la : live.alts) {
                if (slotsafe && la.slotread) continue;
                auto ls = orig;
                ls.shrunk = sa.root;
                ls.shrunkexact = sa.exact;
                if (!sa.exact && shrunkat) ls.bound = shrunkat;
                ls.live = la.root;
                ls.liveexact = la.exact;
                ls.liveprojected = la.projected;
                if (!ls.shrunk || !ls.live) continue;
                // Passed on from the caller's parameter: that is what its
                // callers see used.
                if (ls.live != orig.live && IsClassRoot(ls.live)) ls.name = string(ls.live->name);
                // What the storage the argument names holds, where the pair
                // is about that: each view it leads to as the caller sees
                // it, by its record of the storage (HeldViews), a class of
                // the caller's passing the question on. A view of the
                // callee's into what an argument views, where the argument
                // is a view the storage of one of the caller's classes held
                // (RootAlt::classread), points where that storage's views
                // do: it is judged as what that storage holds, of a type
                // leading to what the view points at, and so is a walk's
                // path, which such storage may hold into an array of either
                // kind. A temporary nothing recorded the contents of may hold
                // anything outliving it.
                auto contents = orig.contents;
                auto held = orig.pointee;
                if (!contents && (orig.growonly || orig.inplace) && orig.pointee && la.classread) {
                    contents = true;
                    held = ast.SliceOf(orig.pointee, at->line);
                }
                vector<LiveShrink> each;
                if (!contents) {
                    each.push_back(ls);
                } else if (IsTemp(la.root) &&
                           std::none_of(temps.begin(), temps.end(),
                                        [&](auto &t) { return t.first == la.root; })) {
                    auto x = ls;
                    x.contents = false;
                    x.live = ls.shrunk;
                    x.liveexact = false;
                    x.liveprojected = false;
                    x.pointee = nullptr;
                    each.push_back(x);
                } else {
                    Prov lp;
                    lp.alts = { la };
                    lp.byteview = orig.byteview;
                    vector<LiveView> vs;
                    HeldViews(lp, held, false, vs, &temps);
                    for (auto &w : vs) {
                        for (auto &b : w.p.alts) {
                            auto x = ls;
                            x.live = b.root;
                            x.liveexact = b.exact;
                            x.liveprojected = b.projected;
                            x.pointee = w.pointee;
                            x.byteview = w.p.byteview;
                            x.contents = w.contents;
                            if (x.live) each.push_back(x);
                        }
                    }
                }
                for (auto &x : each) {
                    if (NoteLiveShrink(x, caller)) continue;
                    // The array as the caller names it: the shrunk argument's
                    // root, or where that only bounds the array, the view's
                    // own, or else the first array it bounds that the view
                    // may point into, of the bound's type where it has one.
                    auto arr = x.shrunkexact || !x.liveexact ? x.shrunk : x.live;
                    auto unnamed = x.bound && arr != x.live;
                    auto st = x.bound ? x.bound
                                      : x.shrunk->type ? LoadType(x.shrunk->type) : nullptr;
                    if (!x.shrunkexact && !x.liveexact && st) {
                        Roots one;
                        one.Set(x.shrunk, false);
                        for (auto &t : ShrinkTargets(one, st)) {
                            if (t.bound ||
                                MayAliasRoots(x.live, false, t.root, true, caller) == AL_NO)
                                continue;
                            arr = t.root;
                            unnamed = false;
                            break;
                        }
                    }
                    auto what = unnamed ? cat("an array ", x.shrunk->name, " leads to")
                                        : string(arr->name);
                    Error(at, cat("cannot call ", name, ": it ",
                                  x.bound || !shrunk.Exact() ? "may shrink " : "shrinks ",
                                  what, " while ", livename, " is still used, and ", livename,
                                  " may refer into ", unnamed ? "it" : what,
                                  x.growonly ? " (§5.1)" : " (§5.2)"));
                }
            }
        }
    }
}

// The callee's shrinks of grow-shrink arrays (§5.2) are the caller's:
// nothing in scope may refer into an argument it shrinks through or a
// external owner it shrinks, and both are recorded for the caller's callers.
// A balanced shrink (NoteShrink) frees nothing a view the caller holds can
// reach, provided no other shrink of the call may be of the same array. A
// back edge applies the shrinks of the round before (RecordOf), and none
// in the cycle's first round.
inline void TypeCheck::ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                          string_view name, const vector<Node *> &argnodes) {
    // The callee may store into what the caller emptied in ways no event
    // shows (a store of its own unrecorded, one of a nested function's
    // re-applied in place), so a kill of the caller's outlasts a call only
    // where the callee cannot write into the part it emptied; its recorded
    // stores elsewhere in the container come after the kill.
    erase_if(cur.kills, [&](const Kill &k) { return CallMayWrite(spec, argvals, k); });
    // A shrink of an array the call may free: a grow-only array takes the
    // §5.1 scan (variables and recorded stores), a grow-shrink one the §5.2
    // scan (variables only) and the pairs its callers judge (NoteLiveViews),
    // unless it is judged balanced.
    struct Hit {
        VarDef *root;
        TypeExpr *bound;
        TypeExpr *arr;   // Null where the record does not say.
        bool growonly;
        ShrinkBalance balance;
        const char *how;
        const FnRecord::ShrinkOrder *order;   // Null where the record does not say.
        bool viaclass;
    };
    vector<Hit> hits;
    auto storesbase = storeevents.size();
    vector<int> origins;
    auto current = CurRealFrame().spec;
    auto classbase = current ? current->record.classevents.size() : 0;
    vector<int> classorigins;
    // The part of the caller's holder a kill of the callee's emptied: what
    // the argument for reference parameter p names, which is one container's
    // own storage or a `let` reference's or parameter's pointee there, then
    // the part's path. Stores into a variable a nested function or a function
    // value names keep the events of an earlier check of its body, which no
    // order says anything of, so a kill never empties one.
    auto argkill = [&](int p, const string &path) {
        if (p < 0 || (size_t)p >= argnodes.size() || (size_t)p >= argvals.size()) return;
        auto &v = argvals[(size_t)p];
        if (!v.Exact()) return;
        auto x = v.Root();
        if (!x || x->isglobal || IsTemp(x) || x->captured) return;
        auto n = argnodes[(size_t)p];
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) n = u->child;
        VarDef *base = nullptr;
        string bpath;
        if (!KillPath(n, base, bpath)) return;
        if (IsRefOrSlice(base->type) ? !base->ref.Exact() || base->ref.Root() != x : base != x)
            return;
        cur.kills.push_back({ x, base, bpath + path, storesbase });
    };
    auto apply = [&](const Hit &h, ShrinkBalance judged) {
        auto what = cat("call ", name, ", which ", h.how);
        auto op = cat(what, " ", h.root->name);
        if (h.growonly) {
            // The callee's shrink is judged as it stood in its body: against
            // the stores the call replays from class events made before it,
            // and with the parts of the caller's holders emptied before it
            // holding only what the call stores there.
            struct Restore {
                TypeCheck &tc;
                CallOrder order;
                size_t kills;
                ~Restore() {
                    tc.callorder = order;
                    tc.cur.kills.resize(std::min(kills, tc.cur.kills.size()));
                }
            } restore { *this, callorder, cur.kills.size() };
            if (h.order) {
                callorder = { storesbase, &origins, h.order->before, &classorigins, classbase };
                for (auto &[p, path] : h.order->kills) argkill(p, path);
            }
            GrowOnlyShrinkAt(at, what, h.root, string(h.root->name), h.bound, h.arr,
                             h.viaclass);
        } else if (judged == SB_UNBALANCED) {
            ShrinkGrowShrink(at, op, h.root, string(h.root->name), h.bound);
        } else {
            NoteShrink(h.root, h.bound, judged);
        }
        NoteShrinkEvent(at, h.root, h.bound, op, h.arr);
    };
    // A shrink of the `arr` at root, and, where root is inexact or only
    // bounds it, of every other array it may be (ShrinkTargets). An
    // external's type is its root's own, which a parameter class takes from
    // its call site. A callee's are applied once all of them are known.
    auto shrink = [&](const Roots &roots, TypeExpr *arr, const char *how,
                      ShrinkBalance balance, const FnRecord::ShrinkOrder *order,
                      bool classbound = false) {
        auto growonly = arr ? GrowOnlyTail(arr) : IsGrowOnlyRootVar(roots.Root());
        for (auto &t : ShrinkTargets(roots, arr, false, classbound && growonly))
            hits.push_back({ t.root, t.bound ? arr : nullptr, arr, growonly,
                             growonly ? SB_UNBALANCED : balance,
                             roots.Exact() && t.root == roots.Root() ? how : "may shrink",
                             order, t.viaclass });
    };
    {
        struct Restore {
            Replay &r;
            Replay saved;
            ~Restore() { r = saved; }
        } restore { replay, replay };
        replay = { -1, &classorigins, classbase };
        ApplyCalleeStores(spec, argvals, at, &origins);
    }
    // The record read: none in a cycle's first round (RecordOf), whose back
    // edge shrinks nothing yet.
    auto rec = RecordOf(spec);
    if (!rec) return;
    // Where each grow-only shrink stands in the body, as the record says: not
    // for a body still being checked or a recursive cycle's, whose rounds
    // replace the record, nor a function value's body.
    auto ordered = rec == &spec->record && !spec->inprogress && !spec->incycle &&
                   !spec->sf->isrec && !spec->isfunval;
    auto orderof = [&](int param, VarDef *external,
                       TypeExpr *bound) -> const FnRecord::ShrinkOrder * {
        if (!ordered) return nullptr;
        for (auto &o : rec->shrinkorders)
            if (o.param == param && o.external == external && !o.bound == !bound &&
                (!bound || TypeEq(o.bound, bound)))
                return &o;
        return nullptr;
    };
    for (size_t i = 0; i < argvals.size() && i < spec->argtypes.size(); i++) {
        auto vi = (int)(spec->params.size() + i);
        if (auto view = rec->shrinkparams.find(vi); view != rec->shrinkparams.end())
            shrink(argvals[i].held, nullptr, "shrinks", view->second,
                   orderof(vi, nullptr, nullptr));
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
            shrink(ClassArgRoots(pt, argvals[i]), nullptr, "shrinks", entry->second,
                   orderof((int)i, nullptr, nullptr));
            continue;
        }
        // What shrinks through a parameter is its pointee: a resizable one,
        // which every other parameter in its class points into.
        if (!argvals[i].Root() || pt->kind != TY_REF || ClassOf(pt->ref->sub) != SC_RESIZABLE)
            continue;
        shrink(argvals[i], LoadType(pt->ref->sub), "shrinks", entry->second,
               orderof((int)i, nullptr, nullptr));
    }
    // An array only reached through the references an argument holds or
    // points at: any of its type that the argument's roots, or its contents'
    // for a by-value holder, bound.
    for (auto &b : rec->shrinkparambounds) {
        if (b.key >= (int)spec->params.size()) {
            auto p = b.key - spec->params.size();
            if (p < argvals.size()) {
                Roots roots = argvals[p].held;
                roots.Weaken();
                shrink(roots, b.type, "may shrink", b.balance,
                       orderof(b.key, nullptr, b.type), true);
            }
            continue;
        }
        if (b.key >= (int)argvals.size()) continue;
        auto pt = spec->argtypes[b.key];
        auto roots = ClassArgRoots(pt, argvals[b.key]);
        roots.Weaken();
        // Through a reference or slice naming one local holder exactly, the
        // references are those its record says it holds (HeldAt), the
        // callee's own stores there among them by now (ApplyCalleeStores): a
        // variable one names exactly is the array where its storage holds one
        // and its references lead to none, leads to none where neither may
        // be, and otherwise only bounds the array, as any other root does. A
        // parameter's class of this activation that only bounds one bounds
        // the array as what the storage behind the class leads to, which the
        // class's bound records for the callers (ShrinkTargets'
        // `classbound`). A reference the holder's own root only bounds was
        // derived from what the holder holds, which its other entries say,
        // unless it points into the holder's own storage, the array where
        // that can hold one. Where it holds anything else -- a reference into
        // a class's storage, whose candidates name it best -- the argument
        // bounds the array.
        auto own = CurRealFrame().spec;
        auto ownclass = [&](VarDef *r) {
            return own && IsClassRoot(r) &&
                   find(own->classroots.begin(), own->classroots.end(), r) !=
                       own->classroots.end();
        };
        auto holder = IsRefOrSlice(pt) ? HolderSource(argvals[b.key]) : nullptr;
        if (holder && (!holder->type || holder->isglobal || IsRefOrSlice(holder->type)))
            holder = nullptr;
        auto held = holder ? HeldAt(argvals[b.key]) : Roots {};
        if (!held.None() && !held.unknown && held.All([&](const RootAlt &a) {
                return a.exact ? a.root && a.root->type && !IsRefOrSlice(a.root->type)
                               : ownclass(a.root) || a.root == holder;
            })) {
            roots.Clear();
            for (auto a : held.alts) {
                a.from = nullptr;
                a.classread = false;
                if (!a.exact && ownclass(a.root)) {
                    roots.Add(a);
                    continue;
                }
                if (a.root == holder && !a.exact) {
                    if (!MayHoldValueOf(LoadType(holder->type), b.type)) continue;
                    a.exact = true;
                }
                auto rt = LoadType(a.root->type);
                vector<TypeExpr *> reached;
                ReachedThroughRefs(rt, reached);
                auto leads = any_of(reached.begin(), reached.end(),
                                    [&](TypeExpr *t) { return MayHoldValueOf(t, b.type); });
                if (!leads && !MayHoldValueOf(rt, b.type)) continue;
                a.exact = !leads && CanContain(rt, b.type);
                roots.Add(a);
            }
        }
        shrink(roots, b.type, "may shrink", b.balance, orderof(b.key, nullptr, b.type), true);
    }
    for (auto &[vd, balance] : rec->shrinkexternals) {
        auto storage = vd->componentbase
                           ? LoadType(GetStructInst(vd->componentbase)->ftypes[vd->componentfield])
                           : nullptr;
        shrink(MapClass(spec, vd, argvals), storage, "shrinks", balance,
               orderof(-1, vd, nullptr));
    }
    for (auto &b : rec->shrinkexternalbounds) {
        auto one = MapClass(spec, b.key, argvals);
        one.Weaken();
        shrink(one, b.type, "may shrink", b.balance, orderof(-1, b.key, b.type));
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
    if (ClassOf(elem) != SC_FIXED) return ast.ArrayOf(elem, A_VAR, al->line);
    // A negative fill count is the literal's own error to report.
    auto n = al->fillval ? FillCount(al->fillcount) : (int64_t)al->elems.size();
    return ast.ArrayOf(elem, A_FIXED, al->line, std::max<int64_t>(n, 0));
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
        lv.isslot = true;
        ev = ContainerRead(lv);
    }
    DestScope ds(*this, Dest(rv, false, rv.reached));
    MustFit(ev, an, ev.type);
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
    if (aexact && bexact && a->componentowner && b->componentowner &&
        a->componentowner == b->componentowner && a->componentfield != b->componentfield &&
        TypeEq(a->componentbase, b->componentbase)) return AL_NO;
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

inline TypeExpr *TypeCheck::SoleResizableIn(TypeExpr *t) {
    if (!t || ClassOf(t) != SC_RESIZABLE) return nullptr;
    if (t->kind == TY_ARRAY) return t;
    TypeExpr *arr = nullptr;
    auto mixed = AnyField(t, [&](TypeExpr *ft) {
        if (ClassOf(ft) != SC_RESIZABLE) return false;
        auto a = SoleResizableIn(ft);
        if (!a || (arr && !TypeEq(a, arr))) return true;
        arr = a;
        return false;
    });
    return mixed ? nullptr : arr;
}

inline void TypeCheck::NoteGrow(Node *at, const Roots &roots, const string &what, TypeExpr *t) {
    auto arr = SoleResizableIn(t);
    auto note = [&](vector<TypeExpr *> &types) {
        for (auto u : types)
            if (!u == !arr && (!u || TypeEq(u, arr))) return;
        types.push_back(arr);
    };
    for (auto &a : roots.alts) {
        auto root = a.root;
        if (!root || IsTemp(root)) continue;
        cur.growlog.push_back({ at, root, a.exact, what, false, arr });
        NoteRootEvent(root, [&](FnSpec *s, int i) { note(s->record.growparams[i]); },
                      [&](FnSpec *s, VarDef *r) { note(s->record.growexternals[r]); });
    }
}

// A shrink of the array at root, or of one of its type that root bounds,
// logged with the growths: a value under construction in the array meanwhile
// would be freed under it (CheckGrowsSince).
inline void TypeCheck::NoteShrinkEvent(Node *at, VarDef *root, TypeExpr *bound,
                                       const string &what, TypeExpr *t) {
    if (!root || IsTemp(root)) return;
    cur.growlog.push_back({ at, root, !bound, what, true, SoleResizableIn(t) });
}

// The value built at the top or in a slot of the array `built` may be, by
// the expression checked since cur.growlog was `base` long: none of the growths
// logged meanwhile may have been of that array, or it would have landed
// inside the value, and none of the shrinks. A resizable array of another
// type is another array, whatever the roots, and lies outside the value, as
// resizables are never elements and a stack holds one (§1.3, §3.4); any
// other growable array may lie inside it.
inline void TypeCheck::CheckGrowsSince(size_t base, const Roots &built, const string &what,
                                       TypeExpr *t) {
    auto arr = SoleResizableIn(t);
    for (auto i = base; i < cur.growlog.size(); i++) {
        auto e = cur.growlog[i];
        if (arr && e.arr && !TopConstEq(arr, e.arr)) continue;
        for (auto &b : built.alts) {
            auto may = MayAliasRoots(e.root, e.exact, b.root, b.exact);
            if (may == AL_NO) continue;
            auto msg = cat("cannot ", e.what, ": ", what, " is still under construction, and "
                           "the ", e.shrink ? "shrink would free the storage it is built in"
                                            : "growth would land inside it", " (§1.3)");
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
    auto grows = [&](size_t i, const char *how, TypeExpr *arr) {
        auto view = i >= spec->params.size();
        if (view) i -= spec->params.size();
        if (i >= argvals.size()) return;
        auto roots = view ? argvals[i].held.AsRoots()
                          : ClassArgRoots(spec->argtypes[i], argvals[i]);
        auto root = roots.Root();
        if (!root || IsTemp(root)) return;
        NoteGrow(at, roots, cat("call ", name, ", which ", how, " ", root->name), arr);
    };
    if (spec->sf->isextern) {
        for (size_t i = 0; i < spec->argtypes.size(); i++) {
            auto pt = spec->argtypes[i];
            if (IsPlainRef(pt) && ClassOf(pt->ref->sub) == SC_RESIZABLE)
                grows(i, "may grow", pt->ref->sub);
        }
        return;
    }
    // The record read: none in a cycle's first round (RecordOf), whose back
    // edge grows nothing yet.
    auto rec = RecordOf(spec);
    if (!rec) return;
    for (auto &[pi, arrs] : rec->growparams)
        for (auto arr : arrs) grows((size_t)pi, "grows", arr);
    for (auto &[vd, arrs] : rec->growexternals)
        for (auto arr : arrs)
            NoteGrow(at, MapClass(spec, vd, argvals), cat("call ", name, ", which grows ", vd->name), arr);
}

// Whether storage of type y can be, or hold, the location `lval` assigned,
// of type `target`, or a resizable on the path from there to its array:
// one of those itself, or for a field path, storage holding the value the
// field lies in by value. A variable lies in no other storage; the pointee
// of a reference, which an assignment through a reference variable or field
// writes (§3.8), may lie in anything that can hold a `target`.
inline bool TypeCheck::MayHoldBuilt(TypeExpr *y, Node *lval, TypeExpr *target) {
    for (auto t = target; t;) {
        if (TypeEq(y, t) || TypeEq(LoadType(y), t)) return true;
        if (t->kind == TY_ARRAY) break;
        TypeExpr *next = nullptr;
        AnyField(t, [&](TypeExpr *ft) { return ResizableArrayIn(ft) && (next = ft); });
        t = next;
    }
    if (lval->exprtype && lval->exprtype->kind == TY_REF) return CanContain(y, target);
    if (auto d = Is<Dot>(lval)) {
        auto h = d->obj->exprtype;
        if (h && h->kind == TY_REF) h = h->ref->sub;
        return !h || CanContain(y, h);
    }
    auto id = Is<Ident>(lval);
    if (id && id->vdef && id->vdef->type && id->vdef->type->kind != TY_REF) return false;
    return CanContain(y, target);
}

// Where naming v leads, if that can be the array under construction at
// `built`, of type arr, which the location `lval` of type `target` holds: v
// is the array's variable or the value holding it, or a reference to storage
// that can be or hold the location (MayHoldBuilt). A slice, or a reference
// to anything else, points into the old elements, which the shrink the
// assignment starts with keeps from being used (§5.1), or elsewhere.
inline TypeCheck::Alias TypeCheck::ReachesBuilt(VarDef *v, VarDef *built, bool exact,
                                                Node *lval, TypeExpr *target, TypeExpr *arr,
                                                VarDef *&root) {
    auto t = v->type;
    if (!t || t->kind == TY_SLICE || IsRelRef(t))
        return AL_NO;
    auto isref = t->kind == TY_REF;
    if (isref ? !MayHoldBuilt(t->ref->sub, lval, target) : !CanContain(t, arr)) return AL_NO;
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
inline void TypeCheck::CheckBuiltUses(Node *rhs, Node *lval, TypeExpr *target,
                                      const Roots &built, TypeExpr *arr) {
    if (!built.Root() || IsTemp(built.Root())) return;
    auto what = ExprStr(lval);
    auto base = lval;
    while (auto d = Is<Dot>(base)) base = d->obj;
    auto lvvar = Is<Ident>(base) ? Is<Ident>(base)->vdef : nullptr;
    // `via` says which function names v, when the right-hand side's own
    // text does not.
    auto report = [&](Node *at, Alias may, VarDef *root, VarDef *b, const string &how,
                      const string &why) {
        auto msg = cat("cannot ", how, " in the value assigned to ", what, ": ",
                       why.empty() ? string() : cat(why, ", and "),
                       "that value is built over the old contents of ", what,
                       " (§4.4); build it in a variable of its own, and assign copy() of "
                       "that");
        if (may == AL_YES) Error(at, msg);
        growconflicts.push_back({ at, CurRealFrame().spec, root, b, msg });
    };
    auto check = [&](Node *at, VarDef *v, const string &how, const string &via) {
        for (auto &b : built.alts) {
            if (!b.root || IsTemp(b.root)) continue;
            VarDef *root = nullptr;
            auto may = ReachesBuilt(v, b.root, b.exact, lval, target, arr, root);
            if (may == AL_NO) continue;
            // The reference the assignment writes through needs no mention.
            auto refers = v->type->kind == TY_REF && v != lvvar;
            auto why = via.empty() ? (refers ? cat(v->name, " may refer to ", what) : string())
                                   : cat(via, refers ? cat(", which may refer to ", what) : "");
            report(at, may, root, b.root, how, why);
        }
    };
    // A field path reads a value out of the variable's storage, a reference
    // among them, which may lead to the location whatever the variable's own
    // type holds: where the references stored there point, by a local
    // holder's record, else anywhere the variable's storage bounds.
    auto checkread = [&](Ident *id, Node *path) {
        auto v = id->vdef;
        // The field's own type: a reference loads as what it refers to.
        TypeExpr *vt = nullptr;
        if (auto d = Is<Dot>(path); d && d->fieldidx >= 0) {
            auto ot = d->obj->exprtype;
            if (ot && ot->kind == TY_REF) ot = ot->ref->sub;
            auto runs = ot ? FieldRuns(ot) : span<const FieldRun>();
            if (runs.size() == 1) vt = (*runs[0].ftypes)[d->fieldidx];
        }
        if (!v || !v->type || !vt) return;
        vector<TypeExpr *> reached;
        ReachedThroughRefs(vt, reached);
        if (none_of(reached.begin(), reached.end(),
                    [&](TypeExpr *y) { return MayHoldBuilt(y, lval, target); }))
            return;
        Roots at;
        if (IsRefOrSlice(v->type)) at = Bounds(RefRootsOf(v));
        else if (!v->isglobal && !v->contents.Unknown()) at = v->contents;
        else at = Bounds(RootsOf(v));
        for (auto &b : built.alts) {
            if (!b.root || IsTemp(b.root)) continue;
            for (auto &a : at.alts) {
                auto may = MayAliasRoots(a.root, a.exact, b.root, b.exact);
                if (may == AL_NO) continue;
                report(id, may, a.root, b.root, cat("use ", ExprStr(path)),
                       cat(ExprStr(path), " may refer to ", what));
                break;
            }
        }
    };
    EachUse(rhs,
            [&](Ident *id, Node *path) {
                if (FieldsApart(path, lval, target)) return;
                check(id, id->vdef, cat("use ", ExprStr(path)), string());
                if (path != id) checkread(id, path);
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
// to what `lval` names: none to storage that can be or hold that location
// (MayHoldBuilt), at any remove, and, where `lval` ends in a reference whose
// pointee is assigned, not that location itself. A view of the old
// elements it leads to is the shrink rule's to judge (§5.1).
inline bool TypeCheck::FieldsApart(Node *use, Node *lval, TypeExpr *target) {
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
        auto runs = ot ? FieldRuns(ot) : span<const FieldRun>();
        if (runs.size() != 1) return false;
        auto ft = (*runs[0].ftypes)[uf[0]->fieldidx];
        // Through a reference field the location is its pointee, which may
        // be this field itself or lie in it (`d.r .= d.own`).
        if (lval->exprtype && lval->exprtype->kind == TY_REF && CanContain(ft, target))
            return false;
        if (IsFlat(ft)) return true;
        vector<TypeExpr *> reached;
        ReachedThroughRefs(ft, reached);
        for (auto y : reached)
            if (MayHoldBuilt(y, lval, target)) return false;
        return true;
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
        // Rounds reuse the same annotated body; the record only tells us
        // whether a completed round exists to justify reading it.
        EachUse(sp->body, [&](Ident *id, Node *) { named.push_back(id->vdef); },
                [&](Call *, FnSpec *callee) { visit(callee); });
    };
    visit(spec);
    set<VarDef *> seen;
    auto add = [&](VarDef *v) {
        if (seen.insert(v).second) out.push_back(v);
        for (auto c : v->components)
            if (c && seen.insert(c).second) out.push_back(c);
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
inline void TypeCheck::ElemArg(Node *&n, TypeExpr *elem, Val &rv, bool fill) {
    SlotScope ss(*this, true);
    auto orig = n;
    Dest d(rv, false, rv.reached);
    d.fill = fill;
    auto v = CheckValueAt(n, elem, d, true);
    // The element meets the array's element type as a value meets any
    // destination.
    JudgeCastAt(orig, elem, v);
}

}  // namespace goose
