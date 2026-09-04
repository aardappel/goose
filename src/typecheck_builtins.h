// Goose compiler — the typechecker's builtins (definitions of TypeCheck
// members, typecheck.h): the builtin functions and array members (§3.3,
// §3.7, §5.4, §11.2), text rendering through user `format` overloads, and
// the shrink rules of §5.1 and §5.2.
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

// An argument of print/str/format (§3.7): every value type has a text
// form -- scalars and bool as text, u8 arrays and slices as their bytes
// (quoted inside an aggregate), other arrays as [a, b], structs and
// variants as their positional literal, references as their pointee,
// null as null. A user overload fn format(out: u8[>..]&, v: T) renders a
// T instead wherever one occurs; its specialization is recorded on the
// call for codegen.
inline void TypeCheck::CheckPrintable(Call *c, const char *what, Node *a) {
    auto av = CheckValue(a, nullptr);
    vector<TypeExpr *> seen;
    CheckRenderable(c, what, av.type, a, seen);
}

inline void TypeCheck::CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at,
                                       vector<TypeExpr *> &seen) {
    for (auto s : seen) if (TypeEq(s, t)) return;   // Recursion through references.
    seen.push_back(t);
    if (UserFormat(c, t)) return;
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: return;
        case TY_ARRAY: CheckRenderable(c, what, t->arr->sub, at, seen); return;
        case TY_SLICE: CheckRenderable(c, what, t->sub, at, seen); return;
        case TY_REF: CheckRenderable(c, what, t->ref->sub, at, seen); return;
        case TY_STRUCT: {
            auto si = GetStructInst(t);
            for (auto ft : si->ftypes) if (ft) CheckRenderable(c, what, ft, at, seen);
            return;
        }
        case TY_ENUM: {
            auto ei = GetEnumInst(t);
            for (auto &vf : ei->vftypes)
                for (auto ft : vf) if (ft) CheckRenderable(c, what, ft, at, seen);
            return;
        }
        case TY_VARIANT: {
            auto ei = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(ei->en, t->var->variant);
            for (auto ft : ei->vftypes[vi]) if (ft) CheckRenderable(c, what, ft, at, seen);
            return;
        }
        default:
            Error(at, cat(what, " cannot render a value of type ", TypeStr(t)));
    }
}

// The user's `format` overload for t, instantiated for a builder rooted
// anywhere and a T by value or by reference (the two parameter shapes
// such an overload takes), once per print call.
inline FnSpec *TypeCheck::UserFormat(Call *c, TypeExpr *t) {
    for (auto &fs : c->fmtspecs) if (TypeEq(fs.first, t)) return fs.second;
    auto fit = ast.functionmap.find("format");
    if (fit == ast.functionmap.end()) return nullptr;
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
        argvals[0].type = pt0;
        argvals[0].root = temproot;
        argvals[0].rootexact = true;
        argvals[0].writable = true;
        argvals[1].type = pt1;
        argvals[1].root = temproot;
        argvals[1].rootexact = true;
        argvals[1].writable = true;
        MatchInfo mi;
        mi.sf = sf;
        mi.env = nullptr;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why)) continue;
        auto sp = GetOrCreateSpec(mi, argvals, c);
        c->fmtspecs.push_back({ t, sp });
        return sp;
    }
    return nullptr;
}

// A shrink (`pop`, `resize` down, `clear`) of a grow-only array (§5.1).
// Everything below the stack top belongs to the array's elements for as
// long as it lives, so handing part of the region back is safe exactly
// when nothing can still point into it: the receiver is a local of the
// function being checked (not a global, not a field, not reached through a
// reference -- those have holders this pass cannot see); the call stands on
// its own (a statement, an initializer, or the right-hand side of an
// assignment to a variable), so no reference taken earlier in the same
// expression outlives it; and no live value can hold a reference or slice
// rooted at the array. Roots are only known per variable, so the scan is
// conservative wherever they are: a value whose type contains references
// at all counts as a possible holder, and so does a `var` reference the
// same-depth rebinding rule (§9.2) could retarget into the array.
inline void TypeCheck::CheckGrowShrink(Node *at, bool standalone, const char *op, Node *recv,
                                       TypeExpr *rtype) {
    auto id = Is<Ident>(recv);
    auto vd = id ? id->vdef : nullptr;
    if (!vd || rtype->kind != TY_ARRAY)
        Error(at, cat(op, " on a grow-only array names the array's own variable, not a "
                      "reference or an element of another value (§5.1)"));
    GrowOnlyShrinkAt(at, standalone, op, vd);
}

// A shrink of the grow-only array (or value holding one) that local vd
// owns: only where nothing in scope can still refer into it (§5.1).
inline void TypeCheck::GrowOnlyShrinkAt(Node *c, bool standalone, const char *op, VarDef *vd) {
    if (vd->isglobal || vd->isparam || !frames.back().spec ||
        vd->ownerspec != frames.back().spec)
        Error(c, cat("cannot ", op, " ", vd->name,
                     ": a shrink of a grow-only array applies to a local of the function "
                     "that owns it (§5.1)"));
    if (vd->reusable)
        Error(c, cat("cannot ", op, " reusable pool ", vd->name,
                     ": its slots stay live for the freelist (§5.4)"));
    if (!standalone)
        Error(c, cat("cannot ", op, " ", vd->name,
                     " inside a larger expression: a reference taken earlier in it may "
                     "still be live, so bind the result first (§5.1)"));
    if (invalue)
        Error(c, cat("cannot ", op, " ", vd->name,
                     " inside a value-producing expression: references taken earlier in "
                     "it may still be live (§5.1)"));
    for (auto v : vars) {
        if (v == vd || !v->type) continue;
        auto t = v->type;
        if (t->kind == TY_REF || t->kind == TY_SLICE) {
            // A recorded root is exact only while the variable keeps its
            // first binding: a `var` may since have been rebound to any
            // root at the same depth, and one not bound yet can still
            // commit to this array further down a loop body.
            auto root = RefRootOf(v);
            auto holds = root == vd || (v->isvar && Depth(root) == Depth(vd)) ||
                         (!v->refrootknown && Depth(v) >= Depth(vd));
            if (!holds) continue;
        } else {
            // Any other value holds references only where a store put
            // them, which the outlives rule permits only into storage the
            // array outlives (§9.2); flat types have no room for one.
            if (IsFlat(t) || Depth(v) < Depth(vd)) continue;
        }
        Error(c, cat("cannot ", op, " ", vd->name, " while ", v->name,
                     " is in scope: it may hold a reference or slice into it (§5.1)"));
    }
}

// A shrink of the grow-shrink array rooted at root (§5.2): no variable in
// scope may refer into it. Such references are held only by variables
// (they cannot be stored), so the scan is exact, up to a `var` reference
// the same-depth rebinding rule could have retargeted into it.
inline void TypeCheck::CheckShrinkHolders(Node *at, const string &op, VarDef *root,
                                          const string &what) {
    VisibleVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        if (v->type->kind != TY_REF && v->type->kind != TY_SLICE) return;
        // A reference to the whole array (or the value holding it) is the
        // path to it, not something a shrink invalidates.
        if (v->type->kind == TY_REF && ContainsGrowShrink(v->type->ref->sub)) return;
        auto r = RefRootOf(v);
        auto holds = r == root || (v->isvar && Depth(r) == Depth(root)) ||
                     (!v->refrootknown && Depth(v) >= Depth(root));
        if (!holds) return;
        Error(at, cat("cannot ", op, " while ", v->name, " (bound at ", Where(v->line),
                      ") is in scope: it may refer into ", what, " (§5.2)"));
    });
}

// Records a shrink for the callers' sake (§5.2): of a global, on the
// specialization being checked; through a parameter class, on the
// specialization that owns those parameters (a function value's body may
// shrink through its enclosing function's).
inline void TypeCheck::NoteShrink(VarDef *root) {
    if (root->isglobal) {
        if (auto spec = CurRealFrame().spec) spec->shrinkglobals.insert(root);
        return;
    }
    if (root->type) return;  // A local: its owner sees every shrink directly.
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        auto spec = frames[fi].spec;
        if (!spec) continue;
        auto found = false;
        for (size_t i = 0; i < spec->params.size(); i++) {
            if (RefRootOf(spec->params[i]) != root) continue;
            spec->shrinkparams.insert((int)i);
            found = true;
        }
        if (found) return;
    }
}

inline void TypeCheck::ShrinkGrowShrink(Node *at, const string &op, VarDef *root,
                                        const string &what) {
    if (!root) return;
    CheckShrinkHolders(at, op, root, what);
    NoteShrink(root);
}

// The callee's shrinks of grow-shrink arrays (§5.2) are the caller's:
// nothing in scope may refer into an argument it shrinks through or a
// global it shrinks, and both are recorded for the caller's own callers.
// A back edge's summary is incomplete, so it counts as shrinking every
// grow-shrink array it can reach.
inline void TypeCheck::ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                          string_view name) {
    auto pending = spec->inprogress;
    for (size_t i = 0; i < argvals.size() && i < spec->argtypes.size(); i++) {
        auto pt = spec->argtypes[i];
        auto shrinks = pending ? pt->kind == TY_REF && ContainsGrowShrink(pt->ref->sub)
                               : spec->shrinkparams.count((int)i) > 0;
        if (!shrinks) continue;
        auto root = CanonRoot(argvals[i].root);
        if (!root) continue;
        ShrinkGrowShrink(at, cat("call ", name, ", which shrinks ", root->name), root,
                         string(root->name));
    }
    if (pending) {
        for (auto g : ast.globals)
            for (auto vd : g->defs)
                if (vd->type && ContainsGrowShrink(vd->type))
                    ShrinkGrowShrink(at, cat("call ", name, ", which may shrink ", vd->name),
                                     vd, string(vd->name));
    } else {
        for (auto vd : spec->shrinkglobals)
            ShrinkGrowShrink(at, cat("call ", name, ", which shrinks ", vd->name), vd,
                             string(vd->name));
    }
}

// Element construction targets the array's storage (relative references
// in the element must derive from the same root, §3.9).
inline void TypeCheck::ElemArg(Node *&n, TypeExpr *elem, Val &rv) {
    CheckValueAt(n, elem, Dest { rv.root, rv.rootexact }, true);
}

// ------------------------------------------------------------------
// Calling a function value F(a): the body is cloned and checked inline
// in the lexical environment it was written in (§7.6).

inline Val TypeCheck::CheckFunValCall(Call *c, const FnValBind &fb) {
    if (c->trailing)
        Error(c, "a function value call cannot itself take a trailing block");
    if (fb.named) {
        vector<SFunction *> cands = { fb.named };
        Node *nopre = nullptr;
        return ResolveCall(c, cands, fb.env, fb.named->name, nullptr, nopre);
    }
    auto fv = fb.fv;
    vector<Val> argvals;
    for (auto a : c->args) {
        auto v = CheckV(a, nullptr);
        a->exprtype = v.type;
        argvals.push_back(v);
    }
    vector<Param> params;
    if (fv->explicit_params) {
        params = fv->params;
        if (params.size() != argvals.size())
            Error(c, cat("this function value takes ", (int64_t)params.size(),
                         " argument(s), ", (int64_t)argvals.size(), " given"));
    } else if (argvals.size() == 1) {
        Param p;
        p.name = "it";
        params.push_back(p);
    } else if (!argvals.empty()) {
        Error(c, "a block with multiple arguments needs named parameters (x, y => ...)");
    }
    // Parameter types: annotations resolve in the defining environment.
    vector<TypeExpr *> ptypes;
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].type) {
            auto t = SubstEnv(params[i].type, fb.env);
            ValidateType(t, c->line, VT_PARAM);
            ptypes.push_back(t);
        } else {
            auto nt = NaturalType(argvals[i]);
            if (!nt || nt->kind == TY_VOID || nt == fntype)
                Error(c->args[i], "cannot infer a type for this argument");
            ptypes.push_back(nt);
        }
    }
    {
        DestScope ds(*this, Dest {});
        for (size_t i = 0; i < ptypes.size(); i++) CheckArg(c->args[i], ptypes[i]);
    }
    // Check the body inline, with lookups chaining to the definer.
    Frame f;
    f.sf = fb.env ? fb.env->sf : CurRealFrame().sf;
    f.spec = CurRealFrame().spec;
    f.lexspec = fb.env;
    f.lexframe = fb.env ? FrameOfSpec(fb.env) : 0;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = c->line;
    f.isfunval = true;
    frames.push_back(f);
    PushScope(SK_FN);
    c->fvparams.clear();
    for (size_t i = 0; i < params.size(); i++) {
        auto vd = NewVar(params[i].name, ptypes[i], c->line, params[i].isvar);
        vd->assigned = true;
        if (ptypes[i]->kind == TY_REF || ptypes[i]->kind == TY_SLICE)
            BindRefProvenance(vd, argvals[i]);
        c->fvparams.push_back(vd);
    }
    c->fvtarget = fb.env ? fb.env->sf : nullptr;
    c->fvbody = (Block *)fv->body->Clone(ast);
    ValueRegion vr(*this, true);   // The body runs inside this call's expression.
    for (auto st : c->fvbody->stmts) CheckStmt(st);
    Val v = VoidVal();
    if (auto tail = c->fvbody->tail) {
        auto fi = Is<IfExpr>(tail);
        if ((fi && !fi->elseb) || Is<Guard>(tail)) CheckStmtExpr(tail);
        else v = CheckValue(c->fvbody->tail, nullptr);
    }
    c->fvbody->exprtype = v.type;
    PopScope();
    frames.pop_back();
    return v;
}

}  // namespace goose
