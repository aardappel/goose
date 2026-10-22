// Goose compiler — a JSON reader, for what CMake's file API replies about
// a package's link line (native.h). Enough of JSON for that: objects,
// arrays, strings with their escapes, numbers, true, false and null. And a
// string with its escapes, for what the compiler writes as JSON: the
// diagnostics `--diagnostics json` prints (main.cpp), `goose toolchain
// --json` (toolchain.h) and the language server's messages (lsp_rpc.h).
#pragma once

namespace goose {

// Appends v as a JSON string literal. The text is UTF-8 and goes through as
// it is; the characters JSON cannot hold in a string are escaped.
inline void JsonQuote(string &s, string_view v) {
    s += '"';
    for (auto c : v) {
        switch (c) {
            case '"':  s += "\\\""; break;
            case '\\': s += "\\\\"; break;
            case '\b': s += "\\b"; break;
            case '\f': s += "\\f"; break;
            case '\n': s += "\\n"; break;
            case '\r': s += "\\r"; break;
            case '\t': s += "\\t"; break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", (unsigned)(uint8_t)c);
                    s += buf;
                } else {
                    s += c;
                }
        }
    }
    s += '"';
}

struct Json {
    enum Kind { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT } kind = NUL;
    bool b = false;
    double num = 0;
    string str;
    vector<Json> arr;
    vector<pair<string, Json>> obj;

    // Define these after Json is complete: pair's constructor traits can
    // otherwise instantiate this recursive type's vector members too early.
    Json();
    ~Json();
    Json(const Json &);
    Json(Json &&) noexcept;
    Json &operator=(const Json &);
    Json &operator=(Json &&) noexcept;

    const Json *Get(const string &key) const;
    string Str(const string &key) const;
    const vector<Json> &Arr(const string &key) const;
};

inline Json::Json() = default;
inline Json::~Json() = default;
inline Json::Json(const Json &) = default;
inline Json::Json(Json &&) noexcept = default;
inline Json &Json::operator=(const Json &) = default;
inline Json &Json::operator=(Json &&) noexcept = default;

// Iterating obj instantiates pair<string, Json>, which needs Json complete.
inline const Json *Json::Get(const string &key) const {
    for (auto &[k, v] : obj) if (k == key) return &v;
    return nullptr;
}

inline string Json::Str(const string &key) const {
    auto v = Get(key);
    return v && v->kind == STRING ? v->str : string();
}

inline const vector<Json> &Json::Arr(const string &key) const {
    static const vector<Json> none;
    auto v = Get(key);
    return v && v->kind == ARRAY ? v->arr : none;
}

struct JsonReader {
    const char *p;
    const char *start;

    [[noreturn]] void Fail(const char *what) {
        throw CompileError { cat("malformed JSON at byte ", (int)(p - start), ": ", what) };
    }
    void Space() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; }
    void Expect(char c) {
        Space();
        if (*p != c) Fail(cat("expected ", string(1, c)).c_str());
        p++;
    }
    static void Utf8(string &s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63));
        } else {
            s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63));
            s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63));
        }
    }
    unsigned Hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4; i++, p++) {
            auto c = *p;
            auto d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) Fail("bad \\u escape");
            v = v * 16 + (unsigned)d;
        }
        return v;
    }
    string String() {
        Expect('"');
        string s;
        for (;;) {
            auto c = *p++;
            if (!c) Fail("unterminated string");
            if (c == '"') return s;
            if (c != '\\') { s += c; continue; }
            c = *p++;
            switch (c) {
                case '"': case '\\': case '/': s += c; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'u': {
                    auto cp = Hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        auto lo = Hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    Utf8(s, cp);
                    break;
                }
                default: Fail("bad escape");
            }
        }
    }
    Json Value() {
        Space();
        Json v;
        switch (*p) {
            case '{':
                v.kind = Json::OBJECT;
                p++;
                Space();
                if (*p == '}') { p++; return v; }
                for (;;) {
                    auto key = String();
                    Expect(':');
                    v.obj.emplace_back(key, Value());
                    Space();
                    if (*p == ',') { p++; continue; }
                    Expect('}');
                    return v;
                }
            case '[':
                v.kind = Json::ARRAY;
                p++;
                Space();
                if (*p == ']') { p++; return v; }
                for (;;) {
                    v.arr.push_back(Value());
                    Space();
                    if (*p == ',') { p++; continue; }
                    Expect(']');
                    return v;
                }
            case '"':
                v.kind = Json::STRING;
                v.str = String();
                return v;
            case 't': case 'f': case 'n': {
                const char *word = *p == 't' ? "true" : *p == 'f' ? "false" : "null";
                auto n = strlen(word);
                if (strncmp(p, word, n)) Fail("bad literal");
                p += n;
                v.kind = *word == 'n' ? Json::NUL : Json::BOOL;
                v.b = *word == 't';
                return v;
            }
            default: {
                char *end;
                v.num = strtod(p, &end);
                if (end == p) Fail("unexpected character");
                v.kind = Json::NUMBER;
                p = end;
                return v;
            }
        }
    }
};

inline Json ParseJson(const string &text) {
    JsonReader r { text.c_str(), text.c_str() };
    auto v = r.Value();
    r.Space();
    if (*r.p) r.Fail("text after the value");
    return v;
}

}  // namespace goose
