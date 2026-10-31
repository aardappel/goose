inline bool SyntaxCheckDiagnostic(Workspace &ws, const Located &loc, vector<RefactorEdit> edits) {
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
        for (auto &file : before.files) {
            auto idx = after.FileOf(PathKey(PathOfUri(file.uri)));
            if (idx < 0) return false;
            auto &updated = after.files[(size_t)idx];
            Located oldloc { &before, &file, 0 }, newloc { &after, &updated, 0 };
            bool changed = file.uri == loc.fi->uri;
            for (auto &occ : file.occs) {
                if (changed && removed(occ.start)) continue;
                auto next = updated.At(changed ? shift(occ.start) : occ.start);
                if (!next || next->kind != occ.kind) return false;
                auto a = DefinitionOf(oldloc, occ), b = DefinitionOf(newloc, *next);
                if (a.Known() && !b.Known()) return false;
                if (a.Known()) {
                    auto &af = before.files[(size_t)a.fileidx];
                    auto &bf = after.files[(size_t)b.fileidx];
                    if (af.uri != bf.uri || (af.uri == loc.fi->uri ? shift(a.start) : a.start) != b.start)
                        return false;
                } else if (occ.name != next->name) return false;
            }
        }
    }
    return true;
}

inline void SyntaxAssists(Workspace &ws, const Json &params, JsonOut &j) {
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
    auto context = params.Get("context");
    auto only = context ? context->Get("only") : nullptr;
    if (only) {
        bool ok = false;
        for (auto &k : only->arr)
            if (k.str == "refactor" || k.str == "refactor.rewrite") ok = true;
        if (!ok) return;
    }
    auto emit = [&](string_view title, size_t x, size_t y, string replacement) {
        vector<RefactorEdit> edits {{x, y, replacement}};
        if (x <= y && y <= f.text.text.size() && CheckRefactor(ws, loc, edits))
            WriteRefactor(j, loc, doc->second.version, title, "refactor.rewrite", edits);
    };
    auto fix = [&](string_view title, size_t x, size_t y, string replacement) {
        vector<RefactorEdit> edits {{x, y, replacement}};
        bool errors = false;
        for (auto &d : loc.an->diags) if (!d.warning) errors = true;
        if (errors && x <= y && y <= f.text.text.size() && SyntaxCheckDiagnostic(ws, loc, edits))
            WriteRefactor(j, loc, doc->second.version, title, "refactor.rewrite", edits);
    };
    auto available = [&](string_view name) {
        if (loc.an->ast->LookupGlobal(name, "")) return false;
        for (auto sf : loc.an->ast->functions) if (sf->name == name) return false;
        return true;
    };
    auto extent = [&](const Node *n) {
        auto r = RefactorExtent(f, n);
        if (r.first < 0 || r.second <= r.first) return r;
        int depth = 0, missing = 0;
        char quote = 0;
        bool escaped = false;
        for (auto i = r.first; i < r.second; ++i) {
            char c = f.text.text[i];
            if (quote) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') quote = c;
            else if (c == '(') ++depth;
            else if (c == ')' && --depth < -missing) missing = -depth;
        }
        auto begin = r.first;
        while (missing--) {
            while (begin > 0 && isspace((unsigned char)f.text.text[begin-1])) --begin;
            if (begin == 0 || f.text.text[begin-1] != '(') return pair<int64_t,int64_t>{-1,-1};
            --begin;
        }
        r.first = begin;
        return r;
    };
    auto text = [&](const Node *n) -> string {
        auto e = extent(n);
        return e.first >= 0 && e.second > e.first ? string(f.text.text.substr(e.first, e.second-e.first)) : string();
    };
    auto clean = [](string_view s) { return s.find("//") == string_view::npos && s.find("/*") == string_view::npos; };
    auto lineStart = f.text.text.rfind('\n', start);
    lineStart = lineStart == string::npos ? 0 : lineStart + 1;
    if (start < end && f.text.text.substr(lineStart, start-lineStart).find_first_not_of(" \t") == string::npos &&
        (end == f.text.text.size() || f.text.text[end] == '\n')) {
        auto selected = string(f.text.text.substr(start, end-start));
        auto next = end < f.text.text.size() ? f.text.text.find('\n', end+1) : end;
        bool detached = end == f.text.text.size() ||
            f.text.text.substr(end+1, next == string::npos ? next : next-end-1).find_first_not_of(" \t\r") == string::npos;
        if (detached && selected.starts_with("/*") && selected.ends_with("*/")) {
            int depth = 1;
            bool complete = true;
            for (size_t i = 2; i < selected.size(); ++i) {
                if (selected.compare(i, 2, "/*") == 0) { ++depth; ++i; }
                else if (selected.compare(i, 2, "*/") == 0) {
                    --depth; ++i;
                    if (!depth && i+1 != selected.size()) complete = false;
                }
            }
            if (complete && !depth) {
                auto content = selected.substr(2, selected.size()-4);
                string converted = "//";
                for (auto c : content) { converted += c; if (c == '\n') converted += "//"; }
                emit("Convert nested block comment to lines", start, end, converted);
            }
        }
        if (detached && selected.starts_with("//") && selected.find("/*") == string::npos &&
            selected.find("*/") == string::npos && selected.find('\n') != string::npos) {
            string converted = "/*";
            bool ok = true;
            for (size_t i = 0; i < selected.size();) {
                auto e = selected.find('\n', i);
                if (e == string::npos) e = selected.size();
                auto first = selected.find_first_not_of(" \t", i);
                if (first >= e || selected.compare(first, 2, "//") != 0) { ok = false; break; }
                converted += selected.substr(i, first-i) + selected.substr(first+2, e-first-2);
                if (e < selected.size()) converted += '\n';
                i = e+1;
            }
            if (ok) emit("Convert multiline comments to block", start, end, converted + "*/");
        }
    }

    // Compile named rules in the document's namespace and import context.
    auto namedRule = [&]() {
        auto rule = string(f.text.text.substr(start, end-start));
        if (!rule.starts_with("// ") || rule.find('\n') != string::npos) return;
        auto arrow = rule.find("==>>");
        if (arrow == string::npos) return;
        vector<string> names, types, uses;
        bool valid = true;
        auto expand = [&](string input, bool pattern) {
            string out;
            for (size_t i = 0; i < input.size();) {
                if (input[i] != '$') { out += input[i++]; continue; }
                size_t a = ++i;
                while (i < input.size() && (isalnum((unsigned char)input[i]) || input[i] == '_')) ++i;
                auto name = input.substr(a, i-a);
                if (name.empty()) { valid = false; break; }
                auto it = find(names.begin(), names.end(), name);
                size_t index = it - names.begin();
                string type;
                if (i < input.size() && input[i] == ':') {
                    a = ++i;
                    while (i < input.size() && (isalnum((unsigned char)input[i]) || input[i] == '_')) ++i;
                    type = input.substr(a, i-a);
                }
                if (it == names.end()) {
                    if (!pattern || (type != "i64" && type != "f64" && type != "bool" && type != "string")) {
                        valid = false; break;
                    }
                    names.push_back(name); types.push_back(type);
                } else if (!type.empty() && type != types[index]) { valid = false; break; }
                uses.push_back(name);
                out += "normal_ssr_capture_" + name;
            }
            return out;
        };
        auto pattern = expand(rule.substr(3, arrow-3), true);
        uses.clear();
        auto replacement = expand(rule.substr(arrow+4), false);
        // Keep the established single-x arithmetic route and its refusals.
        if (names.size() == 1 && names[0] == "x" && pattern.find('(') == string::npos) return;
        if (!valid || names.empty() || !clean(pattern) || !clean(replacement)) return;
        if (f.text.text.find("normal_ssr_") != string::npos) return;
        string parameters;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) parameters += ", ";
            parameters += "normal_ssr_capture_" + names[i] + ": " + types[i];
        }
        string source = string(f.text.text) + "\nfn normal_ssr_pattern(" + parameters + ") { " + pattern +
            " }\nfn normal_ssr_replacement(" + parameters + ") { " + replacement + " }\n";
        Analysis probe; probe.ast = make_unique<Ast>(); auto &ast = *probe.ast; ast.exemplars = true;
        bool failed = false;
        ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
        SourceProvider provider = [&](const string &path) -> const string * {
            return PathKey(path) == PathKey(doc->second.path) ? &source : ws.TextOf(path);
        };
        try {
            RunOnCompilerStack([&]() -> int {
                ParseProgram(ast, doc->second.path, ws.stdlibdirs, DirOf(ws.exe), provider);
                ResolveTypeNames(ast); probe.IndexSource(); TypeCheckProgram(ast, false, true); probe.Build(); return 0;
            });
        } catch (CompileError &) { return; }
        if (failed || !ast.errors.empty()) return;
        const Node *pat = nullptr, *rep = nullptr;
        for (auto sf : ast.functions) {
            if (sf->name == "normal_ssr_pattern" && sf->body && sf->body->stmts.empty()) pat = sf->body->tail;
            if (sf->name == "normal_ssr_replacement" && sf->body && sf->body->stmts.empty()) rep = sf->body->tail;
        }
        if (names.size() == 1 && names[0] == "x" && Is<Binary>(pat)) return;
        auto capture = [&](const Node *n) -> string {
            auto id = Is<Ident>(n);
            return id && id->name.starts_with("normal_ssr_capture_") ? string(id->name.substr(19)) : string();
        };
        auto sameFn = [&](const Call *a, const Call *b) {
            a = a ? SpecializedCall(probe,a) : nullptr;
            b = b ? SpecializedCall(*loc.an,b) : nullptr;
            if (!a || !b || !a->spec || !b->spec) return false;
            auto x = PlaceOfFn(probe, a->spec->sf), y = PlaceOfFn(*loc.an, b->spec->sf);
            return x.Known() && y.Known() && x.start == y.start &&
                probe.files[x.fileidx].uri == loc.an->files[y.fileidx].uri;
        };
        function<bool(const Node *)> supported = [&](const Node *n) {
            if (!n) return false;
            if (!capture(n).empty() || Is<IntLit>(n)) return true;
            if (auto b = Is<Binary>(n)) return (b->op == T_PLUS || b->op == T_MINUS || b->op == T_MUL) && supported(b->left) && supported(b->right);
            if (auto u = Is<Unary>(n)) return u->op == T_MINUS && supported(u->child);
            if (auto c = Is<Call>(n)) {
                c = SpecializedCall(probe,c);
                if (!c || !c->spec || !Is<Ident>(c->callee) || c->ndefaults || c->trailing) return false;
                auto name = c->spec->sf->name;
                size_t count = 0;
                for (auto sf : ast.functions) if (sf->name == name) ++count;
                if (count != 1) return false;
                for (auto &o : f.occs) if (o.IsDeclaration() && o.name == name && o.kind != Occurrence::FN) return false;
                for (auto a : c->args) if (!supported(a)) return false;
                return true;
            }
            return false;
        };
        if (!supported(pat) || !supported(rep)) return;
        vector<RefactorEdit> edits;
        for (auto &span : f.spans) {
            map<string, const Node *> bindings;
            vector<string> order;
            function<bool(const Node *, const Node *)> match = [&](const Node *p, const Node *n) {
                auto name = capture(p);
                if (!name.empty()) {
                    auto type = loc.an->TypeOf(n);
                    auto index = find(names.begin(), names.end(), name) - names.begin();
                    if (!type || TypeText(type) != types[index]) return false;
                    order.push_back(name);
                    auto it = bindings.find(name);
                    if (it == bindings.end()) { bindings[name] = n; return true; }
                    // Repeated captures require equal pure trees and bindings.
                    function<bool(const Node *, const Node *)> equal = [&](const Node *a, const Node *b) {
                        if (Is<Ident>(a)) {
                            auto y = Is<Ident>(b); if (!y) return false;
                            auto ar = extent(a), br = extent(b);
                            auto ao = ar.first >= 0 ? f.At(ar.first) : nullptr;
                            auto bo = br.first >= 0 ? f.At(br.first) : nullptr;
                            if (!ao || !bo) return false;
                            auto ad = DefinitionOf(loc,*ao), bd = DefinitionOf(loc,*bo);
                            return ad.Known() && bd.Known() && ad.fileidx == bd.fileidx && ad.start == bd.start;
                        }
                        if (auto x = Is<IntLit>(a)) { auto y = Is<IntLit>(b); return y && x->val == y->val && x->uns == y->uns; }
                        if (auto x = Is<Binary>(a)) { auto y = Is<Binary>(b); return y && x->op == y->op && equal(x->left,y->left) && equal(x->right,y->right); }
                        return false;
                    };
                    return equal(it->second,n);
                }
                if (auto x = Is<IntLit>(p)) { auto y = Is<IntLit>(n); return y && x->val == y->val && x->uns == y->uns; }
                if (auto x = Is<Binary>(p)) { auto y = Is<Binary>(n); return y && x->op == y->op && match(x->left,y->left) && match(x->right,y->right); }
                if (auto x = Is<Unary>(p)) { auto y = Is<Unary>(n); return y && x->op == y->op && match(x->child,y->child); }
                if (auto x = Is<Call>(p)) {
                    auto y = Is<Call>(n);
                    if (!sameFn(x,y) || x->args.size() != y->args.size()) return false;
                    for (size_t i = 0; i < x->args.size(); ++i) if (!match(x->args[i],y->args[i])) return false;
                    return true;
                }
                return false;
            };
            if (!match(pat,span.node)) continue;
            // A replacement consumes every capture once in original order.
            vector<string> unique;
            for (auto &name : order) if (find(unique.begin(),unique.end(),name) == unique.end()) unique.push_back(name);
            if (unique != uses) continue;
            if (unique.size() != order.size()) {
                function<bool(const Node *)> pure = [&](const Node *n) {
                    if (Is<Ident>(n) || Is<IntLit>(n)) return true;
                    if (auto b = Is<Binary>(n)) return pure(b->left) && pure(b->right);
                    return false;
                };
                bool allPure = true;
                for (auto &binding : bindings) if (!pure(binding.second)) allPure = false;
                if (!allPure) continue;
            }
            auto output = rule.substr(arrow+4);
            string expanded;
            for (size_t i = 0; i < output.size();) {
                if (output[i] != '$') { expanded += output[i++]; continue; }
                size_t a = ++i;
                while (i < output.size() && (isalnum((unsigned char)output[i]) || output[i] == '_')) ++i;
                auto name = output.substr(a,i-a);
                if (i < output.size() && output[i] == ':') { ++i; while (i < output.size() && isalnum((unsigned char)output[i])) ++i; }
                auto value = text(bindings[name]);
                if (value.empty() || !clean(value)) { expanded.clear(); break; }
                expanded += "(" + value + ")";
            }
            auto e = extent(span.node);
            if (!expanded.empty() && e.first >= 0 && clean(text(span.node))) edits.push_back({(size_t)e.first,(size_t)e.second,"("+expanded+")"});
        }
        sort(edits.begin(),edits.end(),[](auto &a,auto &b) { return a.start != b.start ? a.start < b.start : a.end > b.end; });
        vector<RefactorEdit> disjoint;
        for (auto &e : edits) if (disjoint.empty() || e.start >= disjoint.back().end) disjoint.push_back(e);
        if (!disjoint.empty() && CheckRefactor(ws,loc,disjoint)) WriteRefactor(j,loc,doc->second.version,
            "Apply selected structural rule (intended result change)","refactor.rewrite",disjoint);
    };
    namedRule();
    // Compile the selected bounded rule before matching source AST nodes.
    auto rule = string(f.text.text.substr(start, end-start));
    if (rule.rfind("// ", 0) == 0 && rule.find('\n') == string::npos) {
        auto arrow = rule.find("==>>");
        auto trim = [](string s) {
            auto a = s.find_first_not_of(" \t\r");
            auto b = s.find_last_not_of(" \t\r");
            return a == string::npos ? string() : s.substr(a, b-a+1);
        };
        if (arrow != string::npos && rule.find("==>>", arrow+4) == string::npos) {
            auto pattern = trim(rule.substr(3, arrow-3));
            auto replacement = trim(rule.substr(arrow+4));
            auto capture = pattern.find("$x:i64");
            if (capture != string::npos && replacement.find("$x") != string::npos &&
                replacement.find("$x", replacement.find("$x")+2) == string::npos) {
                pattern.replace(capture, 6, "syntax_capture");
                for (size_t at = 0; (at = pattern.find("$x", at)) != string::npos; at += 14)
                    pattern.replace(at, 2, "syntax_capture");
                if (pattern.find('$') == string::npos && clean(pattern)) {
                    auto templateText = replacement;
                    templateText.replace(templateText.find("$x"), 2, "syntax_capture");
                    if (templateText.find('$') != string::npos || !clean(templateText)) return;
                    string source = cat("fn syntax_replacement(syntax_capture: i64) -> i64 { ", templateText,
                        " }\nfn syntax_rule(syntax_capture: i64) -> i64 { ", pattern, " }\nfn main() {}\n");
                    Ast ast;
                    bool failed = false;
                    ast.sink = [&](const Diagnostic &d) { if (!d.warning) failed = true; };
                    SourceProvider provider = [&](const string &path) -> const string * {
                        return PathKey(path) == PathKey(doc->second.path) ? &source : ws.TextOf(path);
                    };
                    try {
                        RunOnCompilerStack([&]() -> int {
                            ParseProgram(ast, doc->second.path, ws.stdlibdirs, DirOf(ws.exe), provider);
                            ResolveTypeNames(ast);
                            TypeCheckProgram(ast, false, true);
                            return 0;
                        });
                    } catch (CompileError &) { failed = true; }
                    function<bool(const Node *)> safeTemplate = [&](const Node *n) {
                        if (auto id = Is<Ident>(n)) return id->name == "syntax_capture";
                        if (Is<IntLit>(n)) return true;
                        if (auto b = Is<Binary>(n)) return
                            (b->op == T_PLUS || b->op == T_MINUS || b->op == T_MUL) &&
                            safeTemplate(b->left) && safeTemplate(b->right);
                        if (auto u = Is<Unary>(n)) return u->op == T_MINUS && safeTemplate(u->child);
                        return false;
                    };
                    for (auto sf : ast.functions) if (sf->name == "syntax_replacement" &&
                        (!sf->body || !sf->body->stmts.empty() || !safeTemplate(sf->body->tail))) failed = true;
                    const Binary *pat = nullptr;
                    if (!failed) for (auto sf : ast.functions)
                        if (sf->name == "syntax_rule" && sf->body) pat = Is<Binary>(sf->body->tail);
                    if (pat && Is<Ident>(pat->left) && Is<Ident>(pat->left)->name == "syntax_capture" &&
                        (Is<IntLit>(pat->right) || (Is<Ident>(pat->right) && Is<Ident>(pat->right)->name == "syntax_capture")) && (pat->op == T_PLUS || pat->op == T_MINUS || pat->op == T_MUL)) {
                        vector<RefactorEdit> edits;
                        set<size_t> seen;
                        for (auto &span : f.spans) {
                            auto bin = Is<Binary>(span.node);
                            if (!bin || bin->op != pat->op || !clean(text(bin))) continue;
                            if (auto lit = Is<IntLit>(pat->right)) {
                                auto actual = Is<IntLit>(bin->right);
                                if (!actual || actual->val != lit->val || actual->uns != lit->uns) continue;
                            } else {
                                function<bool(const Node *, const Node *)> same = [&](const Node *a, const Node *b) {
                                    if (auto x = Is<Ident>(a)) {
                                        auto y = Is<Ident>(b);
                                        if (!y) return false;
                                        auto occurrence = [&](const Node *node, string_view name) -> const Occurrence * {
                                            auto r = extent(node);
                                            const Occurrence *found = nullptr;
                                            for (auto &occ : f.occs) if (occ.kind == Occurrence::IDENT && occ.name == name &&
                                                r.first >= 0 && occ.start >= (size_t)r.first && occ.start < (size_t)r.second) {
                                                if (found) return nullptr;
                                                found = &occ;
                                            }
                                            return found;
                                        };
                                        auto ao = occurrence(a, x->name), bo = occurrence(b, y->name);
                                        if (!ao || !bo) return false;
                                        auto ad = DefinitionOf(loc, *ao), bd = DefinitionOf(loc, *bo);
                                        return ad.Known() && bd.Known() && ad.fileidx == bd.fileidx && ad.start == bd.start;
                                    }
                                    if (auto x = Is<IntLit>(a)) {
                                        auto y = Is<IntLit>(b);
                                        return y && x->val == y->val && x->uns == y->uns &&
                                            ((!x->fixedtype && !y->fixedtype) || (x->fixedtype && y->fixedtype &&
                                             TypeText(x->fixedtype) == TypeText(y->fixedtype)));
                                    }
                                    if (auto x = Is<Binary>(a)) {
                                        auto y = Is<Binary>(b);
                                        return y && x->op == y->op && same(x->left, y->left) && same(x->right, y->right);
                                    }
                                    if (auto x = Is<Unary>(a)) {
                                        auto y = Is<Unary>(b);
                                        return y && x->op == y->op && same(x->child, y->child);
                                    }
                                    return false;
                                };
                                if (!same(bin->left, bin->right)) continue;
                            }
                            auto type = loc.an->TypeOf(bin);
                            auto captured = loc.an->TypeOf(bin->left);
                            if (!type || !captured || TypeText(type) != "i64" || TypeText(captured) != "i64") continue;
                            if (Is<Ident>(bin->left)) {
                                auto pos = extent(bin->left).first;
                                auto occ = pos >= 0 ? f.At(pos) : nullptr;
                                if (!occ || !DefinitionOf(loc, *occ).Known()) continue;
                            }
                            auto e = extent(bin);
                            if (e.first >= 0 && seen.insert(e.first).second)
                                edits.push_back({(size_t)e.first, (size_t)e.second, [&]() {
                                    auto result = replacement;
                                    result.replace(result.find("$x"), 2, cat("(", text(bin->left), ")"));
                                    return replacement == "$x" ? result : cat("(", result, ")");
                                }()});
                        }
                        sort(edits.begin(), edits.end(), [](auto &a, auto &b) {
                            return a.start != b.start ? a.start < b.start : a.end > b.end;
                        });
                        vector<RefactorEdit> disjoint;
                        for (auto &edit : edits)
                            if (disjoint.empty() || edit.start >= disjoint.back().end) disjoint.push_back(edit);
                        edits = std::move(disjoint);
                        if (!edits.empty() && CheckRefactor(ws, loc, edits))
                            WriteRefactor(j, loc, doc->second.version,
                                "Apply selected structural rule (intended result change)", "refactor.rewrite", edits);
                    }
                }
            }
        }
    }
    vector<const Node *> nodes;
    function<void(const Node *)> collect = [&](const Node *n) {
        if (!n) return;
        nodes.push_back(n);
        if (auto fd = Is<FnDecl>(n)) collect(fd->sf->body);
        else n->Children([&](Node *c) { collect(c); });
    };
    for (auto d : loc.an->ast->topdecls) collect(d);
    auto publish = [&](string_view title, vector<RefactorEdit> edits) {
        sort(edits.begin(), edits.end(), [](auto &a, auto &b) { return a.start < b.start; });
        for (size_t k = 1; k < edits.size(); ++k)
            if (edits[k].start < edits[k-1].end) return;
        if (CheckRefactor(ws, loc, edits))
            WriteRefactor(j, loc, doc->second.version, title, "refactor.rewrite", edits);
    };
    for (auto node : nodes) {
        if (auto decl = Is<VarDecl>(node); decl && !decl->isglobal && !decl->isvar &&
            !decl->byref && decl->names.size() == 1 && decl->inits.size() == 1) {
            auto r = extent(decl);
            auto checked = loc.an->Find(node, [](const Node *n) {
                auto d = Is<VarDecl>(n); return d && d->defs.size() == 1;
            });
            auto d = checked ? Is<VarDecl>(checked) : nullptr;
            if (r.first < 0 || start < (size_t)r.first || start > (size_t)r.second || !d ||
                !d->defs[0]->type || d->defs[0]->type->kind != TY_BOOL || !clean(text(decl))) continue;
            string name = "BooleanChoice";
            for (int k = 2; f.text.text.find(name) != string::npos; ++k) name = cat("BooleanChoice", k);
            auto init = text(decl->inits[0]);
            vector<RefactorEdit> edits {{0, 0, cat("enum ", name, " { False, True }\n")},
                {(size_t)r.first, (size_t)r.second, cat("let ", decl->names[0], ": ", name,
                 " = if ", init, " { ", name, ".True } else { ", name, ".False }")}};
            bool ok = true;
            for (auto use : nodes) if (auto id = Is<Ident>(use)) {
                auto resolved = loc.an->Find(use, [](const Node *n) { auto i = Is<Ident>(n); return i && i->vdef; });
                if (!resolved || Is<Ident>(resolved)->vdef != d->defs[0]) continue;
                auto u = extent(use);
                if (u.first < 0) { ok = false; break; }
                edits.push_back({(size_t)u.first, (size_t)u.second, cat("(", id->name, " == ", name, ".True)")});
            }
            if (ok) publish("Convert bool to two-variant enum", edits);
        }
        if (auto fd = Is<FnDecl>(node); fd && fd->sf->body && fd->sf->generics.empty() && fd->sf->ns.empty()) {
            auto sf = fd->sf;
            for (size_t k = 0; k < sf->params.size(); ++k) {
                auto &param = sf->params[k];
                auto pos = f.text.OffsetOf(param.line.line, param.line.col);
                if (start != (size_t)pos || !param.type || param.type->kind != TY_BOOL || param.defaultval || param.isvar) continue;
                vector<VarDef *> defs;
                for (auto def : loc.an->ast->vardefs)
                    if (def->isparam && def->name == param.name && def->line.line == param.line.line && def->line.col == param.line.col) defs.push_back(def);
                if (defs.empty()) continue;
                string name = "BooleanChoice";
                for (int i = 2; f.text.text.find(name) != string::npos; ++i) name = cat("BooleanChoice", i);
                auto typepos = f.text.text.find("bool", pos);
                if (typepos == string::npos) continue;
                vector<RefactorEdit> edits {{0, 0, cat("enum ", name, " { False, True }\n")}, {typepos, typepos+4, name}};
                bool ok = true;
                for (auto use : nodes) {
                    if (auto id = Is<Ident>(use)) {
                        auto resolved = loc.an->Find(use, [](const Node *n) { auto i = Is<Ident>(n); return i && i->vdef; });
                        if (resolved && find(defs.begin(), defs.end(), Is<Ident>(resolved)->vdef) != defs.end()) {
                            auto r = extent(use);
                            if (r.first < 0) { ok = false; break; }
                            edits.push_back({(size_t)r.first, (size_t)r.second, cat("(", id->name, " == ", name, ".True)")});
                        }
                    }
                    if (auto call = Is<Call>(use)) {
                        auto checked = loc.an->Find(use, [](const Node *n) { auto c = Is<Call>(n); return c && c->spec; });
                        if (!checked || Is<Call>(checked)->spec->sf != sf) continue;
                        if (k >= call->args.size() || !Is<Ident>(call->callee) || call->trailing) { ok = false; break; }
                        auto arg = call->args[k]; auto r = extent(arg);
                        if (r.first < 0) { ok = false; break; }
                        edits.push_back({(size_t)r.first, (size_t)r.second,
                            cat("if ", text(arg), " { ", name, ".True } else { ", name, ".False }")});
                    }
                }
                if (ok) publish("Convert bool parameter to two-variant enum", edits);
            }
        }
        auto ed = Is<EnumDecl>(node);
        if (!ed || !ed->en->generics.empty() || !ed->en->ns.empty() || ed->en->broken) continue;
        auto en = ed->en;
        for (auto &v : en->variants) {
            auto pos = f.text.OffsetOf(v.line.line, v.line.col);
            if (start != (size_t)pos || !v.has_payload || v.fields.empty()) continue;
            bool ok = true;
            string fields;
            for (auto &field : v.fields) {
                if (!field.type || field.ispad || field.isconst || field.defaultval) { ok = false; break; }
                auto t = TypeText(field.type);
                if (t.empty() || t == "?") ok = false;
                fields += cat(field.name, ": ", t, ", ");
            }
            auto open = f.text.text.find('{', pos), close = f.text.text.find('}', open);
            if (!ok || close == string::npos || !clean(f.text.text.substr(open, close-open))) continue;
            string name = cat(en->name, v.name, "Payload");
            for (int k = 2; f.text.text.find(name) != string::npos; ++k) name = cat(en->name, v.name, "Payload", k);
            auto insert = f.text.OffsetOf(en->line.line, en->line.col);
            vector<RefactorEdit> edits {{(size_t)insert, (size_t)insert, cat("struct ", name, " { ", fields, "}\n")},
                {open+1, close, cat(" payload: ", name, " ")}};
            for (auto use : nodes) {
                if (auto literal = Is<StructLit>(use)) {
                    auto checked = loc.an->Find(use, [](const Node *n) { auto s = Is<StructLit>(n); return s && s->variant; });
                    if (!checked || Is<StructLit>(checked)->variant != &v) continue;
                    auto r = extent(use);
                    if (r.first < 0 || literal->defaultall || literal->inits.size() != v.fields.size()) { ok = false; break; }
                    vector<const FieldInit *> inits;
                    for (auto &i : literal->inits) {
                        if (i.name.empty() || i.fromdefault) ok = false;
                        inits.push_back(&i);
                    }
                    sort(inits.begin(), inits.end(), [&](auto a, auto b) { return extent(a->val).first < extent(b->val).first; });
                    string values;
                    for (auto i : inits) values += cat(i->name, ": ", text(i->val), ", ");
                    edits.push_back({(size_t)r.first, (size_t)r.second,
                        cat(text(use).substr(0, text(use).find('{')), "{ payload: ", name, " { ", values, "} }")});
                }
                if (auto dot = Is<Dot>(use)) {
                    auto t = loc.an->TypeOf(dot->obj);
                    while (t && t->kind == TY_REF) t = t->ref->sub;
                    if (!t || t->kind != TY_VARIANT || t->var->variant != &v) continue;
                    auto r = extent(use);
                    if (r.first < 0) { ok = false; break; }
                    edits.push_back({(size_t)r.first, (size_t)r.second, cat(text(dot->obj), ".payload.", dot->name)});
                }
            }
            if (ok) publish("Extract struct from enum variant", edits);
        }
    }
    function<void(const Node *)> walk = [&](const Node *n) {
        if (!n) return;
        auto e = extent(n);
        if (e.first == (int64_t)start && e.second == (int64_t)end) {
            if (auto decl = Is<VarDecl>(n); decl && decl->type &&
                IsIntT(decl->type) && decl->inits.size() == 1 && clean(text(n))) {
                auto init = decl->inits[0];
                if (auto id = Is<Ident>(init); id && !loc.an->ast->LookupGlobal(id->name, ""))
                    fix("TODO: Generate missing integer constant with zero default", f.text.text.size(), f.text.text.size(),
                        cat("\nlet ", id->name, ": ", TypeText(decl->type), " = 0;\n"));
                if (auto call = Is<Call>(init); call && Is<Ident>(call->callee) &&
                    !call->trailing && call->tyargs.empty() && available(Is<Ident>(call->callee)->name)) {
                    string params;
                    bool ok = true;
                    for (size_t k = 0; k < call->args.size(); ++k) {
                        auto type = loc.an->TypeOf(call->args[k]);
                        if (!clean(text(call->args[k]))) { ok = false; break; }
                        if (k) params += ", ";
                        params += cat("arg", k);
                        if (type) params += cat(": ", TypeText(type));
                    }
                    if (ok) fix("TODO: Generate missing function with zero default", f.text.text.size(), f.text.text.size(),
                        cat("\nfn ", Is<Ident>(call->callee)->name, "(", params, ") -> ", TypeText(decl->type), " { return 0; }\n"));
                }
            }
            if (auto call = Is<Call>(n); call && !call->trailing && call->tyargs.empty() &&
                Is<Ident>(call->callee) && call->args.size() == 2 && clean(text(n))) {
                auto x = extent(call->args[0]), y = extent(call->args[1]);
                if (x.first >= e.first && y.second <= e.second && x.second < y.first) {
                    auto replacement = text(n);
                    replacement.replace(y.first-e.first, y.second-y.first, text(call->args[0]));
                        replacement.replace(x.first-e.first, x.second-x.first, text(call->args[1]));
                    emit("Swap two call arguments (changes argument roles and evaluation order)", start, end, replacement);
                }
            }
            if (auto bin = Is<Binary>(n); bin && clean(text(n)) &&
                (bin->op == T_PLUS || bin->op == T_MINUS || bin->op == T_MUL)) {
                auto x = extent(bin->left), y = extent(bin->right);
                if (x.first >= e.first && y.second <= e.second && x.second < y.first) {
                    auto middle = string(f.text.text.substr(x.second, y.first-x.second));
                    auto symbol = middle.find_first_not_of(" \t\r\n");
                    if (symbol != string::npos) {
                        for (auto op : {T_PLUS, T_MINUS, T_MUL}) if (op != bin->op) {
                            auto replacement = cat("(", text(bin->left), ") ",
                                op == T_PLUS ? '+' : op == T_MINUS ? '-' : '*',
                                " (", text(bin->right), ")");
                            emit(cat("Replace arithmetic with ", op == T_PLUS ? "+" : op == T_MINUS ? "-" : "*",
                                     " (changes result)"), start, end, replacement);
                        }
                        auto replacement = text(n);
                        replacement.replace(y.first-e.first, y.second-y.first, cat("(", text(bin->left), ")"));
                        replacement.replace(x.first-e.first, x.second-x.first, cat("(", text(bin->right), ")"));
                        emit("Flip binary operands (changes result and evaluation order)", start, end, replacement);
                    }
                }
            }
            if (auto sl = Is<StructLit>(n); sl && !sl->defaultall && clean(text(n))) {
                auto checked = loc.an->Find(n, [](const Node *node) {
                    auto literal = Is<StructLit>(node);
                    return literal && literal->sinst;
                });
                auto instance = checked ? Is<StructLit>(checked)->sinst : nullptr;
                if (instance) {
                auto &fields = instance->st->fields;
                bool ok = sl->inits.size() == fields.size() && !fields.empty();
                vector<const FieldInit *> original;
                for (auto &init : sl->inits) {
                    if (init.name.empty() || init.fromdefault || !loc.an->TypeOf(init.val) ||
                        TypeText(loc.an->TypeOf(init.val)) == "?") ok = false;
                    original.push_back(&init);
                }
                sort(original.begin(), original.end(), [&](auto a, auto b) {
                    return extent(a->val).first < extent(b->val).first;
                });
                string prefix = "syntax_field_value_";
                if (f.text.text.find(prefix) != string::npos) ok = false;
                if (ok) {
                    string replacement = "{ ";
                    map<string_view, string> names;
                    bool reordered = false;
                    for (size_t k = 0; k < original.size(); ++k) {
                        auto init = original[k];
                        auto name = cat(prefix, k);
                        names[init->name] = name;
                        replacement += cat("let ", name, ": ", TypeText(loc.an->TypeOf(init->val)),
                                           " = ", text(init->val), "; ");
                        if (fields[k].name != init->name) reordered = true;
                    }
                    replacement += cat(TypeText(sl->type), " { ");
                    for (auto &field : fields) replacement += cat(field.name, ": ", names[field.name], ", ");
                    replacement += "} }";
                    if (reordered) {
                        emit("Reorder constructor fields", start, end, replacement);
                        fix("TODO: Reorder effectful constructor fields preserving source order", start, end, replacement);
                    }
                }
                }
            }
            if (auto loop = Is<ForLoop>(n); loop && !loop->byref && loop->idxvar.empty() && clean(text(n))) {
                auto r = Is<RangeExpr>(loop->iter);
                auto types = r ? loc.an->TypesOf(r->lo, 2) : vector<TypeExpr *>();
                if (r && types.size() == 1 && IsIntT(types[0])) {
                    auto type = loop->vartype ? TypeText(loop->vartype) : TypeText(types[0]);
                    string counter = "syntax_range_counter", limit = "syntax_range_limit";
                    if (f.text.text.find(counter) == string::npos && f.text.text.find(limit) == string::npos)
                        emit("Convert range for to while", start, end,
                            cat("{ var ", counter, ": ", type, " = ", text(r->lo),
                                "; let ", limit, ": ", type, " = ", text(r->hi),
                                "; while ", counter, " < ", limit, " { let ", loop->var,
                                loop->vartype ? cat(": ", TypeText(loop->vartype)) : string(),
                                " = ", counter, "; ", counter, " += 1; ", text(loop->body), " } }"));
                }
            }
            if (auto block = Is<Block>(n); block && ((block->stmts.size() == 2 && !block->tail) ||
                (block->stmts.size() == 1 && block->tail)) && clean(text(n))) {
                auto guard = Is<IfExpr>(block->stmts[0]);
                auto body = Is<Block>(block->tail ? block->tail : block->stmts[1]);
                if (guard && !guard->elseb && body) {
                    auto last = guard->thenb->tail;
                    if (!last && !guard->thenb->stmts.empty()) last = guard->thenb->stmts.back();
                    if (auto ret = Is<Return>(last); ret && ret->from.empty())
                        emit("Move return guard into else", start, end,
                             cat("{ if !(", text(guard->cond), ") ", text(body), " else ", text(guard->thenb), " }"));
                }
            }
            if (auto i = Is<IfExpr>(n)) {
                auto outer = i->thenb;
                const Node *single = outer->tail;
                if (!single && outer->stmts.size() == 1) single = outer->stmts[0];
                if (auto inner = Is<IfExpr>(single); inner && !i->elseb && !inner->elseb &&
                    (outer->stmts.empty() || (outer->stmts.size() == 1 && !outer->tail)) && clean(text(n)))
                    emit("Merge nested if conditions", start, end,
                         cat("if (", text(i->cond), ") && (", text(inner->cond), ") ", text(inner->thenb)));
                if (clean(text(n))) {
                    emit("Unwrap then branch (always executes selected branch)", start, end, text(outer));
                    if (auto other = Is<Block>(i->elseb))
                        emit("Unwrap else branch (always executes selected branch)", start, end, text(other));
                    if (auto other = Is<Block>(i->elseb); other && !outer->tail && !other->tail &&
                        outer->stmts.size() == 1 && other->stmts.size() == 1) {
                        auto left = Is<Assign>(outer->stmts[0]);
                        auto right = Is<Assign>(other->stmts[0]);
                        if (left && right && left->op == T_ASSIGN && right->op == T_ASSIGN &&
                            Is<Ident>(left->lval) && Is<Ident>(right->lval) &&
                            text(left->lval) == text(right->lval))
                            emit("Pull assignment above branches", start, end,
                                cat(text(left->lval), " = if ", text(i->cond), " { ",
                                    text(left->rhs), " } else { ", text(right->rhs), " };"));
                    }
                }
                auto last = outer->tail;
                if (!last && !outer->stmts.empty()) last = outer->stmts.back();
                if (auto r = Is<Return>(last); r && r->from.empty() && i->elseb &&
                    Is<Block>(i->elseb) && clean(text(n)))
                    emit("Remove else after return", start, end,
                         cat("if ", text(i->cond), " ", text(outer), " ", text(i->elseb)));
                if (auto other = Is<Block>(i->elseb); other && clean(text(n))) {
                    auto lastElse = other->tail;
                    if (!lastElse && !other->stmts.empty()) lastElse = other->stmts.back();
                    if (auto ret = Is<Return>(lastElse); ret && ret->from.empty())
                        emit("Convert else return to guard", start, end,
                            cat("if !(", text(i->cond), ") ", text(other), " ", text(outer)));
                }
            }
            if (auto m = Is<MatchExpr>(n); m && clean(text(n))) {
                auto types = loc.an->TypesOf(m->scrutinee, 2);
                if (types.size() == 1 && IsIntT(types[0]))
                    for (auto &arm : m->arms) for (auto &item : arm.pat.items)
                        if (auto id = Is<Ident>(item.lo); id && !item.hi &&
                            !loc.an->ast->LookupGlobal(id->name, ""))
                            fix("TODO: Generate missing integer match constant with zero default",
                                f.text.text.size(), f.text.text.size(),
                                cat("\nlet ", id->name, ": ", TypeText(types[0]), " = 0;\n"));
                if (types.size() == 1 && AdtOf(types[0])) {
                    auto en = AdtOf(types[0])->enu->en;
                    bool plain = true;
                    set<string_view> covered;

                    for (auto &arm : m->arms) {
                        if (arm.expansion || !arm.pat.binder.empty()) plain = false;
                        for (auto &item : arm.pat.items) {
                            auto id = Is<Ident>(item.lo);
                            if (!id || item.hi || !en->FindVariant(id->name)) plain = false;
                            else covered.insert(id->name);
                        }
                    }
                    for (auto &arm : m->arms) if (!arm.expansion && arm.pat.binder.empty())
                        for (auto &item : arm.pat.items) if (auto id = Is<Ident>(item.lo);
                            id && !item.hi && !en->FindVariant(id->name) && en->ns.empty() && en->generics.empty() && en->line.fileidx == f.fileidx) {
                            auto at = f.text.OffsetOf(en->end.line, en->end.col);
                            if (at > 0 && f.text.text[at-1] == '}') {
                                auto last = f.text.text.find_last_not_of(" \t\r\n", at-2);
                                auto comma = last != string::npos && f.text.text[last] != ',' && f.text.text[last] != '{' ? ", " : " ";
                                fix("TODO: Generate missing payload-free enum variant", at-1, at-1, cat(comma, id->name, " "));
                            }
                        }
                    bool wildcard = false;
                    for (auto &arm : m->arms) if (arm.pat.kind == P_WILDCARD) wildcard = true;
                    if (plain && !wildcard) {
                        string added;
                        for (auto &v : en->variants) if (!covered.count(v.name))
                            added += cat(v.name, " => abort(\"TODO: implement match arm\"), ");
                        if (!added.empty() && end && f.text.text[end-1] == '}') {
                            auto before = string(f.text.text.substr(start, end-start-1));
                            auto last = before.find_last_not_of(" \t\r\n");
                            string comma = last != string::npos && before[last] != ',' && before[last] != '{' ? ", " : " ";
                            fix("TODO: Insert missing enum match arms", end-1, end-1, comma + added);
                        }
                    }
                    if (plain) for (auto &arm : m->arms) if (!arm.expansion && arm.pat.kind == P_WILDCARD) {
                        auto body = extent(arm.body);
                        if (body.first < e.first) continue;
                        auto prefix = text(m).rfind("_", (size_t)(body.first-e.first));
                        if (prefix == string::npos) continue;
                        auto x = (size_t)e.first + prefix;
                        auto between = string(f.text.text.substr(x+1, body.first-x-1));
                        between.erase(remove_if(between.begin(), between.end(), [](char c) {
                            return c==' ' || c=='\n' || c=='\r' || c=='\t';
                        }), between.end());
                        if (between != "=>") continue;
                        string expanded;
                        for (auto &v : en->variants) if (!covered.count(v.name)) {
                            if (!expanded.empty()) expanded += ", ";
                            expanded += cat(v.name, " => ", text(arm.body));
                        }
                        if (!expanded.empty()) emit("Expand exhaustive enum arms", x, body.second, expanded);
                    }
                }
                for (auto &arm : m->arms) {
                    if (arm.expansion || arm.pat.byref || arm.pat.items.size() < 2) continue;
                    vector<string> patterns;
                    bool literal = true;
                    for (auto &p : arm.pat.items) {
                        if (!p.lo || p.hi || (!Is<IntLit>(p.lo) && !Is<Ident>(p.lo) && !Is<Dot>(p.lo))) { literal = false; break; }
                        patterns.push_back(text(p.lo));
                    }
                    if (!literal) continue;
                    auto x = extent(arm.pat.items.front().lo).first;
                    auto y = extent(arm.body).second;
                    if (x < 0 || y <= x) continue;
                    string flipped, split;
                    for (size_t k = 0; k < patterns.size(); ++k) {
                        if (k) { flipped += ", "; split += ", "; }
                        flipped += patterns[patterns.size()-1-k];
                        split += cat(patterns[k], arm.pat.binder.empty() ? string() : cat(" ", arm.pat.binder), " => ", text(arm.body));
                    }
                    emit("Flip match pattern list", x, y, cat(flipped, arm.pat.binder.empty() ? string() : cat(" ", arm.pat.binder), " => ", text(arm.body)));
                    emit("Unmerge match pattern list", x, y, split);
                    break;
                }
                for (size_t k = 1; k < m->arms.size(); ++k) {
                    auto &a = m->arms[k-1]; auto &b = m->arms[k];
                    auto normalized = [&](const Node *body, size_t index) {
                        auto r = extent(body);
                        if (r.first < 0) return string();
                        bool safe = true;
                        function<void(const Node *)> check = [&](const Node *n) {
                            if (Is<VarDecl>(n) || Is<FnDecl>(n) || Is<FunVal>(n) || Is<ForLoop>(n) || Is<MatchExpr>(n)) safe = false;
                            n->Children([&](Node *c) { check(c); });
                        };
                        check(body);
                        if (!safe) return string();
                        const Occurrence *binder = nullptr;
                        for (auto &occ : f.occs) if (occ.kind == Occurrence::BINDER && occ.node == m && occ.index == (int)index) binder = &occ;
                        string result = text(body);
                        vector<RefactorEdit> substitutions;
                        for (auto &occ : f.occs) if (occ.kind == Occurrence::IDENT && occ.start >= (size_t)r.first && occ.start < (size_t)r.second) {
                            auto def = DefinitionOf(loc, occ);
                            if (!def.Known()) return string();
                            auto key = binder && def.fileidx == f.fileidx && def.start == binder->start
                                ? string("@payload") : cat("@", def.fileidx, ":", def.start);
                            substitutions.push_back({occ.start-(size_t)r.first, occ.start+occ.len-(size_t)r.first, key});
                        }
                        sort(substitutions.begin(), substitutions.end(), [](auto &a, auto &b) { return a.start > b.start; });
                        for (auto &sub : substitutions) result.replace(sub.start, sub.end-sub.start, sub.text);
                        return result;
                    };
                    auto leftBody = normalized(a.body, k-1), rightBody = normalized(b.body, k);
                    if (a.expansion || b.expansion || a.pat.byref || b.pat.byref ||
                        a.pat.binder.empty() != b.pat.binder.empty() ||
                        a.pat.items.size()!=1 || b.pat.items.size()!=1 ||
                        a.pat.items[0].hi || b.pat.items[0].hi ||
                        !a.pat.items[0].lo || !b.pat.items[0].lo ||
                        (leftBody.empty() || leftBody != rightBody)) continue;
                    auto x=extent(a.pat.items[0].lo).first, y=extent(b.body).second;
                    if (x>=0 && y>x) emit("Merge adjacent match arms", x, y,
                        cat(text(a.pat.items[0].lo), ", ", text(b.pat.items[0].lo), a.pat.binder.empty() ? string() : cat(" ", a.pat.binder), " => ", text(a.body)));
                    break;
                }
            }
        }
        if (auto sd = Is<StructDecl>(n); sd && start == (size_t)f.text.OffsetOf(sd->st->line.line, sd->st->line.col)) {
            auto st = sd->st;
            bool plain = st->generics.empty() && st->ns.empty() && !st->broken;
            for (auto &field : st->fields)
                if (!field.type || field.ispad || field.defaultval || field.isconst) plain = false;
            if (plain && !st->fields.empty()) {
                auto pos = f.text.OffsetOf(st->end.line, st->end.col);
                string parameters, values;
                for (auto &field : st->fields) {
                    if (!parameters.empty()) { parameters += ", "; values += ", "; }
                    parameters += cat(field.name, ": ", TypeText(field.type));
                    values += cat(field.name, ": ", field.name);
                    if (available(cat("set_", st->name, "_", field.name))) emit(cat("Generate setter for ", field.name), pos, pos,
                        cat("\nfn set_", st->name, "_", field.name, "(value: ", st->name,
                            "&, replacement: ", TypeText(field.type), ") { value.", field.name, " = replacement; }\n"));
                    if (available(cat("get_", st->name, "_", field.name))) emit(cat("Generate getter for ", field.name), pos, pos,
                        cat("\nfn get_", st->name, "_", field.name, "(value: ", st->name, ") { return value.", field.name, "; }\n"));
                }
                if (available(cat("new_", st->name))) emit("Generate new constructor", pos, pos,
                    cat("\nfn new_", st->name, "(", parameters, ") { return ", st->name, " { ", values, " }; }\n"));
            }
        }
        if (auto ed = Is<EnumDecl>(n); ed && f.text.OffsetOf(ed->en->line.line, ed->en->line.col) >= 0 &&
            start == (size_t)f.text.OffsetOf(ed->en->line.line, ed->en->line.col)) {
            auto en = ed->en;
            bool plain = en->generics.empty() && en->ns.empty() && !en->broken;
            if (plain) for (auto &v : en->variants) if (v.has_payload)
                for (auto &field : v.fields) if (field.type && !field.ispad && !field.isconst) {
                    auto name = cat("project_", en->name, "_", v.name, "_", field.name);
                    if (available(name)) emit(cat("Generate checked payload projection for ", v.name, ".", field.name),
                        f.text.OffsetOf(en->end.line, en->end.col), f.text.OffsetOf(en->end.line, en->end.col),
                        cat("\nfn ", name, "(value: ", en->name, ") -> ", TypeText(field.type),
                            " { match value { ", v.name, " payload => payload.", field.name, ", _ => abort(\"wrong enum variant\") } }\n"));
                }
            for (auto &v : en->variants) if (v.has_payload) plain = false;
            if (plain) for (auto &v : en->variants) {
                auto pos = f.text.OffsetOf(en->end.line, en->end.col);
                if (available(cat("is_", v.name))) emit(cat("Generate enum is_", v.name), pos, pos,
                     cat("\nfn is_", v.name, "(value: ", en->name, ") -> bool { match value { ",
                         v.name, " => true, _ => false } }\n"));
            }
        }
        if (auto decl = Is<VarDecl>(n); decl && decl->inits.size() == 1) {
            auto init = decl->inits[0]; auto r = extent(init);
            if (r.first == (int64_t)start && r.second == (int64_t)end &&
                !Is<Block>(init) && clean(text(init)))
                emit("Add braces to initializer", start, end, cat("{ ", text(init), " }"));
        }
        if (auto match = Is<MatchExpr>(n)) for (auto &arm : match->arms) {
            auto r = extent(arm.body);
            if (r.first == (int64_t)start && r.second == (int64_t)end &&
                !Is<Block>(arm.body) && clean(text(arm.body)))
                emit("Add braces to match arm", start, end, cat("{ ", text(arm.body), " }"));
        }
        if (auto fd = Is<FnDecl>(n)) {
            auto sf = fd->sf;
            auto pos = f.text.OffsetOf(sf->line.line, sf->line.col);
            if (sf->body) for (auto &param : sf->params) {
                auto at = f.text.OffsetOf(param.line.line, param.line.col);
                if (start != (size_t)at || param.name == "_") continue;
                vector<VarDef *> bindings;
                for (auto v : loc.an->ast->vardefs)
                    if (v->isparam && v->name == param.name && v->line.line == param.line.line &&
                        v->line.col == param.line.col) bindings.push_back(v);
                bool used = bindings.empty();
                function<void(const Node *)> uses = [&](const Node *node) {
                    if (!node) return;
                    if (auto id = Is<Ident>(node); id &&
                        (id->name == param.name ||
                         find(bindings.begin(), bindings.end(), id->vdef) != bindings.end())) used = true;
                    node->Children([&](Node *child) { uses(child); });
                };
                uses(sf->body);
                auto body = extent(sf->body);
                if (!used && body.first >= 0 && f.text.text[body.first] == '{')
                    emit("Bind unused parameter", body.first+1, body.first+1, cat(" let _ = ", param.name, ";"));
            }
            if (pos >= 0 && start == (size_t)pos && sf->doc.empty() && sf->body) {
                string documentation = "/// " + string(sf->name) + ".\n";
                emit("Generate documentation template", pos, pos, documentation);
            }
            if (start == (size_t)pos && sf->name == "len" && sf->ns.empty() && sf->generics.empty() &&
                sf->params.size() == 1 && sf->params[0].type && !sf->params[0].defaultval && !sf->params[0].isvar) {
                auto at = f.text.OffsetOf(sf->end.line, sf->end.col);
                if (available("is_empty")) emit("Generate is_empty from len", at, at,
                    cat("\nfn is_empty(value: ", TypeText(sf->params[0].type), ") -> bool { return len(value) == 0; }\n"));
            }
            walk(sf->body);
        } else n->Children([&](Node *child) { walk(child); });
    };
    for (auto d : loc.an->ast->topdecls) walk(d);
    if (start == 0 && end == f.text.text.size() && clean(f.text.text)) {
        vector<pair<string, string>> declarations;
        set<string_view> names;
        bool ok = true;
        for (auto node : loc.an->ast->topdecls) {
            auto fn = Is<FnDecl>(node);
            if (!fn || !fn->sf->ns.empty() || !fn->sf->doc.empty() ||
                !names.insert(fn->sf->name).second) { ok = false; break; }
            auto sf = fn->sf;
            auto x = f.text.OffsetOf(sf->line.line, sf->line.col);
            auto y = f.text.OffsetOf(sf->end.line, sf->end.col);
            if (x < 0 || y <= x) { ok = false; break; }
            declarations.emplace_back(string(sf->name), string(f.text.text.substr(x, y-x)));
        }
        if (ok && declarations.size() > 1) {
            sort(declarations.begin(), declarations.end());
            string replacement;
            for (auto &decl : declarations) replacement += decl.second + "\n";
            if (replacement != f.text.text) emit("Sort function declarations", start, end, replacement);
        }
    }

    if (end > start+2 && f.text.text[start]=='(' && f.text.text[end-1]==')') {
        auto candidate = string(f.text.text);
        auto replacement = candidate.substr(start+1,end-start-2);
        candidate.replace(start,end-start,replacement);
        string before, after, beforeDocs, afterDocs, why;
        if (format::ParseForCheck(doc->second.path, string(f.text.text), before, beforeDocs, why) &&
            format::ParseForCheck(doc->second.path, candidate, after, afterDocs, why) &&
            before == after && beforeDocs == afterDocs)
            emit("Remove redundant parentheses", start,end,replacement);
    }
}
