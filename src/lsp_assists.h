// Included inside lsp_features after the checked-edit helpers.
inline bool AssistValue(const Node *n) {
    if (Is<IntLit>(n) || Is<FltLit>(n) || Is<StrLit>(n) || Is<BoolLit>(n)) return true;
    if (auto u = Is<Unary>(n))
        return (u->op == T_MINUS || u->op == T_NOT || u->op == T_BITNOT) && AssistValue(u->child);
    if (Is<ArrayLit>(n) || Is<StructLit>(n)) {
        bool valid = true;
        n->Children([&](Node *ch) { if (!AssistValue(ch)) valid = false; });
        return valid;
    }
    return false;
}

inline bool AssistIntegerSyntax(const Node *n) {
    if (Is<IntLit>(n)) return true;
    if (auto u = Is<Unary>(n)) return AssistIntegerSyntax(u->child);
    if (auto b = Is<Binary>(n)) return AssistIntegerSyntax(b->left) && AssistIntegerSyntax(b->right);
    return false;
}

inline void CatalogAssists(Workspace &ws, const Json &params, JsonOut &j) {
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
    if (start > end) return;
    auto context = params.Get("context");
    auto only = context ? context->Get("only") : nullptr;
    auto wanted = [&](string_view kind) {
        if (!only) return true;
        for (auto &k : only->arr)
            if (kind == k.str || (kind.size() > k.str.size() &&
                kind.substr(0, k.str.size()) == k.str && kind[k.str.size()] == '.')) return true;
        return false;
    };
    auto emit = [&](string_view title, string_view kind, vector<RefactorEdit> edits) {
        if (wanted(kind) && CheckRefactor(ws, loc, edits, true))
            WriteRefactor(j, loc, doc->second.version, title, kind, edits);
    };
    auto extent = [&](const Node *n) { return RefactorExtent(f, n); };
    auto text = [&](const Node *n) -> string {
        auto e = extent(n);
        if (e.first < 0 || e.second <= e.first || (size_t)e.second > f.text.text.size()) return {};
        return string(f.text.text.substr(e.first, e.second - e.first));
    };
    auto selected = [&](const Node *n) {
        auto e = extent(n);
        return e.first == (int64_t)start && e.second == (int64_t)end;
    };
    auto replace = [&](const Node *n, string_view title, string replacement) {
        auto e = extent(n);
        if (e.first >= 0 && e.second > e.first)
            emit(title, "refactor.rewrite", {{(size_t)e.first, (size_t)e.second, replacement}});
    };
    // Alias edits are confined to concrete scalar names in this document.
    auto scalar = [&](TypeExpr *t) {
        if (!t) return false;
        auto name = TypeText(t);
        return name == "i64" || name == "f64" || name == "bool" || name == "string";
    };
    auto valueType = [&](TypeExpr *t) {
        if (!t || t->kind == TY_REF) return false;
        if (t->kind == TY_SLICE) return IsU8(t->sub);
        auto name = TypeText(t);
        return name.find('?') == string::npos && name.find('&') == string::npos &&
               t->kind != TY_VOID && t->kind != TY_FN;
    };
    auto checkedType = [&](const Node *node) {
        if (auto call = Is<Call>(node)) {
            auto resolved = SpecializedCall(*loc.an, call);
            if (resolved && resolved->exprtype) return resolved->exprtype;
        }
        return loc.an->TypeOf(node);
    };
    for (auto &occ : f.occs) {
        if (occ.start != start || (end != start && end != start + occ.len)) continue;
        if (occ.kind == Occurrence::TYPE && valueType(occ.type)) {
            string name = "ExtractedType";
            while (f.text.text.find(name) != string::npos) name += "_";
            emit("Extract type alias", "refactor.extract",
                {{0, 0, cat("type ", name, " = ", TypeText(occ.type), ";\n")},
                 {start, start + occ.len, name}});
        }
        if (occ.kind == Occurrence::ALIAS && valueType(occ.al->type)) {
            auto al = occ.al;
            auto begin = f.text.OffsetOf(al->line.line, al->line.col);
            auto finish = f.text.OffsetOf(al->end.line, al->end.col);
            if (begin < 0 || finish <= begin) continue;
            map<string, pair<const FileIndex *, vector<RefactorEdit>>> changes;
            changes[f.uri] = {&f, {{(size_t)begin, (size_t)finish, ""}}};
            bool valid = true;
            for (auto &root : ws.roots) {
                if (!root->analysis) continue;
                auto &an = *root->analysis;
                for (auto type : an.ast->alltypes) {
                    auto target = type->sourcealias;
                    if (!target || target->line.fileidx < 0) continue;
                    auto &decl = an.files[(size_t)target->line.fileidx];
                    if (decl.uri != f.uri || target->qname != al->qname ||
                        target->line.line != al->line.line || target->line.col != al->line.col) continue;
                    if (type->line.fileidx < 0) { valid = false; continue; }
                    auto &file = an.files[(size_t)type->line.fileidx];
                    auto at = file.text.OffsetOf(type->line.line, type->line.col);
                    if (at < 0) { valid = false; continue; }
                    if (file.uri == f.uri && at >= begin && at < finish) continue;
                    auto len = file.text.NameLengthAt((size_t)at);
                    if (!len) { valid = false; continue; }
                    auto [it, added] = changes.emplace(file.uri, make_pair(&file, vector<RefactorEdit>{}));
                    if (it->second.first->text.text != file.text.text) valid = false;
                    auto &edits = it->second.second;
                    bool duplicate = false;
                    for (auto &edit : edits) if (edit.start == (size_t)at) duplicate = true;
                    if (!duplicate) edits.push_back({(size_t)at, (size_t)at + len, cat("(", TypeText(al->type), ")")});
                }
            }
            for (auto &[uri, change] : changes)
                sort(change.second.begin(), change.second.end(), [](auto &a, auto &b) { return a.start < b.start; });
            if (valid && wanted("refactor.inline") && CheckWorkspaceRefactor(ws, changes)) {
                j.BeginObject(); j.Member("title", "Inline type alias"); j.Member("kind", "refactor.inline");
                j.Key("edit"); j.BeginObject(); j.Key("documentChanges"); j.BeginArray();
                for (auto &[uri, change] : changes) {
                    j.BeginObject(); j.Key("textDocument"); j.BeginObject(); j.Member("uri", uri);
                    j.Key("version");
                    auto open = ws.open.find(PathKey(PathOfUri(uri)));
                    if (open == ws.open.end()) j.Null(); else j.Int(open->second.version);
                    j.EndObject(); j.Key("edits"); j.BeginArray();
                    for (auto &edit : change.second) {
                        j.BeginObject(); j.Key("range"); WriteRange(j, change.first->text, edit.start, edit.end);
                        j.Member("newText", edit.text); j.EndObject();
                    }
                    j.EndArray(); j.EndObject();
                }
                j.EndArray(); j.EndObject(); j.EndObject();
            }
        }
    }
    // Inline a resolved scalar expression with fresh argument storage.
    for (auto &span : f.calls) {
        if (span.start != start || span.end != end) continue;
        auto call = SpecializedCall(*loc.an, span.call);
        if (!call || !call->spec || !Is<Ident>(span.call->callee)) continue;
        auto sf = call->spec->sf;
        if (sf->line.fileidx < 0 || (size_t)sf->line.fileidx >= loc.an->files.size()) continue;
        auto &sourceFileIndex = loc.an->files[(size_t)sf->line.fileidx];
        Located sourceLoc {loc.an, &sourceFileIndex, 0};
        auto sourceExtent = [&](const Node *n) { return RefactorExtent(sourceFileIndex, n); };
        auto body = sf->body;
        if (!body || sf->isrec || span.call->trailing ||
            span.call->ndefaults || sf->params.size() != span.call->args.size() ||
            body->stmts.empty() || body->tail) continue;
        auto ret = Is<Return>(body->stmts.back());
        if (!ret || !ret->from.empty() || ret->vals.size() != 1) continue;
        auto expr = ret->vals[0];
        auto ex = sourceExtent(expr);
        auto first = sourceExtent(body->stmts.front());
        ex.first = first.first;
        bool valid = ex.first >= 0;
        for (size_t i = 0; i + 1 < body->stmts.size(); ++i) {
            if (Is<Call>(body->stmts[i])) valid = false;
            if (auto vd = Is<VarDecl>(body->stmts[i]); vd &&
                (vd->byref || vd->names.size() != 1)) valid = false;
        }
        vector<RefactorEdit> substitutions;
        map<size_t, Place> qualifiedBindings;
        map<size_t, size_t> parameterBindings;
        vector<string> generatedNames;
        auto freshName = [&](string name) {
            while (f.text.text.find(name) != string::npos ||
                   sourceFileIndex.text.text.find(name) != string::npos ||
                   any_of(generatedNames.begin(), generatedNames.end(), [&](auto &used) {
                       return used.find(name) != string::npos || name.find(used) != string::npos;
                   })) name += "_";
            generatedNames.push_back(name);
            return name;
        };
        string prefix = "block { ";
        for (size_t i = 0; i < sf->params.size(); ++i) {
            auto &param = sf->params[i];
            auto argumentType = loc.an->TypeOf(call->args[i]);
            if (param.isvar || !valueType(argumentType)) valid = false;
            string fresh = freshName(cat("inline_argument_", i));
            auto generatedDeclaration = start + prefix.size() + 4;
            prefix += cat("let ", fresh, ": ", TypeText(argumentType),
                          " = ", text(span.call->args[i]), "; ");
            auto decl = sourceFileIndex.text.OffsetOf(param.line.line, param.line.col);
            for (auto &occ : sourceFileIndex.occs) if (occ.kind == Occurrence::IDENT &&
                occ.start >= (size_t)ex.first && occ.start < (size_t)ex.second) {
                auto def = DefinitionOf(sourceLoc, occ);
                if (def.Known() && def.fileidx == sourceFileIndex.fileidx && def.start == (size_t)decl)
                {
                    substitutions.push_back({occ.start, occ.start + occ.len, fresh});
                    parameterBindings[occ.start] = generatedDeclaration;
                }
            }
        }
        function<void(const Node *)> pure = [&](const Node *x) {
            if (!(AssistValue(x) || Is<Ident>(x) || Is<Unary>(x) || Is<Binary>(x) ||
                  Is<ArrayLit>(x) || Is<StructLit>(x) || Is<VarDecl>(x) || Is<Call>(x) ||
                  Is<Block>(x) || Is<IfExpr>(x) || Is<While>(x) || Is<LoopExpr>(x) ||
                  Is<Return>(x) || Is<Break>(x) || Is<Continue>(x) || Is<Assign>(x))) valid = false;
            if (auto r = Is<Return>(x); r && (!r->from.empty() || (r->target && r->target != sf))) valid = false;
            if (auto vd = Is<VarDecl>(x); vd && vd->byref) valid = false;
            if (auto u = Is<Unary>(x); u && u->op != T_MINUS && u->op != T_NOT && u->op != T_BITNOT) valid = false;
            if (auto id = Is<Ident>(x)) {
                auto e = sourceExtent(id);
                bool bound = false;
                for (auto &sub : substitutions) if (sub.start == (size_t)e.first) bound = true;
                if (!bound) {
                    auto occ = sourceFileIndex.At(e.first);
                    if (!occ) { valid = false; return; }
                    auto def = DefinitionOf(sourceLoc, *occ);
                    if (!def.Known() || def.fileidx != sourceFileIndex.fileidx ||
                        (def.start < (size_t)ex.first || def.start >= (size_t)ex.second)) {
                        auto declaration = def.Known() ? loc.an->files[(size_t)def.fileidx].At(def.start) : nullptr;
                        if (!declaration || declaration->kind != Occurrence::FN || !occ->callee || !declaration->sf) valid = false;
                        else {
                            auto target = declaration->sf;
                            size_t count = 0;
                            for (auto candidate : loc.an->ast->functions)
                                if (candidate->qname == target->qname) ++count;
                            if (count != 1) valid = false;
                            auto begin = occ->start;
                            while (begin && (IsIdentCont(sourceFileIndex.text.text[begin-1]) ||
                                   sourceFileIndex.text.text[begin-1] == ':')) --begin;
                            qualifiedBindings[begin] = def;
                            substitutions.push_back({begin, occ->start + occ->len, string(target->qname)});
                        }
                    }
                }
            }
            x->Children([&](Node *ch) { pure(ch); });
        };
        pure(expr);
        for (size_t i = 0; i + 1 < body->stmts.size(); ++i) pure(body->stmts[i]);
        if (!valid) continue;
        auto replacement = string(sourceFileIndex.text.text.substr(ex.first, ex.second - ex.first));
        string helper = freshName("inline_body");
        sort(substitutions.begin(), substitutions.end(), [](auto &a, auto &b) { return a.start > b.start; });
        for (auto &sub : substitutions)
            replacement.replace(sub.start - ex.first, sub.end - sub.start, sub.text);
        vector<RefactorEdit> edits {{start, end, cat(prefix, "fn ", helper, "() { ", replacement, "; } break ", helper, "(); }")}};
        map<size_t, size_t> syntheticBindings;
        for (auto &[offset, declaration] : parameterBindings) {
            int64_t delta = 0;
            for (auto &sub : substitutions) if (sub.end <= offset)
                delta += (int64_t)sub.text.size() - (sub.end-sub.start);
            syntheticBindings[start + prefix.size() + 3 + helper.size() + 5 +
                              offset-ex.first + delta] = declaration;
        }
        map<size_t, Place> insertedBindings;
        for (auto &[offset, target] : qualifiedBindings) {
            int64_t delta = 0;
            for (auto &sub : substitutions) if (sub.end <= offset)
                delta += (int64_t)sub.text.size() - (sub.end-sub.start);
            auto sub = find_if(substitutions.begin(), substitutions.end(), [&](auto &s) { return s.start == offset; });
            auto leaf = sub->text.rfind("::");
            auto at = start + prefix.size() + 3 + helper.size() + 5 + offset-ex.first + delta;
            insertedBindings[at + (leaf == string::npos ? 0 : leaf+2)] = target;
        }
        if (wanted("refactor.inline") && CheckRefactor(ws, loc, edits, false, {}, insertedBindings, syntheticBindings))
            WriteRefactor(j, loc, doc->second.version, "Inline resolved scalar call", "refactor.inline", edits);
    }
    // Match declarations across independently checked workspace roots.
    for (auto &decl : f.occs) {
        if (decl.kind != Occurrence::PARAM || decl.start != start || !decl.sf ||
            (end != start && end != start + decl.len)) continue;
        auto sf = decl.sf;
        size_t index = 0;
        while (index < sf->params.size() && &sf->params[index] != decl.param) ++index;
        if (index == sf->params.size() || sf->params[index].defaultval) continue;
        auto fn = PlaceOfFn(*loc.an, sf);
        if (!fn.Known()) continue;
        bool valid = true, effects = false;
        auto from = start;
        size_t to;
        if (index + 1 < sf->params.size())
            to = f.text.OffsetOf(sf->params[index + 1].line.line, sf->params[index + 1].line.col);
        else {
            to = f.text.text.find(')', start);
            if (index) from = f.text.text.rfind(',', start);
        }
        if (to == string::npos || from == string::npos) continue;
        map<string, pair<const FileIndex *, vector<RefactorEdit>>> changes;
        changes[f.uri] = {&f, {{from, to, ""}}};
        for (auto &root : ws.roots) {
            if (!root->analysis) continue;
            auto &an = *root->analysis;
            auto matches = [&](Place place, size_t offset) {
                return place.Known() && an.files[(size_t)place.fileidx].uri == f.uri && place.start == offset;
            };
            for (auto &file : an.files) {
                Located here {&an, &file, 0};
                for (auto &occ : file.occs) {
                    auto def = DefinitionOf(here, occ);
                    if (occ.kind == Occurrence::IDENT && matches(def, start)) valid = false;
                    if (matches(def, fn.start) && !occ.IsDeclaration() && !occ.callee) valid = false;
                }
                auto fileExtent = [&](const Node *node) { return RefactorExtent(file, node); };
                auto fileText = [&](const Node *node) {
                    auto e = fileExtent(node);
                    if (e.first < 0 || e.second <= e.first) { valid = false; return string(); }
                    return string(file.text.text.substr(e.first, e.second - e.first));
                };
                for (auto &span : file.calls) {
                    auto call = SpecializedCall(an, span.call);
                    if (!call || !call->spec || !matches(PlaceOfFn(an, call->spec->sf), fn.start)) continue;
                    auto [it, added] = changes.emplace(file.uri, make_pair(&file, vector<RefactorEdit>{}));
                    if (it->second.first->text.text != file.text.text) valid = false;
                    auto &edits = it->second.second;
                    bool duplicate = false;
                    for (auto &edit : edits) if (edit.start >= span.start && edit.start < span.end) duplicate = true;
                    if (duplicate) continue;
                    if (!Is<Ident>(span.call->callee) || span.call->ndefaults || span.call->trailing ||
                        span.call->args.size() != sf->params.size()) { valid = false; continue; }
                    if (AssistValue(span.call->args[index])) {
                        auto e = fileExtent(span.call->args[index]);
                        auto begin = (size_t)e.first, finish = (size_t)e.second;
                        if (index + 1 < span.call->args.size()) finish = fileExtent(span.call->args[index + 1]).first;
                        else if (index) begin = file.text.text.rfind(',', begin);
                        edits.push_back({begin, finish, ""});
                    } else {
                        effects = true;
                        string replacement = "block { ", arguments;
                        for (size_t i = 0; i < span.call->args.size(); ++i) {
                            auto type = an.TypeOf(call->args[i]);
                            if (!valueType(type) || sf->params[i].isvar) { valid = false; break; }
                            string fresh = cat("retained_argument_", i);
                            while (file.text.text.find(fresh) != string::npos) fresh += "_";
                            replacement += cat("let ", fresh, ": ", TypeText(type), " = ", fileText(span.call->args[i]), "; ");
                            if (i != index) {
                                if (!arguments.empty()) arguments += ", ";
                                arguments += fresh;
                            }
                        }
                        replacement += cat("break ", fileText(span.call->callee), "(", arguments, "); }");
                        edits.push_back({span.start, span.end, replacement});
                    }
                }
            }
        }
        for (auto &[uri, change] : changes)
            sort(change.second.begin(), change.second.end(), [](auto &a, auto &b) { return a.start < b.start; });
        if (valid && wanted("refactor.rewrite") && CheckWorkspaceRefactor(ws, changes)) {
            j.BeginObject();
            j.Member("title", effects ? "Remove unused input preserving argument evaluation" : "Remove unused parameter (literal arguments)");
            j.Member("kind", "refactor.rewrite");
            j.Key("edit"); j.BeginObject(); j.Key("documentChanges"); j.BeginArray();
            for (auto &[uri, change] : changes) {
                j.BeginObject(); j.Key("textDocument"); j.BeginObject(); j.Member("uri", uri);
                j.Key("version");
                auto open = ws.open.find(PathKey(PathOfUri(uri)));
                if (open == ws.open.end()) j.Null(); else j.Int(open->second.version);
                j.EndObject(); j.Key("edits"); j.BeginArray();
                for (auto &edit : change.second) {
                    j.BeginObject(); j.Key("range"); WriteRange(j, change.first->text, edit.start, edit.end);
                    j.Member("newText", edit.text); j.EndObject();
                }
                j.EndArray(); j.EndObject();
            }
            j.EndArray(); j.EndObject(); j.EndObject();
        }
    }

    function<void(const Node *, bool)> walk = [&](const Node *n, bool local) {
        if (!n) return;
        if (auto block = Is<Block>(n); block && local && start < end) {
            for (auto stmt : block->stmts) {
                auto assign = Is<Assign>(stmt);
                auto se = extent(stmt);
                size_t finish = se.second;
                if (finish < f.text.text.size() && f.text.text[finish] == ';') ++finish;
                if ((!assign && !Is<IfExpr>(stmt)) || se.first != (int64_t)start || finish != end) continue;
                string target;
                bool safe = true;
                function<void(const Node *)> outputs = [&](const Node *node) {
                    if (auto assignment = Is<Assign>(node)) {
                        if (!Is<Ident>(assignment->lval)) safe = false;
                        auto name = text(assignment->lval);
                        if (!target.empty() && target != name) safe = false;
                        target = name;
                    }
                    node->Children([&](Node *child) { outputs(child); });
                };
                outputs(stmt);
                if (target.empty()) continue;
                function<void(const Node *)> inspect = [&](const Node *node) {
                    if (Is<Call>(node) || Is<Unary>(node) || Is<FunVal>(node) ||
                        Is<Return>(node) || Is<Break>(node) || Is<Continue>(node) ||
                        Is<Defer>(node) || Is<FnDecl>(node) || Is<VarDecl>(node)) safe = false;
                    node->Children([&](Node *child) { inspect(child); });
                };
                inspect(stmt);
                string args, params, setup;
                set<size_t> captures;
                for (auto &use : f.occs) if (use.kind == Occurrence::IDENT && use.start >= start && use.start < end) {
                    auto def = DefinitionOf(loc, use);
                    if (!def.Known() || def.fileidx != f.fileidx) { safe = false; continue; }
                    auto decl = f.At(def.start);
                    if (!decl) { safe = false; continue; }
                    if (decl->kind == Occurrence::FN || decl->kind == Occurrence::GLOBAL) continue;
                    if (!valueType(loc.an->TypeOf(use.node))) { safe = false; continue; }
                    if (!captures.insert(def.start).second) continue;
                    string fresh = cat("extracted_input_", captures.size());
                    while (f.text.text.find(fresh) != string::npos) fresh += "_";
                    if (!args.empty()) { args += ", "; params += ", "; }
                    args += decl->name; params += fresh;
                    setup += cat(decl->name == target ? "var " : "let ", decl->name, " = ", fresh, "; ");
                }
                string name = "extracted_assignment";
                while (f.text.text.find(name) != string::npos) name += "_";
                if (safe) emit("Extract assignment with explicit inputs and output", "refactor.extract",
                    {{start, end, cat("fn ", name, "(", params, ") { ", setup,
                      f.text.text.substr(start, end - start), " return ", target, "; } ",
                      target, " = ", name, "(", args, ");")}});
            }
            vector<const VarDecl *> outputs;
            bool valid = true;
            size_t finish = start;
            for (auto stmt : block->stmts) {
                auto e = extent(stmt);
                if (e.first < (int64_t)start || e.first >= (int64_t)end) continue;
                auto v = Is<VarDecl>(stmt);
                if (!v || v->isvar || v->byref || v->isconst || v->names.size() != 1 ||
                    v->inits.size() != 1 || v->type || v->reusable) { valid = false; break; }
                finish = e.second;
                if (finish < f.text.text.size() && f.text.text[finish] == ';') ++finish;
                outputs.push_back(v);
                function<void(const Node *)> inspect = [&](const Node *x) {
                    if (Is<Return>(x) || Is<Break>(x) || Is<Continue>(x) || Is<Defer>(x) ||
                        Is<FunVal>(x) || Is<FnDecl>(x) || Is<Unary>(x)) valid = false;
                    x->Children([&](Node *ch) { inspect(ch); });
                };
                inspect(v);
            }
            if (outputs.empty() || extent(outputs.front()).first != (int64_t)start || finish != end) valid = false;
            map<size_t, string> rebound;
            string names, inputs;
            for (auto v : outputs) {
                if (!names.empty()) names += ", ";
                names += v->names[0];
                for (auto &occ : f.occs) if (occ.kind == Occurrence::LOCAL && occ.node == v)
                    rebound[occ.start] = string(occ.name);
                set<string> types;
                auto consider = [&](const Node *x) {
                    auto vd = Is<VarDecl>(x);
                    if (!vd || vd->defs.empty() || !vd->defs[0]) return;
                    if (!valueType(vd->defs[0]->type)) valid = false;
                    else types.insert(TypeText(vd->defs[0]->type));
                };
                consider(v);
                if (auto it = loc.an->clones.find(v); it != loc.an->clones.end())
                    for (auto clone : it->second) consider(clone);
                if (types.size() != 1) valid = false;
            }
            set<size_t> captures;
            for (auto &occ : f.occs) if (occ.kind == Occurrence::IDENT && occ.start >= start && occ.start < end) {
                auto def = DefinitionOf(loc, occ);
                if (!def.Known() || def.fileidx != f.fileidx) { valid = false; continue; }
                if (def.start >= start && def.start < end) continue;
                auto declaration = f.At(def.start);
                if (!declaration) { valid = false; continue; }
                if (declaration->kind == Occurrence::FN || declaration->kind == Occurrence::GLOBAL) continue;
                if (declaration->isvar || !valueType(loc.an->TypeOf(occ.node))) { valid = false; continue; }
                if (declaration->kind != Occurrence::LOCAL && declaration->kind != Occurrence::PARAM) { valid = false; continue; }
                if (captures.insert(def.start).second) {
                    if (!inputs.empty()) inputs += ", ";
                    inputs += declaration->name;
                }
            }
            if (valid) {
                string name = "extracted_statements";
                while (f.text.text.find(name) != string::npos) name += "_";
                vector<RefactorEdit> edits {{start, end, cat("fn ", name, "(", inputs, ") { ",
                    f.text.text.substr(start, end - start), " return ", names, "; } let ", names,
                    " = ", name, "(", inputs, ");")}};
                if (wanted("refactor.extract") && CheckRefactor(ws, loc, edits, false, rebound))
                    WriteRefactor(j, loc, doc->second.version, "Extract statements with explicit inputs and outputs", "refactor.extract", edits);
            }
        }
        if (auto v = Is<VarDecl>(n); v && local && !v->isglobal && !v->byref &&
            !v->reusable && v->names.size() == 1 && v->inits.size() == 1) {
            auto init = v->inits[0];
            auto e = extent(init);
            auto decl = NodeExtent(f, v);
            auto pos = f.text.OffsetOf(v->line.line, v->line.col);
            auto line = f.text.text.rfind('\n', pos);
            line = line == string::npos ? 0 : line + 1;
            auto indent = f.text.text.substr(line, pos - line);
            // Hoist only the entire initializer: no sibling is reordered.
            int64_t closed;
            auto address = Is<Unary>(init);
            bool forwardReference = address && address->op == T_BITAND;
            bool referenceResult = false;
            for (auto type : loc.an->TypesOf(init, 64))
                if (type->kind == TY_REF) referenceResult = true;
            if ((!referenceResult || forwardReference) && selected(init) && !AssistIntegerSyntax(init) && !ClosedInteger(init, closed) &&
                indent.find_first_not_of(" \t") == string::npos) {
                string name = "extracted_value";
                while (f.text.text.find(name) != string::npos) name += "_";
                auto expression = text(init);
                if (!expression.empty())
                    emit("Extract initializer into variable", "refactor.extract",
                        {{(size_t)pos, (size_t)pos, cat("let ", name, " = ", expression, ";\n", indent)},
                         {(size_t)e.first, (size_t)e.second, forwardReference ? cat("&", name) : name}});
            }
            // A lexical helper leaves evaluation at the selected expression.
            function<void(const Node *)> extract = [&](const Node *part) {
                if (!part) return;
                if (part != init && selected(part) && AssistValue(part) &&
                    indent.find_first_not_of(" \t") == string::npos &&
                    f.text.text.find('&') == string::npos) {
                    string name = "extracted_value";
                    while (f.text.text.find(name) != string::npos) name += "_";
                    auto pe = extent(part);
                    emit("Extract literal into variable", "refactor.extract",
                        {{(size_t)pos, (size_t)pos,
                          cat("let ", name, " = ", text(part), ";\n", indent)},
                         {(size_t)pe.first, (size_t)pe.second, name}});
                }
                if (part != init && selected(part) && valueType(checkedType(part)) &&
                    (Is<Call>(part) || Is<Binary>(part) || Is<ArrayLit>(part) || Is<StructLit>(part))) {
                    string name = "extracted_value";
                    while (f.text.text.find(name) != string::npos) name += "_";
                    auto pe = extent(part);
                    emit("Extract nested expression into variable", "refactor.extract",
                        {{(size_t)pe.first, (size_t)pe.second,
                          cat("block { let ", name, " = ", text(part), "; break ", name, "; }")}});
                }
                if (selected(part) && (Is<Binary>(part) || Is<Call>(part) ||
                    Is<Dot>(part) || Is<ArrayLit>(part) || Is<StructLit>(part) || AssistValue(part))) {
                    bool control = false;
                    function<void(const Node *)> inspect = [&](const Node *x) {
                        if (Is<Return>(x) || Is<Break>(x) || Is<Continue>(x) ||
                            Is<Defer>(x) || Is<FunVal>(x)) control = true;
                        x->Children([&](Node *ch) { inspect(ch); });
                    };
                    inspect(part);
                    for (auto type : loc.an->TypesOf(part, 64))
                        if (type->kind == TY_REF) control = true;
                    if (!control) {
                        string name = "extracted_function";
                        while (f.text.text.find(name) != string::npos) name += "_";
                        auto pe = extent(part);
                        emit("Extract expression into function", "refactor.extract",
                            {{(size_t)pos, (size_t)pos,
                              cat("fn ", name, "() { return ", text(part), "; } ")},
                             {(size_t)pe.first, (size_t)pe.second, cat(name, "()")}});
                    }
                }
                part->Children([&](Node *ch) { extract(ch); });
            };
            extract(init);
            for (auto &occ : f.occs) {
                if (occ.node != v || occ.kind != Occurrence::LOCAL || occ.start != start ||
                    (end != start && end != start + occ.len)) continue;
                if (!v->type) {
                    set<string> types;
                    auto consider = [&](const Node *node) {
                        auto vd = Is<VarDecl>(node);
                        if (vd && vd->defs.size() == 1 && vd->defs[0] && vd->defs[0]->type)
                            types.insert(TypeText(vd->defs[0]->type));
                    };
                    consider(v);
                    if (auto it = loc.an->clones.find(v); it != loc.an->clones.end())
                        for (auto c : it->second) consider(c);
                    if (types.size() == 1 && types.begin()->find('?') == string::npos)
                        emit("Add explicit binding type", "refactor.rewrite",
                             {{start + occ.len, start + occ.len, cat(": ", *types.begin())}});
                }
                if (!v->isvar && !v->isconst && !v->type && AssistValue(init) &&
                    decl.first >= 0 && f.text.text.substr(decl.first, 3) == "let" &&
                    f.text.text.find('&') == string::npos)
                    emit("Promote local to const", "refactor.rewrite",
                         {{(size_t)decl.first, (size_t)decl.first + 3, "const"}});
                bool immutableAlias = false;
                if (Is<Ident>(init)) {
                    auto ie = extent(init);
                    auto use = f.At(ie.first);
                    if (use) {
                        auto def = DefinitionOf(loc, *use);
                        if (def.Known() && def.fileidx == f.fileidx) {
                            auto source = f.At(def.start);
                            if (source && !source->isvar) {
                                auto vd = Is<VarDecl>(source->node);
                                if (vd && !vd->byref)
                                    immutableAlias = scalar(loc.an->TypeOf(init));
                            }
                        }
                    }
                }
                if (!v->isvar && !v->byref && !v->type && decl.first >= 0 &&
                    valueType(checkedType(init)) && !AssistValue(init)) {
                    vector<const Occurrence *> uses;
                    for (auto &use : f.occs) if (use.kind == Occurrence::IDENT) {
                        auto def = DefinitionOf(loc, use);
                        if (def.Known() && def.fileidx == f.fileidx && def.start == start) uses.push_back(&use);
                    }
                    auto finish = (size_t)decl.second;
                    if (finish < f.text.text.size() && f.text.text[finish] == ';') ++finish;
                    auto next = f.text.text.find_first_not_of(" \t\r\n", finish);
                    // Only traverse children evaluated before any other expression.
                    function<bool(const Node *)> firstUse = [&](const Node *node) {
                        if (!node) return false;
                        auto e = extent(node);
                        if (Is<Ident>(node)) return uses.size() == 1 && e.first == (int64_t)uses[0]->start;
                        if (auto vd = Is<VarDecl>(node)) return !vd->byref && (vd->inits.size() == 1 && firstUse(vd->inits[0]));
                        if (auto ret = Is<Return>(node)) return !ret->vals.empty() && firstUse(ret->vals[0]);
                        if (auto binary = Is<Binary>(node)) return firstUse(binary->left);
                        if (auto unary = Is<Unary>(node))
                            return (unary->op == T_MINUS || unary->op == T_NOT || unary->op == T_BITNOT) && firstUse(unary->child);
                        if (auto call = Is<Call>(node))
                            return Is<Ident>(call->callee) && !call->args.empty() && firstUse(call->args[0]);
                        return false;
                    };
                    bool first = false;
                    function<void(const Node *)> findNext = [&](const Node *node) {
                        auto e = extent(node);
                        if (e.first == (int64_t)next && firstUse(node)) first = true;
                        if (auto fd = Is<FnDecl>(node)) findNext(fd->sf->body);
                        else node->Children([&](Node *child) { findNext(child); });
                    };
                    if (uses.size() == 1 && next != string::npos && f.text.text.find('&') == string::npos)
                        for (auto declaration : loc.an->ast->topdecls) findNext(declaration);
                    if (first)
                        emit("Inline single-use value at evaluation position", "refactor.inline",
                            {{(size_t)decl.first, finish, ""},
                             {uses[0]->start, uses[0]->start + uses[0]->len, cat("(", text(init), ")")}});
                }
                int64_t integer;
                if (!v->isvar && !v->type && v->doc.empty() &&
                    (AssistValue(init) || immutableAlias) && (v->isconst || !ClosedInteger(init, integer)) && decl.first >= 0) {
                    auto finish = (size_t)decl.second;
                    if (finish < f.text.text.size() && f.text.text[finish] == ';') ++finish;
                    auto spelling = f.text.text.substr(decl.first, finish - decl.first);
                    if (spelling.find("//") != string::npos || spelling.find("/*") != string::npos) continue;
                    vector<RefactorEdit> edits {{(size_t)decl.first, finish, ""}};
                    for (auto &use : f.occs) {
                        if (use.kind != Occurrence::IDENT) continue;
                        auto target = DefinitionOf(loc, use);
                        if (target.Known() && target.fileidx == f.fileidx && target.start == start &&
                            !(use.start >= (size_t)decl.first && use.start < finish))
                            edits.push_back({use.start, use.start + use.len, cat("(", text(init), ")")});
                    }
                    // Address-taking exposes the local's storage identity.
                    if (edits.size() > 1 && f.text.text.find('&') == string::npos)
                        emit(v->isconst ? "Inline const literal" : "Inline immutable literal variable", "refactor.inline", edits);
                }
            }
        }
        if (local && selected(n) && valueType(checkedType(n)) &&
            (Is<Call>(n) || Is<Binary>(n) || Is<Dot>(n))) {
            bool safe = true;
            function<void(const Node *)> inspect = [&](const Node *node) {
                if (Is<Return>(node) || Is<Break>(node) || Is<Continue>(node) ||
                    Is<Defer>(node) || Is<FunVal>(node)) safe = false;
                for (auto type : loc.an->TypesOf(node, 64))
                    if (type->kind == TY_REF) safe = false;
                node->Children([&](Node *child) { inspect(child); });
            };
            inspect(n);
            if (safe) {
                string name = "extracted_expression";
                while (f.text.text.find(name) != string::npos) name += "_";
                emit("Extract expression at evaluation position", "refactor.extract",
                     {{start, end, cat("block { fn ", name, "() { return ", text(n),
                                      "; } break ", name, "(); }")}});
            }
        }
        if (selected(n)) {
            auto spelling = text(n);
            if (spelling.size() > 2 && spelling.front() == '(' && spelling.back() == ')') {
                auto candidate = string(f.text.text);
                candidate.replace(start, end - start, spelling.substr(1, spelling.size() - 2));
                if (format::CheckSame(string(f.text.text), candidate, doc->second.path).empty())
                    replace(n, "Remove redundant parentheses", spelling.substr(1, spelling.size() - 2));
            }
            if (auto u = Is<Unary>(n); u && u->op == T_NOT) {
                if (auto bin = Is<Binary>(u->child); bin &&
                    (bin->op == T_ANDAND || bin->op == T_OROR))
                    replace(n, "Apply De Morgan's law", cat("(!(", text(bin->left), ") ",
                        bin->op == T_ANDAND ? "||" : "&&", " !(", text(bin->right), "))"));
            }
            if (auto bin = Is<Binary>(n)) {
                string op;
                if (bin->op == T_ANDAND) op = "||";
                if (bin->op == T_OROR) op = "&&";
                if (!op.empty()) replace(n, "Apply De Morgan's law", cat("!(!(",
                    text(bin->left), ") ", op, " !(", text(bin->right), "))"));
                // Literals cannot carry effects or mutable reads.
                if (AssistValue(bin->left) && AssistValue(bin->right) &&
                    (bin->op == T_PLUS || bin->op == T_MUL))
                    replace(n, "Flip literal operands", cat("(", text(bin->right), ") ",
                        bin->op == T_PLUS ? "+" : "*", " (", text(bin->left), ")"));
            }
            if (auto i = Is<IntLit>(n); i && !i->uns && i->val >= 0) {
                auto spelling = text(n);
                if (spelling.find('_') != string::npos) {
                    spelling.erase(std::remove(spelling.begin(), spelling.end(), '_'), spelling.end());
                    replace(n, "Remove numeric separators", spelling);
                }
                if (i->val <= INT64_MAX) {
                    auto bits = (uint64_t)i->val;
                    string hex;
                    do { hex += "0123456789abcdef"[bits & 15]; bits >>= 4; } while (bits);
                    reverse(hex.begin(), hex.end());
                    auto result = spelling.starts_with("0x") ? to_string(i->val) : cat("0x", hex);
                    if (result != text(n)) replace(n, "Convert integer representation", result);
                }
            }
            if (auto w = Is<While>(n)) {
                auto body = text(w->body);
                if (body.starts_with("{"))
                    replace(n, "Convert while to loop", cat("loop { if !(", text(w->cond),
                        ") { break; }", body.substr(1)));
            }
            if (auto cond = Is<IfExpr>(n); cond && cond->elseb && Is<Block>(cond->elseb))
                replace(n, "Invert if condition", cat("if !(", text(cond->cond), ") ",
                    text(cond->elseb), " else ", text(cond->thenb)));
        }
        if (auto fd = Is<FnDecl>(n)) {
            auto sf = fd->sf;
            for (auto &param : sf->params) {
                if (!valueType(param.type)) continue;
                auto at = f.text.OffsetOf(param.line.line, param.line.col);
                if (at < 0) continue;
                auto typepos = (size_t)at + param.name.size();
                while (typepos < f.text.text.size() && isspace((unsigned char)f.text.text[typepos])) ++typepos;
                if (typepos >= f.text.text.size() || f.text.text[typepos++] != ':') continue;
                while (typepos < f.text.text.size() && isspace((unsigned char)f.text.text[typepos])) ++typepos;
                auto spelling = TypeText(param.type);
                if (start != typepos || end != start + spelling.size() ||
                    f.text.text.substr(start, end - start) != spelling) continue;
                bool indexed = false;
                for (auto &occ : f.occs)
                    if (occ.kind == Occurrence::TYPE && occ.start == start &&
                        occ.len == end - start && valueType(occ.type)) indexed = true;
                if (indexed) continue;
                string name = "ExtractedType";
                while (f.text.text.find(name) != string::npos) name += "_";
                emit("Extract type alias", "refactor.extract",
                    {{0, 0, cat("type ", name, " = ", spelling, ";\n")}, {start, end, name}});
            }

            auto body = sf->body ? extent(sf->body) : pair<int64_t, int64_t>{-1, -1};
            size_t namepos = string::npos;
            for (auto &occ : f.occs)
                if (occ.kind == Occurrence::FN && occ.sf == sf) namepos = occ.start;
            if (!sf->has_rets && !sf->IsGeneric() && body.first >= 0 &&
                start == namepos && (end == start || end == start + sf->name.size())) {
                set<string> signatures;
                for (auto spec : sf->specs) {
                    string signature;
                    bool valid = !spec->rets.empty();
                    for (auto type : spec->rets) {
                        auto t = TypeText(type);
                        if (t.find('?') != string::npos || t.find('&') != string::npos) valid = false;
                        if (!signature.empty()) signature += ", ";
                        signature += t;
                    }
                    if (valid) signatures.insert(signature);
                }
                if (signatures.size() == 1)
                    emit("Add explicit return type", "refactor.rewrite",
                        {{(size_t)body.first, (size_t)body.first,
                          cat("-> ", *signatures.begin(), " ")}});
            }
            walk(sf->body, true);
        }
        else n->Children([&](Node *ch) { walk(ch, local); });
    };
    for (auto d : loc.an->ast->topdecls) walk(d, false);
    // Restrict comment edits to complete, standalone, single-line comments.
    auto line = f.text.text.rfind('\n', start);
    line = line == string::npos ? 0 : line + 1;
    if (start < end && f.text.text.substr(line, start - line).find_first_not_of(" \t") == string::npos) {
        auto s = string(f.text.text.substr(start, end - start));
        auto after = end == f.text.text.size() || f.text.text[end] == '\n' || f.text.text[end] == '\r';
        if (after && s.starts_with("//") && s.find('\n') == string::npos) {
            if (s.starts_with("///"))
                emit("Convert documentation to comment", "refactor.rewrite", {{start, start + 3, "//"}});
            else {
                emit("Convert comment to documentation", "refactor.rewrite", {{start, start + 2, "///"}});
                if (s.find("*/") == string::npos)
                    emit("Convert line comment to block", "refactor.rewrite", {{start, end, cat("/*", s.substr(2), " */")}});
            }
        }
        if (after && s.starts_with("/*") && s.ends_with("*/") && s.find('\n') == string::npos &&
            s.find("/*", 2) == string::npos && s.find("*/") == s.size() - 2)
            emit("Convert block comment to line", "refactor.rewrite", {{start, end, cat("//", s.substr(2, s.size() - 4))}});
    }
}
