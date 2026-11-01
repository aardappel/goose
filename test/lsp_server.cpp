// Run adjacent requests through the real handler, without a reader thread or
// background analysis between them. Keep the compiler's normal include order.
#define main goose_original_main
#include "../src/main.cpp"
#undef main

using namespace goose;

static void Require(bool ok, const string &message) {
    if (!ok) throw runtime_error(message);
}

static void Handle(Server &server, const string &method, const string &params, bool request = true) {
    Message m;
    m.method = method;
    m.id = request ? "1" : "";
    m.json = ParseJson("{\"params\":" + params + "}");
    Require(server.Handle(m), "server stopped at " + method);
}

static string DocumentParams(const string &uri, const string &text, bool change = false) {
    JsonOut j;
    j.BeginObject();
    j.Key("textDocument");
    j.BeginObject();
    j.Member("uri", uri);
    j.Member("version", change ? 2 : 1);
    if (!change) {
        j.Member("languageId", "goose");
        j.Member("text", text);
    }
    j.EndObject();
    if (change) {
        j.Key("contentChanges");
        j.BeginArray();
        j.BeginObject();
        j.Member("text", text);
        j.EndObject();
        j.EndArray();
    }
    j.EndObject();
    return j.s;
}

static string Position(const string &uri, int line) {
    JsonOut j;
    j.BeginObject();
    j.Key("textDocument");
    j.BeginObject();
    j.Member("uri", uri);
    j.EndObject();
    j.Key("position");
    j.BeginObject();
    j.Member("line", line);
    j.Member("character", 3);
    j.EndObject();
    j.EndObject();
    return j.s;
}

static string Item(Server &server, const string &uri, int line) {
    JsonOut j;
    Require(PrepareCallHierarchy(server.ws, ParseJson(Position(uri, line)), j), "cannot prepare item");
    auto items = ParseJson(j.s);
    Require(items.kind == Json::ARRAY && items.arr.size() == 1, "expected one item");
    return "{\"item\":" + j.s.substr(1, j.s.size() - 2) + "}";
}

static bool Graph(Server &server, const string &caller, const string &a, const string &b, bool changed) {
    auto calls = [&](const string &item, bool incoming, const string &name) {
        JsonOut j;
        auto params = ParseJson(item);
        auto ok = incoming ? IncomingCalls(server.ws, params, j) : OutgoingCalls(server.ws, params, j);
        Require(ok, "cannot resolve saved item");
        auto result = ParseJson(j.s);
        Require(result.kind == Json::ARRAY, "expected call array");
        if (name.empty()) return result.arr.empty();
        if (result.arr.size() != 1) return false;
        auto fn = result.arr[0].Get(incoming ? "from" : "to");
        return fn && JsonStr(fn->Get("name")) == name;
    };
    return calls(caller, false, changed ? "b" : "a") &&
           calls(a, true, changed ? "" : "caller") &&
           calls(b, true, changed ? "caller" : "");
}

int main(int argc, char **argv) {
    try {
        Require(argc == 3, "usage: goose_lsp_test STDLIB FIXTURE");
        package_nofetch = true;
        const auto uri = UriOfPath(CanonicalPath(argv[2]));
        const string before = "fn a() -> i64 { 1 }\nfn b() -> i64 { 2 }\nfn caller() -> i64 { a() }\n";
        const string after = "fn a() -> i64 { 1 }\nfn b() -> i64 { 2 }\nfn caller() -> i64 { b() }\n";
        int failures = 0;
        // Each query must settle on its own, including a newly added caller.
        for (int first = 0; first < 3; first++) {
            Server server(ExePath(argv[0]), argv[1]);
            Handle(server, "initialize", "{\"capabilities\":{}}");
            Handle(server, "textDocument/didOpen", DocumentParams(uri, before), false);
            Handle(server, "textDocument/prepareCallHierarchy", Position(uri, 2));
            auto caller = Item(server, uri, 2), a = Item(server, uri, 0), b = Item(server, uri, 1);
            Require(Graph(server, caller, a, b, false), "initial graph is wrong");
            auto root = server.ws.FindRoot(PathKey(argv[2]));
            Require(root && root->analysis && !root->dirty, "initial analysis is absent or dirty");
            Handle(server, "textDocument/didChange", DocumentParams(uri, after, true), false);
            Require(root->dirty && root->analysis, "change did not retain dirty analysis");
            const string method = first == 0 ? "callHierarchy/outgoingCalls" : "callHierarchy/incomingCalls";
            Handle(server, method, first == 0 ? caller : first == 1 ? a : b);
            if (!Graph(server, caller, a, b, true) || root->dirty) {
                fprintf(stderr, "FAIL: stale graph after %s (%s)\n", method.c_str(),
                        first == 0 ? "caller" : first == 1 ? "a" : "b");
                failures++;
            }
            // The same saved items must also work after a document request.
            Handle(server, "textDocument/prepareCallHierarchy", Position(uri, 2));
            Require(Graph(server, caller, a, b, true) && !root->dirty, "settled graph is wrong");
        }
        return failures ? 1 : 0;
    } catch (const exception &e) {
        fprintf(stderr, "lsp test failed: %s\n", e.what());
        return 1;
    }
}
