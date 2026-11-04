// `import c "x.h";`: a C header read by TinyCC (third_party/tinycc/tcc_cdecl.c)
// and turned into the text of a Goose module, which the driver then parses
// like any other (ParseProgram). The module declares, under the C names:
//
//   * each function as an `extern fn` bound to its C symbol, in the shapes of
//     spec §7.10 and docs/reference.md §24;
//   * a pointer to a struct the header never completes (`sqlite3 *`) as a
//     handle, `extern struct sqlite3 { p: u64 }`: a typed token Goose passes
//     along, named by a typedef of the pointer where there is one;
//   * each complete struct a function needs, as an extern struct with `pad`
//     where C pads;
//   * enum constants and object-like macros with a constant value, as `let`;
//   * function-like macros calling a function with their parameters and
//     constants, as Goose functions of untyped parameters.
//
// What has no Goose shape (variables, unions and bitfields by value, long
// double, ...) is left out, and the module ends with a comment saying what
// and why; using one is an error saying so (Ast::cimportskipped). Sizes, signs and layouts are TinyCC's for the machine the
// compiler runs on; what the module assumes about the target is returned as
// C assertions, which the generated C checks when it is compiled.

#pragma once

#include "tcc_cdecl.h"

namespace goose {

struct CImported {
    string text;                 // The Goose module.
    vector<string> asserts;      // C conditions the module assumes of the target.
    vector<string> files;        // The header's own files (not the system ones).
    vector<pair<string, string>> skipped;  // The names left out, and why.
    // C that includes the header and asserts the layouts the module assumed,
    // for the target's compiler to check (BuildNativePart).
    string check;
};

namespace cimport {

struct Ty;
using TyP = shared_ptr<Ty>;

struct Param {
    string name;
    TyP ty;
};

struct Ty {
    int kind = GCD_OTHER;
    int size = -1, align = 0;
    bool uns = false, cnst = false, islong = false, ischar = false, isenum = false,
         bitfield = false;
    int tag = -1;                // Struct/union: index in Reader::tags.
    TyP target;                  // Pointee, element, or a function's result.
    long long count = -1;        // An array's length.
    vector<Param> params;        // A function's.
    bool variadic = false, oldstyle = false;
    string tdefname;             // A scalar's or pointer's typedef, as written (`sqlite3_int64`).
};

struct Field {
    string name;                 // Empty for an anonymous member.
    int offset = 0;
    TyP ty;
};

struct Tag {
    string name;                 // The tag, or the typedef naming an anonymous one.
    string alias;                // A typedef naming it, if any.
    string ptralias;             // A typedef naming a pointer to it (`XML_Parser`), if any.
    bool isunion = false, complete = false;
    int size = -1, align = 0;
    vector<Field> fields;
};

struct Decl {
    int kind = 0;
    string name;
    TyP ty;
    long long value = 0;         // An enum constant's.
};

// A function-like macro that calls a function with its parameters and
// constants (`deflateInit(strm, level)` is `deflateInit_((strm), (level),
// ZLIB_VERSION, (int)sizeof(z_stream))`): each argument is a parameter's
// index, or a constant's value (param -1).
struct MacroArg {
    int param = -1;
    struct Macro *value = nullptr;
};
struct FMacro {
    string name, callee;
    vector<string> params;
    vector<MacroArg> args;
};

struct Macro {
    string name;
    int kind = GCD_MACRO_NONE;
    long long i = 0;
    bool uns = false;
    int size = 0;
    double d = 0;
    string s;
};

// What a parse collects, converted out of TinyCC's structures while they live.
struct Reader {
    map<const void *, int> tagof;
    vector<Tag> tags;
    vector<Decl> decls;
    vector<Macro> macros;
    vector<string> deps;
    set<string> names;           // Every name declared (also macros).
    vector<string> fmacros;      // Function-like macros.
    vector<FMacro> wrappers;     // Those that call a function (FMacro).
    vector<unique_ptr<Macro>> argvalues;
    Macro *evaluated = nullptr;  // What gcd_eval reported last (OnMacro).
    string errors;

    int TagOf(const void *tag, bool isunion) {
        auto it = tagof.find(tag);
        if (it != tagof.end()) return it->second;
        auto idx = (int)tags.size();
        tagof[tag] = idx;
        tags.emplace_back();
        auto name = gcd_tag_name(tag);
        tags[idx].name = name ? name : "";
        tags[idx].isunion = isunion;
        tags[idx].complete = gcd_tag_complete(tag);
        return idx;
    }

    void FillTag(int idx, const void *type) {
        if (!tags[idx].complete || !tags[idx].fields.empty()) return;
        int align = 0;
        auto size = gcd_type_size(type, &align);
        tags[idx].size = size;
        tags[idx].align = align;
        vector<Field> fields;
        for (auto f = gcd_type_first(type); f; f = gcd_sym_next(f)) {
            auto name = gcd_sym_name(f);
            fields.push_back({ name ? name : "", gcd_sym_offset(f), nullptr });
        }
        // Mark the tag as filled before converting the field types: a field
        // pointing back at this struct then finds it and stops.
        tags[idx].fields = fields;
        size_t k = 0;
        for (auto f = gcd_type_first(type); f; f = gcd_sym_next(f), k++)
            tags[idx].fields[k].ty = Convert(gcd_sym_type(f));
    }

    TyP Convert(const void *t) {
        auto ty = make_shared<Ty>();
        ty->kind = gcd_type_kind(t);
        ty->size = gcd_type_size(t, &ty->align);
        ty->uns = gcd_type_unsigned(t);
        ty->cnst = gcd_type_const(t);
        ty->islong = gcd_type_long(t);
        ty->ischar = gcd_type_char(t);
        ty->isenum = gcd_type_enum(t);
        ty->bitfield = gcd_type_bitfield(t);
        if (auto tn = gcd_type_typedef(t)) ty->tdefname = tn;
        switch (ty->kind) {
            case GCD_PTR:
            case GCD_ARRAY:
                ty->target = Convert(gcd_type_target(t));
                ty->count = gcd_type_count(t);
                break;
            case GCD_FUNC:
                ty->target = Convert(gcd_type_target(t));
                ty->variadic = gcd_type_variadic(t);
                ty->oldstyle = gcd_type_oldstyle(t);
                for (auto p = gcd_type_first(t); p; p = gcd_sym_next(p)) {
                    auto name = gcd_sym_name(p);
                    ty->params.push_back({ name ? name : "", Convert(gcd_sym_type(p)) });
                }
                break;
            case GCD_STRUCT:
            case GCD_UNION:
                ty->tag = TagOf(gcd_type_tag(t), ty->kind == GCD_UNION);
                FillTag(ty->tag, t);
                break;
        }
        return ty;
    }
};

// The tokens of a macro body as gcd_macro_body writes them, separated by
// spaces, a string or character literal kept whole.
inline vector<string> MacroTokens(const string &body) {
    vector<string> toks;
    string cur;
    char quote = 0;
    for (size_t i = 0; i < body.size(); i++) {
        auto c = body[i];
        if (quote) {
            cur += c;
            if (c == '\\' && i + 1 < body.size()) cur += body[++i];
            else if (c == quote) quote = 0;
        } else if (c == ' ') {
            if (!cur.empty()) toks.push_back(cur), cur.clear();
        } else {
            if (c == '"' || c == '\'') quote = c;
            cur += c;
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

// A function-like macro whose body is one call of a function, each
// argument a parameter or a constant (FMacro), into r.wrappers.
inline void ReadWrapper(Reader &r, const char *name, const void *sym) {
    auto np = gcd_macro_nparams(sym);
    if (np < 0) return;
    FMacro m;
    m.name = name;
    char buf[4096];
    for (auto i = 0; i < np; i++) {
        if (!gcd_macro_param(sym, i, buf, sizeof buf)) return;
        m.params.push_back(buf);
    }
    if (!gcd_macro_body(sym, buf, sizeof buf)) return;
    auto t = MacroTokens(buf);
    // Parentheses around the whole of a range of tokens.
    auto enclosed = [&](size_t a, size_t b) {
        if (b - a < 2 || t[a] != "(" || t[b - 1] != ")") return false;
        auto depth = 0;
        for (auto k = a; k < b; k++) {
            if (t[k] == "(") depth++;
            else if (t[k] == ")" && --depth == 0 && k + 1 < b) return false;
        }
        return true;
    };
    size_t a = 0, b = t.size();
    while (enclosed(a, b)) a++, b--;
    auto ident = [](const string &s) { return !s.empty() && (isalpha((unsigned char)s[0]) || s[0] == '_'); };
    if (b - a < 3 || !ident(t[a]) || t[a + 1] != "(" || !enclosed(a + 1, b)) return;
    m.callee = t[a];
    auto isparam = [&](const string &s) {
        for (size_t k = 0; k < m.params.size(); k++) if (m.params[k] == s) return (int)k;
        return -1;
    };
    // The arguments, at the commas outside parentheses.
    vector<pair<size_t, size_t>> args;
    auto depth = 0;
    size_t start = a + 2;
    for (auto k = a + 2; k < b - 1; k++) {
        if (t[k] == "(") depth++;
        else if (t[k] == ")") depth--;
        else if (t[k] == "," && depth == 0) args.emplace_back(start, k), start = k + 1;
    }
    if (start < b - 1) args.emplace_back(start, b - 1);
    for (auto [x, y] : args) {
        while (enclosed(x, y)) x++, y--;
        MacroArg arg;
        if (y - x == 1 && isparam(t[x]) >= 0) {
            arg.param = isparam(t[x]);
        } else {
            string text;
            for (auto k = x; k < y; k++) {
                if (isparam(t[k]) >= 0) return;   // A parameter inside an expression.
                Append(text, k > x ? " " : "", t[k]);
            }
            r.evaluated = nullptr;
            if (!gcd_eval(text.c_str(), "\001arg") || !r.evaluated) return;
            arg.value = r.evaluated;
        }
        m.args.push_back(arg);
    }
    r.wrappers.push_back(std::move(m));
}

extern "C" inline void OnDecl(void *ctx, int kind, const char *name, const void *sym) {
    auto &r = *(Reader *)ctx;
    r.names.insert(name);
    if (kind == GCD_MACRO_FUNCTION) {
        r.fmacros.push_back(name);
        ReadWrapper(r, name, sym);
    }
    if (kind == GCD_MACRO || kind == GCD_MACRO_FUNCTION) return;
    if (kind == GCD_TAG) {
        auto t = sym;
        auto k = gcd_type_kind(t);
        if (k == GCD_STRUCT || k == GCD_UNION) {
            auto idx = r.TagOf(gcd_type_tag(t), k == GCD_UNION);
            r.FillTag(idx, t);
        }
        return;
    }
    Decl d;
    d.kind = kind;
    d.name = name;
    if (kind == GCD_ENUM_CONST) d.value = gcd_sym_enum_value(sym);
    d.ty = r.Convert(gcd_sym_type(sym));
    if (kind == GCD_TYPEDEF && (d.ty->kind == GCD_STRUCT || d.ty->kind == GCD_UNION)) {
        auto &tag = r.tags[d.ty->tag];
        if (tag.alias.empty()) tag.alias = name;
    }
    if (kind == GCD_TYPEDEF && d.ty->kind == GCD_PTR && d.ty->target->kind == GCD_STRUCT) {
        auto &tag = r.tags[d.ty->target->tag];
        if (tag.ptralias.empty()) tag.ptralias = name;
    }
    r.decls.push_back(std::move(d));
}

extern "C" inline void OnMacro(void *ctx, const char *name, int kind, long long i, int uns, int size,
                               double d, const char *s, int n) {
    auto &r = *(Reader *)ctx;
    // A macro argument gcd_eval evaluated (ReadWrapper), not a macro.
    if (name[0] == '\001') {
        auto m = make_unique<Macro>();
        m->kind = kind;
        m->i = i;
        m->uns = uns != 0;
        m->size = size;
        m->d = d;
        if (s) m->s.assign(s, (size_t)n);
        r.evaluated = m.get();
        r.argvalues.push_back(std::move(m));
        return;
    }
    r.names.insert(name);
    Macro m;
    m.name = name;
    m.kind = kind;
    m.i = i;
    m.uns = uns != 0;
    m.size = size;
    m.d = d;
    if (s) m.s.assign(s, (size_t)n);
    r.macros.push_back(std::move(m));
}

extern "C" inline void OnDep(void *ctx, const char *path) { ((Reader *)ctx)->deps.push_back(path); }
extern "C" inline void OnError(void *ctx, const char *msg) { Append(((Reader *)ctx)->errors, msg, "\n"); }

// One parse of `source` by TinyCC.
inline bool Parse(Reader &r, const string &libpath, const vector<string> &sysinc,
                  const vector<string> &inc, const vector<string> &defines, const string &source,
                  bool namesonly) {
    auto ptrs = [](const vector<string> &v) {
        vector<const char *> p;
        for (auto &s : v) p.push_back(s.c_str());
        return p;
    };
    auto s = ptrs(sysinc), i = ptrs(inc), d = ptrs(defines);
    gcd_cb cb { &r, OnDecl, OnMacro, OnDep, OnError };
    return gcd_parse(libpath.c_str(), s.data(), (int)s.size(), i.data(), (int)i.size(), d.data(),
                     (int)d.size(), source.c_str(), namesonly, &cb) == 0;
}

// --- from C to Goose -----------------------------------------------------------

inline bool IsGooseKeyword(const string &s) {
    static const set<string> kw = {
        #define F(t, s) s,
        TOKENS_KEYWORDS
        #undef F
        "cstr", "cptr",
    };
    return kw.count(s) != 0;
}

// Names reserved to the C implementation (`__x`, `_X`) are not the header's API.
inline bool Reserved(const string &s) {
    return s.size() >= 2 && s[0] == '_' && (s[1] == '_' || isupper((unsigned char)s[1]));
}

struct Writer {
    Reader &r;
    set<string> &system;         // Names the system headers declare.
    string out, skipped;
    vector<pair<string, string>> skiplist;
    set<int> handles;            // Tags used as handles.
    set<int> returned;           // Structs a function returns a pointer to.
    vector<int> structs;         // Tags emitted as structs, in order.
    map<int, int> structstate;   // Tag -> 0 failed, 1 ok, 2 bytes only, 3 being written.
    map<int, string> structtext;
    map<int, bool> structpad;    // Whether the Goose struct has pad.
    bool usedlong = false, usedptr = false;
    set<string> globals;         // Names the module declares.

    // A struct goes by the typedef that names it (`z_stream`, not `z_stream_s`).
    string TagName(int t) {
        auto &tag = r.tags[t];
        return tag.alias.empty() ? tag.name : tag.alias;
    }

    // The scalar typedefs used, with what each names (`type X = i64;`).
    map<string, string> aliases;

    // A scalar's Goose type, by its typedef's name where it was written with
    // one (an alias, `type sqlite3_int64 = i64;`) unless `base`.
    string Scalar(const Ty &t, bool base = false) {
        auto s = BaseScalar(t);
        if (s.empty() || base || t.tdefname.empty() || !Visible(t.tdefname) || t.isenum) return s;
        auto it = aliases.find(t.tdefname);
        if (it != aliases.end() && it->second != s) return s;
        aliases[t.tdefname] = s;
        return t.tdefname;
    }

    string Unalias(const string &s) {
        auto it = aliases.find(s);
        return it == aliases.end() ? s : it->second;
    }

    string BaseScalar(const Ty &t) {
        switch (t.kind) {
            case GCD_BOOL: return "bool";
            case GCD_FLOAT: return "f32";
            case GCD_DOUBLE: return "f64";
            case GCD_INT: {
                if (t.islong) usedlong = true;
                // A C enum is an int (§24), whatever sign TinyCC gives it.
                if (t.isenum && t.size == 4) return "i32";
                auto u = t.uns ? "u" : "i";
                switch (t.size) {
                    case 1: return cat(u, "8");
                    case 2: return cat(u, "16");
                    case 4: return cat(u, "32");
                    case 8: return cat(u, "64");
                }
                return "";
            }
        }
        return "";
    }

    // A struct Goose only holds a pointer to: one the header never completes
    // (`sqlite3`), or one the library hands out, a function returning a
    // pointer to it (`HPDF_Doc`). Structs the caller fills and passes
    // (`z_stream`) are Goose values instead, passed by reference.
    bool Opaque(const Ty &t) {
        return t.kind == GCD_STRUCT && !HandleName(t.tag).empty() && !Reserved(HandleName(t.tag)) &&
               (!r.tags[t.tag].complete || returned.count(t.tag));
    }

    // A handle goes by the typedef naming a pointer to its struct, where
    // one does (`XML_Parser`, `HPDF_Doc`), else as the struct.
    string HandleName(int t) {
        auto &p = r.tags[t].ptralias;
        return !p.empty() && !Reserved(p) && !system.count(p) ? p : TagName(t);
    }

    string Handle(int tag) {
        handles.insert(tag);
        return HandleName(tag);
    }

    // The handle a pointer `p` to an opaque struct is, by the typedef it was
    // written with where that is another name for it (`HPDF_Page`, an alias
    // of `HPDF_Dict`).
    string HandleFor(const Ty &p) {
        auto h = Handle(p.target->tag);
        auto &n = p.tdefname;
        if (n.empty() || n == h || !Visible(n)) return h;
        auto it = aliases.find(n);
        if (it != aliases.end() && it->second != h) return h;
        aliases[n] = h;
        return n;
    }

    // The Goose struct for a complete C struct: 1 with its fields, 2 as bytes
    // only (it has a field Goose cannot spell, so it crosses by reference
    // only), 0 when there is nothing to emit.
    int Struct(int t) {
        auto it = structstate.find(t);
        if (it != structstate.end()) return it->second == 3 ? 1 : it->second;
        auto &tag = r.tags[t];
        auto name = TagName(t);
        // A struct of the implementation (`__builtin_va_list`) is not one to spell.
        if (!tag.complete || tag.isunion || name.empty() || Reserved(name) || tag.size <= 0) {
            structstate[t] = 0;
            return 0;
        }
        structstate[t] = 3;
        string body;
        auto pos = 0;
        auto ok = true, pad = false;
        set<string> fieldnames;
        for (auto &f : tag.fields) {
            auto ft = FieldType(*f.ty);
            if (ft.empty() || f.name.empty() || f.offset < pos) {
                ok = false;
                break;
            }
            if (f.offset > pos) {
                Append(body, body.empty() ? "" : ", ", "pad ", f.offset - pos);
                pad = true;
            }
            auto fname = f.name;
            if (IsGooseKeyword(fname)) fname += "_";
            fieldnames.insert(fname);
            Append(body, body.empty() ? "" : ", ", fname, ": ", ft);
            pos = f.offset + f.ty->size;
        }
        if (ok && pos < tag.size) {
            Append(body, body.empty() ? "" : ", ", "pad ", tag.size - pos);
            pad = true;
        }
        auto state = ok ? 1 : 2;
        if (!ok) {
            body = cat("bytes: u8[", tag.size, "]");
            pad = true;
        }
        // With its fields it is an extern struct, which crosses by value as
        // C's own; as bytes only, a plain one, which crosses by reference.
        structtext[t] = cat(state == 1 ? "extern struct " : "struct ", name, " { ", body, " }");
        structpad[t] = pad;
        structstate[t] = state;
        structs.push_back(t);
        return state;
    }

    string FieldType(const Ty &t) {
        if (t.bitfield) return "";
        auto s = Scalar(t);
        if (!s.empty()) return s;
        switch (t.kind) {
            case GCD_PTR:
                usedptr = true;
                if (Opaque(*t.target)) return HandleFor(t);
                return "u64";
            case GCD_ARRAY: {
                if (t.count < 0) return "";
                auto e = FieldType(*t.target);
                if (e.empty()) return "";
                return cat(e, "[", t.count, "]");
            }
            case GCD_STRUCT:
                if (Struct(t.tag) == 1) return TagName(t.tag);
                return "";
        }
        return "";
    }

    // A parameter's Goose type, or "" with `why`.
    string ParamType(const Ty &t, string &why) {
        auto s = Scalar(t);
        if (!s.empty()) return s;
        switch (t.kind) {
            case GCD_PTR: {
                usedptr = true;
                auto &p = *t.target;
                auto cnst = p.cnst ? "const " : "";
                if (p.kind == GCD_INT && p.ischar && !p.uns && p.cnst) return "cstr";
                auto ps = Scalar(p);
                if (!ps.empty()) return cat(cnst, ps, "?");
                if (Opaque(p)) return HandleFor(t);
                if (p.kind == GCD_STRUCT) {
                    auto st = Struct(p.tag);
                    if (st) return cat(cnst, TagName(p.tag), "?");
                }
                // `T **`: C writes a pointer, into a handle or else a u64.
                if (p.kind == GCD_PTR && Opaque(*p.target)) return cat(Handle(p.target->tag), "?");
                if (p.kind == GCD_PTR) return cat(cnst, "u64?");
                // A function pointer: cfn(f) of an export fn of this
                // signature, an address C gave, or null.
                if (p.kind == GCD_FUNC) {
                    auto sig = CallbackType(p);
                    return sig.empty() ? "u64" : sig;
                }
                // `void *`: any reference, slice or array, an address as
                // u64, or null.
                if (p.kind == GCD_VOID) return cat(cnst, "cptr");
                return "u64";
            }
            case GCD_STRUCT:
                if (Reserved(TagName(t.tag))) {
                    why = cat("the implementation's ", TagName(t.tag));
                    return "";
                }
                if (Struct(t.tag) == 1) return TagName(t.tag);
                why = cat("a struct ", TagName(t.tag).empty() ? "" : TagName(t.tag) + " ",
                          "by value with a field Goose cannot spell");
                return "";
            case GCD_UNION: why = "a union by value"; return "";
            case GCD_LDOUBLE: why = "long double"; return "";
        }
        why = "a type with no Goose shape";
        return "";
    }

    // `fn(...) -> R` for a C function pointer's pointee: scalars, pointers
    // as u64; "" where it has another shape (it is then a plain u64).
    string CallbackType(const Ty &f) {
        if (f.variadic || (f.oldstyle && f.params.empty())) return "";
        auto one = [&](const Ty &t) -> string {
            auto s = Scalar(t, true);   // A signature in a parameter's type is not resolved.
            if (!s.empty()) return s;
            if (t.kind == GCD_PTR) { usedptr = true; return "u64"; }
            return "";
        };
        string s = "fn(";
        auto k = 0;
        for (auto &p : f.params) {
            auto pt = one(*p.ty);
            if (pt.empty()) return "";
            Append(s, k++ ? ", " : "", pt);
        }
        s += ")";
        if (f.target->kind != GCD_VOID) {
            auto rt = one(*f.target);
            if (rt.empty()) return "";
            Append(s, " -> ", rt);
        }
        return s;
    }

    string ReturnType(const Ty &t, string &why) {
        if (t.kind == GCD_VOID) return "-";
        if (t.kind == GCD_PTR) {
            usedptr = true;
            if (Opaque(*t.target)) return HandleFor(t);
            return "u64";
        }
        return ParamType(t, why);
    }

    void Skip(const string &name, const string &why) {
        Append(skipped, "//   ", name, ": ", why, "\n");
        skiplist.emplace_back(name, why);
    }

    bool Visible(const string &name) { return !Reserved(name) && !system.count(name) && !IsGooseKeyword(name); }

    string Literal(const string &s) {
        string o = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') Append(o, "\\", (char)c);
            else if (c == '\n') o += "\\n";
            else if (c == '\t') o += "\\t";
            else if (c < 32 || c >= 127) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\x%02x", c);
                o += buf;
            } else o += (char)c;
        }
        return o + "\"";
    }

    // The Goose parameter and result types of the functions bound, for the
    // macros calling them (Wrappers).
    map<string, vector<string>> fnparams;
    map<string, string> fnresult;
    set<string> fnvariadic;

    string Function(const Decl &d) {
        auto &f = *d.ty;
        if (f.oldstyle && f.params.empty()) { Skip(d.name, "declared without a prototype"); return ""; }
        string why, params;
        set<string> pnames;
        vector<string> ptypes;
        auto k = 0;
        for (auto &p : f.params) {
            auto pt = ParamType(*p.ty, why);
            if (pt.empty()) { Skip(d.name, cat("parameter ", k + 1, ", ", why)); return ""; }
            ptypes.push_back(pt);
            auto pn = p.name;
            if (pn.empty() || IsGooseKeyword(pn) || pnames.count(pn)) pn = cat(pn.empty() ? "p" : pn, "_", k + 1);
            pnames.insert(pn);
            Append(params, k ? ", " : "", pn, ": ", pt);
            k++;
        }
        // A variadic one takes further arguments of the types each call gives.
        if (f.variadic) params += k ? ", ..." : "...";
        auto rt = ReturnType(*f.target, why);
        if (rt.empty()) { Skip(d.name, cat("result, ", why)); return ""; }
        fnparams[d.name] = ptypes;
        fnresult[d.name] = rt;
        if (f.variadic) fnvariadic.insert(d.name);
        return cat("extern fn ", d.name, "(", params, ")", rt == "-" ? "" : cat(" -> ", rt), ";\n");
    }

    // A constant macro argument as Goose, at a parameter of type pt; "" if
    // it has no Goose spelling there.
    string ArgLiteral(const Macro &m, const string &written) {
        auto pt = Unalias(written);
        auto isint = pt.size() >= 2 && (pt[0] == 'i' || pt[0] == 'u') && isdigit((unsigned char)pt[1]);
        auto isfloat = pt == "f32" || pt == "f64";
        auto pointer = pt == "cstr" || pt == "cptr" || pt == "const cptr" || pt.back() == '?' ||
                       pt.rfind("fn(", 0) == 0;
        switch (m.kind) {
            case GCD_MACRO_STRING: return pt == "cstr" ? Literal(m.s) : "";
            case GCD_MACRO_POINTER:
                if (pointer && m.i == 0) return "null";
                return pt == "u64" ? to_string((unsigned long long)m.i) : "";
            case GCD_MACRO_INT: {
                auto v = m.uns ? to_string(m.size > 4 ? (unsigned long long)m.i : (unsigned long long)(uint32_t)m.i)
                               : to_string(m.size > 4 ? m.i : (long long)(int32_t)m.i);
                if (isint) return v;
                if (isfloat) return v + ".0";
                if (pt == "bool") return m.i ? "true" : "false";
                if (pointer && m.i == 0) return "null";
                return "";
            }
            case GCD_MACRO_FLOAT: {
                if (!isfloat || !std::isfinite(m.d)) return "";
                char buf[64];
                snprintf(buf, sizeof buf, "%.17g", m.d);
                string v = buf;
                if (v.find_first_of(".e") == string::npos) v += ".0";
                return v;
            }
        }
        return "";
    }

    // The C asserting, against the header itself, the sizes and offsets the
    // module took from TinyCC: compiled by the target's own compiler, it
    // stops a build where they differ (another machine, another ABI).
    string Check(const string &hpath, const string &header) {
        string o = cat("/* Generated by the Goose compiler: what `import c \"", header,
                       "\"` assumed, checked against the header\n   by the compiler the program "
                       "is built with. Do not edit. */\n#include \"", hpath,
                       "\"\n#include <stddef.h>\n");
        auto msg = [&](const string &what) { return Literal(cat("import c \"", header, "\" assumed ", what)); };
        for (auto t : structs) {
            if (structstate[t] != 1) continue;
            auto &tag = r.tags[t];
            auto cname = !tag.alias.empty() ? tag.alias : !tag.name.empty() ? cat("struct ", tag.name) : "";
            if (cname.empty()) continue;
            Append(o, "_Static_assert(sizeof(", cname, ") == ", tag.size, ", ",
                   msg(cat("sizeof(", cname, ") == ", tag.size)), ");\n");
            for (auto &f : tag.fields) {
                if (f.name.empty()) continue;
                Append(o, "_Static_assert(offsetof(", cname, ", ", f.name, ") == ", f.offset, ", ",
                       msg(cat("offsetof(", cname, ", ", f.name, ") == ", f.offset)), ");\n");
            }
        }
        static const map<string, int> sizes = { { "i8", 1 }, { "u8", 1 }, { "i16", 2 }, { "u16", 2 },
            { "i32", 4 }, { "u32", 4 }, { "i64", 8 }, { "u64", 8 }, { "f32", 4 }, { "f64", 8 }, { "bool", 1 } };
        for (auto &[name, base] : aliases) {
            auto it = sizes.find(base);
            if (it == sizes.end()) continue;
            Append(o, "_Static_assert(sizeof(", name, ") == ", it->second, ", ",
                   msg(cat("sizeof(", name, ") == ", it->second)), ");\n");
        }
        return o;
    }

    // The function-like macros that call a bound function with their
    // parameters and constants, as Goose functions of untyped parameters,
    // which the call's arguments give their types (as Zig's translate-c
    // makes such macros inline functions).
    string Wrappers() {
        string o;
        for (auto &m : r.wrappers) {
            if (!Visible(m.name) || globals.count(m.name)) continue;
            auto it = fnparams.find(m.callee);
            if (it == fnparams.end() || fnvariadic.count(m.callee) || it->second.size() != m.args.size())
                continue;
            string params, args;
            auto ok = true;
            for (size_t k = 0; k < m.params.size(); k++) {
                auto pn = m.params[k];
                if (IsGooseKeyword(pn)) pn += "_";
                Append(params, k ? ", " : "", pn);
            }
            for (size_t k = 0; ok && k < m.args.size(); k++) {
                auto &a = m.args[k];
                string s;
                if (a.param >= 0) {
                    s = m.params[a.param];
                    if (IsGooseKeyword(s)) s += "_";
                } else {
                    s = ArgLiteral(*a.value, it->second[k]);
                    ok = !s.empty();
                }
                Append(args, k ? ", " : "", s);
            }
            if (!ok) continue;
            auto rt = fnresult[m.callee];
            globals.insert(m.name);
            if (rt == "-") Append(o, "fn ", m.name, "(", params, ") { ", m.callee, "(", args, "); }\n");
            else Append(o, "fn ", m.name, "(", params, ") -> ", rt, " { ", m.callee, "(", args, ") }\n");
        }
        return o;
    }

    string Constants() {
        string o;
        for (auto &d : r.decls) {
            if (d.kind != GCD_ENUM_CONST || !Visible(d.name) || globals.count(d.name)) continue;
            auto t = Scalar(*d.ty);
            if (t.empty()) t = "i32";
            if (t == "i32" && (d.value < INT32_MIN || d.value > INT32_MAX)) t = "i64";
            globals.insert(d.name);
            Append(o, "let ", d.name, ": ", t, " = ", d.value, ";\n");
        }
        for (auto &m : r.macros) {
            if (!Visible(m.name) || globals.count(m.name)) continue;
            switch (m.kind) {
                case GCD_MACRO_INT:
                case GCD_MACRO_POINTER: {
                    auto ptr = m.kind == GCD_MACRO_POINTER;
                    auto uns = m.uns || ptr;
                    auto wide = m.size > 4 || ptr;
                    auto t = cat(uns ? "u" : "i", wide ? "64" : "32");
                    string v;
                    if (uns) v = to_string(wide ? (unsigned long long)m.i : (unsigned long long)(uint32_t)m.i);
                    else if (m.i == LLONG_MIN) v = "-9223372036854775807 - 1";
                    else v = to_string(wide ? m.i : (long long)(int32_t)m.i);
                    globals.insert(m.name);
                    Append(o, "let ", m.name, ": ", t, " = ", v, ";\n");
                    break;
                }
                case GCD_MACRO_FLOAT: {
                    if (!std::isfinite(m.d)) break;
                    char buf[64];
                    snprintf(buf, sizeof buf, "%.17g", m.d);
                    string v = buf;
                    if (v.find_first_of(".e") == string::npos) v += ".0";
                    globals.insert(m.name);
                    Append(o, "let ", m.name, ": ", m.size == 4 ? "f32" : "f64", " = ", v, ";\n");
                    break;
                }
                case GCD_MACRO_STRING:
                    globals.insert(m.name);
                    Append(o, "let ", m.name, " = ", Literal(m.s), ";\n");
                    break;
            }
        }
        return o;
    }
};

}  // namespace cimport

// The Goose module for `import c "header";` in a file of `dir`, with the
// package's C include directories and defines. Throws CompileError with
// TinyCC's messages when the header does not parse.
inline CImported CImport(const string &header, const string &dir, const vector<string> &cincludes,
                         const vector<string> &cdefines, const string &libpath) {
    using namespace cimport;
    // TinyCC's own headers (stdarg.h, stddef.h, ...), then the system's,
    // then goose's minimal stdint.h and friends as a fallback where there is
    // no system C library: put first, they hid the system's (the macOS SDK's
    // <inttypes.h> found no intmax_t).
    vector<string> sysinc = { cat(libpath, "/include") };
    auto sdk = MacSdkPath();
    if (!sdk.empty()) sysinc.push_back(cat(sdk, "/usr/include"));
    #if !defined(_WIN32) && !defined(__APPLE__)
        for (auto d : { "/usr/local/include",
                        #if defined(__x86_64__)
                            "/usr/include/x86_64-linux-gnu",
                        #elif defined(__aarch64__)
                            "/usr/include/aarch64-linux-gnu",
                        #endif
                        "/usr/include" })
            {
                error_code ec;
                if (filesystem::is_directory(filesystem::path(d), ec)) sysinc.push_back(d);
            }
    #endif
    sysinc.push_back(cat(libpath, "/goose/include"));
    vector<string> inc;
    if (!dir.empty()) inc.push_back(dir);
    for (auto &d : cincludes) inc.push_back(d);
    for (auto &d : inc)
        while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    // What clang's headers test for that TinyCC does not define (the macOS
    // SDK's TargetConditionals.h: `!defined(__has_extension) || ...`).
    auto defines = cdefines;
    defines.push_back("__has_extension(x)=0");
    #ifdef __APPLE__
        // The deployment target clang predefines, which the SDK's
        // Availability headers (and SDL's) require: macOS 11, as the
        // compiler's own builds assume.
        defines.push_back("__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=110000");
        defines.push_back("__ENVIRONMENT_OS_VERSION_MIN_REQUIRED__=110000");
    #endif
    Reader r;
    // <x.h>: looked for in the include directories only, the importing
    // file's first, not in the working directory.
    if (!Parse(r, libpath, sysinc, inc, defines, cat("#include <", header, ">\n"), false))
        throw CompileError { cat("cannot import the C header ", header, ":\n", r.errors) };
    // The files the header brought in, its own and the system's: a file
    // under one of the include directories is the header's.
    auto own = [&](const string &f) {
        auto af = AbsolutePath(f);
        for (auto &d : inc)
            if (af.rfind(cat(AbsolutePath(d), "/"), 0) == 0 ||
                af.rfind(AbsolutePath(cat(d, "/")), 0) == 0) return true;
        return false;
    };
    CImported res;
    string sys;
    for (auto &f : r.deps) {
        if (own(f)) res.files.push_back(f);
        else if (f.find("tccdefs.h") == string::npos) Append(sys, "#include \"", f, "\"\n");
    }
    // The names the system headers declare on their own, and TinyCC's
    // predefined ones, are not the header's API.
    Reader s;
    if (!Parse(s, libpath, sysinc, {}, defines, sys, true))
        throw CompileError { cat("cannot read the system headers ", header, " includes:\n", s.errors) };
    Writer w { r, s.names };
    for (auto &d : r.decls)
        if (d.kind == GCD_FUNCTION && w.Visible(d.name) && d.ty->target &&
            d.ty->target->kind == GCD_PTR && d.ty->target->target->kind == GCD_STRUCT)
            w.returned.insert(d.ty->target->target->tag);
    string fns;
    for (auto &d : r.decls) {
        if (!w.Visible(d.name)) continue;
        if (d.kind == GCD_FUNCTION) {
            auto f = w.Function(d);
            if (!f.empty()) {
                w.globals.insert(d.name);
                fns += f;
            }
        } else if (d.kind == GCD_VARIABLE) {
            w.Skip(d.name, "a variable (only functions are bound)");
        } else if (d.kind == GCD_STATIC) {
            w.Skip(d.name, "static: no symbol to bind");
        }
    }
    auto consts = w.Constants();
    auto wrappers = w.Wrappers();
    string types;
    for (auto &[name, base] : w.aliases)
        if (!w.globals.count(name)) Append(types, "type ", name, " = ", base, ";\n");
    for (auto t : w.handles) {
        auto name = w.HandleName(t);
        if (w.globals.count(name)) continue;
        Append(types, "extern struct ", name, " { p: u64 = 0 }\n");
    }
    for (auto t : w.structs)
        if (!w.handles.count(t)) Append(types, w.structtext[t], "\n");
    res.text = cat("// import c \"", header, "\": the Goose declarations of a C header, made by the\n",
                   "// compiler from what TinyCC reads in it (goose --c-import shows them).\n\n",
                   types.empty() ? "" : cat(types, "\n"), consts.empty() ? "" : cat(consts, "\n"), fns,
                   wrappers.empty() ? "" : cat("\n// Function-like macros that call a function above:\n", wrappers));
    // Function-like macros, and object-like ones with no constant value, are
    // not imported either; they are not listed in the module (a header has
    // many, mostly internal), but using one says why it is missing.
    for (auto &m : r.fmacros)
        if (w.Visible(m) && !w.globals.count(m)) res.skipped.emplace_back(m, "a function-like macro");
    for (auto &m : r.macros)
        if (m.kind == GCD_MACRO_NONE && w.Visible(m.name) && !w.globals.count(m.name))
            res.skipped.emplace_back(m.name, "a macro with no constant value");
    for (auto &k : w.skiplist) res.skipped.push_back(k);
    if (!w.skipped.empty()) Append(res.text, "\n// Not imported:\n", w.skipped);
    if (!res.files.empty()) res.check = w.Check(ForwardSlashes(AbsolutePath(res.files[0])), header);
    if (w.usedptr) res.asserts.push_back("sizeof(void *) == 8");
    if (w.usedlong) res.asserts.push_back(cat("sizeof(long) == ", (int)sizeof(long)));
    return res;
}

}  // namespace goose
