// Goose compiler — the debug map that -g writes beside the C, as
// <stem>.debug.json (definitions of CodeGen members, codegen.h). A
// debugger reads the C's own debug information for addresses, lines and
// C types; this holds what that information cannot: the Goose name of each
// function instance and of each local and field, its Goose type (a
// resizable is a bare header in C, a variable-size value a bare pointer,
// T& and T? are one C pointer type), the line range a local is in scope
// over, and the fields of the structs and the variants of the enums those
// types name. Included after json.h, whose JsonQuote it writes with.
//
//   { "version": 1,
//     "globals": { "block": "gs_globals_t", "main": "gs_globals_main", "thread": null,
//                  "vars": [ { "c": "log_g", "name": "log", "type": "u8[>..]",
//                              "file": "/abs/prog.goose", "line": 2 } ] },
//     "functions": [ { "c": "area_g", "name": "geo::area", "file": "/abs/geo.goose",
//                      "line": 3, "end": 7, "bindings": { "T": "i64" }, "fnvals": { "F": "inc" },
//                      "locals": [ { "c": "w_g", "name": "w", "type": "i64",
//                                    "param": true, "line": 3, "end": 7 } ] } ],
//     "types": [ { "c": "Point_g", "name": "Point", "kind": "struct",
//                  "fields": [ { "c": "x_g", "name": "x", "type": "i32" } ] },
//                { "c": "Shape_g", "name": "Shape", "kind": "enum",
//                  "fixed": true, "variants": [ { "name": "Circle", "tag": 0,
//                  "fields": [ ... ] } ] } ] }
//
// "c" is the C spelling a debugger shows; "line" and "end" are the lines a
// local is in scope from and to, inclusive. A function's locals are what
// its body names, temporaries excluded: those have no Goose name.
#pragma once

namespace goose {

inline void CodeGen::MapFunction(FnSpec *sp, const string &cname) {
    auto sf = sp->sf;
    string s = "    { \"c\": ";
    JsonQuote(s, cname);
    s += ", \"name\": ";
    JsonQuote(s, sf->qname);
    s += ", \"file\": ";
    JsonQuote(s, SourcePath(sf->line.fileidx));
    Append(s, ", \"line\": ", sf->line.line, ", \"end\": ", sf->end.line);
    if (!sp->bindings.empty()) {
        s += ", \"bindings\": {";
        for (size_t i = 0; i < sp->bindings.size(); i++) {
            s += i ? ", " : " ";
            JsonQuote(s, sp->bindings[i].first);
            string t;
            sp->bindings[i].second->Dump(t);
            s += ": ";
            JsonQuote(s, t);
        }
        s += " }";
    }
    // What each function-value parameter is bound to: the named function's
    // qualified name, or "{fn line N}" for a block (a function value
    // written in place), so twice<inc> and twice<{...}> can be told apart.
    if (!sp->fnvals.empty()) {
        s += ", \"fnvals\": {";
        for (size_t i = 0; i < sp->fnvals.size(); i++) {
            auto &[g, b] = sp->fnvals[i];
            s += i ? ", " : " ";
            JsonQuote(s, g);
            s += ": ";
            if (b.named) JsonQuote(s, b.named->qname);
            else if (b.fv) JsonQuote(s, cat("{fn line ", b.fv->line.line, "}"));
            else s += "null";
        }
        s += " }";
    }
    s += ",\n      \"locals\": [";
    // In the order the C declares them, which is the order they were named.
    vector<pair<const VarDef *, string>> locals(vnames.begin(), vnames.end());
    sort(locals.begin(), locals.end(), [](auto &a, auto &b) {
        return a.first->line.line != b.first->line.line ? a.first->line.line < b.first->line.line
                                                        : a.second < b.second;
    });
    bool first = true;
    for (auto &[vd, c] : locals) {
        if (vd->isglobal || !vd->type || vd->name.empty()) continue;
        string t;
        vd->type->Dump(t);
        auto it = scopeends.find(vd);
        auto end = it != scopeends.end() ? it->second : sf->end.line;
        s += first ? "\n        { \"c\": " : ",\n        { \"c\": ";
        first = false;
        JsonQuote(s, c);
        s += ", \"name\": ";
        JsonQuote(s, vd->name);
        s += ", \"type\": ";
        JsonQuote(s, t);
        Append(s, ", \"param\": ", vd->isparam ? "true" : "false", ", \"line\": ",
               vd->line.line, ", \"end\": ", end, " }");
        MapType(vd->type);
    }
    s += first ? "] }" : " ] }";
    mapfns.emplace_back(cname, std::move(s));
}

// t's structs and enums, and those their fields name, into maptypes.
inline void CodeGen::MapType(TypeExpr *t) {
    if (!t) return;
    switch (t->kind) {
        case TY_SLICE: return MapType(t->sub);
        case TY_ARRAY: return MapType(t->arr->sub);
        case TY_REF: return MapType(t->ref->sub);
        case TY_VARIANT: return MapType(t->var->adt);
        case TY_STRUCT: case TY_ENUM: break;
        default: return;
    }
    string name;
    t->Dump(name);
    if (t->kind == TY_ENUM && t->enu->varmode && name.size() > 2 &&
        name.compare(name.size() - 2, 2, "..") == 0)
        name.resize(name.size() - 2);   // One entry for both modes.
    if (maptypes.count(name)) return;
    maptypes[name] = "";   // Taken: a type that holds itself through a reference ends here.
    // The "c" and "name" go in front at the end (DebugMap), when the C
    // names the program has are all known (mapcnames).
    string s;
    vector<TypeExpr *> inner;
    auto fields = [&](const vector<Field> &fs, const vector<TypeExpr *> &ftypes) {
        s += "\"fields\": [";
        bool first = true;
        for (size_t i = 0; i < fs.size(); i++) {
            if (fs[i].ispad || i >= ftypes.size() || !ftypes[i]) continue;
            string ft;
            ftypes[i]->Dump(ft);
            s += first ? " { \"c\": " : ", { \"c\": ";
            first = false;
            JsonQuote(s, Sanitize(fs[i].name));
            s += ", \"name\": ";
            JsonQuote(s, fs[i].name);
            s += ", \"type\": ";
            JsonQuote(s, ft);
            s += " }";
            inner.push_back(ftypes[i]);
        }
        s += first ? "]" : " ]";
    };
    if (t->kind == TY_STRUCT) {
        auto si = SI(t);
        Append(s, "\"kind\": \"struct\", \"variable\": ",
               si->sclass == SC_FIXED ? "false" : "true", ",\n      ");
        fields(si->st->fields, si->ftypes);
    } else {
        auto ei = EIOf(t);
        Append(s, "\"kind\": \"enum\", \"fixed\": ", ei->allfixed ? "true" : "false",
               ",\n      \"variants\": [");
        auto &vs = ei->en->variants;
        for (size_t v = 0; v < vs.size(); v++) {
            s += v ? ",\n        { \"name\": " : "\n        { \"name\": ";
            JsonQuote(s, vs[v].name);
            Append(s, ", \"tag\": ", v, ", ");
            fields(vs[v].fields, v < ei->vftypes.size() ? ei->vftypes[v] : vector<TypeExpr *>());
            s += " }";
        }
        s += vs.empty() ? "]" : " ]";
    }
    s += " }";
    maptypes[name] = s;
    for (auto ft : inner) MapType(ft);
}

inline string CodeGen::DebugMap() {
    // The globals are members of one block, gs_globals_t: the main
    // program's is gs_globals_main, and each thread's own is behind gs_gl
    // when the program starts threads.
    string gs;
    for (auto g : ast.globals) {
        for (auto d : g->defs) {
            auto it = gnames.find(d);
            if (it == gnames.end() || !d->type || it->second.compare(0, 7, "GS_GL->") != 0) continue;
            string t, q = g->ns.empty() ? string(d->name) : cat(g->ns, "::", d->name);
            d->type->Dump(t);
            gs += gs.empty() ? "\n    { \"c\": " : ",\n    { \"c\": ";
            JsonQuote(gs, it->second.substr(7));
            gs += ", \"name\": ";
            JsonQuote(gs, q);
            gs += ", \"type\": ";
            JsonQuote(gs, t);
            gs += ", \"file\": ";
            JsonQuote(gs, SourcePath(d->line.fileidx));
            Append(gs, ", \"line\": ", d->line.line, " }");
            MapType(d->type);
        }
    }
    string s = "{ \"version\": 1,\n  \"globals\": { \"block\": \"gs_globals_t\", "
               "\"main\": \"gs_globals_main\", \"thread\": ";
    s += usesthreads ? "\"gs_gl\"" : "null";
    Append(s, ", \"vars\": [", gs, gs.empty() ? "] },\n" : " ] },\n");
    s += "  \"functions\": [\n";
    for (size_t i = 0; i < mapfns.size(); i++) Append(s, i ? ",\n" : "", mapfns[i].second);
    s += "\n  ],\n  \"types\": [\n";
    bool first = true;
    for (auto &[name, obj] : maptypes) {
        if (obj.empty()) continue;
        auto it = mapcnames.find(name);
        s += first ? "    { \"c\": " : ",\n    { \"c\": ";
        first = false;
        JsonQuote(s, it != mapcnames.end() ? it->second : string());
        s += ", \"name\": ";
        JsonQuote(s, name);
        Append(s, ", ", obj);
    }
    s += "\n  ]\n}\n";
    return s;
}

}  // namespace goose
