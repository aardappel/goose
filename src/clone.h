// Goose compiler — tree structure passes: Clone (deep copies) and Children
// (direct-child visiting, the basis for generic walks). Typecheck clones a
// function body per specialization so per-node annotations (types, resolved
// symbols) are per-instantiation; TypeExpr pointers are shared (they are
// templates; concrete types are computed during checking), and symbols
// (SFunction etc.) are shared too. All overrides live together here so each
// pass reads top to bottom.
#pragma once

namespace goose {

struct ExpansionArg {
    int binding = 0;
    Node *value = nullptr;
    TypeExpr *type = nullptr;
    TypeExpr *owner = nullptr;
    string_view field;
    // A variants<E> binding: the variant's name, which a match pattern takes
    // (the type is its variant type; the value, a payload-less one's).
    string_view variant;
};

struct ExpansionClone {
    static constexpr size_t MAXNODES = 100000;
    vector<ExpansionArg> args;
    map<SFunction *, SFunction *> functions;
    string_view trace;
    size_t *size = nullptr;
    function<void(Line, const string &)> error;
    const ExpansionArg *Find(int id) const {
        for (auto &a : args) if (a.binding == id) return &a;
        return nullptr;
    }
};

// The annotation fields typecheck fills are freshly defaulted by the leaf
// constructors below, so cloning an already-checked tree yields a clean one.

inline Node *Node::Clone(Ast &ast) const {
    auto ctx = ast.expansionclone;
    if (ctx) {
        if (++*ctx->size > ExpansionClone::MAXNODES)
            ctx->error(line, cat("const for expansion exceeds ", ExpansionClone::MAXNODES, " generated nodes"));
        if (auto id = Is<Ident>(this)) {
            if (auto arg = ctx->Find(expansionbinding)) {
                if (!arg->value) {
                    // An inner expansion can still be empty, or a nested
                    // function unused. Keep the error with this bound use
                    // until ordinary checking actually reaches it.
                    auto c = ast.New<Ident>(line, id->name, id->ns);
                    c->end = end;
                    c->expansionerror = ast.Intern(
                        arg->variant.empty()
                            ? cat("const for binding ", id->name, " is not a value (or a callable function)")
                            : cat("const for binding ", id->name, " is the variant ", arg->variant,
                                  ", which has a payload: construct it with ", id->name, " { ... }"));
                    c->expansiontrace = ctx->trace;
                    c->origin = Origin();
                    return c;
                }
                auto c = arg->value->Clone(ast);
                c->line = line;
                c->end = end;
                c->origin = Origin();
                c->expansiontrace = ctx->trace;
                return c;
            }
        }
    }
    auto c = Clone1(ast);
    c->end = end;
    c->origin = Origin();
    c->expansionbinding = expansionbinding;
    c->expansiontrace = ctx ? ctx->trace : expansiontrace;
    if (ctx) if (auto d = Is<Dot>(c)) {
        if (auto arg = ctx->Find(expansionbinding)) {
            d->name = arg->field;
            d->accessorowner = arg->owner;
            d->expansionbinding = 0;
        }
    }
    return c;
}

template<typename T> T *CloneOrNull(Ast &ast, T *n) {
    return n ? (T *)n->Clone(ast) : nullptr;
}

inline void CloneNodes(Ast &ast, const vector<Node *> &src, vector<Node *> &dst) {
    dst.reserve(src.size());
    for (auto n : src) dst.push_back(n->Clone(ast));
}

inline Node *IntLit::Clone1(Ast &ast) const {
    auto n = ast.New<IntLit>(line, val, text, uns);
    n->fixedtype = fixedtype;
    return n;
}
inline Node *FltLit::Clone1(Ast &ast) const {
    auto n = ast.New<FltLit>(line, val, text);
    n->fixedtype = fixedtype;
    return n;
}
inline Node *BoolLit::Clone1(Ast &ast) const { return ast.New<BoolLit>(line, val); }
inline Node *NullLit::Clone1(Ast &ast) const { return ast.New<NullLit>(line); }
inline Node *SelfRef::Clone1(Ast &ast) const { return ast.New<SelfRef>(line); }
inline Node *StrLit::Clone1(Ast &ast) const { return ast.New<StrLit>(line, val, multiline); }
inline Node *Ident::Clone1(Ast &ast) const {
    auto n = ast.New<Ident>(line, name, ns);
    n->expansionerror = expansionerror;
    return n;
}

inline Node *ArrayLit::Clone1(Ast &ast) const {
    auto a = ast.New<ArrayLit>(line);
    CloneNodes(ast, elems, a->elems);
    a->fillval = CloneOrNull(ast, fillval);
    a->fillcount = CloneOrNull(ast, fillcount);
    a->capexpr = CloneOrNull(ast, capexpr);
    return a;
}

inline Node *StructLit::Clone1(Ast &ast) const {
    auto sl = ast.New<StructLit>(line, ast.CloneExpansionType(type));
    sl->defaultall = defaultall;
    sl->implicit = implicit;
    sl->sourcefieldindices = sourcefieldindices;
    sl->inits.reserve(inits.size());
    for (auto &fi : inits) sl->inits.push_back(ast.CloneFieldInit(fi));
    return sl;
}

inline Node *Unary::Clone1(Ast &ast) const {
    return ast.New<Unary>(line, op, child->Clone(ast));
}

inline Node *Binary::Clone1(Ast &ast) const {
    return ast.New<Binary>(line, op, left->Clone(ast), right->Clone(ast));
}

inline Node *Dot::Clone1(Ast &ast) const {
    auto d = ast.New<Dot>(line, obj->Clone(ast), name, ns);
    d->accessorowner = accessorowner;
    d->incomplete = incomplete;
    return d;
}

inline Node *Call::Clone1(Ast &ast) const {
    auto c = ast.New<Call>(line, callee->Clone(ast));
    for (auto t : tyargs) c->tyargs.push_back(ast.CloneExpansionType(t));
    c->implicit = implicit;
    for (size_t i = 0; i < args.size(); i++)
        if (!IsDefaultArg(i)) c->args.push_back(args[i]->Clone(ast));
    c->trailing = (FunVal *)CloneOrNull(ast, (Node *)trailing);
    return c;
}

inline Node *Index::Clone1(Ast &ast) const {
    return ast.New<Index>(line, obj->Clone(ast), idx->Clone(ast));
}

inline Node *SliceExpr::Clone1(Ast &ast) const {
    auto sl = ast.New<SliceExpr>(line, obj->Clone(ast));
    sl->lo = CloneOrNull(ast, lo);
    sl->hi = CloneOrNull(ast, hi);
    sl->lo_from_end = lo_from_end;
    sl->hi_from_end = hi_from_end;
    sl->cmpview = cmpview;
    return sl;
}

inline Node *AsCast::Clone1(Ast &ast) const {
    auto c = ast.New<AsCast>(line, child->Clone(ast), ast.CloneExpansionType(type), unchecked);
    c->implicit = implicit;
    return c;
}

inline Node *RangeExpr::Clone1(Ast &ast) const {
    return ast.New<RangeExpr>(line, lo->Clone(ast), hi->Clone(ast));
}

inline Node *Block::Clone1(Ast &ast) const {
    auto b = ast.New<Block>(line);
    b->expansionboundary = expansionboundary;
    b->brokenat = brokenat;
    CloneNodes(ast, stmts, b->stmts);
    b->tail = CloneOrNull(ast, tail);
    return b;
}

inline Node *IfExpr::Clone1(Ast &ast) const {
    return ast.New<IfExpr>(line, cond->Clone(ast), (Block *)thenb->Clone(ast),
                           CloneOrNull(ast, elseb));
}

// An arm as written: a variants<E> binding in its pattern becomes the
// variant's name, which the match resolves as any other (§8.1).
inline MatchArm CloneArm(Ast &ast, const MatchArm &arm) {
    MatchArm a;
    a.pat = arm.pat;
    a.generated = arm.generated;
    a.expansion = CloneOrNull(ast, arm.expansion);
    for (auto &pi : a.pat.items) {
        auto ctx = ast.expansionclone;
        auto arg = ctx ? ctx->Find(pi.lo->expansionbinding) : nullptr;
        if (arg && !arg->variant.empty() && Is<Ident>(pi.lo)) {
            auto id = ast.New<Ident>(pi.lo->line, arg->variant);
            id->end = pi.lo->end;
            id->origin = pi.lo->Origin();
            id->expansiontrace = ctx->trace;
            pi.lo = id;
            pi.variantowner = arg->type->var->adt;
        } else {
            pi.lo = pi.lo->Clone(ast);
        }
        pi.hi = CloneOrNull(ast, pi.hi);
    }
    a.body = CloneOrNull(ast, arm.body);
    return a;
}

inline Node *MatchExpr::Clone1(Ast &ast) const {
    auto m = ast.New<MatchExpr>(line, scrutinee->Clone(ast));
    m->arms.reserve(arms.size());
    for (auto &arm : arms) {
        auto a = CloneArm(ast, arm);
        m->arms.push_back(a);
    }
    return m;
}

inline Node *EarlyBlock::Clone1(Ast &ast) const {
    return ast.New<EarlyBlock>(line, (Block *)body->Clone(ast));
}

inline Node *While::Clone1(Ast &ast) const {
    return ast.New<While>(line, cond->Clone(ast), (Block *)body->Clone(ast));
}

inline Node *LoopExpr::Clone1(Ast &ast) const {
    return ast.New<LoopExpr>(line, (Block *)body->Clone(ast));
}

inline Node *ForLoop::Clone1(Ast &ast) const {
    auto f = ast.New<ForLoop>(line, byref, var, idxvar, iter->Clone(ast),
                              (Block *)body->Clone(ast));
    f->vartype = ast.CloneExpansionType(vartype);
    f->idxtype = ast.CloneExpansionType(idxtype);
    return f;
}

inline Node *Return::Clone1(Ast &ast) const {
    auto r = ast.New<Return>(line);
    CloneNodes(ast, vals, r->vals);
    r->from = from;
    r->ns = ns;
    return r;
}

inline Node *Break::Clone1(Ast &ast) const {
    return ast.New<Break>(line, CloneOrNull(ast, val));
}

inline Node *Continue::Clone1(Ast &ast) const { return ast.New<Continue>(line); }
inline Node *Defer::Clone1(Ast &ast) const {
    return ast.New<Defer>(line, (Block *)body->Clone(ast));
}

// InlineBlocks exist only after typecheck; the annotation-stripping Clone has
// no meaning for them (the optimizer's copier in optimize.h preserves them).
inline Node *InlineBlock::Clone1(Ast &ast) const {
    assert(false);
    return ast.New<InlineBlock>(line, sf, spec, (Block *)body->Clone(ast));
}

inline Node *FunVal::Clone1(Ast &ast) const {
    auto fv = ast.New<FunVal>(line, (Block *)body->Clone(ast));
    fv->params = params;
    for (auto &p : fv->params) p.type = ast.CloneExpansionType(p.type);
    fv->explicit_params = explicit_params;
    return fv;
}

inline Node *VarDecl::Clone1(Ast &ast) const {
    auto vd = ast.New<VarDecl>(line, isvar);
    vd->isconst = isconst;
    vd->reusable = reusable;
    vd->isglobal = isglobal;
    vd->byref = byref;
    vd->results = results;
    vd->names = names;
    vd->type = ast.CloneExpansionType(type);
    CloneNodes(ast, inits, vd->inits);
    return vd;
}

inline Node *Assign::Clone1(Ast &ast) const {
    return ast.New<Assign>(line, op, lval->Clone(ast), rhs->Clone(ast));
}

inline Node *IncDec::Clone1(Ast &ast) const {
    return ast.New<IncDec>(line, op, lval->Clone(ast));
}

// Declarations share their symbol; a nested FnDecl's body is cloned lazily
// per specialization when the function is called, not here.
inline Node *FnDecl::Clone1(Ast &ast) const {
    auto ctx = ast.expansionclone;
    if (!ctx) return ast.New<FnDecl>(line, sf);
    // Each fragment owns the nested declarations it specializes. Their
    // captures and later specialization caches must not alias another entry.
    auto copy = new SFunction(*sf);
    ast.functions.push_back(copy);
    copy->specs.clear();
    ctx->functions[sf] = copy;
    if (auto it = ctx->functions.find(sf->outer); it != ctx->functions.end())
        copy->outer = it->second;
    for (auto &g : copy->generics) g.bound = ast.CloneExpansionType(g.bound);
    for (auto &p : copy->params) {
        p.type = ast.CloneExpansionType(p.type);
        p.defaultval = CloneOrNull(ast, p.defaultval);
    }
    for (auto &t : copy->rets) t = ast.CloneExpansionType(t);
    copy->body = CloneOrNull(ast, sf->body);
    return ast.New<FnDecl>(line, copy);
}

inline Node *ConstFor::Clone1(Ast &ast) const {
    auto cf = ast.New<ConstFor>(line);
    cf->source = source;
    cf->names = names;
    cf->bindings = bindings;
    for (auto t : types) cf->types.push_back(ast.CloneExpansionType(t));
    CloneNodes(ast, constants, cf->constants);
    cf->body = CloneOrNull(ast, body);
    for (auto &fi : inits) cf->inits.push_back(ast.CloneFieldInit(fi));
    cf->armfragments = armfragments;
    for (auto &arm : arms) cf->arms.push_back(CloneArm(ast, arm));
    return cf;
}

inline FieldInit Ast::CloneFieldInit(const FieldInit &fi) {
    auto out = fi;
    out.val = fi.val->Clone(*this);
    if (expansionclone) if (auto a = expansionclone->Find(fi.expansionbinding)) {
        out.name = a->field;
        out.accessorowner = a->owner;
        out.expansionbinding = 0;
    }
    return out;
}

inline TypeExpr *Ast::CloneExpansionType(TypeExpr *t) {
    auto ctx = expansionclone;
    if (!t || !ctx) return t;
    if (t->kind == TY_GENERIC) {
        if (auto a = ctx->Find(t->named->expansionbinding)) {
            auto invalid = [&](string msg) {
                auto n = NewType(TY_GENERIC, t->line);
                n->named = NewDetail<TypeName>();
                *n->named = *t->named;
                n->named->expansionbinding = 0;
                n->named->expansionerror = Intern(std::move(msg));
                return n;
            };
            if (!a->type) return invalid(cat("const for binding ", t->named->name, " is not a type"));
            if (!t->named->args.empty()) return invalid("const for type binding takes no type arguments");
            auto b = a->type;
            if (t->named->varmode) {
                if (b->kind != TY_ENUM) return invalid("variable mode requires an ADT type");
                b = EnumOf(b->enu->en, b->enu->args, true, t->line);
            }
            return t->cq ? ConstOf(b) : b;
        }
    }
    // Copy compound templates: sizes and nested type arguments can depend
    // on this entry even when their outer kind does not.
    auto n = NewType(t->kind, t->line);
    *n = *t;
    auto args = [&](vector<TypeExpr *> &ts) { for (auto &a : ts) a = CloneExpansionType(a); };
    switch (t->kind) {
        case TY_GENERIC: case TY_UNRESOLVED:
            n->named = NewDetail<TypeName>(); *n->named = *t->named;
            args(n->named->args); break;
        case TY_STRUCT:
            n->struc = NewDetail<TypeStruct>(); *n->struc = *t->struc;
            n->struc->inst = nullptr; args(n->struc->args); break;
        case TY_ENUM:
            n->enu = NewDetail<TypeEnum>(); *n->enu = *t->enu;
            n->enu->inst = nullptr; args(n->enu->args); break;
        case TY_ARRAY:
            n->arr = NewDetail<TypeArray>(); *n->arr = *t->arr;
            n->arr->sub = CloneExpansionType(t->arr->sub);
            n->arr->sizeexpr = CloneOrNull(*this, t->arr->sizeexpr);
            if (n->arr->sizeexpr) n->arr->size = -1;
            break;
        case TY_SLICE: n->sub = CloneExpansionType(t->sub); break;
        case TY_REF:
            n->ref = NewDetail<TypeRef>(); *n->ref = *t->ref;
            n->ref->sub = CloneExpansionType(t->ref->sub); break;
        case TY_VARIANT:
            n->var = NewDetail<TypeVariant>(); *n->var = *t->var;
            n->var->adt = CloneExpansionType(t->var->adt); break;
        case TY_FN:
            n->fn = NewDetail<TypeFn>(); *n->fn = *t->fn;
            args(n->fn->args); args(n->fn->rets); break;
        default: return t;
    }
    return n;
}
inline Node *StructDecl::Clone1(Ast &ast) const { return ast.New<StructDecl>(line, st); }
inline Node *EnumDecl::Clone1(Ast &ast) const { return ast.New<EnumDecl>(line, en); }
inline Node *AliasDecl::Clone1(Ast &ast) const { return ast.New<AliasDecl>(line, al); }

// ---------------------------------------------------------------------------
// Children: every direct child once, nulls skipped. A nested FnDecl's body is
// deliberately not a child (it runs in its own specializations); walks that
// want it handle FnDecl explicitly.

#define CH(c) if (c) f(c)

inline void ConstFor::Children(const function<void(Node *)> &f) const {
    for (auto c : constants) f(c);
    CH(body);
    for (auto &fi : inits) f(fi.val);
    for (auto &arm : arms) {
        CH(arm.expansion);
        for (auto &pi : arm.pat.items) {
            f(pi.lo);
            CH(pi.hi);
        }
        CH(arm.body);
    }
}

inline void IntLit::Children(const function<void(Node *)> &) const {}
inline void FltLit::Children(const function<void(Node *)> &) const {}
inline void BoolLit::Children(const function<void(Node *)> &) const {}
inline void NullLit::Children(const function<void(Node *)> &) const {}
inline void SelfRef::Children(const function<void(Node *)> &) const {}
inline void StrLit::Children(const function<void(Node *)> &) const {}
inline void Ident::Children(const function<void(Node *)> &) const {}
inline void Continue::Children(const function<void(Node *)> &) const {}
inline void Defer::Children(const function<void(Node *)> &f) const { f(body); }
inline void FnDecl::Children(const function<void(Node *)> &) const {}
inline void StructDecl::Children(const function<void(Node *)> &) const {}
inline void EnumDecl::Children(const function<void(Node *)> &) const {}
inline void AliasDecl::Children(const function<void(Node *)> &) const {}

inline void ArrayLit::Children(const function<void(Node *)> &f) const {
    for (auto e : elems) f(e);
    CH(fillval);
    CH(fillcount);
    CH(capexpr);
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
        CH(arm.expansion);
        for (auto &pi : arm.pat.items) {
            f(pi.lo);
            CH(pi.hi);
        }
        CH(arm.body);
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

// The direct children of a checked node that run. A call's trailing block
// (Call::trailing) is the unchecked template; what runs is the instance
// typecheck checked into Call::fvbody. Every walk over checked trees --
// the optimizer's, BCE's, codegen's -- takes this in place of Children.
inline void RunChildren(Node *n, const function<void(Node *)> &f) {
    if (auto c = Is<Call>(n)) {
        f(c->callee);
        for (auto a : c->args) f(a);
        if (c->fvbody) f(c->fvbody);
        if (c->defaultinit) f(c->defaultinit);
        return;
    }
    n->Children(f);
}

// The nodes of a checked tree that run, itself included.
inline int CountNodes(Node *n) {
    if (!n) return 0;
    auto c = 1;
    RunChildren(n, [&](Node *ch) { c += CountNodes(ch); });
    return c;
}

// Whether the tree holds a `return` that exits function sf: exactly the ones
// an InlineBlock for sf catches (ast.h). The optimizer asks before unwrapping
// such a block into a plain expression, BCE before taking an inlined body to
// have one exit.
inline bool ReturnsTo(Node *n, SFunction *sf) {
    if (!n) return false;
    if (auto r = Is<Return>(n); r && r->target == sf) return true;
    auto found = false;
    RunChildren(n, [&](Node *ch) { found = found || ReturnsTo(ch, sf); });
    return found;
}

// Whether evaluating n can reach the storage of local v: n names v, or a
// variable that may refer into it or hold a reference into it -- one whose
// provenance names v, or is not known exactly. A parameter or a global
// cannot, as v is a local of this activation; nor can a variable whose
// recorded provenance is complete and elsewhere. A captured v may be
// reached by any call.
inline bool MayReachLocal(Node *n, const VarDef *v) {
    if (v->captured) return true;
    auto reaches = [&](const Roots &r) {
        if (r.unknown) return true;
        for (auto &a : r.alts) if (a.root == v || !a.exact) return true;
        return false;
    };
    auto found = false;
    function<void(Node *)> walk = [&](Node *m) {
        if (!m || found) return;
        if (auto id = Is<Ident>(m); id && id->vdef) {
            auto d = id->vdef;
            if (d == v) found = true;
            else if (!d->isglobal && !d->isparam && d->type) {
                auto refs = d->type->kind == TY_REF || d->type->kind == TY_SLICE;
                if ((refs && (!d->refrootknown || d->ref.alts.empty() || reaches(d->ref))) ||
                    reaches(d->contents))
                    found = true;
            }
        }
        RunChildren(m, walk);
    };
    walk(n);
    return found;
}

// The local a function or inlined body builds at its result destination
// (§7.3): the top-level local that a `return` to `target` hands back in
// position `resultidx`, when no other return there hands back another one,
// and every other value returned there is built without reaching it. That
// value is constructed behind the local's elements and moved down over them,
// as every exit's value is that finds part of a value at its destination.
// Null when there is no such local; then `stop`, if given, names the local a
// return hands back and says what stood in the way where there is one.
struct NamedResultStop {
    const VarDef *local = nullptr;
    string why;
    Node *at = nullptr;           // The value or return that stood in the way.
};
inline const VarDef *NamedResultOf(Block *fnbody, SFunction *target, size_t nrets,
                                   size_t resultidx, NamedResultStop *ns = nullptr) {
    // Only bindings BindLocal places: a multi-name receive wires a call's
    // channels into locals of its own, which are not at a return destination.
    set<const VarDef *> toplocals;
    for (auto st : fnbody->stmts)
        if (auto vd = Is<VarDecl>(st); vd && vd->defs.size() == 1)
            toplocals.insert(vd->defs[0]);
    const VarDef *cand = nullptr;
    vector<Node *> others;
    string stop;
    Node *stopat = nullptr;
    auto stopped = [&](string what, Node *n) {
        if (!stop.empty()) return;
        stop = std::move(what);
        stopat = n;
    };
    auto consider = [&](Node *val) {
        auto id = Is<Ident>(val);
        if (!id || !id->vdef || !toplocals.count(id->vdef)) {
            others.push_back(val);
            return;
        }
        if (cand && cand != id->vdef)
            stopped(cat("another return hands back `", id->vdef->name, "`"), val);
        if (!cand) cand = id->vdef;
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto r = Is<Return>(n); r && r->target == target) {
            if (r->vals.size() != nrets) stopped("a return forwards several results of a call", r);
            else consider(r->vals[resultidx]);
        }
        RunChildren(n, walk);
    };
    walk(fnbody);
    auto tail = fnbody->tail;
    if (tail && nrets == 1 && tail->exprtype && tail->exprtype->kind != TY_VOID) consider(tail);
    if (!cand) return nullptr;
    for (auto v : others)
        if (MayReachLocal(v, cand)) stopped("another return's value may read or write it", v);
    if (stop.empty()) return cand;
    if (ns) *ns = { cand, stop, stopat };
    return nullptr;
}

// The variables one checked node names or binds: an identifier's, a
// declaration's, a `for`'s bindings, a match arm's payload, a function
// value's parameters. Walking the tree is the caller's business.
template<typename F> void NodeVars(Node *n, F f) {
    if (auto id = Is<Ident>(n)) f(id->vdef);
    if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) f(d);
    if (auto fl = Is<ForLoop>(n)) { f(fl->vdef); f(fl->idxdef); }
    if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) f(arm.binder);
    if (auto c = Is<Call>(n)) for (auto p : c->fvparams) f(p);
}

template<typename F> void NodeNamedArrays(Node *n, set<TypeExpr *> &seen, F f) {
    auto type = [&](TypeExpr *t) { NamedArraysIn(t, seen, f); };
    type(n->exprtype);
    NodeVars(n, [&](VarDef *v) { if (v) type(v->type); });
    if (auto c = Is<Call>(n)) for (auto t : c->rettypes) type(t);
    if (auto c = Is<AsCast>(n)) type(c->type);
}

}  // namespace goose
