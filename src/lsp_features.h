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
inline string DeclDetail(const SStruct *st) { return cat("struct ", st->qname, GenericsText(st->generics)); }
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
    if (!span || span->call->callee != occ.node) return nullptr;
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
    auto it = an.patternvariants.find(occ.node);
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

inline bool Definition(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc)) return false;
    auto &an = *loc.an;
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
        return lower.find(query) != string::npos;
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
    map<string, pair<const FileIndex *, set<pair<size_t, size_t>>>> edits;
    ReferencesTo(ws, loc, target, [&](const Analysis &, const FileIndex &f, const Occurrence &o) {
        auto &[file, places] = edits[f.uri];
        file = &f;
        places.insert({ o.start + (o.name.size() - LeafOf(o.name).size()), o.start + o.len });
    });
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
        WriteInlayHint(j, f.text, occ.start + occ.len, cat(": ", *types.begin()), IH_TYPE, false);
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
    };
    vector<Item> items;
    set<string> seen;

    void Add(string_view label, int kind, int group, string detail = {}, string_view doc = {},
             string_view insert = {}) {
        if (label.empty() || !seen.insert(string(label)).second) return;
        items.push_back({ string(label), std::move(detail), DocText(doc), string(insert),
                          cat(group, label), kind });
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
        if (!fns.empty()) out.Add(name(leaf), CK_FUNCTION, 1, FnHeader(fns[0]), fns[0]->doc, leaf);
    for (auto &[leaf, st] : ns.structmap) out.Add(name(leaf), CK_STRUCT, 1, DeclDetail(st), st->doc, leaf);
    for (auto &[leaf, en] : ns.enummap) out.Add(name(leaf), CK_ENUM, 1, DeclDetail(en), en->doc, leaf);
    for (auto &[leaf, al] : ns.aliasmap)
        out.Add(name(leaf), CK_TYPEPARAMETER, 1, DeclDetail(al), al->doc, leaf);
    for (auto &[leaf, vd] : ns.globalmap) {
        auto def = vd->defs.empty() ? nullptr : vd->defs[0];
        out.Add(name(leaf), vd->isvar ? CK_VARIABLE : CK_CONSTANT, 1,
                cat(VarKeyword(vd), leaf, def && def->type ? cat(": ", TypeText(def->type)) : string()),
                vd->doc, leaf);
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
                        out.Add(fd->sf->name, CK_FUNCTION, 0, FnHeader(fd->sf), fd->sf->doc);
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
}

// Whether a function's first parameter takes a value of a type, as a UFCS
// call `value.f()` passes it: untyped or generic, the type itself, or a
// reference to it.
inline bool FirstParamTakes(const TypeExpr *pt, const string &otext) {
    if (!pt || pt->kind == TY_GENERIC) return true;
    if (pt->kind == TY_REF && pt->ref->sub && pt->ref->sub->kind == TY_GENERIC) return true;
    auto ptext = TypeText(pt);
    return ptext == otext || ptext == cat("const ", otext) || ptext.rfind(otext + "&", 0) == 0 ||
           ptext.rfind(cat("const ", otext, "&"), 0) == 0;
}

// The members of the value before a `.`: the fields of its type, the
// functions that take it first (UFCS) and the builtin members of its kind;
// an enum's name offers its variants; a value whose type no instantiation
// gave offers every field and function.
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
    auto fields = [&](const vector<Field> &fs) {
        for (auto &fld : fs)
            if (!fld.ispad)
                out.Add(fld.name, CK_FIELD, 0,
                        cat(fld.isconst ? "let " : "", fld.name, ": ", TypeText(fld.type)), fld.doc);
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
            if (!sf->isnested && !sf->params.empty()) out.Add(sf->name, CK_METHOD, 1, FnHeader(sf), sf->doc);
        builtins(BR_ANY);
        return;
    }
    while (type->kind == TY_REF && type->ref->sub) type = type->ref->sub;
    if (type->kind == TY_STRUCT) fields(type->struc->st->fields);
    else if (type->kind == TY_VARIANT && type->var->variant) fields(type->var->variant->fields);
    auto otext = TypeText(type);
    for (auto sf : ast.functions)
        if (!sf->isnested && !sf->params.empty() && FirstParamTakes(sf->params[0].type, otext))
            out.Add(sf->name, CK_METHOD, 1, FnHeader(sf), sf->doc);
    builtins(RecvKindOf(type));
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
    if (start > 0 && text[start - 1] == '.' && !(start > 1 && text[start - 2] == '.')) {
        MemberCompletions(loc, start - 1, out);
    } else if (start > 1 && text[start - 1] == ':' && text[start - 2] == ':') {
        auto nsend = start - 2;
        auto nsstart = nsend;
        while (nsstart > 0 && IsIdentCont(text[nsstart - 1])) nsstart--;
        auto nsname = text.substr(nsstart, nsend - nsstart);
        if (auto ns = loc.an->ast->FindNS(nsname)) NamespaceCompletions(nsname, *ns, false, out);
    } else {
        ScopeCompletions(loc, offset, out);
    }
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
inline int FormatDocument(Workspace &ws, const Json &params, JsonOut &j, string &error) {
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
        place("start", c.start ? c.start + bom : 0);
        place("end", c.end + bom);
        j.EndObject();
        j.Member("newText", c.text);
        j.EndObject();
    }
    j.EndArray();
    return 1;
}

}  // namespace goose
