// Goose compiler — `goose lsp`, the language server (docs/design/
// language_server.md): the worker that takes the editor's messages from
// the transport, keeps the workspace, analyzes a root once its changes
// have settled, and answers the requests from the latest analyses.
#pragma once

namespace goose {

// The protocol version served; what --probe prints.
constexpr const char *LSP_VERSION = "3.17";

// In main.cpp: stdin and stdout as byte streams, which the framing needs.
void SetBinaryStdio();

struct Server {
    Transport rpc;
    Workspace ws;
    bool shutdown = false;

    Server(const string &exe, const string &stdlib) : ws(rpc, exe, stdlib) {}

    // A reply: `result` writes the result's value, or writes nothing and
    // returns false for null.
    void Respond(const Message &m, const function<bool(JsonOut &)> &result) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Key("id");
        j.Raw(m.id);
        j.Key("result");
        if (!result(j)) j.Null();
        j.EndObject();
        rpc.Send(j.s);
    }

    void RespondError(const Message &m, int code, const string &message) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Key("id");
        j.Raw(m.id);
        j.Key("error");
        j.BeginObject();
        j.Member("code", (int64_t)code);
        j.Member("message", message);
        j.EndObject();
        j.EndObject();
        rpc.Send(j.s);
    }

    void WriteCapabilities(JsonOut &j) {
        j.BeginObject();
        j.Key("capabilities");
        j.BeginObject();
        j.Member("positionEncoding", "utf-16");
        j.Member("textDocumentSync", (int64_t)1);   // Full, with incremental edits spliced where a client sends them.
        j.Member("hoverProvider", true);
        j.Member("definitionProvider", true);
        j.Member("referencesProvider", true);
        j.Key("renameProvider");
        j.BeginObject();
        j.Member("prepareProvider", true);
        j.EndObject();
        j.Member("documentHighlightProvider", true);
        j.Member("typeDefinitionProvider", true);
        j.Member("documentSymbolProvider", true);
        j.Member("workspaceSymbolProvider", true);
        j.Key("workspace");
        j.Raw(R"({"workspaceFolders":{"supported":true,"changeNotifications":true},"fileOperations":{"didRename":{"filters":[{"scheme":"file","pattern":{"glob":"**/*.goose","matches":"file"}}]},"willRename":{"filters":[{"scheme":"file","pattern":{"glob":"**/*.goose","matches":"file"}}]}}})");
        j.Member("callHierarchyProvider", true);
        j.Member("inlayHintProvider", true);
        j.Member("foldingRangeProvider", true);
        j.Member("selectionRangeProvider", true);
        j.Member("documentFormattingProvider", true);
        j.Member("documentRangeFormattingProvider", true);
        j.Key("codeActionProvider");
        j.BeginObject();
        j.Key("codeActionKinds");
        j.BeginArray();
        j.Str("source.format");
        j.Str("source.organizeImports");
        j.Str("refactor.extract");
        j.Str("refactor.inline");
        j.Str("refactor.rewrite");
        j.Str("quickfix");
        j.EndArray();
        j.EndObject();
        j.Key("codeLensProvider");
        j.BeginObject();
        j.Member("resolveProvider", false);
        j.EndObject();
        j.Key("signatureHelpProvider");
        j.BeginObject();
        j.Key("triggerCharacters");
        j.BeginArray();
        j.Str("(");
        j.Str(",");
        j.EndArray();
        j.EndObject();
        j.Key("completionProvider");
        j.BeginObject();
        j.Key("triggerCharacters");
        j.BeginArray();
        j.Str(".");
        j.Str(":");
        j.EndArray();
        j.Member("resolveProvider", false);
        j.EndObject();
        j.Key("semanticTokensProvider");
        j.BeginObject();
        j.Key("legend");
        j.BeginObject();
        j.Key("tokenTypes");
        j.BeginArray();
        for (auto t : SEMANTIC_TYPES) j.Str(t);
        j.EndArray();
        j.Key("tokenModifiers");
        j.BeginArray();
        for (auto m : SEMANTIC_MODIFIERS) j.Str(m);
        j.EndArray();
        j.EndObject();
        j.Member("full", true);
        j.EndObject();
        j.EndObject();
        j.Key("serverInfo");
        j.BeginObject();
        j.Member("name", "goose");
        j.Member("version", RuntimeVersion());
        j.EndObject();
        j.EndObject();
    }

    // Whether a request asks about the documents and waits for the analysis
    // their changes have made due: all but the lenses, semantic tokens and
    // inlay hints of a client that takes their refresh, which are answered
    // from the analysis there is and asked for again once the next is done.
    bool AsksAboutDocuments(const string &method) {
        if (method.rfind("textDocument/", 0) != 0 && method != "workspace/symbol") return false;
        // Formatting reads the editor's text, not an analysis.
        if (method == "textDocument/formatting" || method == "textDocument/rangeFormatting") return false;
        for (auto feature : { "codeLens", "semanticTokens", "inlayHint" })
            if (method.rfind(cat("textDocument/", feature), 0) == 0)
                return find(ws.refreshes.begin(), ws.refreshes.end(), cat("workspace/", feature, "/refresh")) ==
                       ws.refreshes.end();
        return true;
    }

    // Handles one message; false once the client said to exit.
    bool Handle(const Message &m) {
        auto params = m.Params();
        static const Json none;
        auto &p = params ? *params : none;
        auto &method = m.method;
        // A reply to a request of the server's needs nothing.
        if (method.empty()) { if (!shutdown) ws.WatchResponse(m); return true; }
        if (shutdown && method != "exit") return true;
        if (!m.id.empty() && rpc.IsCancelled(m.id)) {
            RespondError(m, -32800, "the request was cancelled");
            return true;
        }
        if (!m.id.empty() && AsksAboutDocuments(method)) ws.Settle();
        if (method == "initialize") {
            auto options = p.Get("initializationOptions");
            ws.ConfigureFolders(p);
            ws.Configure(options);
            if (!options) ws.Discover();
            if (auto commands = options ? options->Get("commands") : nullptr; commands && commands->kind == Json::ARRAY)
                for (auto &c : commands->arr)
                    if (c.kind == Json::STRING) ws.clientcommands.insert(c.str);
            auto caps = p.Get("capabilities");
            auto tcaps = caps ? caps->Get("textDocument") : nullptr;
            auto ccaps = tcaps ? tcaps->Get("completion") : nullptr;
            auto icaps = ccaps ? ccaps->Get("completionItem") : nullptr;
            ws.snippetSupport = JsonBool(icaps ? icaps->Get("snippetSupport") : nullptr);
            auto wcaps = caps ? caps->Get("workspace") : nullptr;
            auto watchers = wcaps ? wcaps->Get("didChangeWatchedFiles") : nullptr;
            ws.watchDynamic = JsonBool(watchers ? watchers->Get("dynamicRegistration") : nullptr);
            ws.watchRelative = JsonBool(watchers ? watchers->Get("relativePatternSupport") : nullptr);
            auto we = wcaps ? wcaps->Get("workspaceEdit") : nullptr;
            auto ro = we ? we->Get("resourceOperations") : nullptr;
            ws.createFileSupport = false;
            if (JsonBool(we ? we->Get("documentChanges") : nullptr) && ro)
                for (auto &op : ro->arr) if (op.str == "create")
                    ws.createFileSupport = true;
            for (auto feature : { "codeLens", "semanticTokens", "inlayHint" }) {
                auto fcaps = wcaps ? wcaps->Get(feature) : nullptr;
                if (JsonBool(fcaps ? fcaps->Get("refreshSupport") : nullptr))
                    ws.refreshes.push_back(cat("workspace/", feature, "/refresh"));
            }
            Respond(m, [&](JsonOut &j) { WriteCapabilities(j); return true; });
        } else if (method == "initialized") {
            ws.watchStarted = true; ws.RegisterWatchers();
        } else if (method == "workspace/didChangeWorkspaceFolders") {
            ws.ChangeFolders(p);
        } else if (method == "shutdown") {
            shutdown = true;
            Respond(m, [](JsonOut &) { return false; });
        } else if (method == "exit") {
            return false;
        } else if (method == "textDocument/didOpen") {
            auto td = p.Get("textDocument");
            if (td) ws.Open(JsonStr(td->Get("uri")), JsonStr(td->Get("text")), JsonInt(td->Get("version")));
        } else if (method == "textDocument/didChange") {
            auto td = p.Get("textDocument");
            auto changes = p.Get("contentChanges");
            if (td && changes) ws.ChangeEdits(JsonStr(td->Get("uri")), *changes, JsonInt(td->Get("version")));
        } else if (method == "textDocument/didClose") {
            if (auto td = p.Get("textDocument")) ws.Close(JsonStr(td->Get("uri")));
        } else if (method == "workspace/didChangeConfiguration") {
            auto settings = p.Get("settings");
            ws.Configure(settings ? settings->Get("goose") : nullptr);
        } else if (method == "workspace/didChangeWatchedFiles") {
            if (auto changes = p.Get("changes"); changes && changes->kind == Json::ARRAY && changes->arr.empty())
                ws.Discover();
            if (auto changes = p.Get("changes"); changes && changes->kind == Json::ARRAY)
                for (auto &c : changes->arr) {
                    auto key = PathKey(PathOfUri(JsonStr(c.Get("uri"))));
                    auto extension = filesystem::path(key).extension();
                    if (!ws.WatchedPath(key) || (extension != ".goose" && extension != ".build")) continue;
                    ws.Discover();
                    if (!ws.open.count(key)) {
                        for (auto &root : ws.roots) {
                            root->discardFailedAnalysis = true;
                            ws.Dirty(*root, 0);
                        }
                    }
                }
        } else if (method == "workspace/didRenameFiles") {
            if (auto files = p.Get("files"); files && files->kind == Json::ARRAY) {
                for (auto &file : files->arr) {
                    auto oldkey = PathKey(PathOfUri(JsonStr(file.Get("oldUri"))));
                    auto path = CanonicalPath(PathOfUri(JsonStr(file.Get("newUri"))));
                    if (auto it = ws.open.find(oldkey); it != ws.open.end()) {
                        auto doc = std::move(it->second); ws.open.erase(it);
                        doc.path = path; ws.open[PathKey(path)] = std::move(doc);
                    }
                    if (auto root = ws.FindRoot(oldkey)) {
                        root->path = path; root->key = PathKey(path);
                    }
                }
                ws.Discover();
                for (auto &root : ws.roots) ws.Dirty(*root, 0);
            }
        } else if (method == "workspace/willRenameFiles") {
            JsonOut edit; string error;
            if (WillRenameFiles(ws, p, edit, error))
                Respond(m, [&](JsonOut &j) { j.Raw(edit.s); return true; });
            else RespondError(m, -32803, error);
        } else if (method == "textDocument/hover") {
            Respond(m, [&](JsonOut &j) { return Hover(ws, p, j); });
        } else if (method == "textDocument/definition") {
            Respond(m, [&](JsonOut &j) { return Definition(ws, p, j); });
        } else if (method == "textDocument/documentSymbol") {
            Respond(m, [&](JsonOut &j) { return DocumentSymbols(ws, p, j); });
        } else if (method == "workspace/symbol") {
            Respond(m, [&](JsonOut &j) { return WorkspaceSymbols(ws, p, j); });
        } else if (method == "textDocument/semanticTokens/full") {
            Respond(m, [&](JsonOut &j) { return SemanticTokens(ws, p, j); });
        } else if (method == "textDocument/signatureHelp") {
            Respond(m, [&](JsonOut &j) { return SignatureHelp(ws, p, j); });
        } else if (method == "textDocument/completion") {
            Respond(m, [&](JsonOut &j) { return Completion(ws, p, j); });
        } else if (method == "textDocument/references") {
            Respond(m, [&](JsonOut &j) { return References(ws, p, j); });
        } else if (method == "textDocument/prepareRename") {
            Respond(m, [&](JsonOut &j) { return PrepareRename(ws, p, j); });
        } else if (method == "textDocument/documentHighlight") {
            Respond(m, [&](JsonOut &j) { return DocumentHighlights(ws, p, j); });
        } else if (method == "textDocument/typeDefinition") {
            Respond(m, [&](JsonOut &j) { return TypeDefinition(ws, p, j); });
        } else if (method == "textDocument/rename") {
            // A name that cannot be renamed is an error the editor shows,
            // not an empty edit.
            JsonOut edit;
            string error;
            if (Rename(ws, p, edit, error)) Respond(m, [&](JsonOut &j) { j.Raw(edit.s); return true; });
            else RespondError(m, -32803, error);
        } else if (method == "textDocument/prepareCallHierarchy") {
            Respond(m, [&](JsonOut &j) { return PrepareCallHierarchy(ws, p, j); });
        } else if (method == "callHierarchy/incomingCalls") {
            Respond(m, [&](JsonOut &j) { return IncomingCalls(ws, p, j); });
        } else if (method == "callHierarchy/outgoingCalls") {
            Respond(m, [&](JsonOut &j) { return OutgoingCalls(ws, p, j); });
        } else if (method == "textDocument/inlayHint") {
            Respond(m, [&](JsonOut &j) { return InlayHints(ws, p, j); });
        } else if (method == "textDocument/foldingRange") {
            Respond(m, [&](JsonOut &j) { return FoldingRanges(ws, p, j); });
        } else if (method == "textDocument/selectionRange") {
            Respond(m, [&](JsonOut &j) { return SelectionRanges(ws, p, j); });
        } else if (method == "textDocument/codeLens") {
            Respond(m, [&](JsonOut &j) { return CodeLenses(ws, p, j); });
        } else if (method == "textDocument/codeAction") {
            Respond(m, [&](JsonOut &j) { return CodeActions(ws, p, j); });
        } else if (method == "textDocument/formatting" || method == "textDocument/rangeFormatting") {
            JsonOut edits;
            string error;
            auto r = FormatDocument(ws, p, edits, error, method == "textDocument/rangeFormatting");
            if (r == 2) RespondError(m, -32603, error);
            else Respond(m, [&](JsonOut &j) { if (r) j.Raw(edits.s); return r == 1; });
        } else if (!m.id.empty()) {
            RespondError(m, -32601, cat("goose lsp does not handle ", method));
        }
        return true;
    }

    // The worker's life: messages as they come, and between them, once a
    // root's changes have settled, its analysis. A change arriving during
    // an analysis cancels it (Transport::cancel), and the root waits for
    // the next quiet moment.
    int Run() {
        thread reader([this] { rpc.ReadAll(stdin); });
        reader.detach();
        for (;;) {
            Message m;
            if (rpc.Pop(m, ws.NextDue())) {
                if (m.exit || !Handle(m)) return shutdown ? 0 : 1;
                continue;
            }
            rpc.cancel.store(false, memory_order_relaxed);
            ws.AnalyzeDue();
        }
    }
};

// `goose lsp [--stdlib dir]`, or `goose lsp --probe`.
inline int RunLsp(const string &exe, const vector<string> &args) {
    string stdlib;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--probe") {
            printf("goose lsp %s\n", LSP_VERSION);
            return 0;
        }
        if (args[i] == "--stdlib" && i + 1 < args.size()) {
            stdlib = args[++i];
            continue;
        }
        // The one transport there is, which clients (vscode-languageclient
        // among them) name when they start a server on stdio.
        if (args[i] == "--stdio") continue;
        fprintf(stderr, "goose lsp takes --probe, --stdio or --stdlib dir, not %s\n", args[i].c_str());
        return 1;
    }
    SetBinaryStdio();
    // A server never waits on the network: a package not fetched is an
    // error at its import.
    package_nofetch = true;
    Server server(exe, stdlib);
    auto code = server.Run();
    // The reader thread is still blocked in fgetc on stdin, holding the
    // stream's lock, which glibc's exit waits for while it closes the
    // streams: the process would never end. Every reply was flushed as it
    // was sent, so nothing of exit's cleanup is needed.
    fflush(stdout);
    fflush(stderr);
    _Exit(code);
}

}  // namespace goose
