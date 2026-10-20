// Bind const-for uses before namespace type resolution. Binding identities
// survive cloning; shadowing is decided at the definition, never at a call.
#pragma once

namespace goose {

struct ExpansionBinder {
    struct Binding { string_view name; int id; bool accessor; bool type; };
    vector<Binding> bindings;
    int next = 0;

    Binding Find(string_view name, bool type = false) {
        for (auto i = bindings.rbegin(); i != bindings.rend(); ++i)
            if (i->name == name && (!type || i->type)) return *i;
        return { name, 0, false, false };
    }
    // Runtime variables/functions hide expression and accessor uses, but
    // do not hide type parameters in type positions (as for normal generics).
    void Hide(string_view name, bool type = false) { bindings.push_back({ name, 0, false, type }); }
    struct Scope {
        ExpansionBinder &b;
        size_t base;
        Scope(ExpansionBinder &b) : b(b), base(b.bindings.size()) {}
        ~Scope() { b.bindings.resize(base); }
    };

    void Type(TypeExpr *t) {
        if (!t) return;
        switch (t->kind) {
            case TY_UNRESOLVED: case TY_GENERIC: {
                auto b = Find(t->named->name, true);
                if (b.id) {
                    t->kind = TY_GENERIC;
                    t->named->expansionbinding = b.id;
                }
                for (auto a : t->named->args) Type(a);
                break;
            }
            case TY_ARRAY: Type(t->arr->sub); Walk(t->arr->sizeexpr); break;
            case TY_SLICE: Type(t->sub); break;
            case TY_REF: Type(t->ref->sub); break;
            case TY_VARIANT: Type(t->var->adt); break;
            case TY_FN:
                for (auto a : t->fn->args) Type(a);
                for (auto r : t->fn->rets) Type(r);
                break;
            default: break;
        }
    }
    void Inits(vector<FieldInit> &inits) {
        for (auto &fi : inits) {
            auto b = Find(fi.name);
            if (b.accessor) fi.expansionbinding = b.id;
            Walk(fi.val);
        }
    }
    // A pattern's names bind as uses do: a variants<E> binding names its
    // variant there (cloning substitutes the variant's name).
    void Arms(vector<MatchArm> &arms) {
        for (auto &a : arms) {
            if (a.expansion) { Walk(a.expansion); continue; }
            for (auto &p : a.pat.items) { Walk(p.lo); Walk(p.hi); }
            Scope scope(*this);
            Hide(a.pat.binder); Walk(a.body);
        }
    }
    void Function(SFunction *sf) {
        Scope scope(*this);
        Hide(sf->name);
        for (auto &g : sf->generics) Hide(g.name, true);
        for (auto &g : sf->generics) Type(g.bound);
        // Parameters hide enclosing expansion bindings throughout the signature.
        for (auto &p : sf->params) Hide(p.name);
        for (auto &p : sf->params) { Type(p.type); Walk(p.defaultval); }
        for (auto t : sf->rets) Type(t);
        Walk(sf->body);
    }
    void Walk(Node *n) {
        if (!n) return;
        if (auto cf = Is<ConstFor>(n)) {
            for (auto t : cf->types) Type(t);
            for (auto c : cf->constants) Walk(c);
            Scope scope(*this);
            for (size_t i = 0; i < cf->names.size(); i++) {
                auto id = ++next;
                cf->bindings.push_back(id);
                bindings.push_back({ cf->names[i], id, cf->source == ES_FIELDS && i == 1, true });
            }
            Walk(cf->body);
            Inits(cf->inits);
            Arms(cf->arms);
            return;
        }
        if (auto b = Is<Block>(n)) {
            Scope scope(*this);
            for (auto st : b->stmts) {
                Walk(st);
                if (auto vd = Is<VarDecl>(st)) for (auto name : vd->names) Hide(name);
                if (auto fd = Is<FnDecl>(st)) Hide(fd->sf->name);
            }
            Walk(b->tail);
            return;
        }
        if (auto fd = Is<FnDecl>(n)) { Function(fd->sf); return; }
        if (auto fv = Is<FunVal>(n)) {
            Scope scope(*this);
            if (!fv->explicit_params) Hide("it");
            for (auto &p : fv->params) Hide(p.name);
            for (auto &p : fv->params) Type(p.type);
            Walk(fv->body);
            return;
        }
        if (auto fl = Is<ForLoop>(n)) {
            Type(fl->vartype); Type(fl->idxtype); Walk(fl->iter);
            Scope scope(*this);
            Hide(fl->var); Hide(fl->idxvar); Walk(fl->body);
            return;
        }
        if (auto m = Is<MatchExpr>(n)) {
            Walk(m->scrutinee);
            Arms(m->arms);
            return;
        }
        if (auto id = Is<Ident>(n)) n->expansionbinding = Find(id->name).id;
        if (auto d = Is<Dot>(n)) {
            auto b = Find(d->name);
            if (b.accessor) n->expansionbinding = b.id;
        }
        if (auto sl = Is<StructLit>(n)) { Type(sl->type); Inits(sl->inits); return; }
        if (auto vd = Is<VarDecl>(n)) Type(vd->type);
        if (auto c = Is<Call>(n)) for (auto t : c->tyargs) Type(t);
        if (auto c = Is<AsCast>(n)) Type(c->type);
        if (auto sd = Is<StructDecl>(n)) {
            for (auto &f : sd->st->fields) { Type(f.type); Walk(f.defaultval); }
        }
        if (auto ed = Is<EnumDecl>(n)) {
            for (auto &v : ed->en->variants)
                for (auto &f : v.fields) { Type(f.type); Walk(f.defaultval); }
        }
        if (auto ad = Is<AliasDecl>(n)) Type(ad->al->type);
        n->Children([&](Node *ch) { Walk(ch); });
    }
};

inline void BindExpansionNames(Ast &ast) {
    ExpansionBinder binder;
    for (auto n : ast.topdecls) binder.Walk(n);
}

} // namespace goose
