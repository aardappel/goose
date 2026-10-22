// Goose compiler — a language server's analysis of one program
// (docs/design/language_server.md §4): the Ast a check of a root left,
// the diagnostics it gave, and the indexes the features read: every
// occurrence of a name in each file, every call's and every node's extent,
// where each checked variable is declared, and each source node's clones,
// which carry the types and resolutions the checker made per
// specialization (Node::origin).
#pragma once

namespace goose {

// A place in a file: a name's, or an extent's.
struct Place {
    int fileidx = -1;
    size_t start = 0, len = 0;
    bool Known() const { return fileidx >= 0; }
};

// A name as a file has it: a use, or a declaration of something.
struct Occurrence {
    enum Kind {
        IDENT, DOT, TYPE, INIT,                     // Uses: a name, a member, a type name, a literal's field.
        FN, STRUCT, ENUM, ALIAS, GLOBAL, LOCAL,     // Declarations.
        FIELD, VARIANT, PARAM, GENERIC, BINDER      // Parts of declarations, and the bindings of loops, arms and blocks.
    };
    Kind kind = IDENT;
    size_t start = 0, len = 0;      // In the file's text.
    string_view name;
    const Node *node = nullptr;     // IDENT, DOT: the source node. INIT: the StructLit. GLOBAL,
                                    // LOCAL: the VarDecl. BINDER: the ForLoop, MatchExpr or FunVal.
    TypeExpr *type = nullptr;       // TYPE
    SFunction *sf = nullptr;        // FN, PARAM
    SStruct *st = nullptr;          // STRUCT
    SEnum *en = nullptr;            // ENUM, VARIANT
    SAlias *al = nullptr;           // ALIAS
    const Field *field = nullptr;   // FIELD
    const SVariant *variant = nullptr;      // VARIANT
    const Param *param = nullptr;   // PARAM
    const GenericParam *generic = nullptr;  // GENERIC
    int index = 0;                  // GLOBAL, LOCAL: which name. INIT: which initializer. BINDER: 0
                                    // the loop variable, 1 its index; a match arm's number; a
                                    // block parameter's number.
    bool callee = false;            // IDENT, DOT: a call's callee.
    bool isvar = false;             // A declaration that can be assigned.

    // Whether it declares what it names rather than using it.
    bool IsDeclaration() const { return kind >= FN; }
};

// A call's extent, for the signature help at a position inside it.
struct CallSpan {
    size_t start = 0, end = 0;
    const Call *call = nullptr;
};

// A node's extent: from the first token in it (a binary's left operand,
// which lies before the operator the node is anchored at) to one past its
// last.
struct NodeSpan {
    size_t start = 0, end = 0;
    const Node *node = nullptr;
};

struct FileIndex {
    int fileidx = 0;
    string uri;
    TextIndex text;
    vector<Occurrence> occs;        // By start, one per start.
    vector<CallSpan> calls;         // By start.
    vector<NodeSpan> spans;         // By start, the outer of two starting together first.

    // The occurrence covering an offset.
    const Occurrence *At(size_t offset) const {
        auto it = upper_bound(occs.begin(), occs.end(), offset,
                              [](size_t o, const Occurrence &x) { return o < x.start; });
        if (it == occs.begin()) return nullptr;
        --it;
        return offset < it->start + it->len ? &*it : nullptr;
    }
    // The innermost call whose extent holds an offset.
    const CallSpan *CallAt(size_t offset) const {
        const CallSpan *found = nullptr;
        for (auto &c : calls) {
            if (c.start > offset) break;
            if (offset <= c.end && (!found || c.start >= found->start)) found = &c;
        }
        return found;
    }
    // The node extents holding an offset, outer first. An offset at an
    // extent's end is in it, so that the end of a name still selects it.
    vector<const NodeSpan *> SpansAt(size_t offset) const {
        vector<const NodeSpan *> found;
        for (auto &s : spans) {
            if (s.start > offset) break;
            if (offset <= s.end) found.push_back(&s);
        }
        return found;
    }
};

struct Analysis {
    unique_ptr<Ast> ast;
    bool parsed = false;            // Past resolution: the indexes stand on a whole program.
    bool indexed = false;           // The source's indexes stand (IndexSource).
    bool built = false;             // All the indexes below stand.
    vector<Diagnostic> diags;       // Warnings as they came, then the error's.
    map<string, int> fileof;        // PathKey -> index in ast->sources.
    vector<FileIndex> files;        // Per source.
    unordered_map<const Node *, const FunVal *> funvals;  // A block's body -> the block.
    unordered_map<const Node *, vector<Node *>> clones;   // Source node -> its clones.
    // The clones an exemplar check made (FnSpec::exemplar), which follow
    // the real ones in `clones`.
    unordered_set<const Node *> exemplarclones;
    // Where each checked variable is declared: the `let` or `var` of a
    // local or global, and the place of its name, which for a loop's, an
    // arm's or a block parameter's binding the checker does not keep (it
    // places those at the loop, the match or the call). A binding with no
    // place is synthesized, such as a block's implicit `it`.
    unordered_map<const VarDef *, const VarDecl *> vardecls;
    unordered_map<const VarDef *, Place> defplaces;
    // The variant a match pattern's name (its source Ident) resolved to.
    unordered_map<const Node *, const SVariant *> patternvariants;

    int FileOf(const string &key) const {
        auto it = fileof.find(key);
        return it == fileof.end() ? -1 : it->second;
    }

    // The first clone of n (or n itself) that `pred` accepts: where the
    // checker's annotations are.
    template<typename F> const Node *Find(const Node *n, F pred) const {
        if (pred(n)) return n;
        if (auto it = clones.find(n); it != clones.end())
            for (auto c : it->second) if (pred(c)) return c;
        return nullptr;
    }

    bool FromExemplar(const Node *clone) const { return exemplarclones.count(clone) > 0; }
    // Whether an exemplar check cloned a source node at all: one that gave
    // it no annotation ended before it.
    bool InExemplar(const Node *n) const {
        if (auto it = clones.find(n); it != clones.end())
            for (auto c : it->second) if (FromExemplar(c)) return true;
        return false;
    }

    // The value type of a source node in the first specialization that
    // gave it one, and all of them, distinct, up to `most`.
    TypeExpr *TypeOf(const Node *n) const {
        auto c = Find(n, [](const Node *x) { return x->exprtype != nullptr; });
        return c ? c->exprtype : nullptr;
    }
    vector<TypeExpr *> TypesOf(const Node *n, size_t most) const {
        vector<TypeExpr *> types;
        set<string> seen;
        auto add = [&](const Node *x) {
            if (!x->exprtype || types.size() >= most) return;
            string s;
            x->exprtype->Dump(s);
            if (seen.insert(s).second) types.push_back(x->exprtype);
        };
        add(n);
        if (auto it = clones.find(n); it != clones.end()) for (auto c : it->second) add(c);
        return types;
    }

    // The declaration (as the source has it) of a variable the checker
    // made: the `let` or `var` of a local or a global.
    const VarDecl *VarDeclOf(const VarDef *vd) const {
        auto it = vardecls.find(vd);
        return it == vardecls.end() ? nullptr : it->second;
    }

    // The byte offset of a name the parser kept as a view into the file's
    // text, -1 for one it interned.
    int64_t OffsetOfView(int fileidx, string_view name) const {
        auto &src = *ast->sources[(size_t)fileidx].second;
        if (name.data() < src.data() || name.data() + name.size() > src.data() + src.size()) return -1;
        return name.data() - src.data();
    }
    Place PlaceOfView(int fileidx, string_view name) const {
        if (fileidx < 0 || fileidx >= (int)files.size() || name.empty()) return {};
        auto off = OffsetOfView(fileidx, name);
        return off < 0 ? Place {} : Place { fileidx, (size_t)off, name.size() };
    }

    // The files: their texts, line starts and URIs, which publishing the
    // diagnostics needs before anything else is indexed.
    void IndexFiles() {
        auto &a = *ast;
        files.resize(a.sources.size());
        for (size_t i = 0; i < a.sources.size(); i++) {
            auto &f = files[i];
            f.fileidx = (int)i;
            f.uri = UriOfPath(CanonicalPath(a.sources[i].first));
            f.text.Index(*a.sources[i].second);
            fileof[PathKey(a.sources[i].first)] = (int)i;
        }
    }

    // The indexes of the program as written, from the Ast as resolution
    // left it. The check rewrites a global's initializer in place, as it
    // checks it rather than a clone of it: it puts the defaults of the
    // parameters a call leaves out among its arguments, orders a struct
    // literal's initializers by field with the defaults filled in, and
    // expands a const for.
    void IndexSource() {
        if (indexed) return;
        indexed = true;
        auto &a = *ast;
        if (files.size() != a.sources.size()) IndexFiles();
        auto infile = [&](int fileidx) { return fileidx >= 0 && fileidx < (int)files.size(); };
        auto add = [&](Occurrence o, int fileidx, int64_t start) {
            if (start < 0 || !infile(fileidx) || !o.len) return;
            o.start = (size_t)start;
            files[(size_t)fileidx].occs.push_back(o);
        };
        auto view = [&](Occurrence o, int fileidx, string_view name) {
            o.name = name;
            o.len = name.size();
            add(o, fileidx, infile(fileidx) ? OffsetOfView(fileidx, name) : -1);
        };
        auto at = [&](Occurrence o, Line l, string_view name) {
            if (l.line <= 0 || !infile(l.fileidx)) return;
            o.name = name;
            o.len = name.size();
            add(o, l.fileidx, files[(size_t)l.fileidx].text.OffsetOf(l.line, l.col));
        };
        auto decl = [&](Occurrence::Kind k) { Occurrence o; o.kind = k; return o; };
        // Declarations and their parts.
        auto generics = [&](const vector<GenericParam> &gs) {
            for (auto &g : gs) {
                auto o = decl(Occurrence::GENERIC);
                o.generic = &g;
                at(o, g.line, g.name);
            }
        };
        auto fields = [&](const vector<Field> &fs) {
            for (auto &f : fs) {
                if (f.ispad) continue;
                auto o = decl(Occurrence::FIELD);
                o.field = &f;
                o.isvar = !f.isconst;
                at(o, f.line, f.name);
            }
        };
        for (auto sf : a.functions) {
            auto o = decl(Occurrence::FN);
            o.sf = sf;
            view(o, sf->line.fileidx, sf->name);
            generics(sf->generics);
            for (auto &p : sf->params) {
                auto po = decl(Occurrence::PARAM);
                po.sf = sf;
                po.param = &p;
                po.isvar = p.isvar;
                at(po, p.line, p.name);
            }
        }
        for (auto st : a.structs) {
            auto o = decl(Occurrence::STRUCT);
            o.st = st;
            view(o, st->line.fileidx, st->name);
            generics(st->generics);
            fields(st->fields);
        }
        for (auto en : a.enums) {
            auto o = decl(Occurrence::ENUM);
            o.en = en;
            view(o, en->line.fileidx, en->name);
            generics(en->generics);
            for (auto &v : en->variants) {
                auto vo = decl(Occurrence::VARIANT);
                vo.en = en;
                vo.variant = &v;
                at(vo, v.line, v.name);
                fields(v.fields);
            }
        }
        for (auto al : a.aliases) {
            auto o = decl(Occurrence::ALIAS);
            o.al = al;
            view(o, al->line.fileidx, al->name);
        }
        for (auto g : a.globals) {
            for (size_t i = 0; i < g->names.size(); i++) {
                auto o = decl(Occurrence::GLOBAL);
                o.node = g;
                o.index = (int)i;
                o.isvar = g->isvar;
                view(o, g->line.fileidx, g->names[i]);
            }
        }
        // Type names, as written: the parser's type expressions, which the
        // resolver rewrote in place, keep the use's place; the checker's
        // copies share it and are left out, as is a place that does not
        // spell the type's name (a declaration's own type at its keyword).
        for (auto t : a.alltypes) {
            if (t->line.line <= 0 || !infile(t->line.fileidx)) continue;
            string_view leaf;
            switch (t->kind) {
                case TY_STRUCT:  leaf = t->struc->st->name; break;
                case TY_ENUM:    leaf = t->enu->en->name; break;
                case TY_GENERIC: leaf = t->named->name; break;
                case TY_VARIANT: leaf = t->var->name; break;
                default: continue;
            }
            auto &f = files[(size_t)t->line.fileidx];
            auto start = f.text.OffsetOf(t->line.line, t->line.col);
            if (start < 0) continue;
            // A variant type is anchored at its `.`; the name follows.
            if (t->kind == TY_VARIANT && (size_t)start < f.text.text.size() && f.text.text[(size_t)start] == '.') start++;
            auto len = f.text.NameLengthAt((size_t)start);
            if (!len) continue;
            auto written = f.text.text.substr((size_t)start, len);
            auto lastleaf = written.substr(written.rfind("::") == string_view::npos ? 0 : written.rfind("::") + 2);
            if (lastleaf != leaf) continue;
            Occurrence o;
            o.kind = Occurrence::TYPE;
            o.type = t;
            o.name = written;
            o.len = len;
            add(o, t->line.fileidx, start);
        }
        // Uses inside bodies and initializers, the bindings declared there,
        // and the extent of every node, from the earliest token in it to
        // its end: a visit gives where its node's tokens start, -1 for
        // none known.
        function<int64_t(Node *, bool)> visit = [&](Node *n, bool callee) -> int64_t {
            if (!n) return -1;
            auto fileidx = n->line.fileidx;
            if (auto id = Is<Ident>(n)) {
                Occurrence o;
                o.kind = Occurrence::IDENT;
                o.node = n;
                o.callee = callee;
                at(o, n->line, id->name);
            } else if (auto d = Is<Dot>(n)) {
                Occurrence o;
                o.kind = Occurrence::DOT;
                o.node = n;
                o.callee = callee;
                o.name = d->name;
                o.len = d->name.size();
                if (n->end.line > 0 && n->end.fileidx == fileidx && infile(fileidx)) {
                    auto end = files[(size_t)fileidx].text.OffsetOf(n->end.line, n->end.col);
                    if (end >= (int64_t)o.len) add(o, fileidx, end - (int64_t)o.len);
                }
            } else if (auto sl = Is<StructLit>(n)) {
                for (size_t i = 0; i < sl->inits.size(); i++) {
                    if (sl->inits[i].name.empty()) continue;
                    auto o = decl(Occurrence::INIT);
                    o.node = n;
                    o.index = (int)i;
                    view(o, fileidx, sl->inits[i].name);
                }
            } else if (auto vd = Is<VarDecl>(n)) {
                if (!vd->isglobal && !vd->results)
                    for (size_t i = 0; i < vd->names.size(); i++) {
                        auto o = decl(Occurrence::LOCAL);
                        o.node = n;
                        o.index = (int)i;
                        o.isvar = vd->isvar;
                        view(o, fileidx, vd->names[i]);
                    }
            } else if (auto fl = Is<ForLoop>(n)) {
                auto o = decl(Occurrence::BINDER);
                o.node = n;
                o.isvar = fl->byref;
                view(o, fileidx, fl->var);
                if (!fl->idxvar.empty()) {
                    o.index = 1;
                    view(o, fileidx, fl->idxvar);
                }
            } else if (Is<MatchExpr>(n) || Is<ConstFor>(n)) {
                // A match's arms, or those a const for in its arm list
                // generates.
                auto &arms = Is<MatchExpr>(n) ? Is<MatchExpr>(n)->arms : Is<ConstFor>(n)->arms;
                for (size_t i = 0; i < arms.size(); i++) {
                    if (arms[i].pat.binder.empty()) continue;
                    auto o = decl(Occurrence::BINDER);
                    o.node = n;
                    o.index = (int)i;
                    o.isvar = arms[i].pat.byref;
                    view(o, fileidx, arms[i].pat.binder);
                }
            } else if (auto fv = Is<FunVal>(n)) {
                funvals[fv->body] = fv;
                for (size_t i = 0; i < fv->params.size(); i++) {
                    auto o = decl(Occurrence::BINDER);
                    o.node = n;
                    o.index = (int)i;
                    o.isvar = fv->params[i].isvar;
                    at(o, fv->params[i].line, fv->params[i].name);
                }
            }
            int64_t start = infile(fileidx) && n->line.line > 0
                ? files[(size_t)fileidx].text.OffsetOf(n->line.line, n->line.col) : -1;
            auto child = [&](Node *ch, bool iscallee) {
                auto s = visit(ch, iscallee);
                if (s >= 0 && ch->line.fileidx == fileidx && (start < 0 || s < start)) start = s;
            };
            if (auto c = Is<Call>(n)) {
                if (start >= 0 && n->end.line > 0) {
                    auto &f = files[(size_t)fileidx];
                    auto end = f.text.OffsetOf(n->end.line, n->end.col);
                    if (end >= start) f.calls.push_back({ (size_t)start, (size_t)end, c });
                }
                child(c->callee, true);
                for (auto arg : c->args) child(arg, false);
                if (c->trailing) child(c->trailing, false);
            } else {
                n->Children([&](Node *ch) { child(ch, false); });
            }
            if (start >= 0 && n->end.line > 0 && n->end.fileidx == fileidx) {
                auto &f = files[(size_t)fileidx];
                auto end = f.text.OffsetOf(n->end.line, n->end.col);
                if (end >= start) f.spans.push_back({ (size_t)start, (size_t)end, n });
            }
            return start;
        };
        for (auto sf : a.functions) {
            if (sf->body) visit(sf->body, false);
            for (auto &p : sf->params) if (p.defaultval) visit(p.defaultval, false);
        }
        for (auto st : a.structs)
            for (auto &f : st->fields) if (f.defaultval) visit(f.defaultval, false);
        for (auto en : a.enums)
            for (auto &v : en->variants)
                for (auto &f : v.fields) if (f.defaultval) visit(f.defaultval, false);
        for (auto g : a.globals) {
            for (auto init : g->inits) visit(init, false);
        }
        // The declarations' extents, outermost of all.
        for (auto d : a.topdecls) {
            if (d->line.line <= 0 || d->end.line <= 0 || !infile(d->line.fileidx)) continue;
            auto &f = files[(size_t)d->line.fileidx];
            auto start = f.text.OffsetOf(d->line.line, d->line.col);
            auto end = f.text.OffsetOf(d->end.line, d->end.col);
            if (start >= 0 && end >= start) f.spans.push_back({ (size_t)start, (size_t)end, d });
        }
        for (auto &f : files) {
            stable_sort(f.occs.begin(), f.occs.end(),
                        [](const Occurrence &x, const Occurrence &y) { return x.start < y.start; });
            f.occs.erase(unique(f.occs.begin(), f.occs.end(),
                                [](const Occurrence &x, const Occurrence &y) { return x.start == y.start; }),
                         f.occs.end());
            stable_sort(f.calls.begin(), f.calls.end(),
                        [](const CallSpan &x, const CallSpan &y) { return x.start < y.start; });
            stable_sort(f.spans.begin(), f.spans.end(), [](const NodeSpan &x, const NodeSpan &y) {
                return x.start != y.start ? x.start < y.start : x.end > y.end;
            });
        }
    }

    // The rest of the indexes, from the Ast as the check left it.
    void Build() {
        if (built) return;
        built = true;
        IndexSource();
        auto &a = *ast;
        auto infile = [&](int fileidx) { return fileidx >= 0 && fileidx < (int)files.size(); };
        // The clones: every specialization's body, the bodies of the
        // function values its calls checked, and the defaults they made.
        // From them, where each checked variable is declared and what each
        // match pattern names, which the source nodes alone do not say. A
        // clone's names are views of the source's, and a match's arms are
        // its own: those a const for generated, one set per entry, have no
        // counterpart among the source's.
        auto bind = [&](const VarDef *vd, Place p) { if (vd && p.Known()) defplaces[vd] = p; };
        auto exemplar = false;
        function<void(Node *)> mark = [&](Node *n) {
            if (!n) return;
            if (n->origin) {
                clones[n->origin].push_back(n);
                if (exemplar) exemplarclones.insert(n);
            }
            auto fileidx = n->Origin()->line.fileidx;
            if (auto v = Is<VarDecl>(n)) {
                for (size_t i = 0; i < v->defs.size() && i < v->names.size(); i++) {
                    if (!v->defs[i]) continue;
                    vardecls[v->defs[i]] = Is<VarDecl>(n->Origin());
                    bind(v->defs[i], PlaceOfView(fileidx, v->names[i]));
                }
            } else if (auto fl = Is<ForLoop>(n)) {
                bind(fl->vdef, PlaceOfView(fileidx, fl->var));
                bind(fl->idxdef, PlaceOfView(fileidx, fl->idxvar));
            } else if (auto m = Is<MatchExpr>(n)) {
                for (auto &arm : m->arms) {
                    bind(arm.binder, PlaceOfView(fileidx, arm.pat.binder));
                    if (arm.variants.size() != arm.pat.items.size()) continue;
                    for (size_t k = 0; k < arm.variants.size(); k++)
                        if (arm.pat.items[k].lo && arm.variants[k])
                            patternvariants[arm.pat.items[k].lo->Origin()] = arm.variants[k];
                }
            } else if (auto c = Is<Call>(n)) {
                if (c->fvbody) {
                    // The block's parameters, where the block spells them.
                    if (auto it = funvals.find(c->fvbody->Origin()); it != funvals.end())
                        for (size_t i = 0; i < c->fvparams.size() && i < it->second->params.size(); i++) {
                            auto &p = it->second->params[i];
                            if (p.line.line <= 0 || !infile(p.line.fileidx)) continue;
                            auto off = files[(size_t)p.line.fileidx].text.OffsetOf(p.line.line, p.line.col);
                            if (off >= 0) bind(c->fvparams[i], Place { p.line.fileidx, (size_t)off, p.name.size() });
                        }
                    mark(c->fvbody);
                }
                if (c->defaultinit) mark(c->defaultinit);
            }
            n->Children(mark);
        };
        for (auto sp : a.fnspecs) if (sp->body) mark(sp->body);
        for (auto g : a.globals) {
            for (auto init : g->inits) mark(init);
            for (size_t i = 0; i < g->defs.size() && i < g->names.size(); i++) {
                if (!g->defs[i]) continue;
                vardecls[g->defs[i]] = g;
                bind(g->defs[i], PlaceOfView(g->line.fileidx, g->names[i]));
            }
        }
        // The exemplars, after the real clones: those of the exemplar's own
        // function and the functions nested in it. A callee's specialization
        // made for one has the caller's opaque types, which say nothing of
        // the callee.
        exemplar = true;
        for (auto sp : a.exemplarspecs) {
            auto own = false;
            for (auto sf = sp->sf; sf && !own; sf = sf->outer) own = sf == sp->exemplar;
            if (own && sp->body) mark(sp->body);
        }
    }
};

}  // namespace goose
