// Goose compiler — tree structure passes: Clone (deep copies) and Children
// (direct-child visiting, the basis for generic walks). Typecheck clones a
// function body per specialization so per-node annotations (types, resolved
// symbols) are per-instantiation; TypeExpr pointers are shared (they are
// templates; concrete types are computed during checking), and symbols
// (SFunction etc.) are shared too. All overrides live together here so each
// pass reads top to bottom.
#pragma once

namespace goose {

// The annotation fields typecheck fills are freshly defaulted by the leaf
// constructors below, so cloning an already-checked tree yields a clean one.

template<typename T> T *CloneOrNull(Ast &ast, T *n) {
    return n ? (T *)n->Clone(ast) : nullptr;
}

inline void CloneNodes(Ast &ast, const vector<Node *> &src, vector<Node *> &dst) {
    dst.reserve(src.size());
    for (auto n : src) dst.push_back(n->Clone(ast));
}

inline Node *IntLit::Clone(Ast &ast) const { return ast.New<IntLit>(line, val, text); }
inline Node *FltLit::Clone(Ast &ast) const { return ast.New<FltLit>(line, val); }
inline Node *BoolLit::Clone(Ast &ast) const { return ast.New<BoolLit>(line, val); }
inline Node *NullLit::Clone(Ast &ast) const { return ast.New<NullLit>(line); }
inline Node *StrLit::Clone(Ast &ast) const { return ast.New<StrLit>(line, val); }
inline Node *Ident::Clone(Ast &ast) const { return ast.New<Ident>(line, name); }

inline Node *ArrayLit::Clone(Ast &ast) const {
    auto a = ast.New<ArrayLit>(line);
    CloneNodes(ast, elems, a->elems);
    a->fillval = CloneOrNull(ast, fillval);
    a->fillcount = CloneOrNull(ast, fillcount);
    return a;
}

inline Node *StructLit::Clone(Ast &ast) const {
    auto sl = ast.New<StructLit>(line, type);
    sl->inits.reserve(inits.size());
    for (auto &fi : inits) sl->inits.push_back({ fi.name, fi.val->Clone(ast) });
    return sl;
}

inline Node *Unary::Clone(Ast &ast) const {
    return ast.New<Unary>(line, op, child->Clone(ast));
}

inline Node *Binary::Clone(Ast &ast) const {
    return ast.New<Binary>(line, op, left->Clone(ast), right->Clone(ast));
}

inline Node *Dot::Clone(Ast &ast) const {
    return ast.New<Dot>(line, obj->Clone(ast), name);
}

inline Node *Call::Clone(Ast &ast) const {
    auto c = ast.New<Call>(line, callee->Clone(ast));
    c->tyargs = tyargs;
    CloneNodes(ast, args, c->args);
    c->trailing = (FunVal *)CloneOrNull(ast, (Node *)trailing);
    return c;
}

inline Node *Index::Clone(Ast &ast) const {
    return ast.New<Index>(line, obj->Clone(ast), idx->Clone(ast));
}

inline Node *SliceExpr::Clone(Ast &ast) const {
    auto sl = ast.New<SliceExpr>(line, obj->Clone(ast));
    sl->lo = CloneOrNull(ast, lo);
    sl->hi = CloneOrNull(ast, hi);
    sl->lo_from_end = lo_from_end;
    sl->hi_from_end = hi_from_end;
    return sl;
}

inline Node *AsCast::Clone(Ast &ast) const {
    return ast.New<AsCast>(line, child->Clone(ast), type, unchecked);
}

inline Node *RangeExpr::Clone(Ast &ast) const {
    return ast.New<RangeExpr>(line, lo->Clone(ast), hi->Clone(ast));
}

inline Node *Block::Clone(Ast &ast) const {
    auto b = ast.New<Block>(line);
    CloneNodes(ast, stmts, b->stmts);
    b->tail = CloneOrNull(ast, tail);
    return b;
}

inline Node *IfExpr::Clone(Ast &ast) const {
    return ast.New<IfExpr>(line, cond->Clone(ast), (Block *)thenb->Clone(ast),
                           CloneOrNull(ast, elseb));
}

inline Node *MatchExpr::Clone(Ast &ast) const {
    auto m = ast.New<MatchExpr>(line, scrutinee->Clone(ast));
    m->arms.reserve(arms.size());
    for (auto &arm : arms) {
        MatchArm a;
        a.pat = arm.pat;
        a.pat.lo = CloneOrNull(ast, arm.pat.lo);
        a.pat.hi = CloneOrNull(ast, arm.pat.hi);
        a.body = arm.body->Clone(ast);
        m->arms.push_back(a);
    }
    return m;
}

inline Node *EarlyBlock::Clone(Ast &ast) const {
    return ast.New<EarlyBlock>(line, (Block *)body->Clone(ast));
}

inline Node *While::Clone(Ast &ast) const {
    return ast.New<While>(line, cond->Clone(ast), (Block *)body->Clone(ast));
}

inline Node *LoopExpr::Clone(Ast &ast) const {
    return ast.New<LoopExpr>(line, (Block *)body->Clone(ast));
}

inline Node *ForLoop::Clone(Ast &ast) const {
    return ast.New<ForLoop>(line, byref, var, idxvar, iter->Clone(ast),
                            (Block *)body->Clone(ast));
}

inline Node *Guard::Clone(Ast &ast) const {
    return ast.New<Guard>(line, cond->Clone(ast), (Block *)CloneOrNull(ast, (Node *)elseb));
}

inline Node *Return::Clone(Ast &ast) const {
    auto r = ast.New<Return>(line);
    CloneNodes(ast, vals, r->vals);
    r->from = from;
    return r;
}

inline Node *Break::Clone(Ast &ast) const {
    return ast.New<Break>(line, CloneOrNull(ast, val));
}

inline Node *Continue::Clone(Ast &ast) const { return ast.New<Continue>(line); }

// InlineBlocks exist only after typecheck; the annotation-stripping Clone has
// no meaning for them (the optimizer's copier in optimize.h preserves them).
inline Node *InlineBlock::Clone(Ast &ast) const {
    assert(false);
    return ast.New<InlineBlock>(line, sf, spec, (Block *)body->Clone(ast));
}

inline Node *FunVal::Clone(Ast &ast) const {
    auto fv = ast.New<FunVal>(line, (Block *)body->Clone(ast));
    fv->params = params;
    fv->explicit_params = explicit_params;
    return fv;
}

inline Node *VarDecl::Clone(Ast &ast) const {
    auto vd = ast.New<VarDecl>(line, isvar);
    vd->reusable = reusable;
    vd->isglobal = isglobal;
    vd->names = names;
    vd->type = type;
    CloneNodes(ast, inits, vd->inits);
    return vd;
}

inline Node *Assign::Clone(Ast &ast) const {
    return ast.New<Assign>(line, op, lval->Clone(ast), rhs->Clone(ast));
}

inline Node *IncDec::Clone(Ast &ast) const {
    return ast.New<IncDec>(line, op, lval->Clone(ast));
}

// Declarations share their symbol; a nested FnDecl's body is cloned lazily
// per specialization when the function is called, not here.
inline Node *FnDecl::Clone(Ast &ast) const { return ast.New<FnDecl>(line, sf); }
inline Node *StructDecl::Clone(Ast &ast) const { return ast.New<StructDecl>(line, st); }
inline Node *EnumDecl::Clone(Ast &ast) const { return ast.New<EnumDecl>(line, en); }
inline Node *AliasDecl::Clone(Ast &ast) const { return ast.New<AliasDecl>(line, al); }

// ---------------------------------------------------------------------------
// Children: every direct child once, nulls skipped. A nested FnDecl's body is
// deliberately not a child (it runs in its own specializations); walks that
// want it handle FnDecl explicitly.

#define CH(c) if (c) f(c)

inline void IntLit::Children(const function<void(Node *)> &) const {}
inline void FltLit::Children(const function<void(Node *)> &) const {}
inline void BoolLit::Children(const function<void(Node *)> &) const {}
inline void NullLit::Children(const function<void(Node *)> &) const {}
inline void StrLit::Children(const function<void(Node *)> &) const {}
inline void Ident::Children(const function<void(Node *)> &) const {}
inline void Continue::Children(const function<void(Node *)> &) const {}
inline void FnDecl::Children(const function<void(Node *)> &) const {}
inline void StructDecl::Children(const function<void(Node *)> &) const {}
inline void EnumDecl::Children(const function<void(Node *)> &) const {}
inline void AliasDecl::Children(const function<void(Node *)> &) const {}

inline void ArrayLit::Children(const function<void(Node *)> &f) const {
    for (auto e : elems) f(e);
    CH(fillval);
    CH(fillcount);
}

inline void StructLit::Children(const function<void(Node *)> &f) const {
    for (auto &fi : inits) f(fi.val);
}

inline void Unary::Children(const function<void(Node *)> &f) const { f(child); }

inline void Binary::Children(const function<void(Node *)> &f) const {
    f(left);
    f(right);
}

inline void Dot::Children(const function<void(Node *)> &f) const { f(obj); }

inline void Call::Children(const function<void(Node *)> &f) const {
    f(callee);
    for (auto a : args) f(a);
    CH((Node *)trailing);
}

inline void Index::Children(const function<void(Node *)> &f) const {
    f(obj);
    f(idx);
}

inline void SliceExpr::Children(const function<void(Node *)> &f) const {
    f(obj);
    CH(lo);
    CH(hi);
}

inline void AsCast::Children(const function<void(Node *)> &f) const { f(child); }

inline void RangeExpr::Children(const function<void(Node *)> &f) const {
    f(lo);
    f(hi);
}

inline void Block::Children(const function<void(Node *)> &f) const {
    for (auto st : stmts) f(st);
    CH(tail);
}

inline void IfExpr::Children(const function<void(Node *)> &f) const {
    f(cond);
    f(thenb);
    CH(elseb);
}

inline void MatchExpr::Children(const function<void(Node *)> &f) const {
    f(scrutinee);
    for (auto &arm : arms) {
        CH(arm.pat.lo);
        CH(arm.pat.hi);
        f(arm.body);
    }
}

inline void EarlyBlock::Children(const function<void(Node *)> &f) const { f(body); }

inline void InlineBlock::Children(const function<void(Node *)> &f) const { f(body); }

inline void While::Children(const function<void(Node *)> &f) const {
    f(cond);
    f(body);
}

inline void LoopExpr::Children(const function<void(Node *)> &f) const { f(body); }

inline void ForLoop::Children(const function<void(Node *)> &f) const {
    f(iter);
    f(body);
}

inline void Guard::Children(const function<void(Node *)> &f) const {
    f(cond);
    CH((Node *)elseb);
}

inline void Return::Children(const function<void(Node *)> &f) const {
    for (auto v : vals) f(v);
}

inline void Break::Children(const function<void(Node *)> &f) const { CH(val); }

inline void FunVal::Children(const function<void(Node *)> &f) const { f(body); }

inline void VarDecl::Children(const function<void(Node *)> &f) const {
    for (auto i : inits) f(i);
}

inline void Assign::Children(const function<void(Node *)> &f) const {
    f(lval);
    f(rhs);
}

inline void IncDec::Children(const function<void(Node *)> &f) const { f(lval); }

#undef CH

}  // namespace goose
