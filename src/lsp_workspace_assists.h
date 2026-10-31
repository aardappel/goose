inline bool WorkspacePlanCheck(Workspace &ws, const map<string,vector<RefactorEdit>> &plan) {
    map<string,string> texts;
    auto shifted = [&](const string &uri,size_t pos) {
        int64_t delta=0;
        auto it=plan.find(uri);
        if(it!=plan.end()) for(auto &e:it->second) if(e.end<=pos) delta+=(int64_t)e.text.size()-(e.end-e.start);
        return (size_t)((int64_t)pos+delta);
    };
    auto removed = [&](const string &uri,size_t pos) {
        auto it=plan.find(uri);
        if(it!=plan.end())for(auto &e:it->second)if(e.start<=pos&&pos<e.end)return true;
        return false;
    };
    for(auto &[uri,edits]:plan){
        auto doc=ws.open.find(PathKey(PathOfUri(uri)));
        if(doc==ws.open.end())return false;
        auto ordered=edits;sort(ordered.begin(),ordered.end(),[](auto &a,auto &b){return a.start<b.start;});
        auto text=doc->second.text;
        for(auto it=ordered.rbegin();it!=ordered.rend();++it)text.replace(it->start,it->end-it->start,it->text);
        texts[PathKey(PathOfUri(uri))]=text;
    }
    for(auto &root:ws.roots)if(root->analysis){
        auto &before=*root->analysis;
        bool affected=false;
        for(auto &file:before.files)if(plan.count(file.uri))affected=true;
        if(!affected)continue;
        for(auto &d:before.diags)if(!d.warning)return false;
        Analysis after;after.ast=make_unique<Ast>();auto &ast=*after.ast;ast.exemplars=true;
        bool failed=false;ast.sink=[&](const Diagnostic &d){if(!d.warning)failed=true;};
        SourceProvider source=[&](const string &p)->const string *{auto it=texts.find(PathKey(p));return it==texts.end()?ws.TextOf(p):&it->second;};
        try{RunOnCompilerStack([&]()->int{ParseProgram(ast,root->path,ws.stdlibdirs,DirOf(ws.exe),source);ResolveTypeNames(ast);after.IndexSource();TypeCheckProgram(ast,false,true);after.Build();return 0;});}catch(CompileError &){return false;}
        if(failed||!ast.errors.empty())return false;
        for(auto &file:before.files){
            int idx=after.FileOf(PathKey(PathOfUri(file.uri)));if(idx<0)return false;
            auto &updated=after.files[(size_t)idx];Located oldloc{&before,&file,0},newloc{&after,&updated,0};
            for(auto &occ:file.occs){
                if(removed(file.uri,occ.start))continue;
                auto next=updated.At(shifted(file.uri,occ.start));if(!next||next->kind!=occ.kind)return false;
                auto a=DefinitionOf(oldloc,occ),b=DefinitionOf(newloc,*next);
                if(a.Known()!=b.Known())return false;
                if(!a.Known())continue;
                auto &af=before.files[(size_t)a.fileidx];auto &bf=after.files[(size_t)b.fileidx];
                if(af.uri!=bf.uri)return false;
                if(removed(af.uri,a.start)){
                    auto decl=bf.At(b.start);
                    if(!decl||!decl->IsDeclaration()||decl->name!=occ.name)return false;
                }else if(shifted(af.uri,a.start)!=b.start)return false;
            }
        }
    }
    return true;
}

inline bool WorkspaceCallableCheck(Workspace &ws, const Located &loc, vector<RefactorEdit> edits, bool movedIdentities, const map<size_t, string> &rebound, const Node *oldCallable, string_view newName, bool importRemoval = false) {
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
        if (importRemoval) {
            if (before.ast->globals.size() != ast.globals.size()) return false;
            for (size_t i = 0; i < ast.globals.size(); i++) {
                auto a = before.ast->globals[i], b = ast.globals[i];
                if (before.ast->sources[a->line.fileidx].first != ast.sources[b->line.fileidx].first ||
                    a->line.line != b->line.line || a->line.col != b->line.col) return false;
            }
        }
        for (auto &file : before.files) {
            auto idx = after.FileOf(PathKey(PathOfUri(file.uri)));
            if (idx < 0) {
                if (!importRemoval) return false;
                for (auto g : before.ast->globals)
                    if (g->line.fileidx == file.fileidx) return false;
                continue;
            }
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
                if (a.Known() != b.Known()) {
                    auto matching = [&](const Located &at, const Occurrence &o, bool old) {
                        if (!o.callee || o.kind != Occurrence::IDENT) return false;
                        auto span = at.fi->CallAt(o.start);
                        if (!span) return false;
                        auto check = [&](const Node *node) {
                            auto call = Is<Call>(node);
                            if (!call) return false;
                            auto sf = CalleeOf(call);
                            if (!old) return newName.empty() ? call->fvbody != nullptr : sf && sf->name == newName;
                            auto body = call->fvbody ? call->fvbody : sf ? sf->body : nullptr;
                            auto original = Is<FunVal>(oldCallable);
                            auto fd = Is<FnDecl>(oldCallable);
                            auto expected = original ? original->body : fd ? fd->sf->body : nullptr;
                            return body && expected && body->line.fileidx == expected->line.fileidx &&
                                body->line.line == expected->line.line && body->line.col == expected->line.col;
                        };
                        auto clones = at.an->clones.find(span->call);
                        if (clones == at.an->clones.end()) return check(span->call);
                        if (clones->second.empty()) return false;
                        for (auto node : clones->second) if (!check(node)) return false;
                        return true;
                    };
                    if (matching(oldloc, occ, true) && matching(newloc, *next, false)) continue;
                    return false;
                }
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

inline void WorkspaceSignatureAssists(Workspace &ws, const Json &params, JsonOut &j) {
    Located loc;
    if (!Locate(ws, params, loc, false)) return;
    auto &f = *loc.fi;
    auto doc = ws.open.find(PathKey(PathOfUri(f.uri)));
    if (doc == ws.open.end() || doc->second.text != f.text.text) return;
    auto range = params.Get("range");
    auto a = range ? range->Get("start") : nullptr;
    auto b = range ? range->Get("end") : nullptr;
    if (!a || !b) return;
    auto start = f.text.OffsetOfPosition(JsonInt(a->Get("line")), JsonInt(a->Get("character")));
    auto end = f.text.OffsetOfPosition(JsonInt(b->Get("line")), JsonInt(b->Get("character")));
    if (start > end || end > f.text.text.size()) return;
    auto context = params.Get("context");
    auto only = context ? context->Get("only") : nullptr;
    auto wanted = [&](string_view kind) {
        if (!only) return true;
        for (auto &k : only->arr)
            if (kind == k.str || (kind.size() > k.str.size() &&
                kind.substr(0, k.str.size()) == k.str && kind[k.str.size()] == '.')) return true;
        return false;
    };
    const Node *callableSource = nullptr;
    string callableName;
    auto emit = [&](string_view title, string_view kind, vector<RefactorEdit> edits) {
        if (wanted(kind) && (callableSource ? WorkspaceCallableCheck(ws, loc, edits, true, {}, callableSource, callableName) : CheckRefactor(ws, loc, edits, true)))
            WriteRefactor(j, loc, doc->second.version, title, kind, edits);
    };
    for (auto &fn : FnExtents(*loc.an)) {
        auto sf = fn.fd->sf;
        if (fn.fd->line.fileidx != f.fileidx || sf->isnested ||
            start != fn.start || end != fn.end || !sf->generics.empty() ||
            sf->isextern || sf->isexport) continue;
        auto fresh = [&](string name) {
            while (f.text.text.find(name) != string::npos) name += "_";
            return name;
        };
        if (ws.createFileSupport && wanted("refactor.extract")) {
            bool pure = !sf->isrec;
            for (auto &occ : f.occs) if (occ.start >= start && occ.start < end) {
                auto def = DefinitionOf(loc, occ);
                if (def.Known() && !(def.fileidx == f.fileidx &&
                    def.start >= start && def.start < end)) pure = false;
            }
            auto leaf = cat(sf->name, "_extracted");
            auto path = cat(DirOf(PathOfUri(f.uri)), leaf, ".goose");
            if (pure && !FileExists(path) && !ws.open.count(PathKey(path))) {
                string content = sf->ns.empty() ? "" : cat("namespace ", sf->ns, ";\n");
                content += string(f.text.text.substr(start, end - start)) + "\n";
                string original(f.text.text);
                original.replace(start, end - start, "");
                original += cat("\nimport .", leaf, ";\n");
                bool valid = true;
                for (auto &root : ws.roots) {
                    if (!root->analysis || root->analysis->FileOf(PathKey(PathOfUri(f.uri))) < 0) continue;
                    Ast checked; checked.exemplars = true;
                    checked.sink = [&](const Diagnostic &d) { if (!d.warning) valid = false; };
                    SourceProvider provider = [&](const string &p) -> const string * {
                        if (PathKey(p) == PathKey(path)) return &content;
                        if (PathKey(p) == PathKey(PathOfUri(f.uri))) return &original;
                        return ws.TextOf(p);
                    };
                    try { RunOnCompilerStack([&]() -> int {
                        ParseProgram(checked, root->path, ws.stdlibdirs, DirOf(ws.exe), provider);
                        ResolveTypeNames(checked); TypeCheckProgram(checked, false, true); return 0;
                    }); } catch (CompileError &) { valid = false; }
                    if (!checked.errors.empty()) valid = false;
                }
                if (valid) {
                    j.BeginObject(); j.Member("title", "Extract declaration to new file");
                    j.Member("kind", "refactor.extract"); j.Key("edit"); j.BeginObject();
                    j.Key("documentChanges"); j.BeginArray();
                    j.BeginObject(); j.Member("kind", "create"); j.Member("uri", UriOfPath(path));
                    j.Key("options"); j.BeginObject(); j.Member("overwrite", false);
                    j.Member("ignoreIfExists", false); j.EndObject(); j.EndObject();
                    j.BeginObject(); j.Key("textDocument"); j.BeginObject();
                    j.Member("uri", UriOfPath(path)); j.Key("version"); j.Null(); j.EndObject();
                    j.Key("edits"); j.BeginArray(); j.BeginObject(); j.Key("range");
                    WriteRange(j, f.text, 0, 0); j.Member("newText", content);
                    j.EndObject(); j.EndArray(); j.EndObject();
                    j.BeginObject(); j.Key("textDocument"); j.BeginObject();
                    j.Member("uri", f.uri); j.Member("version", doc->second.version); j.EndObject();
                    j.Key("edits"); j.BeginArray();
                    for (auto e : vector<RefactorEdit>{{start,end,""},{f.text.text.size(),f.text.text.size(),cat("\nimport .",leaf,";\n")}}) {
                        j.BeginObject(); j.Key("range"); WriteRange(j,f.text,e.start,e.end);
                        j.Member("newText",e.text); j.EndObject();
                    }
                    j.EndArray(); j.EndObject(); j.EndArray(); j.EndObject(); j.EndObject();
                }
            }
        }
        if (sf->rets.size() < 2 || !sf->has_rets || !sf->body) continue;
        bool valid = true;
        for (auto t : sf->rets) {
            bool nominal = false;
            for (auto st : loc.an->ast->structs)
                if (st->generics.empty() && (TypeText(t) == st->qname ||
                    (st->ns == sf->ns && TypeText(t) == st->name))) nominal = true;
            if (set<string>{"i8","i16","i32","i64","u8","u16","u32","u64","f32","f64","bool"}.count(TypeText(t)) == 0 &&
                !nominal) valid = false;
        }
        string name = fresh("ReturnValues"), temp = fresh("return_values");
        vector<RefactorEdit> edits;
        auto body = RefactorExtent(f, sf->body);
        auto arrow = f.text.text.find("->", start);
        if (!valid || body.first < 0 || arrow >= (size_t)body.first) continue;
        edits.push_back({arrow, (size_t)body.first, cat("-> ", name, " ")});
        vector<string> fields;
        string structure = cat("struct ", name, " { ");
        for (size_t i = 0; i < sf->rets.size(); ++i) {
            fields.push_back(i == 0 ? "first" : i == 1 ? "second" : cat("result_", i + 1));
            if (i) structure += ", ";
            structure += cat(fields.back(), ": ", TypeText(sf->rets[i]));
        }
        edits.push_back({start, start, structure + " }\n"});
        size_t returns = 0;
        function<void(const Node *)> rewrite = [&](const Node *n) {
            if (!n) return;
            if (Is<FnDecl>(n) || Is<FunVal>(n)) { valid = false; return; }
            if (auto r = Is<Return>(n)) {
                if (r->vals.size() != fields.size() || !r->from.empty()) { valid=false; return; }
                string value = cat(name, " { ");
                int begin = -1, finish = -1;
                for (size_t i = 0; i < r->vals.size(); ++i) {
                    auto e = RefactorExtent(f, r->vals[i]);
                    if (e.first < 0) { valid=false; return; }
                    if (!i) begin = e.first;
                    finish = e.second;
                    if (i) value += ", ";
                    value += cat(fields[i], ": ", f.text.text.substr(e.first, e.second-e.first));
                }
                edits.push_back({(size_t)begin, (size_t)finish, value + " }"});
                returns++;
            }
            n->Children([&](Node *ch){rewrite(ch);});
        };
        rewrite(sf->body);
        auto declaration = PlaceOfFn(*loc.an,sf);
        map<string,vector<RefactorEdit>> plan;
        set<pair<string,size_t>> calls;
        for(auto &file:loc.an->files) for(auto &occ:file.occs) {
            Located at{loc.an,&file,occ.start}; auto d=DefinitionOf(at,occ);
            if(!d.Known()||d.fileidx!=declaration.fileidx||d.start!=declaration.start||occ.IsDeclaration())continue;
            if(!occ.callee){valid=false;continue;}
            auto call=file.CallAt(occ.start);
            if(!call){valid=false;continue;}
            auto ce=RefactorExtent(file,call->call);
            if(ce.first<0||!calls.insert({file.uri,(size_t)ce.first}).second)continue;
            size_t lhs=ce.first;
            while(lhs && file.text.text[lhs-1]!=';' && file.text.text[lhs-1]!='{' && file.text.text[lhs-1]!='}')lhs--;
            string prefix(file.text.text.substr(lhs,ce.first-lhs));
            auto eq=prefix.find('='); auto comma=prefix.find(',');
            if(eq==string::npos||comma==string::npos||comma>eq){valid=false;continue;}
            auto trim=[](string s){while(!s.empty()&&isspace((unsigned char)s.back()))s.pop_back();size_t i=0;while(i<s.size()&&isspace((unsigned char)s[i]))i++;return s.substr(i);};
            string bindings = trim(prefix.substr(0, eq)), keyword;
            if(bindings.substr(0,4)=="let "||bindings.substr(0,4)=="var ") {
                keyword=bindings.substr(0,4); bindings=trim(bindings.substr(4));
            }
            vector<string> names;
            size_t offset=0;
            while (offset <= bindings.size()) {
                auto end=bindings.find(',',offset);
                names.push_back(trim(bindings.substr(offset,end==string::npos ? end : end-offset)));
                if(end==string::npos)break;
                offset=end+1;
            }
            if(names.size()!=fields.size()){valid=false;continue;}
            for(auto &n:names) {
                if(n.empty() || !IsIdentStart(n[0])) valid=false;
                for(auto c:n)if(!IsIdentCont(c))valid=false;
            }
            while(file.text.text.find(temp)!=string::npos)temp+="_";
            string replacement=cat(" let ",temp," = ",file.text.text.substr(ce.first,ce.second-ce.first));
            for(size_t i=0;i<names.size();++i)
                replacement+=cat("; ",keyword,names[i]," = ",temp,".",fields[i]);
            plan[file.uri].push_back({lhs,(size_t)ce.second,replacement});
            temp += "_";
        }

        if(valid && returns && !calls.empty() && wanted("refactor.rewrite")) {
            plan[f.uri].insert(plan[f.uri].end(),edits.begin(),edits.end());
            if(WorkspacePlanCheck(ws,plan)) {
                j.BeginObject();j.Member("title","Convert multiple returns to named struct");j.Member("kind","refactor.rewrite");
                j.Key("edit");j.BeginObject();j.Key("documentChanges");j.BeginArray();
                for(auto &[uri,changes]:plan){
                    auto file=loc.an->FileOf(PathKey(PathOfUri(uri)));
                    if(file<0)continue;
                    auto &index=loc.an->files[(size_t)file];
                    j.BeginObject();j.Key("textDocument");j.BeginObject();j.Member("uri",uri);
                    j.Member("version",ws.open.at(PathKey(PathOfUri(uri))).version);j.EndObject();
                    j.Key("edits");j.BeginArray();
                    for(auto &e:changes){j.BeginObject();j.Key("range");WriteRange(j,index.text,e.start,e.end);j.Member("newText",e.text);j.EndObject();}
                    j.EndArray();j.EndObject();
                }
                j.EndArray();j.EndObject();j.EndObject();
            }
        }
    }
    for (auto &imp : loc.an->ast->imports) {
        auto e = ExtentIn(f, imp.line, imp.end);
        if (e.first < 0 || start < (size_t)e.first || end > (size_t)e.second) continue;
        vector<RefactorEdit> edits {{(size_t)e.first, (size_t)e.second, ""}};
        if (wanted("source.organizeImports") &&
            WorkspaceCallableCheck(ws, loc, edits, false, {}, nullptr, {}, true))
            WriteRefactor(j, loc, doc->second.version, "Remove unused import", "source.organizeImports", edits);
    }
    vector<const VarDecl *> bindings;
    function<void(const Node *)> collect = [&](const Node *n) {
        if (!n) return;
        if (auto v = Is<VarDecl>(n); v && v->line.fileidx == f.fileidx) bindings.push_back(v);
        if (auto fd = Is<FnDecl>(n)) collect(fd->sf->body);
        else n->Children([&](Node *ch) { collect(ch); });
    };
    for (auto d : loc.an->ast->topdecls) collect(d);
    for (auto v : bindings) {
        if (!v || v->isglobal || v->names.size() != 1 ||
            v->inits.size() != 1 || v->byref) continue;
        auto e = RefactorExtent(f, v);
        if (e.second >= 0 && (size_t)e.second < f.text.text.size() && f.text.text[e.second] == ';') e.second++;
        if (e.first < 0 || start < (size_t)e.first || end > (size_t)e.second) continue;
        TypeExpr *t = nullptr;
        set<string> types;
        auto typed = [&](const Node *n) {
            auto d = Is<VarDecl>(n);
            if (!d || d->defs.size() != 1 || !d->defs[0]->type) return;
            t = d->defs[0]->type;
            types.insert(TypeText(t));
        };
        typed(v);
        auto clones = loc.an->clones.find(v);
        if (clones != loc.an->clones.end()) for (auto n : clones->second) typed(n);
        if (types.size() != 1) continue;
        if (!t || t->kind != TY_STRUCT || !t->struc->st->generics.empty()) continue;
        string aliases;
        set<string> used;
        for (auto &field : t->struc->st->fields) {
            string name(field.name);
            while (f.text.text.find(name) != string::npos || used.count(name)) name += "_";
            used.insert(name);
            aliases += cat(" let ", name, " .= ", v->names[0], ".", field.name, ";");
        }
        if (!aliases.empty()) emit("Destructure struct binding with field references", "refactor.rewrite",
            {{(size_t)e.second, (size_t)e.second, aliases}});
    }
    auto tokenStart = start, tokenEnd = start;
    while (tokenStart && IsIdentCont(f.text.text[tokenStart - 1])) tokenStart--;
    while (tokenEnd < f.text.text.size() && IsIdentCont(f.text.text[tokenEnd])) tokenEnd++;
    if (end <= tokenEnd && tokenStart < tokenEnd &&
        (tokenStart == 0 || f.text.text[tokenStart - 1] != ':')) {
        auto name = f.text.text.substr(tokenStart, tokenEnd - tokenStart);
        auto selected = f.At(tokenStart);
        bool declarationSelected = selected && selected->IsDeclaration();
        auto previous = selected ? DefinitionOf(loc, *selected) : Place{};
        set<string> names;
        for (auto &root : ws.roots) if (root->analysis)
            for (auto sf : root->analysis->ast->functions)
                if (!declarationSelected && !sf->isnested && !sf->ns.empty() && sf->name == name) {
                    auto candidate = PlaceOfFn(*root->analysis, sf);
                    if (previous.Known() && (!candidate.Known() ||
                        loc.an->files[(size_t)previous.fileidx].uri !=
                            root->analysis->files[(size_t)candidate.fileidx].uri ||
                        previous.start != candidate.start)) continue;
                    names.insert(string(sf->qname));
                }
        for (auto &qualified : names) {
            vector<string> requirements {qualified};
            Completions unused;
            string imports;
            bool success = false;
            ImportCompletions(ws, loc, tokenStart, tokenEnd, unused,
                              &requirements, qualified, &imports, &success);
            if (!success) continue;
            string kind = imports.empty() ? "refactor.rewrite" : "quickfix";
            if (!wanted(kind)) continue;
            vector<RefactorEdit> edits {{tokenStart, tokenEnd, qualified}};
            if (!imports.empty()) edits.push_back({f.text.text.size(), f.text.text.size(), imports});
            WriteRefactor(j, loc, doc->second.version,
                imports.empty() ? cat("Qualify path as ", qualified) :
                cat("Import and qualify ", qualified, " (initializes module)"), kind, edits);
        }
    }
    for (auto sf : loc.an->ast->functions) for (auto &generic : sf->generics) {
        auto t = generic.bound;
        if (!t || t->kind != TY_FN || t->line.fileidx != f.fileidx) continue;
        auto offset = f.text.OffsetOf(t->line.line, t->line.col);
        auto spelling = TypeText(t);
        if (offset < 0 || start != (size_t)offset || end != start + spelling.size() ||
            f.text.text.substr(start, end - start) != spelling) continue;
        string fresh = "CallableSignature";
        while (f.text.text.find(fresh) != string::npos) fresh += "_";
        emit("Generate function type alias", "refactor.extract",
            {{0, 0, cat("type ", fresh, " = ", spelling, ";\n")}, {start, end, fresh}});
    }
    auto hasReturn = [&](const Node *body) {
        bool found = false;
        function<void(const Node *)> scan = [&](const Node *n) {
            if (!n) return;
            if (Is<Return>(n)) found = true;
            n->Children([&](Node *ch) { scan(ch); });
        };
        scan(body);
        return found;
    };
    auto functions = FnExtents(*loc.an);
    for (auto &fn : functions) {
        auto sf = fn.fd->sf;
        if (!sf->isnested || fn.start != start || fn.end != end || !sf->generics.empty() ||
            sf->isrec || sf->isinline || sf->has_rets || hasReturn(sf->body)) continue;
        string parameters;
        bool valid = true;
        for (auto &p : sf->params) {
            if (p.defaultval || p.isvar) valid = false;
            if (!parameters.empty()) parameters += ", ";
            parameters += string(p.name);
            if (p.type) parameters += cat(": ", TypeText(p.type));
        }
        auto declaration = PlaceOfFn(*loc.an, sf);
        size_t uses = 0;
        for (auto &file : loc.an->files) for (auto &occ : file.occs) {
            Located at {loc.an, &file, occ.start};
            auto def = DefinitionOf(at, occ);
            if (occ.name == sf->name && def.Known() && def.fileidx == declaration.fileidx && def.start == declaration.start &&
                !(file.uri == f.uri && occ.start >= start && occ.start < end)) uses++;
        }
        if (!valid || uses != 1) continue;
        function<void(const Node *)> convert = [&](const Node *n) {
            if (!n) return;
            if (auto call = Is<Call>(n); call && !call->trailing && !call->args.empty()) {
                auto arg = call->args.back();
                auto ae = RefactorExtent(f, arg), ce = RefactorExtent(f, call);
                if (ae.first >= 0 && ce.first > (int64_t)end && Is<Ident>(arg)) {
                    auto occ = f.At(ae.first);
                    auto def = occ ? DefinitionOf(loc, *occ) : decltype(declaration){};
                    if (def.Known() && def.fileidx == declaration.fileidx && def.start == declaration.start) {
                        auto body = RefactorExtent(f, sf->body);
                        if (body.first >= 0) {
                            auto replacement = string(f.text.text.substr(ce.first, ae.first - ce.first));
                            while (!replacement.empty() && isspace((unsigned char)replacement.back())) replacement.pop_back();
                            if (replacement.back() == ',') replacement.pop_back();
                            auto bodyText = string(f.text.text.substr(body.first + 1, body.second - body.first - 2));
                            replacement += cat(") { ", parameters, " => ", bodyText, " }");
                            callableSource = fn.fd; callableName.clear();
                            emit("Convert nested function to closure", "refactor.rewrite",
                                {{start, end, ""}, {(size_t)ce.first, (size_t)ce.second, replacement}});
                        }
                    }
                }
            }
            n->Children([&](Node *ch) { convert(ch); });
        };
        convert(sf->outer->body);
    }
    function<void(const Node *)> walk = [&](const Node *n) {
        if (!n || n->line.fileidx != loc.an->FileOf(PathKey(PathOfUri(f.uri)))) return;
        if (auto call = Is<Call>(n); call && call->trailing) {
            auto fv = call->trailing;
            auto e = RefactorExtent(f, fv);
            auto ce = RefactorExtent(f, call);
            if (e.first >= 0 && ce.first >= 0 && start == (size_t)e.first && end == (size_t)e.second &&
                fv->explicit_params && !hasReturn(fv->body)) {
                auto body = RefactorExtent(f, fv->body);
                string params;
                bool valid = body.first >= 0;
                for (auto &p : fv->params) {
                    if (!p.type || p.defaultval || p.isvar) { valid = false; break; }
                    if (!params.empty()) params += ", ";
                    params += cat(p.name, ": ", TypeText(p.type));
                }
                if (valid) {
                    string fresh = "extracted_callable";
                    while (f.text.text.find(fresh) != string::npos) fresh += "_";
                    auto bodyText = string(f.text.text.substr(body.first, body.second - body.first));
                    auto arrow = bodyText.find("=>");
                    if (arrow != string::npos) bodyText = cat("{", bodyText.substr(arrow + 2));
                    auto prefix = string(f.text.text.substr(ce.first, e.first - ce.first));
                    auto close = prefix.rfind(')');
                    if (close != string::npos) {
                        prefix.insert(close, cat(call->args.empty() ? "" : ", ", fresh));
                        callableSource = fv; callableName = fresh;
                        emit("Convert closure to nested function", "refactor.rewrite",
                            {{(size_t)ce.first, (size_t)e.second,
                              cat("{ fn ", fresh, "(", params, ") ", bodyText, "\n", prefix, " }")}});
                    }
                }
            }
        }

    };
    for (auto &span : f.spans) walk(span.node);
}
