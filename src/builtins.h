// Goose compiler — the builtin function table. Builtins are ordinary
// functions reachable both as f(a, b) and, per UFCS, as a.f(b); the entries
// flagged BF_MEMBER are the array operations (§3.3, §5.4), which resolution
// tries before user functions at a.f() sites (§7.1) — their receiver is
// simply the first argument. BF_PROPERTY entries (.len/.cap) are field-like
// accesses, not calls.
//
// The table is one X-macro so the enum, name, arity, receiver rules,
// signature and the arrays a builtin grows or shrinks can never drift apart.
// The checking code in typecheck_builtins.h (CheckBuiltin) works from all of
// them; an entry whose arguments or result a signature cannot express
// (polymorphic arguments, explicit type arguments, kind-dependent forms) has
// "" for that part and gets dedicated code there, dispatched on the enum.
//
// Signature characters, applying to the arguments after the receiver for
// members: i int, e construct one element of the receiver's element type, a
// an array/slice of that element type; an e or an a also gives a pending
// `var x = []` receiver its element type (§4.2). Return characters: i int,
// e element value, r reference to an element, s slice of elements (a slice
// pool's, rooted at the receiver like r).
//
// `grows` is what the builtin does to the array it grows, as a diagnostic
// about the growth says it (§1.3(4)) -- "push into" for push -- or "" where
// it grows none. That array is the receiver, or under BF_OUTARG the `out`
// array a call may pass as its second argument.
#pragma once

namespace goose {

enum BuiltinFlags {
    BF_MEMBER    = 1 << 0,  // First argument is an array receiver.
    BF_PROPERTY  = 1 << 1,  // .name access, not a call.
    BF_TYARGS    = 1 << 2,  // Takes an explicit <T> list.
    BF_WRITE     = 1 << 3,  // Receiver needs writable provenance (§9.5).
    BF_REUSABLE  = 1 << 4,  // Receiver must be a reusable pool of slots (§5.4).
    BF_SHRINKS   = 1 << 5,  // May shrink its receiver (§5.1, §5.2).
    BF_SLICEPOOL = 1 << 6,  // Receiver must be a reusable[] pool of slices (§5.4).
    BF_OUTARG    = 1 << 7,  // Grows its second argument, an `out` array.
};

// Receiver kind masks (members only): which array flavors have the member.
enum BuiltinRecv {
    BR_FIXED = 1 << 0, BR_VAR = 1 << 1, BR_LIMITED = 1 << 2,
    BR_GROW = 1 << 3, BR_GROWSHRINK = 1 << 4, BR_SLICE = 1 << 5,
    BR_ANY = BR_FIXED | BR_VAR | BR_LIMITED | BR_GROW | BR_GROWSHRINK | BR_SLICE,
    BR_GROWABLE = BR_LIMITED | BR_GROW | BR_GROWSHRINK,
    // Array kinds `index_of` accepts (§3.3): fixed strides once the element
    // type is fixed-size, which the check requires on top of this.
    BR_INDEXABLE = BR_FIXED | BR_LIMITED | BR_GROW | BR_GROWSHRINK,
    // A grow-only array shrinks too, where the checker can see that nothing is
    // rooted in it (§5.1, CheckGrowShrink in typecheck_builtins.h).
    BR_SHRINKABLE = BR_LIMITED | BR_GROWSHRINK | BR_GROW,
};

// The receiver kind of a (dereferenced) type: one BR_ bit for an array
// or slice, nothing for any other type.
inline int RecvKindOf(TypeExpr *t) {
    if (t->kind == TY_SLICE) return BR_SLICE;
    if (t->kind != TY_ARRAY) return 0;
    switch (t->arr->akind) {
        case A_FIXED:      return BR_FIXED;
        case A_VAR:        return BR_VAR;
        case A_LIMITED:    return BR_LIMITED;
        case A_GROW:       return BR_GROW;
        case A_GROWSHRINK: return BR_GROWSHRINK;
    }
    return 0;
}

//        enum            name                min max  args  rets recv            grows          flags
#define BUILTINS \
    F(B_PRINT,            "print",            0, 99,   "",   "",  0,              "",            0) \
    F(B_STR,              "str",              0, 99,   "",   "",  0,              "",            0) \
    F(B_FORMAT,           "format",           1, 99,   "",   "",  BR_GROWABLE,    "format into", BF_MEMBER | BF_WRITE) \
    F(B_ASSERT,           "assert",           1,  1,   "",   "",  0,              "",            0) \
    F(B_ABORT,            "abort",            1,  1,   "",   "",  0,              "",            0) \
    F(B_EXIT,             "exit",             1,  1,   "",   "",  0,              "",            0) \
    F(B_HARDWARE_THREADS, "hardware_threads", 0,  0,   "",   "i", 0,              "",            0) \
    F(B_THREAD_SPAWN,     "thread_spawn",     1, 99,   "",   "i", 0,              "",            0) \
    F(B_THREAD_SPAWN_CONTAINED, "thread_spawn_contained", 1, 99, "", "i", 0,       "",            0) \
    F(B_THREAD_WAIT,      "thread_wait",      1,  2,   "",   "",  0,              "append to",   BF_OUTARG) \
    F(B_QPUT,             "qput",             1,  1,   "",   "",  0,              "",            0) \
    F(B_QGET,             "qget",             0,  1,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_QPOLL,            "qpoll",            0,  0,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_QLIMIT,           "qlimit",           1,  1,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_DEFAULT,          "default",          0,  0,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_COPY,             "copy",             1,  1,   "",   "",  0,              "",            0) \
    F(B_MOVE_FROM,        "move_from",        2,  2,   "",   "",  BR_SLICE,       "",            BF_MEMBER | BF_WRITE) \
    F(B_TO_BYTES,         "to_bytes",         1,  2,   "",   "",  BR_ANY,         "append to",   BF_MEMBER | BF_OUTARG) \
    F(B_BYTES_OF,         "bytes_of",         1,  1,   "",   "",  BR_ANY,         "",            BF_MEMBER) \
    F(B_FROM_BYTES,       "from_bytes",       1,  1,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_EMBED_SHADER,     "embed_shader",     1, 99,   "",   "",  0,              "",            0) \
    F(B_EMBED_FILE,       "embed_file",       1,  1,   "",   "",  0,              "",            0) \
    F(B_FILE,             "src_file",         0,  0,   "",   "",  0,              "",            0) \
    F(B_LINE,             "src_line",         0,  0,   "",   "i", 0,              "",            0) \
    F(B_LEN,              "len",              1,  1,   "",   "i", BR_ANY,         "",            BF_MEMBER | BF_PROPERTY) \
    F(B_CAP,              "cap",              1,  1,   "",   "i", BR_LIMITED,     "",            BF_MEMBER | BF_PROPERTY) \
    F(B_INDEX_OF,         "index_of",         2,  2,   "",   "i", BR_INDEXABLE,   "",            BF_MEMBER) \
    F(B_RANGE_INDEX,      "range_index",      2,  3,   "",   "i", BR_INDEXABLE,   "",            BF_MEMBER) \
    F(B_CHECK_LENGTH,     "check_length",     2,  3,   "i",  "i", BR_GROW | BR_GROWSHRINK, "",     BF_MEMBER) \
    F(B_PUSH,             "push",             2,  2,   "e",  "r", BR_GROWABLE,    "push into",   BF_MEMBER | BF_WRITE) \
    F(B_APPEND,           "append",           2,  2,   "a",  "",  BR_GROWABLE,    "append to",   BF_MEMBER | BF_WRITE) \
    F(B_POP,              "pop",              1,  1,   "",   "e", BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_POP_N,            "pop_n",            2,  2,   "i",  "",  BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_RESIZE,           "resize",           2,  3,   "",   "",  BR_SHRINKABLE,  "resize",      BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_CLEAR,            "clear",            1,  1,   "",   "",  BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_ALLOC_INDEX,      "alloc_index",      2,  2,   "e",  "i", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_ALLOC_REF,        "alloc_ref",        2,  2,   "e",  "r", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_FREE,             "free",             2,  2,   "i",  "",  BR_GROW,        "",            BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_ALLOC_SLICE,      "alloc_slice",      2,  2,   "",   "s", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_SLICEPOOL) \
    F(B_REALLOC_SLICE,    "realloc_slice",    3,  3,   "",   "s", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_SLICEPOOL) \
    F(B_FREE_SLICE,       "free_slice",       2,  2,   "",   "",  BR_GROW,        "",            BF_MEMBER | BF_WRITE | BF_SLICEPOOL)

enum BuiltinKind {
    #define F(k, n, lo, hi, a, r, rv, gr, fl) k,
    BUILTINS
    #undef F
    B_COUNT
};

struct BuiltinDef {
    BuiltinKind kind;
    const char *name;
    int minargs, maxargs;      // Receiver included for members.
    const char *args;          // Post-receiver argument signature; "" = none/custom.
    const char *rets;          // Return signature; "" = void or custom.
    int recv;                  // BuiltinRecv mask for members.
    const char *grows;         // What a growth by it is called; "" = grows nothing.
    int flags;
};

inline constexpr BuiltinDef builtindefs[] = {
    #define F(k, n, lo, hi, a, r, rv, gr, fl) { k, n, lo, hi, a, r, rv, gr, fl },
    BUILTINS
    #undef F
};

// The first definition of that name. Every call and member access asks.
inline const BuiltinDef *LookupBuiltin(string_view name) {
    static const auto byname = [] {
        unordered_map<string_view, const BuiltinDef *> m;
        for (auto &d : builtindefs) m.emplace(d.name, &d);
        return m;
    }();
    auto it = byname.find(name);
    return it == byname.end() ? nullptr : it->second;
}

// The definition of a kind a checked Call records (Call::builtin).
inline const BuiltinDef &BuiltinByKind(int kind) {
    assert(kind >= 0 && kind < (int)(sizeof(builtindefs) / sizeof(builtindefs[0])) &&
           builtindefs[kind].kind == kind);
    return builtindefs[kind];
}

// Metadata for LSP display (hover, signature help, completion, inlay hints).
struct BuiltinDoc {
    BuiltinKind kind;
    const char *forms;          // "\n"-separated forms; "" for BF_PROPERTY (.len, .cap).
    const char *doc;            // Markdown documentation (non-empty).
};

inline constexpr BuiltinDoc builtindocs[] = {
    { B_PRINT, "print(values...)", "Writes the arguments' text and a newline to standard output as a single write." },
    { B_STR, "str(values...) -> u8[>..]", "Returns the arguments' text as a fresh `u8[>..]`, built at its destination." },
    { B_FORMAT, "format(out, values...)", "Appends the arguments' text to `out`, any growable `u8` array." },
    { B_ASSERT, "assert(c)", "Aborts unless `c` holds (`bool` or an optional, which it narrows afterwards)." },
    { B_ABORT, "abort(msg: u8[:])", "Aborts with `msg` and ends the program (or the current contained worker). Never returns." },
    { B_EXIT, "exit(code: i64)", "Ends the program with exit status `code`. Never returns." },
    { B_HARDWARE_THREADS, "hardware_threads() -> i64", "Returns a sensible worker count, never more than the data stacks allow." },
    { B_THREAD_SPAWN, "thread_spawn(worker, args...) -> i64", "Starts a `thread_fn` worker with flat copies of `args`; returns its id, never reused." },
    { B_THREAD_SPAWN_CONTAINED, "thread_spawn_contained(worker, args...) -> i64", "Starts a `thread_fn` worker whose failed checks or `abort` end only that worker; returns its id, never reused." },
    { B_THREAD_WAIT, "thread_wait(id: i64) -> bool\nthread_wait(id: i64, msg) -> bool", "Blocks until worker `id` has ended and releases its storage; returns `false` if it was contained and aborted, appending its abort message to growable `u8` array `msg` on the first wait." },
    { B_QPUT, "qput(v: T) -> bool", "Puts flat `v` on the typed queue for `T`; returns `false` at once without queueing when the queue is full." },
    { B_QGET, "qget<T>() -> T\nqget<T>(ms: i64) -> T, bool", "Takes the next `T` from its typed queue, blocking or waiting at most `ms` milliseconds (returning `T`'s zero value and `false` on timeout)." },
    { B_QPOLL, "qpoll<T>() -> T, bool", "Takes one `T` from its typed queue if available, else returns `T`'s zero value and `false`." },
    { B_QLIMIT, "qlimit<T>(n: i64)", "Sets the typed queue for `T` to hold at most `n` messages (`n > 0`)." },
    { B_DEFAULT, "default<T>() -> T", "Returns the value a fixed-size `T` has before anything is written to it, declared field defaults applied." },
    { B_COPY, "copy(x: T) -> T", "Returns a fresh copy of the stored value or reference `x` names." },
    { B_MOVE_FROM, "move_from(dst: T[:], src: T[:])", "Copies fixed-size elements from `src` into equal-length slice `dst` with overlap-safe ordering." },
    { B_TO_BYTES, "to_bytes(a) -> u8[>..]\nto_bytes(a, out)", "Builds a framed image of array or slice `a` (a varint byte count then its element region), fresh as `u8[>..]` or appended to growable `u8` array `out`." },
    { B_BYTES_OF, "bytes_of(a) -> const u8[:]", "Returns the element region of array or slice `a` alone as a read-only `const u8[:]` view without copying." },
    { B_FROM_BYTES, "from_bytes<A>(b: u8[:]) -> A, bool", "Verifies untrusted image `b` and copies it into a fresh array `A` (`T[>..]`, `T[>..<]`, or `T[]`), or returns an empty array and `false`." },
    { B_EMBED_SHADER, "embed_shader(path: const u8[:]) -> const u8[:]\nembed_shader(stage: const u8[:], source: const u8[:]...) -> const u8[:]", "Compiles GLSL at compile time from file `path` or stage (`\"vert\"`, `\"frag\"`, `\"comp\"`) and `source` strings into a static `const u8[:]` shader blob." },
    { B_EMBED_FILE, "embed_file(path: const u8[:]) -> const u8[:]", "Reads file `path` (relative to the calling source file) at compile time into static `const u8[:]` data." },
    { B_FILE, "src_file() -> const u8[:]", "Returns the base file name of the source file containing the call as `const u8[:]` (or of the caller when evaluated as an omitted default)." },
    { B_LINE, "src_line() -> i64", "Returns the 1-based line number of the call (or of the caller when evaluated as an omitted default)." },
    { B_LEN, "", "The element count of the array or slice." },
    { B_CAP, "", "The capacity of the limited array (`T[..k]` or `T[..]`)." },
    { B_INDEX_OF, "index_of(a, r: T&) -> i64", "Returns the index of the element `r` refers to in fixed-stride array `a` (proven rooted in `a`)." },
    { B_RANGE_INDEX, "range_index(a, s: T[:]) -> i64\nrange_index(a, s: T[:], msg: const u8[:]) -> i64", "Returns the start index in fixed-stride array `a` of contained slice `s` (0 when empty), aborting with `msg` if out of range." },
    { B_CHECK_LENGTH, "check_length(a, n: i64) -> i64\ncheck_length(a, n: i64, msg: const u8[:]) -> i64", "Validates `n` against a data-stack reservation without changing `a`, returning `n` or aborting with `msg`." },
    { B_PUSH, "push(a, v: T) -> T&", "Constructs element `v` in place at the end of growable array `a` and returns a reference to it." },
    { B_APPEND, "append(a, src)", "Appends the elements of array, slice, or array literal `src` into growable array `a`." },
    { B_POP, "pop(a) -> T", "Removes and returns the last element of `a`, aborting if empty." },
    { B_POP_N, "pop_n(a, n: i64)", "Drops the last `n` elements of `a`, aborting if fewer than `n` exist." },
    { B_RESIZE, "resize(a, n: i64)\nresize(a, n: i64, v: T)", "Shrinks `a` to length `n`; with `v`, grows with copies of `v` or shrinks." },
    { B_CLEAR, "clear(a)", "Empties `a`, setting its length to 0." },
    { B_ALLOC_INDEX, "alloc_index(a, v: T) -> i64", "Allocates or reuses a slot in a `reusable` pool with `v` and returns its index. Deprecated: prefer `slot_pool<T>`." },
    { B_ALLOC_REF, "alloc_ref(a, v: T) -> T&", "Allocates or reuses a slot in a `reusable` pool with `v` and returns a reference to it. Deprecated: prefer `slot_pool<T>`." },
    { B_FREE, "free(a, i: i64)", "Marks slot `i` available for reuse in a `reusable` pool. Deprecated: prefer `slot_pool<T>`." },
    { B_ALLOC_SLICE, "alloc_slice(a, n: i64) -> T[:]", "Allocates or reuses a default-initialized run of `n` elements in a `reusable[]` pool. Deprecated: prefer `slice_pool<T>`." },
    { B_REALLOC_SLICE, "realloc_slice(a, s: T[:], n: i64) -> T[:]", "Resizes pool slice `s` to `n` elements in a `reusable[]` pool. Deprecated: prefer `slice_pool<T>`." },
    { B_FREE_SLICE, "free_slice(a, s: T[:])", "Marks slice `s` available for reuse in a `reusable[]` pool. Deprecated: prefer `slice_pool<T>`." },
};

static_assert(sizeof(builtindocs) / sizeof(builtindocs[0]) == B_COUNT,
              "builtindocs must stay in sync with BUILTINS");

struct ParsedBuiltinForm {
    string_view text;
    string_view tyargs;
    string_view params[3];
    int nparams = 0;
    int minargs = 0;
    int maxargs = 0;
    bool variadic = false;
    string_view rets;

    constexpr bool IsVariadic(int idx) const { return variadic && idx == nparams - 1; }

    static constexpr string_view Trim(string_view s) {
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
        return s;
    }

    static constexpr string_view ParamName(string_view param) {
        auto c = param.find(':');
        if (c != string_view::npos) param = param.substr(0, c);
        if (param.size() >= 3 && param.substr(param.size() - 3) == "...")
            param.remove_suffix(3);
        return Trim(param);
    }
};

constexpr bool ParseBuiltinForm(string_view line, int prev_max, ParsedBuiltinForm &f,
                                string_view *name_out = nullptr) {
    f = {};
    f.text = line;
    size_t i = 0, n = line.size();
    while (i < n && line[i] != '<' && line[i] != '(') i++;
    if (i == 0 || i == n) return false;
    if (name_out) *name_out = line.substr(0, i);
    if (line[i] == '<') {
        size_t lt = i;
        int angle = 0, bracket = 0;
        for (; i < n; i++) {
            auto ch = line[i];
            if (ch == '[') bracket++;
            else if (ch == ']') { if (bracket > 0) bracket--; }
            else if (bracket == 0) {
                if (ch == '<') angle++;
                else if (ch == '>' && --angle == 0) { i++; break; }
            }
        }
        if (angle != 0 || i == n || line[i] != '(') return false;
        f.tyargs = line.substr(lt, i - lt);
    }
    if (i >= n || line[i] != '(') return false;
    size_t lp = i++;
    int paren = 1, bracket = 0;
    size_t rp = string_view::npos;
    for (; i < n; i++) {
        auto ch = line[i];
        if (ch == '[') bracket++;
        else if (ch == ']') { if (bracket > 0) bracket--; }
        else if (ch == '(') paren++;
        else if (ch == ')') {
            if (--paren == 0) { rp = i; break; }
        }
    }
    if (rp == string_view::npos) return false;
    auto inner = ParsedBuiltinForm::Trim(line.substr(lp + 1, rp - (lp + 1)));
    if (!inner.empty()) {
        size_t start = 0;
        paren = bracket = 0;
        int angle = 0;
        for (size_t j = 0; j <= inner.size(); j++) {
            bool split = (j == inner.size());
            if (!split) {
                auto ch = inner[j];
                if (ch == '[') bracket++;
                else if (ch == ']') { if (bracket > 0) bracket--; }
                else if (ch == '(') paren++;
                else if (ch == ')') { if (paren > 0) paren--; }
                else if (ch == '<' && bracket == 0) angle++;
                else if (ch == '>' && bracket == 0 && !(j > 0 && inner[j - 1] == '-') && angle > 0) angle--;
                else if (ch == ',' && bracket == 0 && paren == 0 && angle == 0) split = true;
            }
            if (split) {
                auto p = ParsedBuiltinForm::Trim(inner.substr(start, j - start));
                if (p.empty() || f.nparams >= 3) return false;
                f.params[f.nparams++] = p;
                start = j + 1;
            }
        }
        for (int k = 0; k < f.nparams; k++) {
            auto p = f.params[k];
            if (p.size() >= 3 && p.substr(p.size() - 3) == "...") {
                if (k != f.nparams - 1) return false;
                f.variadic = true;
            }
            auto pname = ParsedBuiltinForm::ParamName(p);
            if (pname.empty()) return false;
            for (size_t c = 0; c < pname.size(); c++) {
                auto ch = pname[c];
                bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
                bool digit = ch >= '0' && ch <= '9';
                if (c == 0 ? !alpha : !(alpha || digit)) return false;
            }
        }
    }
    auto rest = line.substr(rp + 1);
    if (!rest.empty()) {
        if (rest.size() <= 4 || rest.substr(0, 4) != " -> ") return false;
        f.rets = ParsedBuiltinForm::Trim(rest.substr(4));
        if (f.rets.empty()) return false;
    }
    if (f.variadic) {
        // Trailing x... accepts 0+ items unless the previous form already ended at nparams - 1 (embed_shader).
        f.minargs = prev_max >= f.nparams - 1 ? f.nparams : f.nparams - 1;
        f.maxargs = 99;
    } else {
        f.minargs = f.nparams;
        f.maxargs = f.nparams;
    }
    return true;
}

constexpr bool ParseBuiltinForms(string_view forms, ParsedBuiltinForm (&out)[2], int &nforms,
                                 string_view expected_name = {}) {
    nforms = 0;
    if (forms.empty()) return true;
    int prev_max = -1;
    size_t pos = 0;
    while (pos <= forms.size()) {
        auto nl = forms.find('\n', pos);
        auto end = nl == string_view::npos ? forms.size() : nl;
        auto line = forms.substr(pos, end - pos);
        if (line.empty() || nforms >= 2) return false;
        string_view fname;
        if (!ParseBuiltinForm(line, prev_max, out[nforms], &fname)) return false;
        if (!expected_name.empty() && fname != expected_name) return false;
        prev_max = out[nforms].maxargs;
        nforms++;
        if (nl == string_view::npos) break;
        pos = nl + 1;
    }
    return true;
}

constexpr bool CheckBuiltinDocs() {
    for (int k = 0; k < B_COUNT; k++) {
        const auto &def = builtindefs[k];
        const auto &doc = builtindocs[k];
        if (doc.kind != k || !doc.forms || !doc.doc || doc.doc[0] == '\0') return false;
        if (def.flags & BF_PROPERTY) {
            if (doc.forms[0] != '\0') return false;
            continue;
        }
        ParsedBuiltinForm forms[2];
        int nforms = 0;
        if (!ParseBuiltinForms(doc.forms, forms, nforms, def.name) || nforms < 1 || nforms > 2)
            return false;
        int lo = forms[0].minargs;
        int hi = forms[0].maxargs;
        for (int i = 0; i < nforms; i++) {
            const auto &f = forms[i];
            if (i > 0 && f.minargs <= forms[i - 1].maxargs) return false;
            if ((!f.tyargs.empty()) != ((def.flags & BF_TYARGS) != 0)) return false;
            if ((def.flags & BF_MEMBER) && f.nparams < 1) return false;
            if (def.rets[0] != '\0') {
                string_view want = def.rets[0] == 'i' ? "i64"
                                 : def.rets[0] == 'e' ? "T"
                                 : def.rets[0] == 'r' ? "T&"
                                 : def.rets[0] == 's' ? "T[:]" : "";
                if (want.empty() || f.rets != want) return false;
            }
            if (f.minargs < lo) lo = f.minargs;
            if (f.maxargs > hi) hi = f.maxargs;
        }
        if (lo != def.minargs || hi != def.maxargs) return false;
    }
    return true;
}
static_assert(CheckBuiltinDocs(), "builtindocs must stay in sync with BUILTINS");

inline constexpr const BuiltinDoc &BuiltinDocByKind(int kind) {
    assert(kind >= 0 && kind < B_COUNT && builtindocs[kind].kind == kind);
    return builtindocs[kind];
}
}  // namespace goose
