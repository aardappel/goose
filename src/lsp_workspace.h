// Goose compiler — the language server's workspace (docs/design/
// language_server.md §4): the documents the editor has open, served to the
// compiler before the disk; the roots, which are the programs analyzed
// (the entry files the editor configured, and the open files none of
// those reaches); when each is analyzed again; and the diagnostics
// published for a root's files.
#pragma once

namespace goose {

// In main.cpp.
vector<string> StdlibDirs(const string &stdlibdir, const string &exe);
void ParseProgram(Ast &ast, const string &rootpath, const vector<string> &stdlibdirs,
                  const string &exedir, const SourceProvider &sources);
int RunOnCompilerStack(const function<int()> &fn);

inline bool ExpandSimpleSnippet(const string &body, string &expanded) {
    vector<pair<bool, string>> parts;
    map<string, string> defaults;
    auto escaped = [&](size_t n) {
        return body[n] == '\\' && n + 1 < body.size() &&
            (body[n + 1] == '$' || body[n + 1] == '}' || body[n + 1] == '\\');
    };
    for (size_t i = 0; i < body.size();) {
        if (escaped(i)) {
            parts.emplace_back(false, string(1, body[i + 1]));
            i += 2; continue;
        }
        if (body[i] != '$') {
            parts.emplace_back(false, string(1, body[i++])); continue;
        }
        auto n = i + 1;
        bool brace = n < body.size() && body[n] == '{';
        if (brace) n++;
        auto digits = n;
        while (n < body.size() && isdigit((unsigned char)body[n])) n++;
        if (digits == n) return false;
        auto index = body.substr(digits, n - digits);
        auto nonzero = index.find_first_not_of('0');
        index = nonzero == string::npos ? "0" : index.substr(nonzero);
        if (brace) {
            if (n < body.size() && body[n] == ':') {
                n++;
                string value;
                while (n < body.size() && body[n] != '}') {
                    if (body[n] == '$' || body[n] == '{') return false;
                    if (escaped(n)) n++;
                    value += body[n++];
                }
                auto [it, inserted] = defaults.emplace(index, value);
                if (!inserted && it->second != value) return false;
            }
            if (n == body.size() || body[n] != '}') return false;
            n++;
        }
        parts.emplace_back(true, index);
        i = n;
    }
    for (auto &[placeholder, value] : parts)
        expanded += placeholder ? defaults[value] : value;
    return true;
}

struct Document {
    string path;        // Canonical.
    string text;
    int64_t version = 0;
};

// A diagnostic as the protocol's JSON. Its key, the range, severity and
// message it starts with, is what two roots' reports of one error in a
// file both reach share; the related lines after it, an instantiation
// chain, may differ by root.
struct PublishedDiagnostic {
    string json;
    size_t keylen = 0;
    string_view Key() const { return string_view(json).substr(0, keylen); }
};

struct Root {
    string path, key;               // CanonicalPath and PathKey.
    bool indexed = false;
    bool configured = false;        // An entry file the editor configured, else an open file.
    unique_ptr<Analysis> analysis;  // The latest that got past resolution, which queries read.
    set<string> files;              // Keys of the files the latest attempt reached.
    // The diagnostics the latest attempt gave each file it reached, by
    // URI, an empty list for one it found none in.
    map<string, vector<PublishedDiagnostic>> published;
    bool dirty = true;
    bool discardFailedAnalysis = false;
    chrono::steady_clock::time_point due;

    // Whether the root is the file or its latest attempt reached it.
    bool Reaches(const string &k) const { return k == key || files.count(k) > 0; }
};

struct Workspace {
    Transport &rpc;
    string exe;
    vector<string> stdlibdirs;
    map<string, Document> open;     // By PathKey.
    vector<unique_ptr<Root>> roots;
    set<string> workspaceDirs, declaredDirs, excludedDirs, packageExcludes;
    bool indexComplete = true;
    bool discoveryPending = false;

    bool Excluded(const string &path) const {
        auto key = PathKey(path);
        for (auto &dir : excludedDirs)
            if (key == dir || key.rfind(dir + "/", 0) == 0) return true;
        for (auto &dir : packageExcludes)
            if (key == dir || key.rfind(dir + "/", 0) == 0) return true;
        return false;
    }

    void Discover() {
        discoveryPending = true;
        indexComplete = false;
    }

    void Scan() {
        auto epoch = rpc.scanEpoch.load(memory_order_relaxed);
        auto interrupted = [&] {
            return rpc.scanEpoch.load(memory_order_relaxed) != epoch;
        };
        {
            lock_guard<mutex> lock(rpc.m);
            for (auto &msg : rpc.queue)
                if (msg.exit || msg.method == "shutdown" || msg.method == "exit" ||
                    msg.method.rfind("workspace/did", 0) == 0 ||
                    msg.method.rfind("textDocument/did", 0) == 0) return;
        }
        auto previousExcludes = packageExcludes;
        auto abort = [&] {
            packageExcludes = previousExcludes;
            indexComplete = false;
            RequestIndexStatus("interrupted", 0, 0);
        };
        RequestIndexStatus("scanning", 0, 0);
        set<string> found;
        size_t visited = 0;
        indexComplete = true;
        packageExcludes.clear();
        for (auto &r : roots) if (r->configured) {
            Package pkg { PathStem(r->path), DirOf(r->path), false };
            try {
                LoadSidecar(pkg, true);
                for (auto &path : pkg.exclude) {
                    auto key = PathKey(path);
                    while (key.size() > 1 && key.back() == '/') key.pop_back();
                    packageExcludes.insert(key);
                }
            } catch (CompileError &) { indexComplete = false; }
        }
        for (auto &dir : workspaceDirs) {
            std::error_code ec;
            filesystem::recursive_directory_iterator it(dir,
                filesystem::directory_options::none, ec), end;
            if (ec) indexComplete = false;
            while (it != end && !ec) {
                if (interrupted()) { abort(); return; }
                if (++visited > 4096) { indexComplete = false; break; }
                auto path = it->path();
                auto name = path.filename().string();
                if (it->is_symlink(ec) || Excluded(path.string()) ||
                    (it->is_directory(ec) && (name[0] == '.' || name == "build" || name == "node_modules" ||
                        filesystem::exists(path / ".git", ec)))) {
                    it.disable_recursion_pending();
                } else if (it->is_regular_file(ec) && path.extension() == ".goose") {
                    if (found.size() >= 256) { indexComplete = false; break; }
                    found.insert(CanonicalPath(path.string()));
                }
                if (it.depth() >= 16) { it.disable_recursion_pending(); indexComplete = false; }
                it.increment(ec);
            }
            if (ec) indexComplete = false;
            if (!indexComplete) break;
        }
        if (interrupted()) { abort(); return; }
        discoveryPending = false;
        RequestIndexStatus(indexComplete ? "complete" : "incomplete", visited, found.size());
        for (auto &r : roots) r->indexed = false;
        for (auto &path : found) {
            bool covered = false;
            for (auto &entry : roots)
                if (entry->configured && entry->key != PathKey(path) &&
                    entry->Reaches(PathKey(path))) covered = true;
            if (covered) continue;
            auto r = FindRoot(PathKey(path));
            if (!r) r = AddRoot(path, false);
            r->indexed = true;
        }
        PruneUnreached();
        for (auto &r : roots) {
            r->discardFailedAnalysis = true;
            Dirty(*r, 0);
        }
    }

    void RequestIndexStatus(const string &state, size_t entries, size_t sources) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Member("method", "window/logMessage");
        j.Key("params"); j.BeginObject();
        j.Member("type", (int64_t)3);
        j.Member("message", cat("workspace index ", state, ": ", entries,
            "/4096 entries, ", sources, "/256 sources, depth limit 16"));
        j.EndObject(); j.EndObject();
        rpc.Send(j.s);
    }

    static string FolderPath(const string &path) {
        auto canonical = CanonicalPath(path);
        while (canonical.size() > 1 && canonical.back() == '/') canonical.pop_back();
        return canonical;
    }

    void ConfigureFolders(const Json &params) {
        if (auto folders = params.Get("workspaceFolders"); folders && folders->kind == Json::ARRAY)
            for (auto &folder : folders->arr) {
                auto path = PathOfUri(JsonStr(folder.Get("uri")));
                if (!path.empty()) workspaceDirs.insert(FolderPath(path));
            }
        else if (auto uri = params.Get("rootUri"); uri && uri->kind == Json::STRING)
            if (!PathOfUri(uri->str).empty()) workspaceDirs.insert(FolderPath(PathOfUri(uri->str)));
        declaredDirs = workspaceDirs;
    }
    bool watchDynamic = false, watchRelative = false, watchStarted = false;
    string watchRequest, watchRegistration;
    string watchState = "unsupported";

    void WatchStatus(const string &state) {
        watchState = state;
        JsonOut j;
        j.BeginObject(); j.Member("jsonrpc", "2.0");
        j.Member("method", "window/logMessage");
        j.Key("params"); j.BeginObject(); j.Member("type", 3);
        j.Member("message", cat("workspace file watchers: ", state));
        j.EndObject(); j.EndObject(); rpc.Send(j.s);
    }

    void RegisterWatchers() {
        if (!watchStarted) return;
        if (!watchDynamic) { WatchStatus("unsupported; disk updates require client notifications"); return; }
        if (!watchRegistration.empty()) {
            JsonOut u;
            u.BeginObject(); u.Member("jsonrpc", "2.0");
            u.Member("id", cat("goose-unwatch-", ++requests));
            u.Member("method", "client/unregisterCapability");
            u.Key("params"); u.BeginObject(); u.Key("unregisterations");
            u.BeginArray(); u.BeginObject(); u.Member("id", watchRegistration);
            u.Member("method", "workspace/didChangeWatchedFiles");
            u.EndObject(); u.EndArray(); u.EndObject(); u.EndObject(); rpc.Send(u.s);
        }
        watchRegistration.clear(); watchRequest.clear();
        if (declaredDirs.empty()) { WatchStatus("inactive; no declared workspace folders"); return; }
        watchRegistration = cat("goose-watch-", ++requests);
        watchRequest.clear(); JsonQuote(watchRequest, watchRegistration);
        JsonOut j;
        j.BeginObject(); j.Member("jsonrpc", "2.0");
        j.Member("id", watchRegistration); j.Member("method", "client/registerCapability");
        j.Key("params"); j.BeginObject(); j.Key("registrations"); j.BeginArray();
        j.BeginObject(); j.Member("id", watchRegistration);
        j.Member("method", "workspace/didChangeWatchedFiles");
        j.Key("registerOptions"); j.BeginObject(); j.Key("watchers"); j.BeginArray();
        for (auto &dir : declaredDirs) for (auto pattern : {"**/*.goose", "**/*.build"}) {
            j.BeginObject(); j.Key("globPattern");
            if (watchRelative) {
                j.BeginObject(); j.Member("baseUri", UriOfPath(dir));
                j.Member("pattern", pattern); j.EndObject();
            } else {
                string escaped;
                for (char c : dir) { if (string("*?[]{}\\").find(c) != string::npos) escaped += '\\'; escaped += c; }
                j.Str(cat(escaped, "/", pattern));
            }
            j.Member("kind", 7); j.EndObject();
        }
        j.EndArray(); j.EndObject(); j.EndObject(); j.EndArray();
        j.EndObject(); j.EndObject(); rpc.Send(j.s); WatchStatus("pending");
    }

    void WatchResponse(const Message &m) {
        if (m.id != watchRequest || watchRequest.empty()) return;
        watchRequest.clear();
        if (m.json.Get("error")) WatchStatus("refused; disk updates require client notifications");
        else if (m.json.Get("result")) WatchStatus("registered");
        else WatchStatus("invalid response; watcher registration unconfirmed");
    }

    void RebuildFolders() {
        workspaceDirs = declaredDirs;
        for (auto &r : roots) if (r->configured)
            workspaceDirs.insert(FolderPath(DirOf(r->path)));
    }

    bool WatchedPath(const string &key) const {
        for (auto &dir : workspaceDirs) {
            auto base = PathKey(dir);
            while (base.size() > 1 && base.back() == '/') base.pop_back();
            if (key.rfind(base == "/" ? base : base + "/", 0) == 0) return true;
        }
        return false;
    }

    void ChangeFolders(const Json &params) {
        auto event = params.Get("event");
        if (!event) return;
        for (auto name : {"removed", "added"})
            if (auto list = event->Get(name); list && list->kind == Json::ARRAY)
                for (auto &folder : list->arr) {
                    auto path = PathOfUri(JsonStr(folder.Get("uri")));
                    if (path.empty()) continue;
                    path = FolderPath(path);
                    if (string(name) == "removed") declaredDirs.erase(path);
                    else declaredDirs.insert(path);
                }
        RebuildFolders();
        ++rpc.scanEpoch;
        for (auto &r : roots) {
            r->indexed = false;
            r->discardFailedAnalysis = true;
            Dirty(*r, 0);
        }
        PruneUnreached(); Discover(); RegisterWatchers();
    }

    // The commands the client runs itself, which code lenses may name: the
    // protocol has no way to ask, so its initialization options list them
    // (`commands`).
    set<string> clientcommands;
    bool snippetSupport = false;
    bool createFileSupport = false;
    struct CustomSnippet {
        string prefix, body, description, scope;
        bool postfix;
        vector<string> requirements;
    };
    vector<CustomSnippet> customSnippets;
    function<bool(const string &, size_t, size_t, const vector<string> &, string &)> snippetImports;
    // The refresh requests the client takes (`workspace/codeLens/refresh`,
    // `workspace/semanticTokens/refresh`, `workspace/inlayHint/refresh`),
    // and the id of the last request the server sent.
    vector<string> refreshes;
    int64_t requests = 0;
    // How long after the last change to a root's files it is analyzed again.
    static constexpr int DEBOUNCE_MS = 150;
    // The analyses let go of, and the thread that frees them (Reap).
    mutex reapm;
    condition_variable reapcv;
    deque<unique_ptr<Analysis>> reapq;
    bool reapstop = false;
    thread reaper;

    Workspace(Transport &r, const string &e, const string &stdlib) : rpc(r), exe(e) {
        stdlibdirs = StdlibDirs(stdlib, exe);
    }

    ~Workspace() {
        {
            lock_guard<mutex> lock(reapm);
            reapstop = true;
        }
        reapcv.notify_all();
        if (reaper.joinable()) reaper.join();
    }

    // Frees an analysis a root no longer reads, on a thread of its own: a
    // large program's tree takes a tenth of the time to free that it took
    // to check, and the requests that follow a root's analysis (the
    // editor's refreshes among them) or a close would otherwise wait for
    // it. The Ast owns everything it frees, so the next analysis can run
    // meanwhile.
    void Reap(unique_ptr<Analysis> old) {
        if (!old) return;
        lock_guard<mutex> lock(reapm);
        reapq.push_back(std::move(old));
        if (!reaper.joinable())
            reaper = thread([this] {
                unique_lock<mutex> lock(reapm);
                for (;;) {
                    reapcv.wait(lock, [&] { return reapstop || !reapq.empty(); });
                    if (reapq.empty()) return;
                    auto a = std::move(reapq.front());
                    reapq.pop_front();
                    lock.unlock();
                    a.reset();
                    lock.lock();
                }
            });
        reapcv.notify_all();
    }

    // The editor's options: `entryFiles`, the programs to analyze, and
    // `stdlib`, the standard library's directory.
    void Configure(const Json *options) {
        if (!options || options->kind != Json::OBJECT) return;
        customSnippets.clear();
        if (auto list = options->Get("snippets"); list && list->kind == Json::ARRAY) {
            for (auto &entry : list->arr) {
                auto prefix = entry.Get("prefix"), body = entry.Get("body");
                auto scope = entry.Get("scope"), mode = entry.Get("postfix");
                auto description = entry.Get("description");
                if (!prefix || prefix->kind != Json::STRING || prefix->str.empty() ||
                    !body || body->kind != Json::STRING || body->str.empty() ||
                    !scope || scope->kind != Json::STRING || (scope->str != "expression" && scope->str != "item" && scope->str != "type") ||
                    !mode || mode->kind != Json::BOOL ||
                    (description && description->kind != Json::STRING)) continue;
                bool valid = true;
                for (auto c : prefix->str) if (!isalnum((unsigned char)c) && c != '_') valid = false;
                auto receiver = body->str.find("${receiver}");
                bool postfix = JsonBool(mode);
                if (postfix && scope->str != "expression") continue;
                if (postfix ? receiver == string::npos ||
                    body->str.find("${receiver}", receiver + 11) != string::npos : receiver != string::npos) continue;
                if (postfix) {
                    auto before = body->str.substr(0, receiver);
                    auto after = body->str.substr(receiver + 11);
                    if (after.size() >= 2 && after.substr(after.size() - 2) == "$0") after.resize(after.size() - 2);
                    for (char c : before)
                        if (!isalnum((unsigned char)c) && c != '_' && c != ':' &&
                            c != '(' && !isspace((unsigned char)c) && c != '!' && c != '-' && c != '+') valid = false;
                    for (char c : after) if (c != ')' && !isspace((unsigned char)c)) valid = false;
                }
                vector<string> requirements;
                if (auto list = entry.Get("requires")) {
                    if (list->kind != Json::ARRAY) continue;
                    for (auto &value : list->arr) {
                        if (value.kind != Json::STRING || value.str.empty()) valid = false;
                        else requirements.push_back(value.str);
                    }
                }
                auto validationBody = body->str;
                if (postfix) validationBody.replace(receiver, 11, "0");
                string expanded;
                if (!ExpandSimpleSnippet(validationBody, expanded)) valid = false;
                if (valid) customSnippets.push_back({prefix->str, body->str,
                    description ? description->str : "custom snippet", scope->str, postfix, requirements});
            }
        }
        if (auto lib = options->Get("stdlib"); lib && lib->kind == Json::STRING && !lib->str.empty())
            stdlibdirs = StdlibDirs(lib->str, exe);
        if (auto entries = options->Get("entryFiles"); entries && entries->kind == Json::ARRAY) {
            set<string> keep;
            for (auto &e : entries->arr) {
                if (e.kind != Json::STRING || e.str.empty()) continue;
                auto key = PathKey(e.str);
                keep.insert(key);

                if (auto r = FindRoot(key)) r->configured = true;
                else AddRoot(e.str, true);
            }
            for (auto &r : roots)
                if (r->configured && !keep.count(r->key)) r->configured = false;
            PruneUnreached();
        }
        RebuildFolders();
        if (auto excludes = options->Get("exclude"); excludes && excludes->kind == Json::ARRAY) {
            excludedDirs.clear();
            for (auto &e : excludes->arr) if (e.kind == Json::STRING)
                for (auto &dir : workspaceDirs)
                    excludedDirs.insert(PathKey((filesystem::path(dir) / e.str).string()));
        }
        Discover();
    }

    Root *FindRoot(const string &key) {
        for (auto &r : roots) if (r->key == key) return r.get();
        return nullptr;
    }

    Root *AddRoot(const string &path, bool configured) {
        auto r = make_unique<Root>();
        r->path = CanonicalPath(path);
        r->key = PathKey(path);
        r->configured = configured;
        Dirty(*r, 0);
        roots.push_back(std::move(r));
        return roots.back().get();
    }

    // A root goes; the files it reached keep the diagnostics the other
    // roots reaching them gave.
    void RemoveRoot(Root *root) {
        auto it = find_if(roots.begin(), roots.end(), [&](const unique_ptr<Root> &r) { return r.get() == root; });
        if (it == roots.end()) return;
        auto gone = std::move(*it);
        roots.erase(it);
        Reap(std::move(gone->analysis));
        for (auto &[uri, diags] : gone->published) PublishDiagnostics(uri);
    }

    bool Reached(const string &key) const {
        for (auto &r : roots) if (r->Reaches(key)) return true;
        return false;
    }

    // Every open file no root reaches becomes a root of its own: one just
    // opened, or one whose root went or stopped reaching it.
    void RootOpenFiles() {
        for (auto &[key, doc] : open) if (!Reached(key)) AddRoot(doc.path, false);
    }

    // An open-file root is dropped where a root reaches its file, which is
    // then checked as part of that program; where a root no longer
    // configured is open, it stays as an open file's root, else it goes.
    // An open file that only the roots gone reached gets a root again.
    void PruneUnreached() {
        for (size_t i = 0; i < roots.size();) {
            auto &r = *roots[i];
            auto reached = false;
            for (auto &o : roots) if (o.get() != &r && o->Reaches(r.key)) reached = true;
            bool configuredReach = false;
            for (auto &o : roots)
                if (o->configured && o.get() != &r && o->Reaches(r.key)) configuredReach = true;
            if (!r.configured && (configuredReach ||
                (!r.indexed && (reached || !open.count(r.key))))) RemoveRoot(&r);
            else i++;
        }
        RootOpenFiles();
    }

    void Dirty(Root &r, int delayms) {
        r.dirty = true;
        r.due = chrono::steady_clock::now() + chrono::milliseconds(delayms);
    }

    // A file changed: every root reaching it is analyzed again; an open
    // file no root reaches becomes a root of its own.
    void Touch(const string &key) {
        for (auto &r : roots) if (r->Reaches(key)) Dirty(*r, DEBOUNCE_MS);
        RootOpenFiles();
    }

    void Open(const string &uri, const string &text, int64_t version = 0) {
        auto path = PathOfUri(uri);
        auto key = PathKey(path);
        open[key] = Document { CanonicalPath(path), text, version };
        Touch(key);
        if (workspaceDirs.empty()) {
            workspaceDirs.insert(CanonicalPath(DirOf(path)));
            Discover();
        }
    }

    // An incremental edit: the range (protocol positions) in `text` that
    // `replacement` replaces. Clamped to the text, like OffsetOfPosition.
    static string Splice(const string &text, const Json &range, const string &replacement) {
        TextIndex t;
        t.Index(text);
        auto pos = [&](const Json *p) {
            return t.OffsetOfPosition(JsonInt(p ? p->Get("line") : nullptr), JsonInt(p ? p->Get("character") : nullptr));
        };
        auto from = range.kind == Json::OBJECT ? pos(range.Get("start")) : 0;
        auto to = range.kind == Json::OBJECT ? pos(range.Get("end")) : text.size();
        from = min(from, text.size());
        to = min(max(to, from), text.size());
        auto out = text;
        return out.replace(from, to - from, replacement);
    }

    // Applies one didChange notification: every incremental edit (one with
    // a range) spliced into the overlay text in order, else the last full
    // text (textDocumentSync Full, and a client that batches a save behind
    // its edits), so that a client sending fragments never reads as the
    // whole file.
    void ChangeEdits(const string &uri, const Json &changes, int64_t version = 0) {
        if (changes.kind != Json::ARRAY || changes.arr.empty()) return;
        auto key = PathKey(PathOfUri(uri));
        auto it = open.find(key);
        if (it == open.end() || version <= it->second.version) return;
        it->second.version = version;
        for (auto &c : changes.arr) {
            if (c.kind != Json::OBJECT) continue;
            if (auto r = c.Get("range"); r && r->kind == Json::OBJECT)
                it->second.text = Splice(it->second.text, *r, JsonStr(c.Get("text")));
            else
                it->second.text = JsonStr(c.Get("text"));
        }
        Touch(key);
    }

    void Close(const string &uri) {
        auto key = PathKey(PathOfUri(uri));
        open.erase(key);
        if (auto r = FindRoot(key); r && !r->configured && !r->indexed) {
            RemoveRoot(r);
            RootOpenFiles();
        } else {
            Touch(key);   // Its text is the disk's again.
        }
    }

    // The next moment an analysis is due, if any root is dirty.
    optional<chrono::steady_clock::time_point> NextDue() const {
        if (discoveryPending) return chrono::steady_clock::now();
        optional<chrono::steady_clock::time_point> due;
        for (auto &r : roots)
            if (r->dirty && (!due || r->due < *due)) due = r->due;
        return due;
    }

    // The dirty root due first, if any.
    Root *FirstDue() {
        for (auto &r : roots) if (r->configured && r->dirty) return r.get();
        Root *next = nullptr;
        for (auto &r : roots)
            if (r->dirty && (!next || r->due < next->due)) next = r.get();
        return next;
    }

    // Analyzes the dirty root due first, if its time has come.
    bool AnalyzeDue() {
        if (discoveryPending) { Scan(); return true; }
        auto next = FirstDue();
        if (!next || next->due > chrono::steady_clock::now()) return false;
        Analyze(*next);
        PruneUnreached();
        return true;
    }

    // Analyzes every dirty root now, its debounce not waited out, so that a
    // request is answered from the documents as they stand when it arrives
    // rather than from the analysis before their last change, or from none
    // yet. A change arriving after the request cancels none of these: the
    // request asks about the text before it.
    void Settle() {
        if (discoveryPending) Scan();
        while (auto next = FirstDue()) Analyze(*next, false);
        PruneUnreached();
    }

    // The text of a file as the editor has it, else null for the disk's.
    const string *TextOf(const string &path) {
        auto it = open.find(PathKey(path));
        return it == open.end() ? nullptr : &it->second.text;
    }

    // One analysis of a root: the whole program as `check` sees it, from
    // the editor's buffers, fetching nothing. Its diagnostics are
    // published for every file it reached; one that got past resolution
    // replaces the analysis queries read. A cancelled one leaves the root
    // dirty for the next; one not cancellable runs to its end.
    void Analyze(Root &r, bool cancellable = true) {
        auto an = make_unique<Analysis>();
        an->ast = make_unique<Ast>();
        auto &ast = *an->ast;
        ast.cancel = cancellable ? &rpc.cancel : nullptr;
        ast.exemplars = true;
        ast.sink = [&](const Diagnostic &d) { an->diags.push_back(d); };
        SourceProvider sources = [&](const string &path) { return TextOf(path); };
        try {
            RunOnCompilerStack([&]() -> int {
                // The parser and the checker go on past the errors they
                // recover from, which the sink collects; the passes after
                // the check want a program without any.
                ParseProgram(ast, r.path, stdlibdirs, DirOf(exe), sources);
                ResolveTypeNames(ast);
                an->parsed = true;
                // The source's indexes stand on the tree as written, which
                // the check rewrites where it checks in place (a global's
                // initializer); the clones' on the tree as the checker left
                // it: the optimizer then rewrites the bodies, inlining calls
                // and dropping constants, and the nodes the features read
                // with them.
                an->IndexSource();
                TypeCheckProgram(ast, false, true);
                an->Build();
                if (!ast.errors.empty()) return 0;
                // At the level a run or build defaults to, so that the
                // checkfree verdicts here are theirs.
                Optimizer opt(ast, 2);
                BCE bce(ast);
                bce.RunAll();
                if (!bce.checkfreefails.empty()) throw bce.CheckfreeError();
                return 0;
            });
        } catch (CompileError &e) {
            if (e.diags.empty()) {
                Diagnostic d;
                d.message = d.text = e.msg;
                an->diags.push_back(std::move(d));
            } else {
                an->diags.insert(an->diags.end(), e.diags.begin(), e.diags.end());
            }
        } catch (Cancelled &) {
            return;
        }
        r.dirty = false;
        r.files.clear();
        for (auto &[path, text] : ast.sources) r.files.insert(PathKey(path));
        if (!an->indexed) an->IndexFiles();
        Publish(r, *an);
        if (an->parsed || !r.analysis || r.discardFailedAnalysis) {
            r.discardFailedAnalysis = false;
            if (!an->built) an->Build();
            Reap(std::move(r.analysis));
            r.analysis = std::move(an);
            // An editor asks for lenses, semantic tokens and inlay hints as
            // a file opens or changes, which may be before the analysis
            // answering it, and asks again only when told to.
            for (auto &method : refreshes) Request(method);
        }
        PruneUnreached();
    }

    // A request of the server's without parameters; the reply is ignored.
    void Request(const string &method) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Member("id", ++requests);
        j.Member("method", method);
        j.EndObject();
        rpc.Send(j.s);
    }

    // Where a file's queries are answered from: the analysis of the first
    // root (the configured ones first) that reached it.
    bool Locate(const string &key, Analysis *&an, int &fileidx) {
        for (auto configured : { true, false })
            for (auto &r : roots) {
                if (r->configured != configured || !r->analysis) continue;
                auto f = r->analysis->FileOf(key);
                if (f < 0) continue;
                an = r->analysis.get();
                fileidx = f;
                return true;
            }
        return false;
    }

    // --- diagnostics -----------------------------------------------------

    // A place as the protocol's range: where the compiler gave no end, the
    // name at the start, or one byte; where no column, the line's text.
    static void WriteRangeOf(JsonOut &j, const DiagLoc &w, const TextIndex &t) {
        auto line = max(0, w.line - 1);
        auto ltext = t.LineText(line);
        size_t sc = 0, ec = 0;
        auto eline = line;
        if (w.col > 0) {
            sc = min((size_t)(w.col - 1), ltext.size());
            if (w.endline > 0) {
                eline = max(0, w.endline - 1);
                ec = min((size_t)max(0, w.endcol - 1), t.LineText(eline).size());
            } else {
                auto len = t.NameLengthAt(t.linestarts[(size_t)min(line, t.Lines() - 1)] + sc);
                ec = min(sc + max<size_t>(len, 1), ltext.size());
                if (ec < sc) ec = sc;
            }
        } else {
            auto first = ltext.find_first_not_of(" \t");
            sc = first == string_view::npos ? 0 : first;
            ec = ltext.size();
        }
        j.Key("range");
        j.BeginObject();
        j.Key("start");
        j.BeginObject();
        j.Member("line", (int64_t)line);
        j.Member("character", (int64_t)Utf16Col(ltext, sc));
        j.EndObject();
        j.Key("end");
        j.BeginObject();
        j.Member("line", (int64_t)eline);
        j.Member("character", (int64_t)Utf16Col(t.LineText(eline), ec));
        j.EndObject();
        j.EndObject();
    }

    static PublishedDiagnostic RenderDiagnostic(const Diagnostic &d, const Analysis &an, int fileidx) {
        auto &files = an.files;
        JsonOut j;
        j.BeginObject();
        DiagLoc where = d.where;
        if (where.fileidx != fileidx) where = DiagLoc { fileidx, 1, 0, 0, 0 };
        WriteRangeOf(j, where, files[(size_t)fileidx].text);
        j.Member("severity", (int64_t)(d.warning ? 2 : 1));
        j.Member("source", "goose");
        // The related lines at no known place stay part of the message.
        auto message = d.message;
        for (auto &r : d.related)
            if (r.where.fileidx < 0 || r.where.fileidx >= (int)files.size() || r.where.line <= 0)
                Append(message, "\n", r.message);
        j.Member("message", message);
        PublishedDiagnostic p;
        p.keylen = j.s.size();
        auto any = false;
        for (auto &r : d.related) {
            if (r.where.fileidx < 0 || r.where.fileidx >= (int)files.size() || r.where.line <= 0) continue;
            if (!any) {
                j.Key("relatedInformation");
                j.BeginArray();
                any = true;
            }
            j.BeginObject();
            j.Key("location");
            j.BeginObject();
            j.Member("uri", files[(size_t)r.where.fileidx].uri);
            WriteRangeOf(j, r.where, files[(size_t)r.where.fileidx].text);
            j.EndObject();
            j.Member("message", r.message);
            j.EndObject();
        }
        if (any) j.EndArray();
        j.EndObject();
        p.json = std::move(j.s);
        return p;
    }

    // A file's diagnostics, as every root reaching it last gave them: what
    // they report alike once, and an empty list, so the editor drops the
    // ones before, where they give none or no root reaches the file.
    void PublishDiagnostics(const string &uri) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Member("method", "textDocument/publishDiagnostics");
        j.Key("params");
        j.BeginObject();
        j.Member("uri", uri);
        j.Key("diagnostics");
        j.BeginArray();
        set<string_view> seen;
        for (auto &r : roots)
            if (auto it = r->published.find(uri); it != r->published.end())
                for (auto &d : it->second)
                    if (seen.insert(d.Key()).second) j.Raw(d.json);
        j.EndArray();
        j.EndObject();
        j.EndObject();
        rpc.Send(j.s);
    }

    // The attempt's diagnostics replace those of the root's last attempt,
    // and every file either of them reached is published again.
    // Diagnostics, like locations, go under the URI the server spells for
    // a file (UriOfPath), whatever spelling the editor opened it by. A
    // diagnostic at no place in the program (a file that cannot be opened)
    // is reported at the root file's first line.
    void Publish(Root &r, const Analysis &an) {
        auto &ast = *an.ast;
        map<int, vector<const Diagnostic *>> perfile;
        vector<const Diagnostic *> nowhere;
        for (auto &d : an.diags) {
            if (d.where.fileidx >= 0 && d.where.fileidx < (int)ast.sources.size() && d.where.line > 0)
                perfile[d.where.fileidx].push_back(&d);
            else nowhere.push_back(&d);
        }
        map<string, vector<PublishedDiagnostic>> published;
        for (size_t i = 0; i < ast.sources.size(); i++) {
            auto &diags = published[an.files[i].uri];
            for (auto d : perfile[(int)i]) diags.push_back(RenderDiagnostic(*d, an, (int)i));
            if (i == 0) for (auto d : nowhere) diags.push_back(RenderDiagnostic(*d, an, 0));
        }
        if (ast.sources.empty() && !nowhere.empty()) {
            // The root itself could not be read: its message, at its first line.
            auto &diags = published[UriOfPath(r.path)];
            for (auto d : nowhere) {
                JsonOut j;
                j.BeginObject();
                j.Key("range");
                j.Raw("{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}}");
                j.Member("severity", (int64_t)(d->warning ? 2 : 1));
                j.Member("source", "goose");
                j.Member("message", d->message);
                j.EndObject();
                diags.push_back({ j.s, j.s.size() });
            }
        }
        swap(r.published, published);
        for (auto &[uri, diags] : r.published) PublishDiagnostics(uri);
        for (auto &[uri, diags] : published) if (!r.published.count(uri)) PublishDiagnostics(uri);
    }
};

}  // namespace goose
