// Goose compiler — the language server's features (docs/design/
// language_server.md §6), each a request answered from a root's analysis:
// hover, go to definition, references and rename, document and workspace
// symbols, call hierarchy, semantic tokens, signature help, inlay hints,
// folding ranges, selection ranges, completion and code lens. Every one
// takes the request's params and writes its result, or writes nothing and
// returns false for a null result.
#pragma once

namespace goose {

// Where a request is: the analysis answering for the file, the file's
// index, and the position as a byte offset in its text.
struct Located {
    Analysis *an = nullptr;
    FileIndex *fi = nullptr;
    size_t offset = 0;
};

inline bool Locate(Workspace &ws, const Json &params, Located &loc, bool position = true) {
    auto td = params.Get("textDocument");
    auto uri = JsonStr(td ? td->Get("uri") : nullptr);
    if (uri.empty()) return false;
    int fileidx;
    if (!ws.Locate(PathKey(PathOfUri(uri)), loc.an, fileidx)) return false;
    loc.fi = &loc.an->files[(size_t)fileidx];
    if (position) {
        auto pos = params.Get("position");
        if (!pos) return false;
        loc.offset = loc.fi->text.OffsetOfPosition(JsonInt(pos->Get("line")), JsonInt(pos->Get("character")));
    }
    return true;
}

// --- rendering ---------------------------------------------------------------

inline string TypeText(const TypeExpr *t) {
    string s;
    if (t) t->Dump(s);
    else s = "?";
    return s;
}

inline string GenericsText(const vector<GenericParam> &gs) {
    if (gs.empty()) return {};
    string s = "<";
    for (size_t i = 0; i < gs.size(); i++) {
        if (i) s += ", ";
        s += gs[i].name;
        if (gs[i].bound) Append(s, ": ", TypeText(gs[i].bound));
    }
    return s + ">";
}

// A type's declaration as its header line.
inline string DeclDetail(const SStruct *st) {
    return cat(st->isroot ? "root " : "", "struct ", st->qname, GenericsText(st->generics));
}
inline string DeclDetail(const SEnum *en) { return cat("enum ", en->qname, GenericsText(en->generics)); }
inline string DeclDetail(const SAlias *al) { return cat("type ", al->qname, " = ", TypeText(al->type)); }

// A builtin as its calls show it: `name(N arguments)`.
inline string BuiltinCallText(const BuiltinDef &b) {
    return cat(b.name, "(", b.minargs == b.maxargs ? to_string(b.minargs) : cat(b.minargs, "..", b.maxargs),
               " arguments)");
}

// A function's declaration as its header line, defaults elided.
inline string FnHeader(const SFunction *sf) {
    string s;
    if (sf->isexport) s += "export ";
    if (sf->isextern) s += "extern ";
    if (sf->checkfree && !sf->isnested) s += "checkfree ";
    if (sf->isinline) s += "inline ";
    if (sf->issimd) s += "simd ";
    if (sf->isrec) s += "recursive ";
    s += sf->isthread ? "thread_fn " : "fn ";
    Append(s, sf->qname, GenericsText(sf->generics), "(");
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto &p = sf->params[i];
        if (i) s += ", ";
        if (p.isvar) s += "var ";
        s += p.name;
        if (p.type) Append(s, ": ", TypeText(p.type));
        if (p.defaultval) s += " = ...";
    }
    s += ")";
    if (sf->has_rets && !sf->rets.empty()) {
        s += " -> ";
        for (size_t i = 0; i < sf->rets.size(); i++) {
            if (i) s += ", ";
            s += TypeText(sf->rets[i]);
        }
    }
    return s;
}

// The specializations a function has, as `name(argument types)`, in the
// order they were made. Ones the key tells apart by what the types do not
// show (the roots an argument holds, say) read alike and are listed once.
inline vector<string> SpecLines(const SFunction *sf) {
    vector<string> lines;
    for (auto sp : sf->specs) {
        if (sp->isfunval) continue;
        auto s = cat(sf->qname, "(");
        for (size_t i = 0; i < sp->argtypes.size(); i++) {
            if (i) s += ", ";
            for (auto li : sp->litparams) if (li == (int)i) s += "literal ";
            s += TypeText(sp->argtypes[i]);
        }
        s += ")";
        if (find(lines.begin(), lines.end(), s) == lines.end()) lines.push_back(std::move(s));
    }
    return lines;
}

// SpecLines, up to `most` of them.
inline string SpecsText(const SFunction *sf, size_t most) {
    string s;
    auto lines = SpecLines(sf);
    for (size_t i = 0; i < lines.size(); i++) {
        if (i == most) {
            Append(s, "\n... ", lines.size() - most, " more");
            break;
        }
        Append(s, s.empty() ? "" : "\n", lines[i]);
    }
    return s;
}

// The keyword of a `let`, `var` or `const` declaration.
inline const char *VarKeyword(const VarDecl *vd) { return vd->isconst ? "const " : vd->isvar ? "var " : "let "; }

inline string VarDefHeader(const VarDef *vd) {
    auto s = vd->isparam ? cat("parameter ", vd->isvar ? "var " : "") : string(vd->isvar ? "var " : "let ");
    Append(s, vd->name, ": ", TypeText(vd->type));
    return s;
}

// A documentation comment as Markdown: each line less its `//` and one
// space.
inline string DocText(string_view doc) {
    string s;
    for (size_t pos = 0; pos <= doc.size();) {
        auto end = doc.find('\n', pos);
        if (end == string_view::npos) end = doc.size();
        auto line = doc.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        auto first = line.find_first_not_of(" \t");
        if (first != string_view::npos) line.remove_prefix(first);
        if (line.substr(0, 2) == "//") line.remove_prefix(2);
        if (!line.empty() && line[0] == ' ') line.remove_prefix(1);
        if (!s.empty()) s += '\n';
        s += line;
        pos = end + 1;
    }
    return s;
}

// The enum a variant belongs to: the one whose variants hold it.
inline SEnum *EnumOfVariant(const Ast &ast, const SVariant *v) {
    for (auto en : ast.enums)
        if (v >= en->variants.data() && v < en->variants.data() + en->variants.size()) return en;
    return nullptr;
}

inline string VariantHeader(const SEnum *en, const SVariant *v) {
    auto s = cat(en ? string(en->qname) : string("?"), ".", v->name);
    if (v->has_payload) {
        s += " {";
        auto first = true;
        for (auto &f : v->fields) {
            if (f.ispad) continue;
            Append(s, first ? " " : ", ", f.name, ": ", TypeText(f.type));
            first = false;
        }
        s += " }";
    }
    return s;
}

// The field a member access names, by the object's type: a struct's, or a
// variant's payload's.
inline const Field *FieldOfDot(const Dot *d) {
    if (d->fieldidx < 0 || !d->obj) return nullptr;
    auto t = d->obj->exprtype;
    while (t && t->kind == TY_REF) t = t->ref->sub;
    if (!t) return nullptr;
    const vector<Field> *fields = nullptr;
    if (t->kind == TY_STRUCT) fields = &t->struc->st->fields;
    else if (t->kind == TY_VARIANT && t->var->variant) fields = &t->var->variant->fields;
    if (!fields || d->fieldidx >= (int)fields->size()) return nullptr;
    return &(*fields)[(size_t)d->fieldidx];
}

// The checked call whose callee the occurrence is: the innermost call
// starting there, in the first specialization that resolved it.
inline const Call *ResolvedCall(const Located &loc, const Occurrence &occ) {
    auto span = loc.fi->CallAt(occ.start);
    if (!span) return nullptr;
    if (span->call->callee != occ.node) {
        if (span->call->callee != occ.node->Origin()) return nullptr;
        auto clones = loc.an->clones.find(span->call);
        if (clones != loc.an->clones.end())
            for (auto node : clones->second)
                if (auto call = Is<Call>(node); call && call->callee == occ.node) return call;
        return nullptr;
    }
    auto c = loc.an->Find(span->call, [](const Node *n) {
        auto x = Is<Call>(n);
        return x && (x->spec || x->builtin >= 0 || !x->dispatch.empty() || x->fvbody);
    });
    return c ? Is<Call>(c) : nullptr;
}

inline const SFunction *CalleeOf(const Call *c) {
    if (!c) return nullptr;
    if (c->spec) return c->spec->sf;
    if (!c->dispatch.empty() && c->dispatch[0]) return c->dispatch[0]->sf;
    return nullptr;
}

// The first clone of a name the check resolved to a variable or a function.
inline const Ident *ResolvedIdent(const Analysis &an, const Occurrence &occ) {
    auto c = an.Find(occ.node, [](const Node *n) {
        auto x = Is<Ident>(n);
        return x && (x->vdef || x->fnref);
    });
    return c ? Is<Ident>(c) : nullptr;
}

// The first clone of a member the check resolved to a field, a builtin
// member or a variant.
inline const Dot *ResolvedDot(const Analysis &an, const Occurrence &occ) {
    auto c = an.Find(occ.node, [](const Node *n) {
        auto x = Is<Dot>(n);
        return x && (x->fieldidx >= 0 || x->member >= 0 || x->variantconst);
    });
    return c ? Is<Dot>(c) : nullptr;
}

// The first clone of a call the check resolved to a specialization.
inline const Call *SpecializedCall(const Analysis &an, const Call *call) {
    auto c = an.Find(call, [](const Node *n) {
        auto x = Is<Call>(n);
        return x && x->spec;
    });
    return c ? Is<Call>(c) : nullptr;
}

// The overloads a UFCS call's name resolves to past the scopes around it,
// which depend on its receiver's type where a check gave it one.
inline const vector<SFunction *> &MethodsOf(const Analysis &an, const Dot *d) {
    auto t = an.TypeOf(d->obj);
    while (t && t->kind == TY_REF && t->ref->sub) t = t->ref->sub;
    return t ? an.ast->LookupMethods(d->name, d->ns, t) : an.ast->LookupFunctions(d->name, d->ns);
}

// The field a struct literal's initializer names, in the literal's type as
// the first clone that resolved it has it.
inline const Field *FieldOfInit(const Analysis &an, const Occurrence &occ) {
    auto c = an.Find(occ.node, [](const Node *n) {
        auto x = Is<StructLit>(n);
        return x && (x->variant || (x->sinst && x->sinst->st));
    });
    if (!c) return nullptr;
    auto sl = Is<StructLit>(c);
    auto &fields = sl->variant ? sl->variant->fields : sl->sinst->st->fields;
    for (auto &f : fields) if (!f.ispad && f.name == occ.name) return &f;
    return nullptr;
}

// The variant a match pattern's name resolved to, where the occurrence is
// one.
inline const SVariant *PatternVariantOf(const Analysis &an, const Occurrence &occ) {
    if (occ.kind != Occurrence::IDENT) return nullptr;
    auto it = an.patternvariants.find(occ.node->Origin());
    return it == an.patternvariants.end() ? nullptr : it->second;
}

// The leaf of a spelling: what follows its last `::`.
inline string_view LeafOf(string_view spelled) {
    auto q = spelled.rfind("::");
    return q == string_view::npos ? spelled : spelled.substr(q + 2);
}

// The extent in a file of what runs from a start place to an end place, as
// offsets: -1 for a start not in the file, and the start for an end not
// known or not in it.
inline pair<int64_t, int64_t> ExtentIn(const FileIndex &f, Line start, Line end) {
    if (start.fileidx != f.fileidx || start.line <= 0) return { -1, -1 };
    auto s = f.text.OffsetOf(start.line, start.col);
    auto e = end.line > 0 && end.fileidx == f.fileidx ? f.text.OffsetOf(end.line, end.col) : s;
    return { s, max(s, e) };
}

// A node's extent in its file, from its anchor; -1 for none.
inline pair<int64_t, int64_t> NodeExtent(const FileIndex &f, const Node *n) {
    return n ? ExtentIn(f, n->line, n->end) : pair<int64_t, int64_t> { -1, -1 };
}

// A declaration node's extent, at the file's start where not known.
inline pair<size_t, size_t> ExtentOf(const FileIndex &f, const Node *n) {
    auto [start, end] = NodeExtent(f, n);
    return { (size_t)max<int64_t>(start, 0), (size_t)max<int64_t>(end, 0) };
}

// Whether a declaration's extent, its end known, holds an offset; -1 for
// none, else where it starts.
inline int64_t StartAround(const FileIndex &f, Line start, Line end, size_t offset) {
    auto [s, e] = ExtentIn(f, start, end);
    return end.line > 0 && s >= 0 && (size_t)s <= offset && offset <= (size_t)e ? s : -1;
}

// The functions whose declarations hold an offset in a file, innermost
// first.
inline vector<SFunction *> FunctionsAround(const Analysis &an, const FileIndex &f, size_t offset) {
    vector<pair<int64_t, SFunction *>> around;
    for (auto sf : an.ast->functions)
        if (auto s = StartAround(f, sf->line, sf->end, offset); s >= 0) around.push_back({ s, sf });
    sort(around.begin(), around.end(), [](auto &x, auto &y) { return x.first > y.first; });
    vector<SFunction *> fns;
    for (auto &a : around) fns.push_back(a.second);
    return fns;
}

// The type parameter a name at a place refers to: the innermost function
// declared around the place that has one so named, or an outer one of
// its, else the struct or enum declared there.
inline const GenericParam *GenericAt(const Analysis &an, int fileidx, size_t offset, string_view name) {
    auto &f = an.files[(size_t)fileidx];
    auto named = [&](const vector<GenericParam> &gs) -> const GenericParam * {
        for (auto &g : gs) if (g.name == name) return &g;
        return nullptr;
    };
    auto around = FunctionsAround(an, f, offset);
    for (const SFunction *sf = around.empty() ? nullptr : around[0]; sf; sf = sf->outer)
        if (auto g = named(sf->generics)) return g;
    for (auto st : an.ast->structs)
        if (StartAround(f, st->line, st->end, offset) >= 0) if (auto g = named(st->generics)) return g;
    for (auto en : an.ast->enums)
        if (StartAround(f, en->line, en->end, offset) >= 0) if (auto g = named(en->generics)) return g;
    return nullptr;
}

// --- positions in results ---------------------------------------------------

inline void WritePosition(JsonOut &j, const TextIndex &t, size_t offset) {
    auto [line, character] = t.PositionOf(offset);
    j.BeginObject();
    j.Member("line", (int64_t)line);
    j.Member("character", (int64_t)character);
    j.EndObject();
}

inline void WriteRange(JsonOut &j, const TextIndex &t, size_t start, size_t end) {
    j.BeginObject();
    j.Key("start");
    WritePosition(j, t, start);
    j.Key("end");
    WritePosition(j, t, min(end, t.text.size()));
    j.EndObject();
}

inline void WriteLocation(JsonOut &j, const FileIndex &f, size_t start, size_t end) {
    j.BeginObject();
    j.Member("uri", f.uri);
    j.Key("range");
    WriteRange(j, f.text, start, end);
    j.EndObject();
}

// A declaration's name, as a place: the view the parser kept, else the
// declaration's own place.
inline Place PlaceOfLine(const Analysis &an, Line l, size_t len) {
    Place p;
    if (l.line <= 0 || l.fileidx < 0 || l.fileidx >= (int)an.files.size()) return p;
    auto off = an.files[(size_t)l.fileidx].text.OffsetOf(l.line, l.col);
    if (off < 0) return p;
    p.fileidx = l.fileidx;
    p.start = (size_t)off;
    p.len = len ? len : max<size_t>(an.files[(size_t)l.fileidx].text.NameLengthAt(p.start), 1);
    return p;
}

inline Place PlaceOfName(const Analysis &an, int fileidx, string_view name, Line fallback) {
    if (fileidx >= 0 && fileidx < (int)an.files.size()) {
        auto off = an.OffsetOfView(fileidx, name);
        if (off >= 0) return Place { fileidx, (size_t)off, name.size() };
    }
    return PlaceOfLine(an, fallback, 0);
}

inline Place PlaceOfFn(const Analysis &an, const SFunction *sf) {
    return PlaceOfName(an, sf->line.fileidx, sf->name, sf->line);
}
// A variable's declaration: its parameter, or the name in its `let`,
// `var`, loop, arm or block (Analysis::defplaces); unknown for a binding
// the source does not spell.
inline Place PlaceOfVarDef(const Analysis &an, const VarDef *vd) {
    if (auto it = an.defplaces.find(vd); it != an.defplaces.end()) return it->second;
    if (vd->isparam) return PlaceOfLine(an, vd->line, vd->name.size());
    return {};
}

inline void WritePlace(JsonOut &j, const Analysis &an, const Place &p) {
    auto &f = an.files[(size_t)p.fileidx];
    WriteLocation(j, f, p.start, p.start + p.len);
}

// --- hover ---------------------------------------------------------------------

struct HoverText {
    string header;      // Goose, in a code block.
    string doc;         // Markdown after it.
    string extra;       // More Goose, in a second code block.
};

// Says where the types a hover shows come from an exemplar check's clone
// (Analysis::exemplarclones) rather than from a call's.
inline void NoteExemplar(HoverText &h, const Analysis &an, const Node *clone) {
    if (!clone || !an.FromExemplar(clone)) return;
    Append(h.doc, h.doc.empty() ? "" : "\n\n",
           "In an exemplar instantiation: nothing instantiates this function, so each type "
           "parameter stands for an opaque type of its own.");
}

// What hover says of a node no check annotated.
inline string NotChecked(const Analysis &an, const Node *n) {
    return an.InExemplar(n) ? "Not checked: the exemplar instantiation ends before this code."
                            : "Not checked: no instantiation reaches this code.";
}

// A function: its header, its documentation and its specializations.
inline HoverText FnHover(const SFunction *sf) {
    return { FnHeader(sf), DocText(sf->doc), SpecsText(sf, 4) };
}

// The functions a name could call where no check resolved it: every
// header, and the first one's documentation.
inline HoverText OverloadsHover(const vector<SFunction *> &fns) {
    HoverText h;
    for (auto sf : fns) Append(h.header, h.header.empty() ? "" : "\n", FnHeader(sf));
    h.doc = DocText(fns[0]->doc);
    return h;
}

inline HoverText HoverIdent(const Located &loc, const Occurrence &occ) {
    HoverText h;
    auto &an = *loc.an;
    auto &ast = *an.ast;
    auto id = Is<Ident>(occ.node);
    if (auto v = PatternVariantOf(an, occ)) {
        h.header = VariantHeader(EnumOfVariant(ast, v), v);
        h.doc = DocText(v->doc);
        return h;
    }
    if (auto ci = ResolvedIdent(an, occ)) {
        if (ci->vdef) {
            h.header = VarDefHeader(ci->vdef);
            if (auto vd = an.VarDeclOf(ci->vdef)) h.doc = DocText(vd->doc);
            auto types = an.TypesOf(occ.node, 4);
            if (types.size() > 1) {
                h.extra = "// in other instantiations:";
                for (auto t : types) Append(h.extra, "\n", ci->vdef->name, ": ", TypeText(t));
            }
            NoteExemplar(h, an, ci);
        } else {
            h.header = FnHeader(ci->fnref);
            h.doc = DocText(ci->fnref->doc);
        }
        return h;
    }
    if (occ.callee) {
        auto call = ResolvedCall(loc, occ);
        if (auto sf = CalleeOf(call)) return FnHover(sf);
        if (call && call->builtin >= 0) {
            h.header = cat("builtin ", BuiltinCallText(builtindefs[call->builtin]));
            if (call->exprtype && call->exprtype->kind != TY_VOID)
                Append(h.header, " -> ", TypeText(call->exprtype));
            return h;
        }
        if (call && call->fvbody) {
            h.header = cat("function value ", id->name);
            return h;
        }
    }
    if (auto en = ast.LookupEnum(id->name, id->ns)) {
        h.header = DeclDetail(en);
        h.doc = DocText(en->doc);
        return h;
    }
    if (auto st = ast.LookupStruct(id->name, id->ns)) {
        h.header = DeclDetail(st);
        h.doc = DocText(st->doc);
        return h;
    }
    auto &fns = ast.LookupFunctions(id->name, id->ns);
    if (!fns.empty()) return OverloadsHover(fns);
    if (auto b = LookupBuiltin(GlobalLeaf(id->name))) {
        h.header = cat("builtin ", b->name);
        return h;
    }
    h.header = string(id->name);
    h.doc = NotChecked(an, occ.node);
    return h;
}

inline HoverText HoverDot(const Located &loc, const Occurrence &occ) {
    HoverText h;
    auto &an = *loc.an;
    auto &ast = *an.ast;
    auto d = Is<Dot>(occ.node);
    if (auto cd = ResolvedDot(an, occ)) {
        if (cd->variantconst) {
            h.header = VariantHeader(EnumOfVariant(ast, cd->variantconst), cd->variantconst);
            h.doc = DocText(cd->variantconst->doc);
        } else if (cd->member >= 0) {
            h.header = cat("builtin .", builtindefs[cd->member].name);
            if (cd->exprtype && cd->exprtype->kind != TY_VOID) Append(h.header, ": ", TypeText(cd->exprtype));
        } else {
            auto f = FieldOfDot(cd);
            h.header = cat(f && f->isconst ? "let " : "", cd->name, ": ", TypeText(cd->exprtype));
            if (f) h.doc = DocText(f->doc);
        }
        NoteExemplar(h, an, cd);
        return h;
    }
    if (occ.callee) {
        auto call = ResolvedCall(loc, occ);
        if (auto sf = CalleeOf(call)) return FnHover(sf);
        if (call && call->builtin >= 0) {
            h.header = cat("builtin .", builtindefs[call->builtin].name);
            if (call->exprtype && call->exprtype->kind != TY_VOID)
                Append(h.header, " -> ", TypeText(call->exprtype));
            return h;
        }
    }
    auto &fns = MethodsOf(an, d);
    if (occ.callee && !fns.empty()) return OverloadsHover(fns);
    h.header = cat(".", d->name);
    h.doc = NotChecked(an, occ.node);
    return h;
}

// A type name: its declaration, and the type as written where that
// says more (a generic's arguments).
inline HoverText HoverType(const Located &loc, const Occurrence &occ) {
    HoverText h;
    auto t = occ.type;
    auto written = TypeText(t);
    switch (t->kind) {
        case TY_STRUCT:
            // What resolution gives a name that names nothing.
            if (t->struc->st->unknown) {
                h.header = cat("unknown type ", written);
                break;
            }
            h.header = DeclDetail(t->struc->st);
            h.doc = DocText(t->struc->st->doc);
            if (!t->struc->st->generics.empty()) h.extra = written;
            break;
        case TY_ENUM:
            h.header = DeclDetail(t->enu->en);
            h.doc = DocText(t->enu->en->doc);
            if (!t->enu->en->generics.empty()) h.extra = written;
            break;
        case TY_VARIANT:
            if (t->var->variant) {
                h.header = VariantHeader(EnumOfVariant(*loc.an->ast, t->var->variant), t->var->variant);
                h.doc = DocText(t->var->variant->doc);
            } else {
                h.header = written;
            }
            break;
        case TY_GENERIC:
            h.header = cat("type parameter ", written);
            break;
        default:
            h.header = written;
            break;
    }
    return h;
}

inline HoverText HoverOf(const Located &loc, const Occurrence &occ) {
    HoverText h;
    auto &an = *loc.an;
    switch (occ.kind) {
        case Occurrence::FN:
            h = FnHover(occ.sf);
            break;
        case Occurrence::STRUCT:
            h.header = DeclDetail(occ.st);
            h.doc = DocText(occ.st->doc);
            break;
        case Occurrence::ENUM:
            h.header = DeclDetail(occ.en);
            h.doc = DocText(occ.en->doc);
            break;
        case Occurrence::ALIAS:
            h.header = DeclDetail(occ.al);
            h.doc = DocText(occ.al->doc);
            break;
        case Occurrence::GLOBAL: case Occurrence::LOCAL: {
            auto vd = Is<VarDecl>(occ.node);
            auto i = (size_t)occ.index;
            // A global is checked as written; a local in the body's clones.
            auto c = an.Find(occ.node, [i](const Node *n) {
                auto x = Is<VarDecl>(n);
                return x && x->defs.size() > i && x->defs[i] && x->defs[i]->type;
            });
            auto def = c ? Is<VarDecl>(c)->defs[i] : nullptr;
            h.header = cat(VarKeyword(vd), vd->names[i], ": ",
                           def ? TypeText(def->type) : vd->type ? TypeText(vd->type) : string("?"));
            h.doc = DocText(vd->doc);
            if (c) NoteExemplar(h, an, c);
            else if (!vd->isglobal) Append(h.doc, h.doc.empty() ? "" : "\n\n", NotChecked(an, occ.node));
            break;
        }
        case Occurrence::FIELD:
            h.header = cat(occ.field->isconst ? "let " : "", occ.field->name, ": ", TypeText(occ.field->type));
            h.doc = DocText(occ.field->doc);
            break;
        case Occurrence::VARIANT:
            h.header = VariantHeader(occ.en, occ.variant);
            h.doc = DocText(occ.variant->doc);
            break;
        case Occurrence::PARAM: {
            auto &p = *occ.param;
            h.header = cat("parameter ", p.isvar ? "var " : "", p.name);
            if (p.type) {
                Append(h.header, ": ", TypeText(p.type));
            } else {
                // Untyped: what the specializations gave it.
                auto idx = (size_t)(occ.param - occ.sf->params.data());
                set<string> seen;
                for (auto sp : occ.sf->specs) {
                    if (sp->isfunval || idx >= sp->argtypes.size()) continue;
                    auto s = TypeText(sp->argtypes[idx]);
                    if (seen.insert(s).second && seen.size() <= 4) Append(h.extra, h.extra.empty() ? "" : "\n", p.name, ": ", s);
                }
                for (auto sp : an.ast->exemplarspecs) {
                    if (!seen.empty() || sp->sf != occ.sf || sp->exemplar != occ.sf || idx >= sp->argtypes.size()) continue;
                    h.extra = cat(p.name, ": ", TypeText(sp->argtypes[idx]));
                    if (sp->body) NoteExemplar(h, an, sp->body);
                    seen.insert(h.extra);
                }
                if (seen.empty()) h.doc = "Not checked: no instantiation reaches this code.";
            }
            break;
        }
        case Occurrence::GENERIC:
            h.header = cat("type parameter ", occ.generic->name);
            if (occ.generic->bound) Append(h.header, ": ", TypeText(occ.generic->bound));
            break;
        case Occurrence::BINDER: {
            h.header = string(occ.name);
            TypeExpr *type = nullptr;
            const Node *found = nullptr;
            if (Is<ForLoop>(occ.node)) {
                auto c = an.Find(occ.node, [&](const Node *n) {
                    auto x = Is<ForLoop>(n);
                    return x && (occ.index ? x->idxdef : x->vdef);
                });
                if (c) type = occ.index ? Is<ForLoop>(c)->idxdef->type : Is<ForLoop>(c)->vdef->type;
                found = c;
            } else if (Is<MatchExpr>(occ.node)) {
                auto c = an.Find(occ.node, [&](const Node *n) {
                    auto x = Is<MatchExpr>(n);
                    return x && (size_t)occ.index < x->arms.size() && x->arms[(size_t)occ.index].binder;
                });
                if (c) type = Is<MatchExpr>(c)->arms[(size_t)occ.index].binder->type;
                found = c;
            } else if (Is<ConstFor>(occ.node)) {
                // An arm the const for generates for each entry: the types
                // the clones' arms bind here.
                set<string> types;
                for (auto &[vd, p] : an.defplaces)
                    if (p.fileidx == loc.fi->fileidx && p.start == occ.start && vd->type)
                        types.insert(TypeText(vd->type));
                for (auto &t : types)
                    Append(h.extra, h.extra.empty() ? "// in the arms generated:" : "", "\n", occ.name, ": ", t);
            }
            if (type) Append(h.header, ": ", TypeText(type));
            NoteExemplar(h, an, found);
            break;
        }
        case Occurrence::INIT:
            if (auto f = FieldOfInit(an, occ)) {
                h.header = cat(f->isconst ? "let " : "", f->name, ": ", TypeText(f->type));
                h.doc = DocText(f->doc);
            }
            break;
        case Occurrence::IDENT: return HoverIdent(loc, occ);
        case Occurrence::DOT:   return HoverDot(loc, occ);
        case Occurrence::TYPE:  return HoverType(loc, occ);
    }
    return h;
}

inline Place DefinitionOf(const Located &loc, const Occurrence &occ);

inline bool Hover(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return false;
    auto h = HoverOf(loc, *occ);
    if (h.header.empty()) return false;
    auto value = cat("```goose\n", h.header, "\n```");
    if (!h.extra.empty()) Append(value, "\n```goose\n", h.extra, "\n```");
    if (!h.doc.empty()) Append(value, "\n\n", h.doc);
    auto target = DefinitionOf(loc, *occ);
    if (target.fileidx >= 0) {
        auto &file = loc.an->files[(size_t)target.fileidx];
        Append(value, "\n\n[Source documentation](", file.uri, ")");
    }
    j.BeginObject();
    j.Key("contents");
    j.BeginObject();
    j.Member("kind", "markdown");
    j.Member("value", value);
    j.EndObject();
    j.Key("range");
    WriteRange(j, loc.fi->text, occ->start, occ->start + occ->len);
    j.EndObject();
    return true;
}

// --- go to definition ------------------------------------------------------------

inline Place DefinitionOf(const Located &loc, const Occurrence &occ) {
    auto &an = *loc.an;
    auto &ast = *an.ast;
    switch (occ.kind) {
        case Occurrence::FN:      return PlaceOfFn(an, occ.sf);
        case Occurrence::STRUCT:  return PlaceOfName(an, occ.st->line.fileidx, occ.st->name, occ.st->line);
        case Occurrence::ENUM:    return PlaceOfName(an, occ.en->line.fileidx, occ.en->name, occ.en->line);
        case Occurrence::ALIAS:   return PlaceOfName(an, occ.al->line.fileidx, occ.al->name, occ.al->line);
        case Occurrence::FIELD:   return PlaceOfLine(an, occ.field->line, occ.field->name.size());
        case Occurrence::VARIANT: return PlaceOfLine(an, occ.variant->line, occ.variant->name.size());
        case Occurrence::PARAM:   return PlaceOfLine(an, occ.param->line, occ.param->name.size());
        case Occurrence::GENERIC: return PlaceOfLine(an, occ.generic->line, occ.generic->name.size());
        case Occurrence::GLOBAL: case Occurrence::LOCAL: case Occurrence::BINDER:
            return Place { loc.fi->fileidx, occ.start, occ.len };
        case Occurrence::INIT:
            if (auto f = FieldOfInit(an, occ)) return PlaceOfLine(an, f->line, f->name.size());
            return {};
        case Occurrence::IDENT: {
            auto id = Is<Ident>(occ.node);
            if (auto v = PatternVariantOf(an, occ)) return PlaceOfLine(an, v->line, v->name.size());
            if (auto ci = ResolvedIdent(an, occ))
                return ci->vdef ? PlaceOfVarDef(an, ci->vdef) : PlaceOfFn(an, ci->fnref);
            if (occ.callee)
                if (auto sf = CalleeOf(ResolvedCall(loc, occ))) return PlaceOfFn(an, sf);
            if (auto en = ast.LookupEnum(id->name, id->ns))
                return PlaceOfName(an, en->line.fileidx, en->name, en->line);
            if (auto st = ast.LookupStruct(id->name, id->ns))
                return PlaceOfName(an, st->line.fileidx, st->name, st->line);
            auto &fns = ast.LookupFunctions(id->name, id->ns);
            if (!fns.empty()) return PlaceOfFn(an, fns[0]);
            return {};
        }
        case Occurrence::DOT: {
            auto d = Is<Dot>(occ.node);
            auto c = an.Find(occ.node, [](const Node *n) {
                auto x = Is<Dot>(n);
                return x && (x->fieldidx >= 0 || x->variantconst);
            });
            if (c) {
                auto cd = Is<Dot>(c);
                if (cd->variantconst) return PlaceOfLine(an, cd->variantconst->line, cd->variantconst->name.size());
                if (auto f = FieldOfDot(cd)) return PlaceOfLine(an, f->line, f->name.size());
            }
            if (occ.callee)
                if (auto sf = CalleeOf(ResolvedCall(loc, occ))) return PlaceOfFn(an, sf);
            auto &fns = MethodsOf(an, d);
            if (occ.callee && !fns.empty()) return PlaceOfFn(an, fns[0]);
            return {};
        }
        case Occurrence::TYPE: {
            auto t = occ.type;
            switch (t->kind) {
                case TY_STRUCT:
                    if (t->struc->st->unknown) return {};
                    return PlaceOfName(an, t->struc->st->line.fileidx, t->struc->st->name, t->struc->st->line);
                case TY_ENUM:    return PlaceOfName(an, t->enu->en->line.fileidx, t->enu->en->name, t->enu->en->line);
                case TY_VARIANT: return t->var->variant ? PlaceOfLine(an, t->var->variant->line, t->var->variant->name.size()) : Place {};
                case TY_GENERIC:
                    if (auto g = GenericAt(an, loc.fi->fileidx, occ.start, t->named->name))
                        return PlaceOfLine(an, g->line, g->name.size());
                    return {};
                default:         return {};
            }
        }
    }
    return {};
}

// Source positions unify the checked specializations of one construct.
inline vector<Place> LoopKeywords(const Located &loc, Place *definition) {
    auto &f = *loc.fi;
    vector<pair<Place, Place>> sites;
    auto place = [&](const Node *n, string_view word) -> Place {
        if (!n || n->line.fileidx != f.fileidx) return {};
        auto a = f.text.OffsetOf(n->line.line, n->line.col);
        if (f.text.text.substr(a, word.size()) != word) return {};
        return {f.fileidx, (size_t)a, word.size()};
    };
    auto word = [](const Node *n) -> string_view {
        if (Is<ForLoop>(n)) return "for";
        if (Is<While>(n)) return "while";
        if (Is<LoopExpr>(n)) return "loop";
        if (Is<EarlyBlock>(n)) return "block";
        return {};
    };
    function<void(const Node *)> walk = [&](const Node *n) {
        if (!n) return;
        const Node *target = nullptr;
        auto w = word(n);
        if (!w.empty()) target = n;
        if (auto b = Is<Break>(n)) { target = b->controltarget; w = "break"; }
        if (auto c = Is<Continue>(n)) { target = c->controltarget; w = "continue"; }
        if (target) {
            auto a = place(n, w), b = place(target, word(target));
            if (a.Known() && b.Known()) sites.push_back({a, b});
        }
        if (auto fd = Is<FnDecl>(n)) walk(fd->sf->body);
        else n->Children([&](Node *ch) { walk(ch); });
    };
    for (auto d : loc.an->ast->topdecls) walk(d);
    for (auto sp : loc.an->ast->fnspecs) walk(sp->body);
    for (auto sp : loc.an->ast->exemplarspecs) walk(sp->body);
    Place target;
    for (auto &[a, b] : sites)
        if (a.start <= loc.offset && loc.offset < a.start + a.len) target = b;
    vector<Place> result;
    set<size_t> seen;
    if (target.Known()) {
        if (definition) *definition = target;
        for (auto &[a, b] : sites)
            if (b.start == target.start && seen.insert(a.start).second) result.push_back(a);
    }
    sort(result.begin(), result.end(), [](const Place &a, const Place &b) { return a.start < b.start; });
    return result;
}

// Return targets come from typecheck, including nonlocal returns.
inline vector<Place> ControlKeywords(const Located &loc, Place *definition = nullptr) {
    auto loops = LoopKeywords(loc, definition);
    if (!loops.empty()) return loops;
    vector<pair<Place, const SFunction *>> sites;
    auto &f = *loc.fi;
    function<void(const Node *)> walk = [&](const Node *n) {
        if (!n) return;
        const SFunction *target = nullptr;
        string_view word;
        if (auto fd = Is<FnDecl>(n)) { target = fd->sf; word = "fn"; }
        if (auto ret = Is<Return>(n)) { target = ret->target; word = "return"; }
        if (target && n->line.fileidx == f.fileidx) {
            auto start = f.text.OffsetOf(n->line.line, n->line.col);
            if (f.text.text.substr(start, word.size()) == word)
                sites.push_back({ { f.fileidx, (size_t)start, word.size() }, target });
        }
        if (auto fd = Is<FnDecl>(n)) walk(fd->sf->body);
        else n->Children([&](Node *ch) { walk(ch); });
    };
    for (auto d : loc.an->ast->topdecls) walk(d);
    for (auto sp : loc.an->ast->fnspecs) walk(sp->body);
    for (auto sp : loc.an->ast->exemplarspecs) walk(sp->body);
    const SFunction *target = nullptr;
    for (auto &[p, sf] : sites)
        if (p.start <= loc.offset && loc.offset < p.start + p.len) target = sf;
    if (definition && target) *definition = PlaceOfFn(*loc.an, target);
    vector<Place> result;
    set<size_t> seen;
    if (target) for (auto &[p, sf] : sites)
        if (sf == target && seen.insert(p.start).second) result.push_back(p);
    sort(result.begin(), result.end(), [](const Place &a, const Place &b) { return a.start < b.start; });
    return result;
}

inline bool Definition(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto &an = *loc.an;
    Place keywordDefinition;
    auto keywords = ControlKeywords(loc, &keywordDefinition);
    if (!keywords.empty() && keywordDefinition.Known()) {
        WritePlace(j, an, keywordDefinition);
        return true;
    }
    if (auto occ = loc.fi->At(loc.offset)) {
        auto p = DefinitionOf(loc, *occ);
        if (!p.Known()) return false;
        WritePlace(j, an, p);
        return true;
    }
    // On an import: the file it imports.
    for (auto &imp : an.ast->imports) {
        auto [start, end] = ExtentIn(*loc.fi, imp.line, imp.end);
        if (start < 0 || (int64_t)loc.offset < start || (int64_t)loc.offset > end) continue;
        auto f = an.FileOf(PathKey(imp.path));
        j.BeginObject();
        j.Member("uri", f >= 0 ? an.files[(size_t)f].uri : UriOfPath(CanonicalPath(imp.path)));
        j.Key("range");
        j.Raw("{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}}");
        j.EndObject();
        return true;
    }
    return false;
}

// --- symbols ------------------------------------------------------------------------

// LSP SymbolKind.
enum SymbolKind {
    SK_NAMESPACE = 3, SK_CLASS = 5, SK_FIELD = 8, SK_ENUM = 10, SK_FUNCTION = 12, SK_VARIABLE = 13,
    SK_CONSTANT = 14, SK_STRUCT = 23, SK_ENUMMEMBER = 22, SK_TYPEPARAMETER = 26
};

inline void WriteDocumentSymbol(JsonOut &j, const FileIndex &f, string_view name, int kind,
                                pair<size_t, size_t> extent, Place sel,
                                const function<void()> &children = {}) {
    j.BeginObject();
    j.Member("name", name);
    j.Member("kind", (int64_t)kind);
    j.Key("range");
    WriteRange(j, f.text, extent.first, extent.second);
    j.Key("selectionRange");
    if (sel.Known() && sel.fileidx == f.fileidx) WriteRange(j, f.text, sel.start, sel.start + sel.len);
    else WriteRange(j, f.text, extent.first, extent.first);
    if (children) {
        j.Key("children");
        j.BeginArray();
        children();
        j.EndArray();
    }
    j.EndObject();
}

// A function's symbol, with the functions declared in its body as children.
inline void WriteFnSymbol(JsonOut &j, const Analysis &an, const FileIndex &f, const FnDecl *fd) {
    auto sf = fd->sf;
    WriteDocumentSymbol(j, f, sf->name, SK_FUNCTION, ExtentOf(f, fd), PlaceOfFn(an, sf), [&] {
        function<void(const Node *)> nested = [&](const Node *n) {
            if (!n) return;
            if (auto inner = Is<FnDecl>(n)) {
                WriteFnSymbol(j, an, f, inner);
                return;
            }
            n->Children([&](Node *ch) { nested(ch); });
        };
        if (sf->body) nested(sf->body);
    });
}

inline bool DocumentSymbols(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return false;
    auto &an = *loc.an;
    auto &f = *loc.fi;
    j.BeginArray();
    for (auto d : an.ast->topdecls) {
        if (d->line.fileidx != f.fileidx) continue;
        if (auto fd = Is<FnDecl>(d)) {
            WriteFnSymbol(j, an, f, fd);
        } else if (auto sd = Is<StructDecl>(d)) {
            auto st = sd->st;
            WriteDocumentSymbol(j, f, st->name, SK_STRUCT, ExtentOf(f, d),
                                PlaceOfName(an, st->line.fileidx, st->name, st->line), [&] {
                for (auto &fld : st->fields) {
                    if (fld.ispad) continue;
                    auto p = PlaceOfLine(an, fld.line, fld.name.size());
                    WriteDocumentSymbol(j, f, fld.name, SK_FIELD, { p.start, p.start + p.len }, p);
                }
            });
        } else if (auto ed = Is<EnumDecl>(d)) {
            auto en = ed->en;
            WriteDocumentSymbol(j, f, en->name, SK_ENUM, ExtentOf(f, d),
                                PlaceOfName(an, en->line.fileidx, en->name, en->line), [&] {
                for (auto &v : en->variants) {
                    auto p = PlaceOfLine(an, v.line, v.name.size());
                    WriteDocumentSymbol(j, f, v.name, SK_ENUMMEMBER, { p.start, p.start + p.len }, p);
                }
            });
        } else if (auto ad = Is<AliasDecl>(d)) {
            auto al = ad->al;
            WriteDocumentSymbol(j, f, al->name, SK_TYPEPARAMETER, ExtentOf(f, d),
                                PlaceOfName(an, al->line.fileidx, al->name, al->line));
        } else if (auto vd = Is<VarDecl>(d)) {
            for (auto &name : vd->names)
                WriteDocumentSymbol(j, f, name, vd->isvar ? SK_VARIABLE : SK_CONSTANT, ExtentOf(f, d),
                                    PlaceOfName(an, d->line.fileidx, name, d->line));
        }
    }
    j.EndArray();
    return true;
}

inline bool WorkspaceSymbols(Workspace &ws, const Json &params, JsonOut &j) {
    auto query = JsonStr(params.Get("query"));
    for (auto &c : query) c = (char)tolower((uint8_t)c);
    auto matches = [&](string_view name) {
        if (query.empty()) return true;
        string lower(name);
        for (auto &c : lower) c = (char)tolower((uint8_t)c);
        size_t at = 0;
        for (auto c : lower) if (at < query.size() && c == query[at]) at++;
        return at == query.size();
    };
    set<pair<string, size_t>> seen;
    size_t count = 0;
    j.BeginArray();
    auto write = [&](const Analysis &an, string_view name, string_view ns, int kind, Place p) {
        if (!p.Known() || !matches(name) || count >= 1000) return;
        auto &f = an.files[(size_t)p.fileidx];
        if (!seen.insert({ f.uri, p.start }).second) return;
        count++;
        j.BeginObject();
        j.Member("name", name);
        j.Member("kind", (int64_t)kind);
        if (!ns.empty()) j.Member("containerName", ns);
        j.Key("location");
        WriteLocation(j, f, p.start, p.start + p.len);
        j.EndObject();
    };
    for (auto &r : ws.roots) {
        if (!r->analysis) continue;
        auto &an = *r->analysis;
        auto &ast = *an.ast;
        for (auto sf : ast.functions)
            if (!sf->isnested) write(an, sf->name, sf->ns, SK_FUNCTION, PlaceOfFn(an, sf));
        for (auto st : ast.structs)
            write(an, st->name, st->ns, SK_STRUCT, PlaceOfName(an, st->line.fileidx, st->name, st->line));
        for (auto en : ast.enums)
            write(an, en->name, en->ns, SK_ENUM, PlaceOfName(an, en->line.fileidx, en->name, en->line));
        for (auto al : ast.aliases)
            write(an, al->name, al->ns, SK_TYPEPARAMETER, PlaceOfName(an, al->line.fileidx, al->name, al->line));
        for (auto g : ast.globals)
            for (auto &name : g->names)
                write(an, name, g->ns, g->isvar ? SK_VARIABLE : SK_CONSTANT,
                      PlaceOfName(an, g->line.fileidx, name, g->line));
    }
    j.EndArray();
    return true;
}

// --- semantic tokens ------------------------------------------------------------------

// The legend the server declares: token types by index, modifiers by bit.
inline const char *const SEMANTIC_TYPES[] = {
    "namespace", "type", "struct", "enum", "enumMember", "typeParameter", "parameter", "variable",
    "property", "function", "method"
};
enum SemanticType {
    ST_NAMESPACE, ST_TYPE, ST_STRUCT, ST_ENUM, ST_ENUMMEMBER, ST_TYPEPARAMETER, ST_PARAMETER,
    ST_VARIABLE, ST_PROPERTY, ST_FUNCTION, ST_METHOD
};
inline const char *const SEMANTIC_MODIFIERS[] = { "declaration", "readonly", "defaultLibrary" };
enum SemanticModifier { SM_DECLARATION = 1, SM_READONLY = 2, SM_DEFAULTLIBRARY = 4 };

// The token an occurrence is, or -1 for none.
inline int SemanticTokenOf(const Located &loc, const Occurrence &occ, int &modifiers) {
    auto &an = *loc.an;
    auto &ast = *an.ast;
    modifiers = 0;
    auto readonly = [&](bool isvar) { return isvar ? 0 : SM_READONLY; };
    switch (occ.kind) {
        case Occurrence::FN:      modifiers = SM_DECLARATION; return ST_FUNCTION;
        case Occurrence::STRUCT:  modifiers = SM_DECLARATION; return ST_STRUCT;
        case Occurrence::ENUM:    modifiers = SM_DECLARATION; return ST_ENUM;
        case Occurrence::ALIAS:   modifiers = SM_DECLARATION; return ST_TYPE;
        case Occurrence::VARIANT: modifiers = SM_DECLARATION; return ST_ENUMMEMBER;
        case Occurrence::GENERIC: modifiers = SM_DECLARATION; return ST_TYPEPARAMETER;
        case Occurrence::FIELD:   modifiers = SM_DECLARATION | readonly(occ.isvar); return ST_PROPERTY;
        case Occurrence::PARAM:   modifiers = SM_DECLARATION | readonly(occ.isvar); return ST_PARAMETER;
        case Occurrence::GLOBAL: case Occurrence::LOCAL: case Occurrence::BINDER:
            modifiers = SM_DECLARATION | readonly(occ.isvar);
            return ST_VARIABLE;
        case Occurrence::TYPE:
            switch (occ.type->kind) {
                case TY_STRUCT:  return occ.type->struc->st->unknown ? -1 : ST_STRUCT;
                case TY_ENUM:    return ST_ENUM;
                case TY_VARIANT: return ST_ENUMMEMBER;
                case TY_GENERIC: return ST_TYPEPARAMETER;
                default:         return -1;
            }
        case Occurrence::INIT: return ST_PROPERTY;
        case Occurrence::IDENT: {
            auto id = Is<Ident>(occ.node);
            if (PatternVariantOf(an, occ)) return ST_ENUMMEMBER;
            if (auto ci = ResolvedIdent(an, occ)) {
                if (ci->fnref) return ST_FUNCTION;
                modifiers = readonly(ci->vdef->isvar);
                return ci->vdef->isparam ? ST_PARAMETER : ST_VARIABLE;
            }
            if (occ.callee) {
                auto call = ResolvedCall(loc, occ);
                if (call && call->fvbody) return ST_VARIABLE;
                return ST_FUNCTION;
            }
            if (ast.LookupEnum(id->name, id->ns)) return ST_ENUM;
            if (ast.LookupStruct(id->name, id->ns)) return ST_STRUCT;
            return -1;
        }
        case Occurrence::DOT: {
            if (auto cd = ResolvedDot(an, occ)) {
                if (cd->variantconst) return ST_ENUMMEMBER;
                if (cd->member >= 0) return occ.callee ? ST_METHOD : ST_PROPERTY;
                return ST_PROPERTY;
            }
            return occ.callee ? ST_METHOD : ST_PROPERTY;
        }
    }
    return -1;
}

inline bool SemanticTokens(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return false;
    auto &f = *loc.fi;
    j.BeginObject();
    j.Key("data");
    j.BeginArray();
    int64_t lastline = 0, lastchar = 0;
    for (auto &occ : f.occs) {
        int modifiers;
        auto type = SemanticTokenOf(loc, occ, modifiers);
        if (type < 0) continue;
        auto [line, character] = f.text.PositionOf(occ.start);
        auto ltext = f.text.LineText(line);
        auto startbyte = occ.start - f.text.linestarts[(size_t)line];
        auto length = Utf16Col(ltext, startbyte + occ.len) - character;
        if (length <= 0) continue;
        j.Int(line - lastline);
        j.Int(line == lastline ? character - lastchar : character);
        j.Int(length);
        j.Int(type);
        j.Int(modifiers);
        lastline = line;
        lastchar = character;
    }
    j.EndArray();
    j.EndObject();
    return true;
}

// --- signature help --------------------------------------------------------------------

inline void WriteSignature(JsonOut &j, const SFunction *sf) {
    j.BeginObject();
    j.Member("label", FnHeader(sf));
    if (!sf->doc.empty()) {
        j.Key("documentation");
        j.BeginObject();
        j.Member("kind", "markdown");
        j.Member("value", DocText(sf->doc));
        j.EndObject();
    }
    j.Key("parameters");
    j.BeginArray();
    for (auto &p : sf->params) {
        j.BeginObject();
        auto label = string(p.name);
        if (p.type) Append(label, ": ", TypeText(p.type));
        j.Member("label", label);
        j.EndObject();
    }
    j.EndArray();
    j.EndObject();
}

inline bool SignatureHelp(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto &an = *loc.an;
    auto &ast = *an.ast;
    auto &text = loc.fi->text.text;
    // The innermost call around the position whose arguments it is in.
    const CallSpan *span = nullptr;
    size_t paren = 0;
    for (auto &c : loc.fi->calls) {
        if (c.start > loc.offset) break;
        if (loc.offset > c.end) continue;
        auto p = text.find('(', c.start);
        if (p == string_view::npos || p >= loc.offset || p > c.end) continue;
        if (!span || c.start >= span->start) {
            span = &c;
            paren = p;
        }
    }
    if (!span) return false;
    // The active parameter: the commas at depth 0 before the position,
    // those in literals and comments left out.
    auto active = 0;
    auto depth = 0;
    auto end = min(loc.offset, text.size());
    for (auto i = paren + 1; i < end; i++) {
        auto c = text[i];
        if (text.substr(i, 3) == "\"\"\"") {
            i = min(text.find("\"\"\"", i + 3), end) + 2;
        } else if (c == '"' || c == '\'') {
            for (i++; i < end && text[i] != c; i++) if (text[i] == '\\') i++;
        } else if (text.substr(i, 2) == "//") {
            while (i < end && text[i] != '\n') i++;
        } else if (text.substr(i, 2) == "/*") {
            // Block comments nest.
            for (auto nesting = 0; i < end; i++) {
                if (text.substr(i, 2) == "/*") {
                    nesting++;
                    i++;
                } else if (text.substr(i, 2) == "*/") {
                    i++;
                    if (!--nesting) break;
                }
            }
        } else if (c == '(' || c == '[' || c == '{') {
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            depth--;
        } else if (c == ',' && depth == 0) {
            active++;
        }
    }
    string_view name;
    const vector<SFunction *> *found;
    auto ufcs = false;
    if (auto id = Is<Ident>(span->call->callee)) {
        name = id->name;
        found = &ast.LookupFunctions(name, id->ns);
    } else if (auto d = Is<Dot>(span->call->callee)) {
        name = d->name;
        found = &MethodsOf(an, d);
        ufcs = true;
    } else {
        return false;
    }
    auto &fns = *found;
    auto b = fns.empty() ? LookupBuiltin(GlobalLeaf(name)) : nullptr;
    if (fns.empty() && !b) return false;
    auto activesig = 0;
    if (auto resolved = SpecializedCall(an, span->call))
        for (size_t i = 0; i < fns.size(); i++)
            if (fns[i] == resolved->spec->sf) activesig = (int)i;
    j.BeginObject();
    j.Key("signatures");
    j.BeginArray();
    for (auto sf : fns) WriteSignature(j, sf);
    if (b) {
        j.BeginObject();
        j.Member("label", BuiltinCallText(*b));
        j.Key("parameters");
        j.BeginArray();
        j.EndArray();
        j.EndObject();
    }
    j.EndArray();
    j.Member("activeSignature", (int64_t)activesig);
    j.Member("activeParameter", (int64_t)(active + (ufcs ? 1 : 0)));
    j.EndObject();
    return true;
}

// --- references and rename -----------------------------------------------------------

// Every occurrence that resolves to the declaration whose name is at
// `target` in the located analysis, the declaration's own among them, file
// by file, in every program reaching the declaration's file: the analysis
// of each root that has the file with the located analysis's text, the
// configured roots first. Another program may reach files the located one
// does not, and resolve a use in a file both reach that the located one
// does not (in a generic function only it instantiates). A file is read in
// the analyses that have the text the first one reaching it has, so that
// the places they find agree; a place two of them find is given twice.
inline void ReferencesTo(Workspace &ws, const Located &loc, const Place &target,
                         const function<void(const Analysis &, const FileIndex &, const Occurrence &)> &f) {
    auto key = PathKey(loc.an->ast->sources[(size_t)target.fileidx].first);
    auto text = loc.an->files[(size_t)target.fileidx].text.text;
    map<string, string_view> texts;     // By URI: the first analysis's text of the file.
    for (auto configured : { true, false })
        for (auto &r : ws.roots) {
            if (r->configured != configured || !r->analysis) continue;
            auto &an = *r->analysis;
            auto fileidx = an.FileOf(key);
            if (fileidx < 0 || an.files[(size_t)fileidx].text.text != text) continue;
            for (auto &fi : an.files) {
                auto [it, first] = texts.insert({ fi.uri, fi.text.text });
                if (!first && it->second != fi.text.text) continue;
                Located at { &an, &fi, 0 };
                for (auto &occ : fi.occs) {
                    auto p = DefinitionOf(at, occ);
                    if (p.Known() && p.fileidx == fileidx && p.start == target.start) f(an, fi, occ);
                }
            }
        }
}

inline bool References(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return false;
    auto target = DefinitionOf(loc, *occ);
    if (!target.Known()) return false;
    auto context = params.Get("context");
    auto withdecl = JsonBool(context ? context->Get("includeDeclaration") : nullptr);
    j.BeginArray();
    set<pair<string, size_t>> seen;     // By URI and start.
    ReferencesTo(ws, loc, target, [&](const Analysis &, const FileIndex &f, const Occurrence &o) {
        if ((withdecl || !o.IsDeclaration()) && seen.insert({ f.uri, o.start }).second)
            WriteLocation(j, f, o.start, o.start + o.len);
    });
    j.EndArray();
    return true;
}

inline bool IsKeyword(string_view s) {
    static const set<string_view> keywords = {
        #define F(t, str) str,
        TOKENS_KEYWORDS
        #undef F
    };
    return keywords.count(s) > 0;
}

inline bool IsIdentifier(string_view s) {
    if (s.empty() || !IsIdentStart(s[0])) return false;
    for (auto c : s) if (!IsIdentCont(c)) return false;
    return true;
}

// Whether an occurrence that resolves to no declaration names a builtin:
// a call of one, a builtin member, or a name that is nothing else.
inline bool IsBuiltinUse(const Located &loc, const Occurrence &occ) {
    if (occ.kind == Occurrence::IDENT) {
        if (occ.callee)
            if (auto call = ResolvedCall(loc, occ); call && call->builtin >= 0) return true;
        return LookupBuiltin(GlobalLeaf(Is<Ident>(occ.node)->name)) != nullptr;
    }
    if (occ.kind == Occurrence::DOT) {
        auto c = loc.an->Find(occ.node, [](const Node *n) {
            auto x = Is<Dot>(n);
            return x && x->member >= 0;
        });
        if (c) return true;
        if (occ.callee)
            if (auto call = ResolvedCall(loc, occ); call && call->builtin >= 0) return true;
    }
    return false;
}

// The identifier around an offset, for a position on a keyword.
inline string_view WordAt(const TextIndex &t, size_t offset) {
    auto text = t.text;
    auto s = min(offset, text.size());
    while (s > 0 && IsIdentCont(text[s - 1])) s--;
    auto e = s;
    while (e < text.size() && IsIdentCont(text[e])) e++;
    return text.substr(s, e - s);
}

// The edits renaming what is at the position, as a WorkspaceEdit: the leaf
// of each spelling (`ns::name` keeps its qualifier) and the declaration's
// name. False with the reason where the name cannot be renamed: a keyword,
// a builtin, a name the compiler synthesized, or one with no declaration.
using RenameEdits = map<string, pair<const FileIndex *, set<pair<size_t, size_t>>>>;

// Recheck all names: capture can affect names outside the edit set.
inline bool RenamePreservesBindings(Workspace &ws, const RenameEdits &edits, string_view name) {
    if (!ws.indexComplete) return false;
    for (auto &root : ws.roots) {
        if (!root->analysis || !root->analysis->parsed) return false;
        for (auto &d : root->analysis->diags) if (!d.warning) return false;
    }
    map<string, string> texts;
    auto shifted = [&](const string &uri, size_t offset) {
        auto result = offset;
        if (auto it = edits.find(uri); it != edits.end())
            for (auto [start, end] : it->second.second) {
                if (start >= offset) break;
                result = result - min(offset - start, end - start) + name.size();
            }
        return result;
    };
    for (auto &[uri, edit] : edits) {
        auto current = ws.TextOf(PathOfUri(uri));
        if (current && *current != edit.first->text.text) return false;
        auto text = string(edit.first->text.text);
        for (auto it = edit.second.rbegin(); it != edit.second.rend(); ++it)
            text.replace(it->first, it->second - it->first, name);
        texts[PathKey(PathOfUri(uri))] = std::move(text);
    }
    for (auto &root : ws.roots) {
        if (!root->analysis) continue;
        auto &before = *root->analysis;
        for (auto &file : before.files) {
            auto path = PathOfUri(file.uri);
            auto current = ws.TextOf(path);
            string disk;
            if ((current && *current != file.text.text) ||
                (!current && (!LoadFile(path, disk) || disk != file.text.text))) return false;
        }
        bool affected = false;
        for (auto &[key, text] : texts) if (before.FileOf(key) >= 0) affected = true;
        if (!affected) continue;
        for (auto &[uri, edit] : edits) {
            auto idx = before.FileOf(PathKey(PathOfUri(uri)));
            if (idx >= 0 && before.files[(size_t)idx].text.text != edit.first->text.text) return false;
        }
        for (auto &d : before.diags) if (!d.warning) return false;
        Analysis after;
        after.ast = make_unique<Ast>();
        auto &ast = *after.ast;
        ast.exemplars = true;
        bool failed = false;
        ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
        SourceProvider sources = [&](const string &path) -> const string * {
            auto it = texts.find(PathKey(path));
            return it == texts.end() ? ws.TextOf(path) : &it->second;
        };
        try {
            RunOnCompilerStack([&]() -> int {
                ParseProgram(ast, root->path, ws.stdlibdirs, DirOf(ws.exe), sources);
                ResolveTypeNames(ast);
                after.IndexSource();
                TypeCheckProgram(ast, false, true);
                after.Build();
                return 0;
            });
        } catch (CompileError &) { return false; }
        if (failed || !ast.errors.empty() || before.files.size() != after.files.size()) return false;
        for (auto &file : before.files) {
            auto idx = after.FileOf(PathKey(PathOfUri(file.uri)));
            if (idx < 0) return false;
            auto &updated = after.files[(size_t)idx];
            auto expected = texts.find(PathKey(PathOfUri(file.uri)));
            if (expected == texts.end() && file.text.text != updated.text.text) return false;
            if (file.occs.size() != updated.occs.size()) return false;
            Located oldloc { &before, &file, 0 }, newloc { &after, &updated, 0 };
            for (auto &occ : file.occs) {
                auto next = updated.At(shifted(file.uri, occ.start));
                if (!next || next->kind != occ.kind) return false;
                auto olddef = DefinitionOf(oldloc, occ), newdef = DefinitionOf(newloc, *next);
                auto sameBinding = [&](const Place &oldplace, const Place &newplace) {
                    if (oldplace.Known() != newplace.Known()) return false;
                    if (!oldplace.Known()) return true;
                    auto &decl = before.files[(size_t)oldplace.fileidx];
                    auto &newdecl = after.files[(size_t)newplace.fileidx];
                    return decl.uri == newdecl.uri && shifted(decl.uri, oldplace.start) == newplace.start;
                };
                if (occ.kind == Occurrence::IDENT || occ.kind == Occurrence::DOT) {
                    auto oldclones = before.clones.find(occ.node);
                    auto newclones = after.clones.find(next->node);
                    auto oldcount = oldclones == before.clones.end() ? 0 : oldclones->second.size();
                    auto newcount = newclones == after.clones.end() ? 0 : newclones->second.size();
                    if (oldcount != newcount) return false;
                    for (size_t i = 0; i < oldcount; ++i) {
                        auto oldnode = oldclones->second[i], newnode = newclones->second[i];
                        bool oldname = occ.kind == Occurrence::IDENT ? Is<Ident>(oldnode) != nullptr : Is<Dot>(oldnode) != nullptr;
                        bool newname = occ.kind == Occurrence::IDENT ? Is<Ident>(newnode) != nullptr : Is<Dot>(newnode) != nullptr;
                        if (oldname != newname) return false;
                        if (!oldname) continue;
                        auto oldcopy = occ, newcopy = *next;
                        oldcopy.node = oldnode; newcopy.node = newnode;
                        if (!sameBinding(DefinitionOf(oldloc, oldcopy), DefinitionOf(newloc, newcopy))) return false;
                    }
                }
                if (olddef.Known() != newdef.Known()) return false;
                if (olddef.Known()) {
                    auto &decl = before.files[(size_t)olddef.fileidx];
                    auto &newdecl = after.files[(size_t)newdef.fileidx];
                    if (decl.uri != newdecl.uri || shifted(decl.uri, olddef.start) != newdef.start) return false;
                } else {
                    // Unresolved affected names cannot prove binding stability.
                    if (LeafOf(occ.name) == name || LeafOf(next->name) != LeafOf(occ.name)) return false;
                }
            }
        }
    }
    return true;
}


static bool FileExists(const string &path);

struct ImportMoveEdit {
    const FileIndex *file = nullptr;
    map<pair<size_t, size_t>, string> replacements;
};

inline bool WillRenameFiles(Workspace &ws, const Json &params, JsonOut &j, string &error) {
    ws.Settle();
    if (!ws.indexComplete) { error = "workspace index is incomplete"; return false; }
    auto files = params.Get("files");
    if (!files || files->kind != Json::ARRAY || files->arr.empty()) {
        error = "expected file rename operations"; return false;
    }
    map<string, string> moves;
    set<string> destinations;
    for (auto &file : files->arr) {
        auto oldpath = CanonicalPath(PathOfUri(JsonStr(file.Get("oldUri"))));
        auto newpath = CanonicalPath(PathOfUri(JsonStr(file.Get("newUri"))));
        if (!FileExists(oldpath) || FileExists(newpath) ||
            oldpath.size() < 6 || oldpath.substr(oldpath.size() - 6) != ".goose" ||
            newpath.size() < 6 || newpath.substr(newpath.size() - 6) != ".goose" ||
            !destinations.insert(PathKey(newpath)).second || moves.count(PathKey(oldpath))) {
            error = "file move collides or is not a Goose file"; return false;
        }
        moves[PathKey(oldpath)] = newpath;
    }
    auto moved = [&](const string &path) {
        auto it = moves.find(PathKey(path));
        return it == moves.end() ? path : it->second;
    };
    map<string, ImportMoveEdit> edits;
    map<string, string> texts;
    set<string> reached;
    for (auto &root : ws.roots) {
        if (!root->analysis) continue;
        auto &an = *root->analysis;
        for (auto &d : an.diags) if (!d.warning) {
            error = "file move requires an error-free workspace"; return false;
        }
        for (auto &file : an.files) {
            auto path = PathOfUri(file.uri), key = PathKey(path);
            reached.insert(key);
            auto current = ws.TextOf(path);
            string disk;
            if ((!current && (!LoadFile(path, disk) || disk != file.text.text)) ||
                (current && *current != file.text.text)) {
                error = "source changed since analysis"; return false;
            }
            auto [it, added] = texts.emplace(PathKey(moved(path)), string(file.text.text));
            if (!added && it->second != file.text.text) {
                error = "workspace analyses disagree"; return false;
            }
        }
        for (auto &imp : an.ast->imports) {
            auto &file = an.files[(size_t)imp.line.fileidx];
            auto path = PathOfUri(file.uri);
            if (moved(path) == path && moved(imp.path) == imp.path) continue;
            auto start = file.text.OffsetOf(imp.line.line, imp.line.col);
            auto end = file.text.OffsetOf(imp.end.line, imp.end.col);
            auto original = string(file.text.text.substr(start, end - start));
            Lexer lex(path, original.c_str());
            lex.Next();
            auto relative = lex.tok == T_DOT;
            if (relative) lex.Next();
            auto a = (size_t)(lex.attr.data() - original.data());
            size_t b = a;
            while (lex.tok == T_IDENT || lex.tok == T_DOT) {
                b = (size_t)(lex.p - original.data()); lex.Next();
            }
            if (lex.tok != T_SEMI) {
                error = "package source moves are unsupported"; return false;
            }
            auto pkg = an.ast->filepkg[(size_t)imp.line.fileidx];
            auto base = relative ? DirOf(moved(path)) : an.ast->packages[(size_t)pkg].root;
            base = CanonicalPath(base);
            if (!base.empty() && base.back() != '/') base += '/';
            auto target = moved(imp.path);
            if (target.rfind(base, 0) != 0) {
                error = "destination cannot be spelled by this import"; return false;
            }
            auto spelling = target.substr(base.size(), target.size() - base.size() - 6);
            size_t part = 0;
            for (size_t i = 0; i <= spelling.size(); i++) {
                if (i != spelling.size() && spelling[i] != '/') continue;
                if (!IsIdentifier(string_view(spelling).substr(part, i - part)) ||
                    IsKeyword(string_view(spelling).substr(part, i - part))) {
                    error = "destination is not a Goose import path"; return false;
                }
                if (i < spelling.size()) spelling[i] = '.';
                part = i + 1;
            }
            auto &edit = edits[file.uri]; edit.file = &file;
            auto [it, added] = edit.replacements.emplace(make_pair(start + a, start + b), spelling);
            if (!added && it->second != spelling) {
                error = "import resolves differently across roots"; return false;
            }
        }
    }
    for (auto &[key, path] : moves) if (!reached.count(key)) {
        error = "file is outside the analyzed workspace"; return false;
    }
    for (auto &[uri, edit] : edits) {
        auto &text = texts[PathKey(moved(PathOfUri(uri)))];
        for (auto it = edit.replacements.rbegin(); it != edit.replacements.rend(); ++it)
            text.replace(it->first.first, it->first.second - it->first.first, it->second);
    }
    for (auto &root : ws.roots) {
        if (!root->analysis) continue;
        auto &before = *root->analysis;
        Analysis after; after.ast = make_unique<Ast>();
        auto &ast = *after.ast; ast.exemplars = true;
        bool failed = false;
        ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
        SourceProvider source = [&](const string &path) -> const string * {
            auto it = texts.find(PathKey(path));
            return it == texts.end() ? ws.TextOf(path) : &it->second;
        };
        try {
            RunOnCompilerStack([&]() -> int {
                ParseProgram(ast, moved(root->path), ws.stdlibdirs, DirOf(ws.exe), source);
                ResolveTypeNames(ast); after.IndexSource();
                TypeCheckProgram(ast, false, true); after.Build(); return 0;
            });
        } catch (CompileError &) { failed = true; }
        if (failed || !ast.errors.empty() || before.files.size() != after.files.size() ||
            before.ast->imports.size() != ast.imports.size()) {
            error = "moved program cannot be proven equivalent"; return false;
        }
        for (size_t i = 0; i < ast.imports.size(); i++)
            if (PathKey(moved(before.ast->imports[i].path)) != PathKey(ast.imports[i].path)) {
                error = "move changes import resolution or initialization order"; return false;
            }
        for (auto &file : before.files) {
            auto idx = after.FileOf(PathKey(moved(PathOfUri(file.uri))));
            if (idx < 0 || file.occs.size() != after.files[(size_t)idx].occs.size()) {
                error = "move changes source identity"; return false;
            }
            auto &next = after.files[(size_t)idx];
            Located oldloc { &before, &file, 0 }, newloc { &after, &next, 0 };
            for (size_t i = 0; i < file.occs.size(); i++) {
                auto &a = file.occs[i]; auto &b = next.occs[i];
                auto x = DefinitionOf(oldloc, a), y = DefinitionOf(newloc, b);
                auto unique = [](const Located &loc, const Occurrence &occ, const Place &def) {
                    if (occ.kind != Occurrence::IDENT && occ.kind != Occurrence::DOT) return true;
                    auto it = loc.an->clones.find(occ.node);
                    if (it == loc.an->clones.end()) return true;
                    for (auto node : it->second) {
                        if ((occ.kind == Occurrence::IDENT && !Is<Ident>(node)) ||
                            (occ.kind == Occurrence::DOT && !Is<Dot>(node))) continue;
                        auto copy = occ; copy.node = node;
                        auto other = DefinitionOf(loc, copy);
                        if (def.Known() != other.Known() || (def.Known() &&
                            (def.fileidx != other.fileidx || def.start != other.start))) return false;
                    }
                    return true;
                };
                if (!unique(oldloc, a, x) || !unique(newloc, b, y)) {
                    error = "ambiguous compiler bindings"; return false;
                }
                if (a.kind != b.kind || a.name != b.name || x.Known() != y.Known()) {
                    error = "move changes name bindings"; return false;
                }
                if (x.Known()) {
                    auto &xf = before.files[(size_t)x.fileidx];
                    auto &yf = after.files[(size_t)y.fileidx];
                    auto xo = xf.At(x.start), yo = yf.At(y.start);
                    if (PathKey(moved(PathOfUri(xf.uri))) != PathKey(PathOfUri(yf.uri)) ||
                        !xo || !yo || xo - xf.occs.data() != yo - yf.occs.data()) {
                        error = "move changes declaration identity"; return false;
                    }
                }
            }
        }
    }
    j.BeginObject(); j.Key("documentChanges"); j.BeginArray();
    for (auto &[uri, edit] : edits) {
        j.BeginObject(); j.Key("textDocument"); j.BeginObject();
        j.Member("uri", uri); j.Key("version");
        auto it = ws.open.find(PathKey(PathOfUri(uri)));
        if (it == ws.open.end()) j.Null(); else j.Raw(to_string(it->second.version));
        j.EndObject(); j.Key("edits"); j.BeginArray();
        for (auto &[range, text] : edit.replacements) {
            j.BeginObject(); j.Key("range");
            WriteRange(j, edit.file->text, range.first, range.second);
            j.Member("newText", text); j.EndObject();
        }
        j.EndArray(); j.EndObject();
    }
    j.EndArray(); j.EndObject(); return true;
}

struct NamespaceSite {
    string name;
    size_t start = 0;
};

inline vector<NamespaceSite> NamespaceSites(const FileIndex &file) {
    vector<NamespaceSite> sites;
    auto text = string(file.text.text);
    Lexer lex(PathOfUri(file.uri), text.c_str());
    bool directive = false;
    while (lex.tok != T_EOF) {
        if (lex.tok == T_NAMESPACE) {
            directive = true;
            lex.Next();
            continue;
        }
        if (lex.tok == T_IDENT) {
            auto name = string(lex.attr);
            auto start = (size_t)(lex.attr.data() - text.data());
            lex.Next();
            if (directive || lex.tok == T_COLONCOLON)
                sites.push_back({ name, start });
        } else lex.Next();
        directive = false;
    }
    return sites;
}

inline optional<NamespaceSite> NamespaceAt(const Located &loc) {
    for (auto &site : NamespaceSites(*loc.fi))
        if (site.start <= loc.offset && loc.offset <= site.start + site.name.size() &&
            loc.an->ast->namespaces.count(site.name)) return site;
    return {};
}

inline void WriteVersionedRename(Workspace &ws, const RenameEdits &edits,
                                  string_view name, JsonOut &j) {
    j.BeginObject();
    j.Key("documentChanges"); j.BeginArray();
    for (auto &[uri, edit] : edits) {
        j.BeginObject(); j.Key("textDocument"); j.BeginObject();
        j.Member("uri", uri); j.Key("version");
        auto it = ws.open.find(PathKey(PathOfUri(uri)));
        if (it == ws.open.end()) j.Null(); else j.Raw(to_string(it->second.version));
        j.EndObject(); j.Key("edits"); j.BeginArray();
        for (auto [start, end] : edit.second) {
            j.BeginObject(); j.Key("range");
            WriteRange(j, edit.first->text, start, end);
            j.Member("newText", name); j.EndObject();
        }
        j.EndArray(); j.EndObject();
    }
    j.EndArray(); j.EndObject();
}

inline bool RenameNamespace(Workspace &ws, const NamespaceSite &site,
                            string_view name, JsonOut &j, string &error) {
    RenameEdits edits;
    for (auto &root : ws.roots) {
        if (!root->analysis) continue;
        auto &an = *root->analysis;
        if (!an.ast->namespaces.count(site.name)) continue;
        if (name != site.name && an.ast->namespaces.count(name)) {
            error = "namespace already exists";
            return false;
        }
        for (auto &file : an.files) {
            for (auto &candidate : NamespaceSites(file)) {
                if (candidate.name != site.name) continue;
                auto &[fi, places] = edits[file.uri];
                if (fi && fi->text.text != file.text.text) {
                    error = "namespace analyses disagree";
                    return false;
                }
                fi = &file;
                places.insert({ candidate.start, candidate.start + site.name.size() });
            }
        }
    }
    if (edits.empty() || !RenamePreservesBindings(ws, edits, name)) {
        error = "rename would change bindings or cannot be proven safe";
        return false;
    }
    WriteVersionedRename(ws, edits, name, j);
    return true;
}

inline bool Rename(Workspace &ws, const Json &params, JsonOut &j, string &error) {
    Located loc;
    if (!Locate(ws, params, loc)) {
        error = "the file has not been analyzed yet";
        return false;
    }
    auto newname = JsonStr(params.Get("newName"));
    if (!IsIdentifier(newname) || IsKeyword(newname)) {
        error = cat("'", newname, "' is not a name a declaration can have");
        return false;
    }
    if (auto ns = NamespaceAt(loc))
        return RenameNamespace(ws, *ns, newname, j, error);
    auto occ = loc.fi->At(loc.offset);
    if (!occ) {
        auto word = WordAt(loc.fi->text, loc.offset);
        error = IsKeyword(word) ? cat("'", word, "' is a keyword") : "there is no name to rename here";
        return false;
    }
    auto leaf = LeafOf(occ->name);
    auto target = DefinitionOf(loc, *occ);
    if (!target.Known()) {
        error = IsBuiltinUse(loc, *occ) ? cat("'", leaf, "' is a builtin")
                                        : cat("'", leaf, "' has no declaration to rename");
        return false;
    }
    // The declaration must spell the name; a binding the compiler
    // synthesized, such as a block's implicit `it`, has none to edit.
    auto &tf = loc.an->files[(size_t)target.fileidx];
    if (tf.text.text.substr(target.start, target.len) != leaf) {
        error = cat("'", leaf, "' is a name the compiler gives, not one declared");
        return false;
    }
    // By URI: the file as an analysis reaching it has it, and the places.
    RenameEdits edits;
    ReferencesTo(ws, loc, target, [&](const Analysis &, const FileIndex &f, const Occurrence &o) {
        auto &[file, places] = edits[f.uri];
        file = &f;
        places.insert({ o.start + (o.name.size() - LeafOf(o.name).size()), o.start + o.len });
    });
    if (!RenamePreservesBindings(ws, edits, newname)) {
        error = "rename would change bindings or cannot be proven safe";
        return false;
    }
    j.BeginObject();
    j.Key("changes");
    j.BeginObject();
    for (auto &[uri, edit] : edits) {
        auto &[file, places] = edit;
        j.Key(uri);
        j.BeginArray();
        for (auto &[start, end] : places) {
            j.BeginObject();
            j.Key("range");
            WriteRange(j, file->text, start, end);
            j.Member("newText", newname);
            j.EndObject();
        }
        j.EndArray();
    }
    j.EndObject();
    j.EndObject();
    return true;
}

inline bool PrepareRename(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    if (auto ns = NamespaceAt(loc)) {
        j.BeginObject(); j.Key("range");
        WriteRange(j, loc.fi->text, ns->start, ns->start + ns->name.size());
        j.Member("placeholder", ns->name); j.EndObject();
        return true;
    }
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return false;
    auto target = DefinitionOf(loc, *occ);
    auto leaf = LeafOf(occ->name);
    if (!target.Known() || leaf.empty()) return false;
    auto &tf = loc.an->files[(size_t)target.fileidx];
    if (tf.text.text.substr(target.start, target.len) != leaf) return false;
    j.BeginObject();
    j.Key("range");
    WriteRange(j, loc.fi->text, occ->start + occ->name.size() - leaf.size(), occ->start + occ->len);
    j.Member("placeholder", leaf);
    j.EndObject();
    return true;
}

inline bool DocumentHighlights(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto keywords = ControlKeywords(loc);
    if (!keywords.empty()) {
        j.BeginArray();
        for (auto p : keywords) {
            j.BeginObject();
            j.Key("range");
            WriteRange(j, loc.fi->text, p.start, p.start + p.len);
            j.Member("kind", (int64_t)1);
            j.EndObject();
        }
        j.EndArray();
        return true;
    }
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return false;
    auto target = DefinitionOf(loc, *occ);
    if (!target.Known()) return false;
    j.BeginArray();
    set<size_t> seen;
    ReferencesTo(ws, loc, target, [&](const Analysis &, const FileIndex &f, const Occurrence &o) {
        if (f.uri != loc.fi->uri || !seen.insert(o.start).second) return;
        j.BeginObject();
        j.Key("range");
        WriteRange(j, f.text, o.start, o.start + o.len);
        j.Member("kind", (int64_t)1);
        j.EndObject();
    });
    j.EndArray();
    return true;
}

inline bool TypeDefinition(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return false;
    vector<TypeExpr *> types;
    if (occ->kind == Occurrence::TYPE) types.push_back(occ->type);
    else if (occ->kind == Occurrence::FIELD) types.push_back(occ->field->type);
    else if (occ->kind == Occurrence::PARAM) {
        types.push_back(occ->param->type);
        auto idx = (size_t)(occ->param - occ->sf->params.data());
        for (auto sp : occ->sf->specs)
            if (!sp->isfunval && idx < sp->argtypes.size())
                types.push_back(sp->argtypes[idx]);
    }
    else if (occ->kind == Occurrence::GLOBAL || occ->kind == Occurrence::LOCAL ||
             occ->kind == Occurrence::BINDER) {
        for (auto &[def, place] : loc.an->defplaces)
            if (place.fileidx == loc.fi->fileidx && place.start == occ->start)
                types.push_back(def->type);
    } else if (occ->node) types = loc.an->TypesOf(occ->node, 256);
    j.BeginArray();
    set<pair<int, size_t>> seen;
    for (auto type : types) {
        if (!type) continue;
        Place place;
        if (type->kind == TY_STRUCT && !type->struc->st->unknown) {
            auto st = type->struc->st;
            place = PlaceOfName(*loc.an, st->line.fileidx, st->name, st->line);
        } else if (type->kind == TY_ENUM) {
            auto en = type->enu->en;
            place = PlaceOfName(*loc.an, en->line.fileidx, en->name, en->line);
        }
        if (place.Known() && seen.insert({place.fileidx, place.start}).second)
            WritePlace(j, *loc.an, place);
    }
    j.EndArray();
    return true;
}

// --- call hierarchy -----------------------------------------------------------------

// The program's function declarations, nested ones among them, each with
// its extent in its file.
struct FnExtent {
    const FnDecl *fd = nullptr;
    size_t start = 0, end = 0;
};

inline vector<FnExtent> FnExtents(const Analysis &an) {
    vector<FnExtent> fns;
    function<void(const Node *)> walk = [&](const Node *n) {
        if (!n) return;
        if (auto fd = Is<FnDecl>(n)) {
            auto fileidx = fd->line.fileidx;
            if (fileidx >= 0 && fileidx < (int)an.files.size()) {
                auto extent = ExtentOf(an.files[(size_t)fileidx], fd);
                fns.push_back({ fd, extent.first, extent.second });
            }
            walk(fd->sf->body);
            return;
        }
        n->Children([&](Node *ch) { walk(ch); });
    };
    for (auto d : an.ast->topdecls) walk(d);
    return fns;
}

// The innermost function declared around an offset in a file, if any: the
// one a call there is made in.
inline const FnExtent *EnclosingFn(const vector<FnExtent> &fns, int fileidx, size_t offset) {
    const FnExtent *found = nullptr;
    for (auto &f : fns)
        if (f.fd->line.fileidx == fileidx && f.start <= offset && offset < f.end &&
            (!found || f.start >= found->start))
            found = &f;
    return found;
}

// The declaration of the function whose name is at a place, if any.
inline const FnExtent *FnAtPlace(const Analysis &an, const vector<FnExtent> &fns, const Place &p) {
    for (auto &f : fns) {
        auto at = PlaceOfFn(an, f.fd->sf);
        if (at.Known() && at.fileidx == p.fileidx && at.start == p.start) return &f;
    }
    return nullptr;
}

// A CallHierarchyItem: the function's declaration as its range, its name as
// the selection range, which a later request finds it by.
inline void WriteCallItem(JsonOut &j, const Analysis &an, const FnExtent &f) {
    auto sf = f.fd->sf;
    auto &file = an.files[(size_t)f.fd->line.fileidx];
    auto name = PlaceOfFn(an, sf);
    j.BeginObject();
    j.Member("name", sf->name);
    j.Member("kind", (int64_t)SK_FUNCTION);
    j.Member("detail", FnHeader(sf));
    j.Member("uri", file.uri);
    j.Key("range");
    WriteRange(j, file.text, f.start, f.end);
    j.Key("selectionRange");
    if (name.Known() && name.fileidx == f.fd->line.fileidx) WriteRange(j, file.text, name.start, name.start + name.len);
    else WriteRange(j, file.text, f.start, f.start);
    j.EndObject();
}

// The function an occurrence's name resolves to, where it is a call's callee.
inline const FnExtent *CalleeAt(const Located &at, const vector<FnExtent> &fns, const Occurrence &occ) {
    if (!occ.callee) return nullptr;
    auto p = DefinitionOf(at, occ);
    return p.Known() ? FnAtPlace(*at.an, fns, p) : nullptr;
}

// textDocument/prepareCallHierarchy: the function named at the position, its
// declaration or a use of it.
inline bool PrepareCallHierarchy(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return false;
    auto target = DefinitionOf(loc, *occ);
    if (!target.Known()) return false;
    auto fns = FnExtents(*loc.an);
    auto f = FnAtPlace(*loc.an, fns, target);
    if (!f) return false;
    j.BeginArray();
    WriteCallItem(j, *loc.an, *f);
    j.EndArray();
    return true;
}

// The function a CallHierarchyItem is, found again by its selection range in
// the analysis answering for its file now.
inline const FnExtent *FnOfItem(Workspace &ws, const Json &params, Located &loc, vector<FnExtent> &fns) {
    auto item = params.Get("item");
    auto sel = item ? item->Get("selectionRange") : nullptr;
    auto start = sel ? sel->Get("start") : nullptr;
    auto uri = JsonStr(item ? item->Get("uri") : nullptr);
    if (!start || uri.empty()) return nullptr;
    int fileidx;
    if (!ws.Locate(PathKey(PathOfUri(uri)), loc.an, fileidx)) return nullptr;
    loc.fi = &loc.an->files[(size_t)fileidx];
    loc.offset = loc.fi->text.OffsetOfPosition(JsonInt(start->Get("line")), JsonInt(start->Get("character")));
    fns = FnExtents(*loc.an);
    auto occ = loc.fi->At(loc.offset);
    if (!occ) return nullptr;
    auto target = DefinitionOf(loc, *occ);
    return target.Known() ? FnAtPlace(*loc.an, fns, target) : nullptr;
}

// Calls grouped by the function at their other end, in the order first met:
// each with the ranges of the callee's name in the caller. The function is
// one of an analysis's, which the analyses of the other programs reaching
// its file have at the same place.
struct CallGroup {
    const Analysis *an = nullptr;
    const FnExtent *fn = nullptr;
    const FileIndex *file = nullptr;
    vector<pair<size_t, size_t>> ranges;
};

inline void WriteCallGroups(JsonOut &j, const vector<CallGroup> &groups, const char *key) {
    j.BeginArray();
    for (auto &g : groups) {
        j.BeginObject();
        j.Key(key);
        WriteCallItem(j, *g.an, *g.fn);
        j.Key("fromRanges");
        j.BeginArray();
        for (auto &r : g.ranges) WriteRange(j, g.file->text, r.first, r.second);
        j.EndArray();
        j.EndObject();
    }
    j.EndArray();
}

inline void AddCall(vector<CallGroup> &groups, const Analysis &an, const FnExtent *fn, const FileIndex &file,
                    const Occurrence &occ) {
    auto &uri = an.files[(size_t)fn->fd->line.fileidx].uri;
    auto it = find_if(groups.begin(), groups.end(), [&](const CallGroup &g) {
        return g.fn->start == fn->start && g.an->files[(size_t)g.fn->fd->line.fileidx].uri == uri;
    });
    if (it == groups.end()) it = groups.insert(groups.end(), CallGroup { &an, fn, &file, {} });
    pair<size_t, size_t> range { occ.start, occ.start + occ.len };
    if (find(it->ranges.begin(), it->ranges.end(), range) == it->ranges.end()) it->ranges.push_back(range);
}

// callHierarchy/incomingCalls: the calls of the item's function, in every
// program reaching it (ReferencesTo), by the function each is made in (a
// call in a global's initializer is in none).
inline bool IncomingCalls(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    vector<FnExtent> fns;
    auto target = FnOfItem(ws, params, loc, fns);
    if (!target) return false;
    map<const Analysis *, vector<FnExtent>> callers;   // Each analysis's functions.
    vector<CallGroup> groups;
    ReferencesTo(ws, loc, PlaceOfFn(*loc.an, target->fd->sf),
                 [&](const Analysis &an, const FileIndex &f, const Occurrence &o) {
        if (!o.callee) return;
        auto it = callers.find(&an);
        if (it == callers.end()) it = callers.emplace(&an, FnExtents(an)).first;
        if (auto caller = EnclosingFn(it->second, f.fileidx, o.start)) AddCall(groups, an, caller, f, o);
    });
    WriteCallGroups(j, groups, "from");
    return true;
}

// callHierarchy/outgoingCalls: the functions the item's function calls, its
// nested functions' calls left to them, by callee; the ranges are in the
// item's file.
inline bool OutgoingCalls(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    vector<FnExtent> fns;
    auto target = FnOfItem(ws, params, loc, fns);
    if (!target) return false;
    auto &an = *loc.an;
    auto &file = an.files[(size_t)target->fd->line.fileidx];
    Located at { &an, &file, 0 };
    vector<CallGroup> groups;
    for (auto &o : file.occs) {
        if (o.start < target->start || o.start >= target->end) continue;
        if (EnclosingFn(fns, file.fileidx, o.start) != target) continue;
        if (auto callee = CalleeAt(at, fns, o)) AddCall(groups, an, callee, file, o);
    }
    WriteCallGroups(j, groups, "to");
    return true;
}

// --- inlay hints ----------------------------------------------------------------------

// LSP InlayHintKind.
enum InlayHintKind { IH_TYPE = 1, IH_PARAMETER = 2 };

// Where a node's tokens start: the earliest place in it, which for a node
// anchored at its operator lies in its left operand. Clones are left out:
// a global's initializer, checked in place, holds the defaults the check
// filled in, which are written elsewhere.
inline int64_t StartOf(const FileIndex &f, const Node *n) {
    int64_t start = -1;
    function<void(const Node *)> walk = [&](const Node *c) {
        if (!c || c->origin || c->line.fileidx != f.fileidx || c->line.line <= 0) return;
        auto s = f.text.OffsetOf(c->line.line, c->line.col);
        if (s >= 0 && (start < 0 || s < start)) start = s;
        c->Children([&](Node *ch) { walk(ch); });
    };
    walk(n);
    return start;
}

inline void WriteInlayHint(JsonOut &j, const TextIndex &t, size_t offset, const string &label, int kind,
                           bool padright) {
    j.BeginObject();
    j.Key("position");
    WritePosition(j, t, offset);
    j.Member("label", label);
    j.Member("kind", (int64_t)kind);
    if (padright) j.Member("paddingRight", true);
    j.EndObject();
}

// The hints in the requested range: the type of each `let` or `var`
// without one written, after its name, where the specializations agree on
// it; and the parameter's name before each argument of a call resolved to
// a function, but for an argument that is that name itself.
inline bool InlayHints(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return false;
    auto &an = *loc.an;
    auto &f = *loc.fi;
    size_t from = 0, to = f.text.text.size();
    if (auto range = params.Get("range")) {
        if (auto s = range->Get("start")) from = f.text.OffsetOfPosition(JsonInt(s->Get("line")), JsonInt(s->Get("character")));
        if (auto e = range->Get("end")) to = f.text.OffsetOfPosition(JsonInt(e->Get("line")), JsonInt(e->Get("character")));
    }
    j.BeginArray();
    for (auto &occ : f.occs) {
        if (occ.start > to) break;
        if (occ.start + occ.len < from) continue;
        if (occ.kind != Occurrence::LOCAL && occ.kind != Occurrence::GLOBAL) continue;
        auto vd = Is<VarDecl>(occ.node);
        if (vd->type) continue;
        auto i = (size_t)occ.index;
        set<string> types;
        auto unknown = false;
        auto consider = [&](const Node *n) {
            auto x = Is<VarDecl>(n);
            if (!x || x->defs.size() <= i || !x->defs[i] || !x->defs[i]->type) return;
            auto t = x->defs[i]->type;
            // What an exemplar knows only as `?` (TypeCheck::OpaqueMember).
            unknown = unknown || (IsOpaque(t) && t->named->name == "?");
            types.insert(TypeText(t));
        };
        consider(occ.node);
        if (auto it = an.clones.find(occ.node); it != an.clones.end()) for (auto c : it->second) consider(c);
        if (types.size() != 1 || unknown) continue;
        if (occ.start + occ.len < from || occ.start + occ.len > to) continue;
        WriteInlayHint(j, f.text, occ.start + occ.len, cat(": ", *types.begin()), IH_TYPE, false);
    }
    set<size_t> hinted;
    for (auto &span : f.spans) {
        auto closure = Is<FunVal>(span.node);
        auto expression = Is<Call>(span.node) || Is<Dot>(span.node);
        if (!closure && !expression) continue;
        auto next = span.end;
        while (next < f.text.text.size() && isspace((unsigned char)f.text.text[next])) next++;
        if (!closure && (next == f.text.text.size() || f.text.text[next] != '.')) continue;
        auto at = span.end;
        if (at < from || at > to || hinted.count(at)) continue;
        string label;
        if (closure) {
            set<string> returns;
            auto consider = [&](const Node *node) {
                auto c = Is<Call>(node);
                if (!c || !c->fvbody || c->fvbody->Origin() != closure->body->Origin()) return;
                returns.insert(c->fvbody->exprtype ? TypeText(c->fvbody->exprtype) : "?");
            };
            for (auto &[original, clones] : an.clones) {
                consider(original);
                for (auto c : clones) consider(c);
            }
            if (returns.size() != 1) continue;
            label = cat(" -> ", *returns.begin());
        } else {
            auto types = an.TypesOf(span.node, 2);
            if (types.size() != 1) continue;
            label = cat(": ", TypeText(types[0]));
        }
        if (label.find('?') != string::npos) continue;
        hinted.insert(at);
        WriteInlayHint(j, f.text, at, label, IH_TYPE, false);
    }
    for (auto &span : f.calls) {
        if (span.start > to) break;
        if (span.end < from) continue;
        auto c = SpecializedCall(an, span.call);
        if (!c) continue;
        auto sf = c->spec->sf;
        // A UFCS call's object is the first parameter's argument. A call
        // checked in place, in a global's initializer, has the defaults of
        // the parameters it leaves out after the arguments written.
        size_t first = Is<Dot>(span.call->callee) ? 1 : 0;
        auto written = span.call->args.size() - (size_t)span.call->ndefaults;
        for (size_t i = 0; i < written && i + first < sf->params.size(); i++) {
            auto &p = sf->params[i + first];
            auto arg = span.call->args[i];
            if (auto id = Is<Ident>(arg); id && LeafOf(id->name) == p.name) continue;
            auto start = StartOf(f, arg);
            if (start < 0 || (size_t)start < from || (size_t)start > to) continue;
            WriteInlayHint(j, f.text, (size_t)start, cat(p.name, ":"), IH_PARAMETER, true);
        }
    }
    j.EndArray();
    return true;
}

// --- folding ranges --------------------------------------------------------------------

// Every brace-delimited body (a block, a match, a struct literal) and
// declaration spanning more than one line, and every run of `//` comment
// lines.
inline bool FoldingRanges(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return false;
    auto &f = *loc.fi;
    auto &t = f.text;
    set<pair<int, int>> seen;
    j.BeginArray();
    auto fold = [&](int startline, int endline, const char *kind) {
        if (endline <= startline || !seen.insert({ startline, endline }).second) return;
        j.BeginObject();
        j.Member("startLine", (int64_t)startline);
        j.Member("endLine", (int64_t)endline);
        j.Member("kind", kind);
        j.EndObject();
    };
    auto extent = [&](size_t start, size_t end) {
        end = min(end, t.text.size());
        if (end <= start) return;
        auto startline = t.LineOf(start);
        auto endline = t.LineOf(end - 1);
        // An editor folds whole lines: text after the extent on its last
        // line stays visible by ending the fold a line short.
        auto rest = t.LineText(endline).substr(min(end - t.linestarts[(size_t)endline], t.LineText(endline).size()));
        if (rest.find_first_not_of(" \t") != string_view::npos) endline--;
        fold(startline, endline, "region");
    };
    for (auto &s : f.spans)
        if (Is<Block>(s.node) || Is<MatchExpr>(s.node) || Is<StructLit>(s.node) || Is<FnDecl>(s.node) ||
            Is<StructDecl>(s.node) || Is<EnumDecl>(s.node))
            extent(s.start, s.end);
    auto runstart = -1;
    for (auto l = 0; l <= t.Lines(); l++) {
        auto line = t.LineText(l);
        auto first = line.find_first_not_of(" \t");
        auto comment = l < t.Lines() && first != string_view::npos && line.substr(first, 2) == "//";
        if (comment) {
            if (runstart < 0) runstart = l;
        } else {
            if (runstart >= 0) fold(runstart, l - 1, "comment");
            runstart = -1;
        }
    }
    j.EndArray();
    return true;
}

// --- selection ranges -------------------------------------------------------------------

// For each position, the chain of extents holding it, from the name or
// innermost node outward to the declaration.
inline bool SelectionRanges(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return false;
    auto &f = *loc.fi;
    auto positions = params.Get("positions");
    if (!positions || positions->kind != Json::ARRAY) return false;
    j.BeginArray();
    for (auto &pos : positions->arr) {
        auto offset = f.text.OffsetOfPosition(JsonInt(pos.Get("line")), JsonInt(pos.Get("character")));
        vector<pair<size_t, size_t>> ranges;
        // The name at the position, where the occurrence's extent is not a node's own.
        if (auto occ = f.At(offset)) ranges.push_back({ occ->start, occ->start + occ->len });
        else if (auto word = WordAt(f.text, offset); !word.empty())
            ranges.push_back({ (size_t)(word.data() - f.text.text.data()), (size_t)(word.data() - f.text.text.data()) + word.size() });
        for (auto s : f.SpansAt(offset)) ranges.push_back({ s->start, s->end });
        sort(ranges.begin(), ranges.end(), [](const pair<size_t, size_t> &x, const pair<size_t, size_t> &y) {
            return x.second - x.first != y.second - y.first ? x.second - x.first < y.second - y.first : x.first < y.first;
        });
        // Innermost first, each holding the one before it.
        vector<pair<size_t, size_t>> chain;
        for (auto &r : ranges) {
            if (!chain.empty() && (r == chain.back() || r.first > chain.back().first || r.second < chain.back().second)) continue;
            chain.push_back(r);
        }
        if (chain.empty()) chain.push_back({ offset, offset });
        for (size_t i = 0; i < chain.size(); i++) {
            j.BeginObject();
            j.Key("range");
            WriteRange(j, f.text, chain[i].first, chain[i].second);
            if (i + 1 < chain.size()) j.Key("parent");
        }
        for (size_t i = 0; i < chain.size(); i++) j.EndObject();
    }
    j.EndArray();
    return true;
}

// --- completion ---------------------------------------------------------------------

// LSP CompletionItemKind.
enum CompletionKind {
    CK_METHOD = 2, CK_FUNCTION = 3, CK_FIELD = 5, CK_VARIABLE = 6, CK_MODULE = 9, CK_PROPERTY = 10,
    CK_ENUM = 13, CK_KEYWORD = 14, CK_ENUMMEMBER = 20, CK_CONSTANT = 21, CK_STRUCT = 22,
    CK_TYPEPARAMETER = 25
};

// The items a completion offers, one per label, in groups the editor sorts
// by: what is nearest first (a local before a global before a builtin
// before a keyword).
struct Completions {
    struct Item {
        string label, detail, doc, insert, sort;
        int kind;
        bool snippet = false;
        int editStart = -1, editEnd = -1;
        string importText;
    };
    const TextIndex *textIndex = nullptr;
    bool snippets = false;
    TypeExpr *expectedType = nullptr;
    vector<Item> items;
    set<string> seen;

    void Add(string_view label, int kind, int group, string detail = {}, string_view doc = {},
             string_view insert = {}) {
        if (label.empty() || !seen.insert(string(label)).second) return;
        items.push_back({ string(label), std::move(detail), DocText(doc), string(insert),
                          cat(group, label), kind });
    }

    static string EscapeSnippet(string_view text) {
        string escaped;
        for (char c : text) {
            if (c == '$' || c == '}' || c == '\\') escaped += '\\';
            escaped += c;
        }
        return escaped;
    }

    void Function(string_view label, SFunction *sf, int group, bool member = false) {
        if (seen.count(string(label))) {
            // Different overloads can require different call shapes.
            for (auto &item : items) if (item.label == label && item.snippet) {
                item.insert.clear();
                item.snippet = false;
            }
            return;
        }
        Add(label, member ? CK_METHOD : CK_FUNCTION, group, FnHeader(sf), sf->doc);
        auto &item = items.back();
        bool matches = sf->rets.size() == 1 && sf->rets[0] == expectedType;
        for (auto spec : sf->specs)
            if (!spec->exemplar && spec->rets.size() == 1 && spec->rets[0] == expectedType)
                matches = true;
        if (expectedType) item.sort = cat(matches ? "0" : "1", item.sort);
        if (!snippets) return;
        item.insert = cat(EscapeSnippet(label), "(");
        for (size_t i = member ? 1 : 0, n = 1; i < sf->params.size(); i++, n++) {
            if (n > 1) item.insert += ", ";
            item.insert += cat("${", n, ":", EscapeSnippet(sf->params[i].name), "}");
        }
        item.insert += ")$0";
        item.snippet = true;
    }

    void Write(JsonOut &j) const {
        j.BeginObject();
        j.Member("isIncomplete", false);
        j.Key("items");
        j.BeginArray();
        for (auto &it : items) {
            j.BeginObject();
            j.Member("label", it.label);
            j.Member("kind", (int64_t)it.kind);
            if (!it.detail.empty()) j.Member("detail", it.detail);
            if (!it.doc.empty()) {
                j.Key("documentation");
                j.BeginObject();
                j.Member("kind", "markdown");
                j.Member("value", it.doc);
                j.EndObject();
            }
            if (!it.insert.empty() && it.insert != it.label) j.Member("insertText", it.insert);
            if (it.editStart >= 0 && textIndex) {
                j.Key("textEdit"); j.BeginObject();
                j.Key("range"); WriteRange(j, *textIndex, it.editStart, it.editEnd);
                j.Member("newText", it.insert); j.EndObject();
            }
            if (!it.importText.empty() && textIndex) {
                j.Key("additionalTextEdits"); j.BeginArray(); j.BeginObject();
                j.Key("range"); WriteRange(j, *textIndex, textIndex->text.size(), textIndex->text.size());
                j.Member("newText", it.importText);
                j.EndObject(); j.EndArray();
            }
            if (it.snippet) j.Member("insertTextFormat", (int64_t)2);
            j.Member("sortText", it.sort);
            j.EndObject();
        }
        j.EndArray();
        j.EndObject();
    }
};

// The namespace a file's declarations are in: that of the first one the
// file declares, "" for none.
inline string_view NamespaceOfFile(const Ast &ast, int fileidx) {
    for (auto sf : ast.functions) if (sf->line.fileidx == fileidx && !sf->isnested) return sf->ns;
    for (auto st : ast.structs) if (st->line.fileidx == fileidx) return st->ns;
    for (auto en : ast.enums) if (en->line.fileidx == fileidx) return en->ns;
    for (auto al : ast.aliases) if (al->line.fileidx == fileidx) return al->ns;
    for (auto g : ast.globals) if (g->line.fileidx == fileidx) return g->ns;
    return {};
}

// The declarations of a namespace, as `leaf` or `ns::leaf` spells them
// from where the completion is.
inline void NamespaceCompletions(string_view nsname, const Namespace &ns, bool qualify, Completions &out) {
    auto name = [&](string_view leaf) { return qualify ? cat(nsname, "::", leaf) : string(leaf); };
    for (auto &[leaf, fns] : ns.functionmap)
        if (!fns.empty()) for (auto sf : fns) out.Function(name(leaf), sf, 1);
    for (auto &[leaf, st] : ns.structmap) out.Add(name(leaf), CK_STRUCT, 1, DeclDetail(st), st->doc);
    for (auto &[leaf, en] : ns.enummap) out.Add(name(leaf), CK_ENUM, 1, DeclDetail(en), en->doc);
    for (auto &[leaf, al] : ns.aliasmap)
        out.Add(name(leaf), CK_TYPEPARAMETER, 1, DeclDetail(al), al->doc);
    for (auto &[leaf, vd] : ns.globalmap) {
        auto def = vd->defs.empty() ? nullptr : vd->defs[0];
        out.Add(name(leaf), vd->isvar ? CK_VARIABLE : CK_CONSTANT, 1,
                cat(VarKeyword(vd), leaf, def && def->type ? cat(": ", TypeText(def->type)) : string()),
                vd->doc);
    }
}

// The names in scope at an offset: the bindings of the blocks around it
// declared before it, the enclosing functions' parameters and type
// parameters and the functions they declare, then every namespace's
// declarations (qualified where the file's is another), the builtins and
// the keywords.
inline void ScopeCompletions(const Located &loc, size_t offset, Completions &out) {
    auto &an = *loc.an;
    auto &ast = *an.ast;
    auto &f = *loc.fi;
    auto contains = [&](const Node *n) {
        auto [s, e] = NodeExtent(f, n);
        return s >= 0 && (size_t)s <= offset && offset <= (size_t)e;
    };
    auto endsbefore = [&](const Node *n) {
        auto [s, e] = NodeExtent(f, n);
        return s >= 0 && (size_t)e < offset;
    };
    auto enclosing = FunctionsAround(an, f, offset);
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !contains(n)) return;
        if (auto b = Is<Block>(n)) {
            for (auto s : b->stmts) {
                if (endsbefore(s)) {
                    if (auto vd = Is<VarDecl>(s)) {
                        if (!vd->results)
                            for (auto &name : vd->names)
                                out.Add(name, vd->isvar ? CK_VARIABLE : CK_CONSTANT, 0, cat(VarKeyword(vd), name));
                    } else if (auto fd = Is<FnDecl>(s)) {
                        out.Function(fd->sf->name, fd->sf, 0);
                    }
                }
                walk(s);
            }
            walk(b->tail);
            return;
        }
        if (auto fl = Is<ForLoop>(n)) {
            if (contains(fl->body)) {
                out.Add(fl->var, CK_VARIABLE, 0, cat("for ", fl->var));
                if (!fl->idxvar.empty())
                    out.Add(fl->idxvar, CK_VARIABLE, 0, cat("for ", fl->var, ", ", fl->idxvar));
            }
        } else if (auto m = Is<MatchExpr>(n)) {
            for (auto &arm : m->arms)
                if (arm.body && contains(arm.body) && !arm.pat.binder.empty())
                    out.Add(arm.pat.binder, CK_VARIABLE, 0, cat("match binding ", arm.pat.binder));
        } else if (auto fv = Is<FunVal>(n)) {
            if (contains(fv->body)) {
                if (fv->params.empty()) out.Add("it", CK_VARIABLE, 0, "the block's parameter");
                for (auto &p : fv->params) out.Add(p.name, CK_VARIABLE, 0, cat("block parameter ", p.name));
            }
        }
        n->Children(walk);
    };
    for (auto sf : enclosing) {
        for (auto &p : sf->params)
            out.Add(p.name, CK_VARIABLE, 0, cat("parameter ", p.isvar ? "var " : "", p.name,
                                                 p.type ? cat(": ", TypeText(p.type)) : string()));
        for (auto &g : sf->generics) out.Add(g.name, CK_TYPEPARAMETER, 0, cat("type parameter ", g.name));
        if (sf->body) walk(sf->body);
    }
    auto filens = enclosing.empty() ? NamespaceOfFile(ast, f.fileidx) : enclosing[0]->ns;
    for (auto &[nsname, ns] : ast.namespaces)
        NamespaceCompletions(nsname, ns, !nsname.empty() && nsname != filens, out);
    for (auto &b : builtindefs)
        if (!(b.flags & BF_PROPERTY)) out.Add(b.name, CK_FUNCTION, 2, "builtin");
    for (int t = T_IMPORT; t <= T_TF64; t++) out.Add(TName((TType)t), CK_KEYWORD, 3);
    if (out.snippets) {
        for (auto &item : out.items) {
            if (item.kind != CK_KEYWORD) continue;
            if (item.label == "fn") item.insert = "fn ${1:name}(${2}) {\n    $0\n}";
            if (item.label == "if") item.insert = "if ${1:true} {\n    $0\n}";
            if (item.label == "match") item.insert = "match ${1:0} {\n    0 => { $0 },\n    _ => {},\n}";
            item.snippet = !item.insert.empty();
        }
    }
}

// Whether a parameter type takes a value of a type, as a call binding the
// function's type parameters would: a type parameter takes any type, a
// generic struct or enum its instantiations whose arguments its own take,
// and any other type itself; a const one only where it is const, or under a
// const reference.
inline bool TypeTakes(const TypeExpr *pt, const TypeExpr *ot, bool constref = false) {
    if (pt->kind == TY_GENERIC) return true;
    if (ot->cq && !pt->cq && !constref) return false;
    auto args = [](const vector<TypeExpr *> &ps, const vector<TypeExpr *> &os) {
        if (ps.size() != os.size()) return false;
        for (size_t i = 0; i < ps.size(); i++)
            if (!ps[i] || !os[i] || !TypeTakes(ps[i], os[i])) return false;
        return true;
    };
    if (pt->kind == TY_STRUCT && ot->kind == TY_STRUCT && !pt->struc->args.empty())
        return pt->struc->st == ot->struc->st && args(pt->struc->args, ot->struc->args);
    if (pt->kind == TY_ENUM && ot->kind == TY_ENUM && !pt->enu->args.empty())
        return pt->enu->en == ot->enu->en && args(pt->enu->args, ot->enu->args);
    auto unconst = [](string s) { return s.rfind("const ", 0) == 0 ? s.substr(6) : s; };
    return unconst(TypeText(pt)) == unconst(TypeText(ot));
}

// Whether a function's first parameter takes a value of a type, as a UFCS
// call `value.f()` passes it: untyped, or taking the value or a reference
// to it.
inline bool FirstParamTakes(const TypeExpr *pt, const TypeExpr *ot) {
    if (!pt) return true;
    if (pt->kind == TY_REF && pt->ref->sub) return TypeTakes(pt->ref->sub, ot, pt->cq);
    return TypeTakes(pt, ot);
}

// The members of the value before a `.`: the fields of its type, the
// functions that take it first (UFCS) and the builtin members of its kind;
// an enum's name offers its variants; a value whose type no instantiation
// gave offers every field and function.
inline size_t CompletionNextToken(string_view text, size_t pos) {
    while (pos < text.size()) {
        if (isspace((unsigned char)text[pos])) { pos++; continue; }
        if (text.substr(pos, 2) == "//") {
            auto end = text.find('\n', pos + 2);
            pos = end == string_view::npos ? text.size() : end + 1;
        } else if (text.substr(pos, 2) == "/*") {
            auto begin = pos;
            int depth = 1;
            pos += 2;
            while (pos < text.size() && depth) {
                if (text.substr(pos, 2) == "/*") { depth++; pos += 2; }
                else if (text.substr(pos, 2) == "*/") { depth--; pos += 2; }
                else pos++;
            }
            if (depth) return begin;
        } else break;
    }
    return pos;
}

inline void MemberCompletions(const Located &loc, size_t dotpos, Completions &out) {
    auto &an = *loc.an;
    auto &ast = *an.ast;
    auto &f = *loc.fi;
    auto objend = dotpos;
    while (objend > 0 && (f.text.text[objend - 1] == ' ' || f.text.text[objend - 1] == '\t')) objend--;
    const NodeSpan *object = nullptr;
    for (auto &s : f.spans) {
        if (s.start >= objend) break;
        if (s.end == objend && (!object || s.start >= object->start)) object = &s;
    }
    // A field's type as the instantiation has it, where there is one.
    auto fields = [&](const vector<Field> &fs, const vector<TypeExpr *> *ftypes = nullptr) {
        for (size_t i = 0; i < fs.size(); i++) {
            auto &fld = fs[i];
            if (fld.ispad) continue;
            auto ft = ftypes && i < ftypes->size() && (*ftypes)[i] ? (*ftypes)[i] : fld.type;
            out.Add(fld.name, CK_FIELD, 0, cat(fld.isconst ? "let " : "", fld.name, ": ", TypeText(ft)), fld.doc);
        }
    };
    auto builtins = [&](int recv) {
        for (auto &b : builtindefs)
            if ((b.flags & (BF_MEMBER | BF_PROPERTY)) && (b.recv & recv))
                out.Add(b.name, b.flags & BF_PROPERTY ? CK_PROPERTY : CK_METHOD, 2, "builtin");
    };
    auto type = object ? an.TypeOf(object->node) : nullptr;

    if (object && !type)
        if (auto id = Is<Ident>(object->node))
            if (auto en = ast.LookupEnum(id->name, id->ns)) {
                for (auto &v : en->variants) out.Add(v.name, CK_ENUMMEMBER, 0, VariantHeader(en, &v), v.doc);
                return;
            }
    if (!type) {
        for (auto st : ast.structs) fields(st->fields);
        for (auto en : ast.enums) for (auto &v : en->variants) fields(v.fields);
        for (auto sf : ast.functions)
            if (!sf->isnested && !sf->params.empty()) out.Function(sf->name, sf, 1, true);
        builtins(BR_ANY);
        return;
    }
    while (type->kind == TY_REF && type->ref->sub) type = type->ref->sub;
    if (type->kind == TY_STRUCT) {
        auto inst = type->struc->inst;
        fields(type->struc->st->fields, inst && !inst->failed ? &inst->ftypes : nullptr);
    } else if (type->kind == TY_VARIANT && type->var->variant) {
        auto adt = AdtOf(type);
        auto inst = adt ? adt->enu->inst : nullptr;
        const vector<TypeExpr *> *ftypes = nullptr;
        if (inst && !inst->failed) {
            auto vi = (size_t)(type->var->variant - inst->en->variants.data());
            if (vi < inst->vftypes.size()) ftypes = &inst->vftypes[vi];
        }
        fields(type->var->variant->fields, ftypes);
    }
    // The functions a UFCS call can name from here (Ast::LookupMethods):
    // those of the type's namespace, the call's and the global one.
    auto enclosing = FunctionsAround(an, f, dotpos);
    auto usens = enclosing.empty() ? NamespaceOfFile(ast, f.fileidx) : enclosing[0]->ns;
    auto tns = NominalNs(type);
    for (auto sf : ast.functions)
        if (!sf->isnested && !sf->params.empty() && (sf->ns.empty() || sf->ns == usens || sf->ns == tns) &&
            FirstParamTakes(sf->params[0].type, type))
            out.Function(sf->name, sf, 1, true);
    builtins(RecvKindOf(type));
    auto end = dotpos + 1;
    while (end < f.text.text.size() && IsIdentCont(f.text.text[end])) end++;
    auto next = CompletionNextToken(f.text.text, end);
    if (object && object->start < object->end &&
        (next == f.text.text.size() || (f.text.text[next] != '(' && f.text.text[next] != '/'))) {
        auto add = [&](string_view label, string prefix) {
            if (out.seen.count(string(label))) return;
            out.Add(label, CK_KEYWORD, 0, "expression transformation");
            auto &item = out.items.back();
            item.insert = cat(prefix, "(", f.text.text.substr(object->start, object->end - object->start), ")");
            item.editStart = object->start;
            item.editEnd = (int)end;
        };
        add("paren", "");
        if (TypeText(type) == "bool") add("not", "!");
    }
}

// Only symbols already checked in another root can supply import candidates.
inline void ImportCompletions(Workspace &ws, const Located &loc, size_t start,
                              size_t end, Completions &out,
                              const vector<string> *requirements = nullptr,
                              const string &replacement = {}, string *imports = nullptr,
                              bool *success = nullptr) {
    if (success) *success = false;
    auto &text = loc.fi->text.text;
    if (!requirements && start == end) return;
    string qualifier;
    if (!requirements && start >= 2 && text.substr(start - 2, 2) == "::") {
        auto begin = start - 2;
        while (begin && IsIdentCont(text[begin - 1])) begin--;
        qualifier = text.substr(begin, start - begin);
        start = begin;
    }
    if (requirements && end == text.size()) return;
    auto prefix = text.substr(start + qualifier.size(), end - start - qualifier.size());
    auto matches = [&](string_view name) {
        size_t i = 0;
        for (auto c : name)
            if (i < prefix.size() && tolower((unsigned char)c) == tolower((unsigned char)prefix[i])) i++;
        return i == prefix.size();
    };
    while (end < text.size() && IsIdentCont(text[end])) end++;
    struct Candidate { string name, path, module; size_t declaration; string doc; int kind; SFunction *fn; };
    map<string, vector<Candidate>> candidates;
    for (auto &root : ws.roots) {
        if (!root->analysis) continue;
        auto &an = *root->analysis;
        auto collect = [&](auto sf, int kind, SFunction *fn) {
            if (sf->ns.empty() || (!requirements && (!matches(sf->name) ||
                (!qualifier.empty() && cat(sf->ns, "::") != qualifier)))) return;
            auto path = an.ast->sources[(size_t)sf->line.fileidx].first;
            if (!requirements && loc.an->FileOf(PathKey(path)) >= 0) return;
            auto relative = filesystem::path(CanonicalPath(path)).lexically_relative(
                filesystem::path(CanonicalPath(DirOf(PathOfUri(loc.fi->uri)))));
            relative.replace_extension();
            string module;
            bool validPath = !relative.empty();
            for (auto part : relative) {
                auto component = part.string();
                if (!IsIdentifier(component)) { validPath = false; break; }
                if (!module.empty()) module += ".";
                module += component;
            }
            if (!validPath) return;
            auto &file = an.files[(size_t)sf->line.fileidx];
            auto extent = ExtentIn(file, sf->line, sf->end);
            if (extent.first < 0) return;
            auto declaration = PlaceOfName(an, sf->line.fileidx, sf->name, sf->line);
            if (!declaration.Known()) return;
            Candidate c { string(sf->qname), path, module, declaration.start, string(sf->doc), kind, fn };
            auto &list = candidates[c.name];
            if (none_of(list.begin(), list.end(), [&](const Candidate &v) {
                return v.path == c.path && v.declaration == c.declaration;
            })) list.push_back(std::move(c));
        };
        for (auto sf : an.ast->functions)
            if (!sf->isnested) collect(sf, CK_FUNCTION, sf);
        if (!requirements) {
            for (auto st : an.ast->structs) collect(st, CK_STRUCT, nullptr);
            for (auto en : an.ast->enums) collect(en, CK_ENUM, nullptr);
            for (auto al : an.ast->aliases) collect(al, CK_TYPEPARAMETER, nullptr);
        }
    }
    vector<Candidate> required;
    string requiredAddition;
    if (requirements) {
        set<string> modules, names;
        for (auto &name : *requirements) {
            if (!names.insert(name).second) continue;
            auto it = candidates.find(name);
            if (it == candidates.end() || it->second.size() != 1) return;
            auto c = it->second[0];
            required.push_back(c);
            if (loc.an->FileOf(PathKey(c.path)) < 0 && modules.insert(c.module).second)
                requiredAddition += cat("\nimport .", c.module, ";\n");
        }
        if (required.empty()) return;
        candidates.clear();
        candidates[replacement] = {required.front()};
    }
    size_t attempts = 0;
    for (auto &[name, list] : candidates) {
        if (list.size() != 1 || out.seen.count(name) || attempts++ >= 16) continue;
        auto &candidate = list[0];
        auto addition = requirements ? requiredAddition : cat("\nimport .", candidate.module, ";\n");
        auto buffer = string(text);
        auto inserted = name;
        auto next = CompletionNextToken(text, end);
        bool call = !requirements && candidate.fn &&
            (next >= text.size() || text[next] != '(');
        if (call && candidate.fn->params.empty()) inserted += "()";
        buffer.replace(start, end - start, inserted);
        buffer += addition;
        bool valid = true, matched = false;
        auto key = PathKey(PathOfUri(loc.fi->uri));
        for (auto &root : ws.roots) {
            if (!root->analysis || !root->Reaches(key)) continue;
            Analysis after;
            after.ast = make_unique<Ast>();
            auto &ast = *after.ast;
            ast.exemplars = true;
            bool aliasUse = false;
            ast.sink = [&](const Diagnostic &d) {
                if (d.warning) return;
                auto &errors = root->analysis->ast->errors;
                if (requirements || none_of(errors.begin(), errors.end(), [&](const Diagnostic &old) {
                    if (old.warning || old.message != d.message || old.where.fileidx < 0 ||
                        d.where.fileidx < 0) return false;
                    auto path = root->analysis->ast->sources[(size_t)old.where.fileidx].first;
                    if (path != ast.sources[(size_t)d.where.fileidx].first) return false;
                    if (PathKey(path) != key)
                        return old.where.line == d.where.line && old.where.col == d.where.col;
                    auto offset = loc.fi->text.OffsetOf(old.where.line, old.where.col);
                    if (offset < 0 || ((size_t)offset >= start && (size_t)offset < end)) return false;
                    if ((size_t)offset >= end) offset = offset - (int64_t)(end - start) + (int64_t)inserted.size();
                    TextIndex updated; updated.Index(buffer);
                    return offset == updated.OffsetOf(d.where.line, d.where.col);
                })) valid = false;
            };
            SourceProvider sources = [&](const string &path) -> const string * {
                return PathKey(path) == key ? &buffer : ws.TextOf(path);
            };
            try {
                RunOnCompilerStack([&]() -> int {
                    ParseProgram(ast, root->path, ws.stdlibdirs, DirOf(ws.exe), sources);
                    if (!requirements && candidate.kind == CK_TYPEPARAMETER)
                        for (auto t : ast.alltypes)
                            if (t->kind == TY_UNRESOLVED && t->named->name == name &&
                                PathKey(ast.sources[(size_t)t->line.fileidx].first) == key) {
                                TextIndex index; index.Index(buffer);
                                if (index.OffsetOf(t->line.line, t->line.col) == (int64_t)start)
                                    aliasUse = true;
                            }
                    ResolveTypeNames(ast); after.IndexSource();
                    TypeCheckProgram(ast, false, true); after.Build(); return 0;
                });
            } catch (CompileError &) { valid = false; }
            if (!valid) break;
            auto &before = *root->analysis;
            for (auto &file : before.files) {
                auto idx = after.FileOf(PathKey(PathOfUri(file.uri)));
                if (idx < 0) { valid = false; break; }
                auto &updated = after.files[(size_t)idx];
                Located oldloc { &before, &file, 0 }, newloc { &after, &updated, 0 };
                for (auto &occ : file.occs) {
                    bool edited = file.uri == loc.fi->uri;
                    if (edited && occ.start >= start && occ.start < end) continue;
                    auto offset = occ.start;
                    if (edited && offset >= end) offset = offset - (end - start) + inserted.size();
                    auto next = updated.At(offset);
                    if (!next || next->kind != occ.kind) { valid = false; break; }
                    auto a = DefinitionOf(oldloc, occ), b = DefinitionOf(newloc, *next);
                    // An accepted import can resolve a broken initializer.
                    if (a.Known() && !b.Known()) { valid = false; break; }
                    if (a.Known()) {
                        auto &oldfile = before.files[(size_t)a.fileidx];
                        auto shifted = a.start;
                        if (oldfile.uri == loc.fi->uri && shifted >= end)
                            shifted = shifted - (end - start) + inserted.size();
                        if (oldfile.uri != after.files[(size_t)b.fileidx].uri || shifted != b.start)
                            valid = false;
                    }
                }
            }
            if (requirements) {
                for (auto &c : required) {
                    bool found = false;
                    for (auto &file : after.files) {
                        Located at { &after, &file, 0 };
                        for (auto &occ : file.occs) {
                            if (file.uri != loc.fi->uri || occ.start < start ||
                                occ.start >= start + name.size()) continue;
                            auto def = DefinitionOf(at, occ);
                            if (def.Known() && PathKey(PathOfUri(after.files[(size_t)def.fileidx].uri)) ==
                                PathKey(c.path) && def.start == c.declaration) found = true;
                        }
                    }
                    if (!found) valid = false;
                }
                if (!valid) break;
            } else {
            auto idx = after.FileOf(key);
            if (idx < 0) { valid = false; break; }
            auto &file = after.files[(size_t)idx];
            Located at { &after, &file, start };
            auto occ = file.At(start + name.size() - 1);
            if (!occ && !aliasUse) { valid = false; break; }
            auto def = occ ? DefinitionOf(at, *occ) : Place {};
            if (aliasUse)
                if (auto al = ast.LookupAlias(name, ""))
                    def = PlaceOfName(after, al->line.fileidx, al->name, al->line);
            if (!def.Known() || PathKey(PathOfUri(after.files[(size_t)def.fileidx].uri)) !=
                PathKey(candidate.path) || def.start != candidate.declaration) { valid = false; break; }
            }
            matched = true;
        }
        if (!valid || !matched) continue;
        if (requirements) {
            *imports = addition;
            if (success) *success = true;
            return;
        }
        if (candidate.fn) out.Function(name, candidate.fn, 3);
        else out.Add(name, candidate.kind, 3, {}, candidate.doc);
        auto &item = out.items.back();
        item.detail = cat("Import .", candidate.module, "; accepting initializes the module");
        if (!call || !out.snippets) { item.insert = inserted; item.snippet = false; }
        item.editStart = (int)start; item.editEnd = (int)end;
        item.importText = addition;
    }
}

// Probe the canonical parser without requiring a checked unknown name.
inline string SnippetScope(Workspace &ws, const Located &loc, size_t start, size_t end) {
    const string marker = "__goose_snippet_probe";
    auto key = PathKey(PathOfUri(loc.fi->uri));
    for (bool item : {false, true}) {
        auto buffer = string(loc.fi->text.text);
        buffer.replace(start, end - start, item ? cat("fn ", marker, "() {}") : marker);
        Analysis probe;
        probe.ast = make_unique<Ast>();
        auto &ast = *probe.ast;
        bool failed = false;
        ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
        SourceProvider sources = [&](const string &path) -> const string * {
            return PathKey(path) == key ? &buffer : ws.TextOf(path);
        };
        try {
            RunOnCompilerStack([&]() -> int {
                ParseProgram(ast, PathOfUri(loc.fi->uri), ws.stdlibdirs, DirOf(ws.exe), sources);
                probe.IndexSource();
                return 0;
            });
        } catch (CompileError &) { continue; }
        if (failed || !ast.errors.empty()) continue;
        auto idx = probe.FileOf(key);
        if (idx < 0) continue;
        if (!item) for (auto type : ast.alltypes) {
            if (type->kind != TY_UNRESOLVED || type->named->name != marker ||
                type->line.fileidx != idx) continue;
            if (probe.files[(size_t)idx].text.OffsetOf(type->line.line, type->line.col) == (int64_t)start)
                return "type";
        }
        auto occ = probe.files[(size_t)idx].At(start + (item ? 3 : 0));
        if (!occ || occ->name != marker) continue;
        if (item && occ->kind == Occurrence::FN && !occ->sf->isnested) return "item";
        if (!item && occ->kind == Occurrence::TYPE) return "type";
        if (!item && occ->kind == Occurrence::IDENT) return "expression";
    }
    return {};
}

inline bool Completion(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto &text = loc.fi->text.text;
    auto offset = min(loc.offset, text.size());
    // The name being typed, and what comes before it: a `.`, a `::`, or
    // anything else.
    auto start = offset;
    while (start > 0 && IsIdentCont(text[start - 1])) start--;
    Completions out;
    out.textIndex = &loc.fi->text;
    auto after = offset;
    while (after < text.size() && IsIdentCont(text[after])) after++;
    after = CompletionNextToken(text, after);
    out.snippets = ws.snippetSupport && (after == text.size() || (text[after] != '(' && text[after] != '/'));
    for (auto &span : loc.fi->spans) {
        auto vd = Is<VarDecl>(span.node);
        if (!vd || vd->inits.size() != 1) continue;
        auto [a, b] = NodeExtent(*loc.fi, vd->inits[0]);
        if (a >= 0 && (size_t)a <= start && offset <= (size_t)b) {
            out.expectedType = vd->type;
            if (!out.expectedType) out.expectedType = loc.an->TypeOf(vd->inits[0]);
        }
    }
    auto contains = [&](const Node *node) {
        auto [a, b] = NodeExtent(*loc.fi, node);
        return a >= 0 && (size_t)a <= start && offset <= (size_t)b;
    };
    for (auto sf : FunctionsAround(*loc.an, *loc.fi, offset)) {
        if (sf->body && sf->body->tail && contains(sf->body->tail)) {
            if (sf->rets.size() == 1) out.expectedType = sf->rets[0];
            break;
        }
    }
    for (auto span : loc.fi->SpansAt(offset)) {
        auto ret = Is<Return>(span->node);
        if (!ret) continue;
        auto checked = loc.an->Find(ret, [](const Node *node) {
            auto r = Is<Return>(node);
            return r && r->targetspec;
        });
        auto r = checked ? Is<Return>(checked) : nullptr;
        if (!r) continue;
        for (size_t i = 0; i < ret->vals.size(); i++)
            if (contains(ret->vals[i]) && i < r->targetspec->rets.size())
                out.expectedType = r->targetspec->rets[i];
    }
    for (auto &span : loc.fi->calls) {
        auto checked = SpecializedCall(*loc.an, span.call);
        if (!checked) continue;
        size_t first = Is<Dot>(span.call->callee) ? 1 : 0;
        for (size_t i = 0; i < span.call->args.size(); i++)
            if (contains(span.call->args[i]) && i + first < checked->spec->argtypes.size())
                out.expectedType = checked->spec->argtypes[i + first];
    }
    if (start > 0 && text[start - 1] == '.' && !(start > 1 && text[start - 2] == '.')) {
        MemberCompletions(loc, start - 1, out);
    } else if (start > 1 && text[start - 1] == ':' && text[start - 2] == ':') {
        auto nsend = start - 2;
        auto nsstart = nsend;
        while (nsstart > 0 && IsIdentCont(text[nsstart - 1])) nsstart--;
        auto nsname = text.substr(nsstart, nsend - nsstart);
        if (auto ns = loc.an->ast->FindNS(nsname)) NamespaceCompletions(nsname, *ns, false, out);
        ImportCompletions(ws, loc, start, offset, out);
    } else {
        ScopeCompletions(loc, offset, out);
        ImportCompletions(ws, loc, start, offset, out);
    }
    auto prefixEnd = offset;
    while (prefixEnd < text.size() && IsIdentCont(text[prefixEnd])) prefixEnd++;
    string scope;
    if (out.snippets && !ws.customSnippets.empty()) scope = SnippetScope(ws, loc, start, prefixEnd);
    if (out.snippets) for (auto &snippet : ws.customSnippets) {
        if (out.seen.count(snippet.prefix)) continue;
        bool member = start > 0 && text[start - 1] == '.';
        if (member != snippet.postfix) continue;
        int a = (int)start, b = (int)offset;
        string body = snippet.body;
        if (member) {
            const NodeSpan *object = nullptr;
            for (auto &span : loc.fi->spans)
                if (span.end == start - 1 && (!object || span.start < object->start)) object = &span;
            if (!object || loc.an->TypesOf(object->node, 2).size() != 1) continue;
            a = (int)object->start;
            auto receiver = cat("(", text.substr(object->start, object->end - object->start), ")");
            body.replace(body.find("${receiver}"), 11, Completions::EscapeSnippet(receiver));
        } else {
            if (scope != snippet.scope) continue;
        }
        while ((size_t)b < text.size() && IsIdentCont(text[b])) b++;
        string expanded;
        bool supported = true;
        supported = ExpandSimpleSnippet(body, expanded);
        ws.snippetImports = [&](const string &, size_t a, size_t b,
                               const vector<string> &requirements, string &addition) {
            if (!supported) return false;
            Completions unused;
            bool success = false;
            ImportCompletions(ws, loc, a, b, unused, &requirements, expanded, &addition, &success);
            return success;
        };
        string imports;
        if (!snippet.requirements.empty() && (!ws.snippetImports ||
            !ws.snippetImports(loc.fi->uri, a, b, snippet.requirements, imports))) continue;
        out.Add(snippet.prefix, CK_KEYWORD, 3, snippet.description +
            (imports.empty() ? "" : "; accepting initializes required modules"));
        auto &item = out.items.back();
        item.insert = body;
        item.importText = imports;
        item.snippet = true;
        item.editStart = a;
        item.editEnd = b;
    }
    ws.snippetImports = {};
    out.Write(j);
    return true;
}

// --- code lens --------------------------------------------------------------------------

// Above each top-level generic function, how many specializations it has,
// their lines as the lens's data; above `main`, "Run". A lens names a
// command only where the client said it has one (Workspace::clientcommands):
// the count is a title alone otherwise, and there is no "Run".
inline bool CodeLenses(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return false;
    auto &an = *loc.an;
    auto &f = *loc.fi;
    auto lens = [&](const Place &p, const string &title, const char *command,
                    const function<void()> &arguments, const function<void()> &data) {
        j.BeginObject();
        j.Key("range");
        WriteRange(j, f.text, p.start, p.start + p.len);
        j.Key("command");
        j.BeginObject();
        j.Member("title", title);
        j.Member("command", command);
        if (arguments) {
            j.Key("arguments");
            j.BeginArray();
            arguments();
            j.EndArray();
        }
        j.EndObject();
        if (data) {
            j.Key("data");
            data();
        }
        j.EndObject();
    };
    j.BeginArray();
    for (auto d : an.ast->topdecls) {
        auto fd = Is<FnDecl>(d);
        if (!fd || d->line.fileidx != f.fileidx) continue;
        auto sf = fd->sf;
        auto p = PlaceOfFn(an, sf);
        if (!p.Known() || p.fileidx != f.fileidx) continue;
        if (sf->IsGeneric()) {
            auto lines = SpecLines(sf);
            auto title = lines.empty() ? string("not instantiated")
                                       : cat(lines.size(), lines.size() == 1 ? " instantiation" : " instantiations");
            auto writelines = [&] {
                j.BeginArray();
                for (auto &l : lines) j.Str(l);
                j.EndArray();
            };
            if (!lines.empty() && ws.clientcommands.count("goose.showInstantiations"))
                lens(p, title, "goose.showInstantiations", [&] {
                    j.Str(sf->qname);
                    writelines();
                }, writelines);
            else
                lens(p, title, "", {}, writelines);
        }
        if (sf->qname == "main" && ws.clientcommands.count("goose.run"))
            lens(p, "Run", "goose.run", [&] { j.Str(f.uri); }, {});
    }
    j.EndArray();
    return true;
}

// --- formatting --------------------------------------------------------------

// `textDocument/formatting`: the open document's text as `goose format`
// writes it, as the changes to its lines (LineChanges), none where it is
// formatted. The editor's text is formatted, not the analysis's, so no
// analysis is waited for. 0 for null: a document not open, or one that does
// not parse, whose errors the diagnostics show already; 1 for the edits in
// `j`; 2 for a result that failed the formatter's check, `error` saying why.
inline int FormatDocument(Workspace &ws, const Json &params, JsonOut &j, string &error, bool rangeOnly = false) {
    auto td = params.Get("textDocument");
    auto uri = JsonStr(td ? td->Get("uri") : nullptr);
    auto it = ws.open.find(PathKey(PathOfUri(uri)));
    if (uri.empty() || it == ws.open.end()) return 0;
    auto &text = it->second.text;
    auto res = Format(text, it->second.path);
    if (!res.ok) {
        if (res.internal.empty()) return 0;
        error = cat("internal error formatting ", it->second.path, ": ", res.internal);
        return 2;
    }
    TextIndex t;
    t.Index(text);
    // A byte-order mark the editor kept in the text is no line's.
    auto bom = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? (size_t)3 : 0;
    auto changes = LineChanges(string_view(text).substr(bom), res.text);
    if (rangeOnly) {
        auto range = params.Get("range");
        if (!range) return 0;
        auto start = range->Get("start"), end = range->Get("end");
        if (!start || !end) return 0;
        auto from = t.OffsetOfPosition(JsonInt(start->Get("line")), JsonInt(start->Get("character")));
        auto to = t.OffsetOfPosition(JsonInt(end->Get("line")), JsonInt(end->Get("character")));
        if (from > to) return 0;
        changes.erase(std::remove_if(changes.begin(), changes.end(), [&](const TextChange &c) {
            return c.start + bom < from || c.end + bom > to || from == to;
        }), changes.end());
        auto partial = text;
        for (auto c = changes.rbegin(); c != changes.rend(); ++c)
            partial.replace(c->start + bom, c->end - c->start, c->text);
        // A selected hunk must preserve the whole program's syntax.
        if (!format::CheckSame(text, partial, it->second.path).empty()) changes.clear();
    }
    j.BeginArray();
    auto place = [&](const char *key, size_t offset) {
        auto [line, character] = t.PositionOf(offset);
        j.Key(key);
        j.BeginObject();
        j.Member("line", (int64_t)line);
        j.Member("character", (int64_t)character);
        j.EndObject();
    };
    for (auto &c : changes) {
        j.BeginObject();
        j.Key("range");
        j.BeginObject();
        place("start", c.start + bom);
        place("end", c.end + bom);
        j.EndObject();
        j.Member("newText", c.text);
        j.EndObject();
    }
    j.EndArray();
    return 1;
}

inline pair<int64_t, int64_t> RefactorExtent(const FileIndex &f, const Node *n) {
    for (auto &span : f.spans) if (span.node == n) return {span.start, span.end};
    return NodeExtent(f, n);
}

inline bool ClosedInteger(const Node *n, int64_t &value) {
    if (auto i = Is<IntLit>(n)) {
        if (i->uns || i->fixedtype) return false;
        value = i->val;
        return true;
    }
    if (auto b = Is<Binary>(n)) {
        int64_t left, right;
        if (!ClosedInteger(b->left, left) || !ClosedInteger(b->right, right)) return false;
        __int128 v;
        if (b->op == T_PLUS) v = (__int128)left + right;
        else if (b->op == T_MINUS) v = (__int128)left - right;
        else if (b->op == T_MUL) v = (__int128)left * right;
        else return false;
        if (v < INT64_MIN || v > INT64_MAX) return false;
        value = (int64_t)v;
        return true;
    }
    return false;
}

struct RefactorEdit { size_t start, end; string text; };

inline bool CheckRefactor(Workspace &ws, const Located &loc, vector<RefactorEdit> edits, bool movedIdentities = false, const map<size_t, string> &rebound = {}, const map<size_t, Place> &insertedBindings = {}, const map<size_t, size_t> &syntheticBindings = {}) {
    sort(edits.begin(), edits.end(), [](auto &a, auto &b) { return a.start < b.start; });
    auto text = string(loc.fi->text.text);
    for (auto it = edits.rbegin(); it != edits.rend(); ++it)
        text.replace(it->start, it->end - it->start, it->text);
    auto shift = [&](size_t pos) {
        int64_t delta = 0;
        for (auto &e : edits) if (e.end <= pos) delta += (int64_t)e.text.size() - (e.end - e.start);
        return (size_t)((int64_t)pos + delta);
    };
    auto removed = [&](size_t pos) {
        for (auto &e : edits) if (e.start <= pos && pos < e.end) return true;
        return false;
    };
    for (auto &root : ws.roots) {
        if (!root->analysis || root->analysis->FileOf(PathKey(PathOfUri(loc.fi->uri))) < 0) continue;
        auto &before = *root->analysis;
        auto fileidx = before.FileOf(PathKey(PathOfUri(loc.fi->uri)));
        if (before.files[(size_t)fileidx].text.text != loc.fi->text.text) return false;
        for (auto &d : before.diags) if (!d.warning) return false;
        Analysis after;
        after.ast = make_unique<Ast>();
        auto &ast = *after.ast;
        ast.exemplars = true;
        bool failed = false;
        ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
        SourceProvider sources = [&](const string &path) -> const string * {
            return PathKey(path) == PathKey(PathOfUri(loc.fi->uri)) ? &text : ws.TextOf(path);
        };
        try {
            RunOnCompilerStack([&]() -> int {
                ParseProgram(ast, root->path, ws.stdlibdirs, DirOf(ws.exe), sources);
                ResolveTypeNames(ast);
                after.IndexSource();
                TypeCheckProgram(ast, false, true);
                after.Build();
                return 0;
            });
        } catch (CompileError &) { return false; }
        if (failed || !ast.errors.empty()) return false;
        auto insertedFile = after.FileOf(PathKey(PathOfUri(loc.fi->uri)));
        if (insertedFile < 0) return false;
        auto &insertedIndex = after.files[(size_t)insertedFile];
        Located insertedLoc {&after, &insertedIndex, 0};
        for (auto &[offset, declaration] : syntheticBindings) {
            auto occ = insertedIndex.At(offset);
            auto decl = insertedIndex.At(declaration);
            if (!occ || !decl) return false;
            auto actual = DefinitionOf(insertedLoc, *occ);
            auto expected = DefinitionOf(insertedLoc, *decl);
            if (!actual.Known() || !expected.Known() ||
                actual.fileidx != insertedFile || actual.start != declaration ||
                expected.fileidx != insertedFile || expected.start != declaration) return false;
        }
        for (auto &[offset, expected] : insertedBindings) {
            auto occ = insertedIndex.At(offset);
            if (!occ) return false;
            auto actual = DefinitionOf(insertedLoc, *occ);
            if (!actual.Known()) return false;
            auto &target = loc.an->files[(size_t)expected.fileidx];
            if (after.files[(size_t)actual.fileidx].uri != target.uri ||
                actual.start != (target.uri == loc.fi->uri ? shift(expected.start) : expected.start)) return false;
        }
        for (auto &file : before.files) {
            auto idx = after.FileOf(PathKey(PathOfUri(file.uri)));
            if (idx < 0) return false;
            auto &updated = after.files[(size_t)idx];
            Located oldloc { &before, &file, 0 }, newloc { &after, &updated, 0 };
            bool changed = file.uri == loc.fi->uri;
            for (auto &occ : file.occs) {
                if (changed && removed(occ.start)) {
                    if (!movedIdentities || occ.kind != Occurrence::IDENT) continue;
                    auto def = DefinitionOf(oldloc, occ);
                    auto *df = def.Known() ? &before.files[(size_t)def.fileidx] : nullptr;
                    // Internal declarations move together with their scope.
                    if (df && df->uri == loc.fi->uri && removed(def.start)) continue;
                    bool retained = false;
                    for (auto &candidate : updated.occs) {
                        if (candidate.kind != occ.kind || candidate.name != occ.name) continue;
                        bool inserted = false;
                        int64_t delta = 0;
                        for (auto &edit : edits) {
                            auto begin = (size_t)((int64_t)edit.start + delta);
                            if (candidate.start >= begin && candidate.start < begin + edit.text.size()) inserted = true;
                            delta += (int64_t)edit.text.size() - (edit.end - edit.start);
                        }
                        if (!inserted) continue;
                        auto nd = DefinitionOf(newloc, candidate);
                        if (!df && !nd.Known()) retained = true;
                        if (df && nd.Known() && after.files[(size_t)nd.fileidx].uri == df->uri &&
                            nd.start == (df->uri == loc.fi->uri ? shift(def.start) : def.start)) retained = true;
                    }
                    if (!retained) return false;
                    continue;
                }
                auto next = updated.At(changed ? shift(occ.start) : occ.start);
                if (!next || next->kind != occ.kind) return false;
                auto a = DefinitionOf(oldloc, occ), b = DefinitionOf(newloc, *next);
                if (a.Known() != b.Known()) return false;
                if (a.Known()) {
                    auto &af = before.files[(size_t)a.fileidx];
                    auto &bf = after.files[(size_t)b.fileidx];
                    auto expected = af.uri == loc.fi->uri ? shift(a.start) : a.start;
                    if (af.uri == loc.fi->uri && rebound.count(a.start)) {
                        bool found = false;
                        int64_t delta = 0;
                        for (auto &edit : edits) {
                            auto begin = (size_t)((int64_t)edit.start + delta);
                            for (auto &candidate : updated.occs)
                                if (candidate.kind == Occurrence::LOCAL &&
                                    candidate.name == rebound.at(a.start) &&
                                    candidate.start >= begin && candidate.start < begin + edit.text.size()) {
                                    expected = candidate.start;
                                    found = true;
                                }
                            delta += (int64_t)edit.text.size() - (edit.end - edit.start);
                        }
                        if (!found) return false;
                    }
                    if (af.uri != bf.uri || expected != b.start) return false;
                } else if (occ.name != next->name) return false;
            }
        }
    }
    return true;
}

inline bool CheckWorkspaceRefactor(Workspace &ws, const map<string, pair<const FileIndex *, vector<RefactorEdit>>> &changes) {
    map<string, string> texts;
    for (auto &[uri, change] : changes) {
        auto edits = change.second;
        sort(edits.begin(), edits.end(), [](auto &a, auto &b) { return a.start < b.start; });
        auto text = string(change.first->text.text);
        size_t previous = 0;
        for (auto &edit : edits) {
            if (edit.start < previous || edit.end < edit.start || edit.end > text.size()) return false;
            previous = edit.end;
        }
        for (auto it = edits.rbegin(); it != edits.rend(); ++it)
            text.replace(it->start, it->end - it->start, it->text);
        texts[PathKey(PathOfUri(uri))] = std::move(text);
    }
    auto editsFor = [&](const string &uri) -> const vector<RefactorEdit> & {
        static const vector<RefactorEdit> empty;
        auto it = changes.find(uri);
        return it == changes.end() ? empty : it->second.second;
    };
    auto shift = [&](const string &uri, size_t pos) {
        int64_t delta = 0;
        for (auto &e : editsFor(uri)) if (e.end <= pos) delta += (int64_t)e.text.size() - (e.end - e.start);
        return (size_t)((int64_t)pos + delta);
    };
    auto removed = [&](const string &uri, size_t pos) {
        for (auto &e : editsFor(uri)) if (e.start <= pos && pos < e.end) return true;
        return false;
    };
    for (auto &root : ws.roots) {
        if (!root->analysis) continue;
        auto &before = *root->analysis;
        bool affected = false;
        for (auto &[key, text] : texts) if (before.FileOf(key) >= 0) affected = true;
        if (!affected) continue;
        for (auto &file : before.files) {
            auto path = PathOfUri(file.uri);
            auto current = ws.TextOf(path);
            string disk;
            if ((current && *current != file.text.text) ||
                (!current && (!LoadFile(path, disk) || disk != file.text.text))) return false;
            auto change = changes.find(file.uri);
            if (change != changes.end() && change->second.first->text.text != file.text.text) return false;
        }
        for (auto &d : before.diags) if (!d.warning) return false;
        Analysis after;
        after.ast = make_unique<Ast>();
        auto &ast = *after.ast;
        ast.exemplars = true;
        bool failed = false;
        ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
        SourceProvider sources = [&](const string &path) -> const string * {
            auto it = texts.find(PathKey(path));
            return it == texts.end() ? ws.TextOf(path) : &it->second;
        };
        try {
            RunOnCompilerStack([&]() -> int {
                ParseProgram(ast, root->path, ws.stdlibdirs, DirOf(ws.exe), sources);
                ResolveTypeNames(ast);
                after.IndexSource();
                TypeCheckProgram(ast, false, true);
                after.Build();
                return 0;
            });
        } catch (CompileError &) { return false; }
        if (failed || !ast.errors.empty()) return false;
        for (auto &file : before.files) {
            auto idx = after.FileOf(PathKey(PathOfUri(file.uri)));
            if (idx < 0) return false;
            auto &updated = after.files[(size_t)idx];
            Located oldloc { &before, &file, 0 }, newloc { &after, &updated, 0 };
            for (auto &occ : file.occs) {
                if (removed(file.uri, occ.start)) continue;
                auto next = updated.At(shift(file.uri, occ.start));
                if (!next || next->kind != occ.kind) return false;
                auto a = DefinitionOf(oldloc, occ), b = DefinitionOf(newloc, *next);
                if (a.Known() != b.Known()) return false;
                if (a.Known()) {
                    auto &af = before.files[(size_t)a.fileidx];
                    auto &bf = after.files[(size_t)b.fileidx];
                    auto expected = shift(af.uri, a.start);
                    if (af.uri != bf.uri || expected != b.start) return false;
                } else if (occ.name != next->name) return false;
            }
        }
    }
    return true;
}

inline void WriteRefactor(JsonOut &j, const Located &loc, int version,
                          string_view title, string_view kind, const vector<RefactorEdit> &edits) {
    j.BeginObject(); j.Member("title", title); j.Member("kind", kind);
    j.Key("edit"); j.BeginObject(); j.Key("documentChanges"); j.BeginArray(); j.BeginObject();
    j.Key("textDocument"); j.BeginObject();
    j.Member("uri", loc.fi->uri); j.Member("version", version); j.EndObject();
    j.Key("edits"); j.BeginArray();
    for (auto &e : edits) {
        j.BeginObject(); j.Key("range"); WriteRange(j, loc.fi->text, e.start, e.end);
        j.Member("newText", e.text); j.EndObject();
    }
    j.EndArray(); j.EndObject(); j.EndArray(); j.EndObject(); j.EndObject();
}

inline void ExtractLiteral(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return;
    auto range = params.Get("range");
    if (!range) return;
    auto a = range->Get("start"), b = range->Get("end");
    if (!a || !b) return;
    auto &f = *loc.fi;
    auto start = f.text.OffsetOfPosition(JsonInt(a->Get("line")), JsonInt(a->Get("character")));
    auto end = f.text.OffsetOfPosition(JsonInt(b->Get("line")), JsonInt(b->Get("character")));
    const VarDecl *found = nullptr;
    const Node *selected = nullptr;
    function<void(const Node *, bool)> walk = [&](const Node *n, bool inblock) {
        if (!n) return;
        if (auto v = Is<VarDecl>(n)) {
            if (inblock && !v->isglobal && !v->byref && !v->isconst && !v->reusable &&
                !v->type && v->names.size() == 1 && v->inits.size() == 1 ) {
                int64_t value;
                if (ClosedInteger(v->inits[0], value)) {
                    function<void(const Node *)> choose = [&](const Node *n) {
                        auto extent = RefactorExtent(f, n);
                        if (extent.first == (int64_t)start && extent.second == (int64_t)end) {
                            found = v;
                            selected = n;
                        }
                        n->Children([&](Node *ch) { choose(ch); });
                    };
                    choose(v->inits[0]);
                }
            }
        }
        if (auto fd = Is<FnDecl>(n)) walk(fd->sf->body, false);
        else n->Children([&](Node *ch) { walk(ch, Is<Block>(n) != nullptr); });
    };
    for (auto d : loc.an->ast->topdecls) walk(d, false);
    if (!found || start >= end) return;
    for (auto &d : loc.an->diags) if (!d.warning) return;
    auto decl = f.text.OffsetOf(found->line.line, found->line.col);
    auto line = f.text.text.rfind('\n', decl);
    line = line == string::npos ? 0 : line + 1;
    auto indent = f.text.text.substr(line, decl - line);
    if (indent.find_first_not_of(" \t") != string::npos) return;
    string name = "extracted_value";
    while (f.text.text.find(name) != string::npos) name += "_";
    auto literal = f.text.text.substr(start, end - start);
    int64_t value;
    if (!ClosedInteger(selected, value) ||
        literal.find_first_not_of("0123456789 +-*()\t") != string::npos) return;
    auto uri = f.uri;
    auto it = ws.open.find(PathKey(PathOfUri(uri)));
    if (it == ws.open.end() || it->second.text != f.text.text) return;
    vector<RefactorEdit> edits { { (size_t)decl, (size_t)decl, cat("let ", name, " = ", literal, ";\n", indent) },
                                 { start, end, name } };
    if (CheckRefactor(ws, loc, edits))
        WriteRefactor(j, loc, it->second.version, "Extract integer initializer into variable", "refactor.extract", edits);
}

inline void InlineInteger(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return;
    auto range = params.Get("range");
    auto a = range ? range->Get("start") : nullptr;
    auto b = range ? range->Get("end") : nullptr;
    if (!a || !b) return;
    auto &f = *loc.fi;
    if (f.text.text.find('&') != string_view::npos) return;
    auto start = f.text.OffsetOfPosition(JsonInt(a->Get("line")), JsonInt(a->Get("character")));
    auto end = f.text.OffsetOfPosition(JsonInt(b->Get("line")), JsonInt(b->Get("character")));
    for (auto &d : loc.an->diags) if (!d.warning) return;
    const VarDecl *found = nullptr;
    function<void(const Node *)> walk = [&](const Node *n) {
        if (!n) return;
        if (auto v = Is<VarDecl>(n)) {
            if (!v->isglobal && !v->isvar && !v->byref && !v->isconst && !v->reusable &&
                !v->type && v->doc.empty() && v->names.size() == 1 && v->inits.size() == 1) {
                for (auto &occ : f.occs)
                    if (occ.node == n && occ.kind == Occurrence::LOCAL && occ.start == start &&
                        (end == start || end == occ.start + occ.len)) found = v;
            }
        }
        if (auto fd = Is<FnDecl>(n)) walk(fd->sf->body);
        else n->Children([&](Node *ch) { walk(ch); });
    };
    for (auto d : loc.an->ast->topdecls) walk(d);
    if (!found) return;
    int64_t value;
    if (!ClosedInteger(found->inits[0], value)) return;
    auto extent = RefactorExtent(f, found->inits[0]);
    if (extent.first < 0) return;
    auto expression = f.text.text.substr(extent.first, extent.second - extent.first);
    if (expression.find_first_not_of("0123456789 +-*()\t") != string::npos) return;
    auto decl = NodeExtent(f, found);
    if (decl.first < 0) return;
    auto finish = (size_t)decl.second;
    if (finish < f.text.text.size() && f.text.text[finish] == ';') ++finish;
    auto spelling = f.text.text.substr(decl.first, finish - decl.first);
    if (spelling.find("//") != string::npos || spelling.find("/*") != string::npos) return;
    vector<RefactorEdit> edits { { (size_t)decl.first, finish, "" } };
    for (auto &occ : f.occs) {
        if (occ.kind != Occurrence::IDENT) continue;
        auto target = DefinitionOf(loc, occ);
        if (!target.Known() || target.fileidx != f.fileidx || target.start != start) continue;
        if (occ.start >= (size_t)decl.first && occ.start < finish) continue;
        edits.push_back({occ.start, occ.start + occ.len, cat("((", expression, ") as i64)")});
    }
    if (edits.size() < 2) return;
    auto it = ws.open.find(PathKey(PathOfUri(f.uri)));
    if (it == ws.open.end() || it->second.text != f.text.text) return;
    if (CheckRefactor(ws, loc, edits))
        WriteRefactor(j, loc, it->second.version, "Inline immutable integer variable", "refactor.inline", edits);
}

#include "lsp_assists.h"
#include "lsp_workspace_assists.h"
#include "lsp_syntax_assists.h"

inline bool CodeActions(Workspace &ws, const Json &params, JsonOut &j) {
    if (!ws.indexComplete) { j.BeginArray(); j.EndArray(); return true; }
    j.BeginArray();
    SyntaxAssists(ws, params, j);
    auto context = params.Get("context");
    auto only = context ? context->Get("only") : nullptr;
    bool extract = !only;
    if (only && only->kind == Json::ARRAY)
        for (auto &kind : only->arr)
            if (kind.str == "refactor" || kind.str == "refactor.extract") extract = true;
    CatalogAssists(ws, params, j);
    WorkspaceSignatureAssists(ws, params, j);
    if (extract) ExtractLiteral(ws, params, j);
    bool inlining = !only;
    if (only && only->kind == Json::ARRAY)
        for (auto &kind : only->arr)
            if (kind.str == "refactor" || kind.str == "refactor.inline") inlining = true;
    if (inlining) InlineInteger(ws, params, j);
    bool requested = false;
    if (only && only->kind == Json::ARRAY)
        for (auto &kind : only->arr)
            if (kind.str == "source" || kind.str == "source.format") requested = true;
    if (requested) {
        JsonOut edits;
        string error;
        if (FormatDocument(ws, params, edits, error) == 1 && edits.s != "[]") {
            auto td = params.Get("textDocument");
            j.BeginObject();
            j.Member("title", "Format document with Goose");
            j.Member("kind", "source.format");
            j.Key("edit");
            j.BeginObject();
            j.Key("documentChanges");
            j.BeginArray();
            j.BeginObject();
            j.Key("textDocument");
            j.BeginObject();
            auto uri = JsonStr(td->Get("uri"));
            j.Member("uri", uri);
            auto &doc = ws.open.at(PathKey(PathOfUri(uri)));
            j.Member("version", doc.version);
            j.EndObject();
            j.Key("edits");
            j.Raw(edits.s);
            j.EndObject();
            j.EndArray();
            j.EndObject();
            j.EndObject();
        }
    }
    j.EndArray();
    return true;
}

}  // namespace goose
