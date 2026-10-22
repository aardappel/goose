// Goose compiler — the language server's transport (docs/design/
// language_server.md §4): JSON-RPC messages framed by Content-Length on
// stdin and stdout, a reader thread feeding the queue the worker drains,
// the JSON writer the replies are built with, and the conversions between
// the protocol's places and the compiler's: file URIs and paths, UTF-16
// columns and byte columns.
#pragma once

namespace goose {

// Writes JSON text: nested objects and arrays, the commas placed as the
// values arrive.
struct JsonOut {
    string s;
    vector<bool> empty;     // Per open container: nothing written in it yet.

    void Sep() {
        if (empty.empty()) return;
        if (!empty.back()) s += ',';
        empty.back() = false;
    }
    void BeginObject() { Sep(); s += '{'; empty.push_back(true); }
    void EndObject() { s += '}'; empty.pop_back(); }
    void BeginArray() { Sep(); s += '['; empty.push_back(true); }
    void EndArray() { s += ']'; empty.pop_back(); }
    // A member's name; the value that follows is written as its own.
    void Key(string_view k) {
        Sep();
        JsonQuote(s, k);
        s += ':';
        empty.back() = true;
    }
    void Str(string_view v) { Sep(); JsonQuote(s, v); }
    void Int(int64_t v) { Sep(); s += to_string(v); }
    void Bool(bool v) { Sep(); s += v ? "true" : "false"; }
    void Null() { Sep(); s += "null"; }
    void Raw(string_view json) { Sep(); s += json; }
    // A literal is a string here, not the bool it would convert to.
    void Member(string_view k, const char *v) { Key(k); Str(v); }
    void Member(string_view k, string_view v) { Key(k); Str(v); }
    void Member(string_view k, const string &v) { Key(k); Str(v); }
    void Member(string_view k, int v) { Key(k); Int(v); }
    void Member(string_view k, int64_t v) { Key(k); Int(v); }
    void Member(string_view k, bool v) { Key(k); Bool(v); }
};

// A JSON value as text again: what a request's id is echoed back as.
inline string JsonText(const Json &v) {
    string s;
    switch (v.kind) {
        case Json::NUL:    return "null";
        case Json::BOOL:   return v.b ? "true" : "false";
        case Json::NUMBER:
            if (v.num == floor(v.num) && fabs(v.num) < 1e15) return to_string((int64_t)v.num);
            Append(s, v.num);
            return s;
        case Json::STRING: JsonQuote(s, v.str); return s;
        default:           return "null";
    }
}

// What a JSON value holds, where it is of the kind asked for.
inline string JsonStr(const Json *v) { return v && v->kind == Json::STRING ? v->str : string(); }
inline int64_t JsonInt(const Json *v, int64_t dflt = 0) {
    return v && v->kind == Json::NUMBER ? (int64_t)v->num : dflt;
}
inline bool JsonBool(const Json *v, bool dflt = false) { return v && v->kind == Json::BOOL ? v->b : dflt; }

// One message from the client: a request (an id), a notification (none),
// or the end of the stream, after which the client is gone.
struct Message {
    Json json;
    string method;
    string id;              // The id's JSON text; "" for a notification.
    bool exit = false;
    const Json *Params() const { return json.Get("params"); }
};

// The messages as they arrive, read on a thread of their own so that a
// change can interrupt the analysis the worker is running: a change to a
// document sets `cancel`, which the parser and the checker poll
// (Ast::cancel). A $/cancelRequest is noted, never queued.
struct Transport {
    mutex m;
    condition_variable cv;
    deque<Message> queue;
    atomic<bool> cancel { false };
    set<string> cancelled;

    // Content-Length framing: the headers, then the body of that length.
    static bool ReadMessage(FILE *in, string &body) {
        size_t length = SIZE_MAX;
        string line;
        for (;;) {
            line.clear();
            int c;
            while ((c = fgetc(in)) != EOF && c != '\n') line += (char)c;
            if (c == EOF) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            if (line.compare(0, 15, "Content-Length:") == 0)
                length = strtoull(line.c_str() + 15, nullptr, 10);
        }
        if (length == SIZE_MAX) return false;
        body.resize(length);
        return length == 0 || fread(body.data(), 1, length, in) == length;
    }

    // The reader thread's life: every message until the stream ends.
    void ReadAll(FILE *in) {
        string body;
        while (ReadMessage(in, body)) {
            Message msg;
            try {
                msg.json = ParseJson(body);
            } catch (CompileError &) {
                continue;
            }
            msg.method = JsonStr(msg.json.Get("method"));
            if (auto id = msg.json.Get("id")) msg.id = JsonText(*id);
            lock_guard<mutex> lock(m);
            if (msg.method == "$/cancelRequest") {
                if (auto p = msg.Params()) if (auto id = p->Get("id")) cancelled.insert(JsonText(*id));
                cv.notify_all();
                continue;
            }
            if (msg.method == "textDocument/didChange" || msg.method == "textDocument/didOpen" ||
                msg.method == "textDocument/didClose")
                cancel.store(true, memory_order_relaxed);
            queue.push_back(std::move(msg));
            cv.notify_all();
        }
        lock_guard<mutex> lock(m);
        Message end;
        end.exit = true;
        queue.push_back(std::move(end));
        cv.notify_all();
    }

    // The next message, waiting until `until` for one where given, else
    // for as long as it takes; false at the deadline.
    bool Pop(Message &out, const optional<chrono::steady_clock::time_point> &until) {
        unique_lock<mutex> lock(m);
        if (until) {
            if (!cv.wait_until(lock, *until, [&] { return !queue.empty(); })) return false;
        } else {
            cv.wait(lock, [&] { return !queue.empty(); });
        }
        out = std::move(queue.front());
        queue.pop_front();
        return true;
    }

    bool IsCancelled(const string &id) {
        lock_guard<mutex> lock(m);
        return cancelled.erase(id) > 0;
    }

    // A message to the client, on stdout, which the worker alone writes.
    void Send(const string &body) {
        fprintf(stdout, "Content-Length: %zu\r\n\r\n", body.size());
        fwrite(body.data(), 1, body.size(), stdout);
        fflush(stdout);
    }
};

// --- paths and URIs ---------------------------------------------------------

// The path a `file:` URI names, with forward slashes and its escapes
// decoded: `file:///c%3A/dir/a.goose` is `c:/dir/a.goose`, and
// `file:///home/a.goose` is `/home/a.goose`.
inline string PathOfUri(const string &uri) {
    if (uri.rfind("file://", 0) != 0) return uri;
    auto p = uri.substr(7);
    auto hex = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
             : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    string out;
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] == '%' && i + 2 < p.size() && hex(p[i + 1]) >= 0 && hex(p[i + 2]) >= 0) {
            out += (char)(hex(p[i + 1]) * 16 + hex(p[i + 2]));
            i += 2;
        } else {
            out += p[i];
        }
    }
    // An empty host, then a Windows drive: "/c:/..." is "c:/...".
    if (out.size() > 2 && out[0] == '/' && isalpha((uint8_t)out[1]) && out[2] == ':') out.erase(0, 1);
    return out;
}

// The URI of a path, as VS Code spells one: `file:///c%3A/dir/a.goose` with
// the drive letter in lower case on Windows, `file:///home/a.goose`
// elsewhere, and everything but unreserved characters escaped.
inline string UriOfPath(const string &path) {
    auto p = ForwardSlashes(path);
    if (p.size() > 1 && p[1] == ':') p[0] = (char)tolower((uint8_t)p[0]);
    string s = "file://";
    if (p.empty() || p[0] != '/') s += '/';
    for (auto c : p) {
        if (isalnum((uint8_t)c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
            s += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof buf, "%%%02X", (unsigned)(uint8_t)c);
            s += buf;
        }
    }
    return s;
}

// A path as the compiler names a file, absolute with forward slashes and
// no `.` or `..` components, and the key two spellings of one file share:
// the same, in lower case on Windows, whose file names are not case
// sensitive.
inline string CanonicalPath(const string &path) {
    return ForwardSlashes(AbsolutePath(path));
}
inline string PathKey(const string &path) {
    auto key = CanonicalPath(path);
    #ifdef _WIN32
        for (auto &c : key) c = (char)tolower((uint8_t)c);
    #endif
    return key;
}

// --- text positions -----------------------------------------------------------

// A UTF-8 sequence's length from its lead byte; a stray continuation byte
// counts as one.
inline size_t Utf8Len(uint8_t c) { return c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

// The UTF-16 units of `line` before its byte `bytecol` (0-based): the
// protocol's column for the compiler's.
inline int Utf16Col(string_view line, size_t bytecol) {
    auto units = 0;
    for (size_t i = 0; i < bytecol && i < line.size();) {
        auto len = Utf8Len((uint8_t)line[i]);
        units += len == 4 ? 2 : 1;
        i += len;
    }
    return units;
}

// The byte in `line` that UTF-16 unit `units` starts: the compiler's column
// for the protocol's.
inline size_t ByteCol(string_view line, int64_t units) {
    size_t i = 0;
    for (int64_t u = 0; u < units && i < line.size();) {
        auto len = Utf8Len((uint8_t)line[i]);
        u += len == 4 ? 2 : 1;
        i += len;
    }
    return i;
}

// A file's text with its line starts: byte offsets to and from the
// compiler's lines and columns and the protocol's.
struct TextIndex {
    string_view text;
    vector<size_t> linestarts;      // Of each 0-based line.

    void Index(string_view t) {
        text = t;
        linestarts.assign(1, 0);
        for (size_t i = 0; i < t.size(); i++) if (t[i] == '\n') linestarts.push_back(i + 1);
    }
    int Lines() const { return (int)linestarts.size(); }
    // The text of 0-based line n, without its line break.
    string_view LineText(int n) const {
        if (n < 0 || n >= Lines()) return {};
        auto start = linestarts[(size_t)n];
        auto end = n + 1 < Lines() ? linestarts[(size_t)n + 1] - 1 : text.size();
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        return line;
    }
    // The 0-based line an offset is on.
    int LineOf(size_t offset) const {
        auto it = upper_bound(linestarts.begin(), linestarts.end(), offset);
        return (int)(it - linestarts.begin()) - 1;
    }
    // A compiler place (1-based line and byte column) as an offset; -1 for
    // none.
    int64_t OffsetOf(int line, int col) const {
        if (line < 1 || line > Lines() || col < 1) return -1;
        return (int64_t)linestarts[(size_t)line - 1] + col - 1;
    }
    // A protocol position (0-based line, UTF-16 column) as an offset,
    // clamped to the text.
    size_t OffsetOfPosition(int64_t line, int64_t character) const {
        if (line < 0) return 0;
        if (line >= Lines()) return text.size();
        auto l = LineText((int)line);
        return linestarts[(size_t)line] + ByteCol(l, character);
    }
    // An offset as a protocol position.
    pair<int, int> PositionOf(size_t offset) const {
        auto line = LineOf(offset);
        auto start = linestarts[(size_t)line];
        return { line, Utf16Col(LineText(line), offset - start) };
    }
    // The identifier, qualified or not (`ns::name`, `::name`), at an
    // offset: its length, 0 for none.
    size_t NameLengthAt(size_t offset) const {
        auto ident = [&](size_t i) {
            auto j = i;
            if (j < text.size() && IsIdentStart(text[j]))
                while (++j < text.size() && IsIdentCont(text[j])) {}
            return j - i;
        };
        auto i = offset;
        if (text.substr(i, 2) == "::") i += 2;
        auto n = ident(i);
        if (!n) return 0;
        i += n;
        while (text.substr(i, 2) == "::" && ident(i + 2)) i += 2 + ident(i + 2);
        return i - offset;
    }
};

}  // namespace goose
