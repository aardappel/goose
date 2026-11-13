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
struct BuiltinParam {
    const char *name = "";
    const char *type = "";      // "" where untyped (polymorphic array receiver, condition, worker, or values).
    bool variadic = false;      // Trailing `...` parameter pack.
};

struct BuiltinForm {
    const char *tyargs = "";    // Explicit type parameter list, e.g. "<T>", "" for none.
    BuiltinParam params[3] = {};
    int nparams = 0;
    int minargs = 0;            // Minimum total arguments (receiver included for BF_MEMBER).
    const char *rets = "";      // Return type list after `->`, "" for void.

    constexpr bool Variadic() const { return nparams > 0 && params[nparams - 1].variadic; }
    constexpr int MaxArgs() const { return Variadic() ? 99 : nparams; }
};

struct BuiltinDoc {
    BuiltinKind kind;
    BuiltinForm forms[2];
    int nforms;                 // 0 for BF_PROPERTY (.len, .cap); 1 or 2 for callable builtins.
    const char *doc;            // Markdown documentation (non-empty).
};

inline constexpr BuiltinDoc builtindocs[] = {
    {
        .kind = B_PRINT,
        .forms = {
            { .params = { { .name = "values", .variadic = true } }, .nparams = 1, .minargs = 0 },
        },
        .nforms = 1,
        .doc = "Writes the arguments' text and a newline to standard output as a single write.",
    },
    {
        .kind = B_STR,
        .forms = {
            { .params = { { .name = "values", .variadic = true } }, .nparams = 1, .minargs = 0, .rets = "u8[>..]" },
        },
        .nforms = 1,
        .doc = "Returns the arguments' text as a fresh `u8[>..]`, built at its destination.",
    },
    {
        .kind = B_FORMAT,
        .forms = {
            { .params = { { .name = "out" }, { .name = "values", .variadic = true } }, .nparams = 2, .minargs = 1 },
        },
        .nforms = 1,
        .doc = "Appends the arguments' text to `out`, any growable `u8` array.",
    },
    {
        .kind = B_ASSERT,
        .forms = {
            { .params = { { .name = "c" } }, .nparams = 1, .minargs = 1 },
        },
        .nforms = 1,
        .doc = "Aborts unless `c` holds (`bool` or an optional, which it narrows afterwards).",
    },
    {
        .kind = B_ABORT,
        .forms = {
            { .params = { { .name = "msg", .type = "u8[:]" } }, .nparams = 1, .minargs = 1 },
        },
        .nforms = 1,
        .doc = "Aborts with `msg` and ends the program (or the current contained worker). Never returns.",
    },
    {
        .kind = B_EXIT,
        .forms = {
            { .params = { { .name = "code", .type = "i64" } }, .nparams = 1, .minargs = 1 },
        },
        .nforms = 1,
        .doc = "Ends the program with exit status `code`. Never returns.",
    },
    {
        .kind = B_HARDWARE_THREADS,
        .forms = {
            { .nparams = 0, .minargs = 0, .rets = "i64" },
        },
        .nforms = 1,
        .doc = "Returns a sensible worker count, never more than the data stacks allow.",
    },
    {
        .kind = B_THREAD_SPAWN,
        .forms = {
            { .params = { { .name = "worker" }, { .name = "args", .variadic = true } }, .nparams = 2, .minargs = 1, .rets = "i64" },
        },
        .nforms = 1,
        .doc = "Starts a `thread_fn` worker with flat copies of `args`; returns its id, never reused.",
    },
    {
        .kind = B_THREAD_SPAWN_CONTAINED,
        .forms = {
            { .params = { { .name = "worker" }, { .name = "args", .variadic = true } }, .nparams = 2, .minargs = 1, .rets = "i64" },
        },
        .nforms = 1,
        .doc = "Starts a `thread_fn` worker whose failed checks or `abort` end only that worker; returns its id, never reused.",
    },
    {
        .kind = B_THREAD_WAIT,
        .forms = {
            { .params = { { .name = "id", .type = "i64" } }, .nparams = 1, .minargs = 1, .rets = "bool" },
            { .params = { { .name = "id", .type = "i64" }, { .name = "msg", .type = "u8[>..]" } }, .nparams = 2, .minargs = 2, .rets = "bool" },
        },
        .nforms = 2,
        .doc = "Blocks until worker `id` has ended and releases its storage; returns `false` if it was contained and aborted, appending its abort message to growable `u8` array `msg` on the first wait.",
    },
    {
        .kind = B_QPUT,
        .forms = {
            { .params = { { .name = "v", .type = "T" } }, .nparams = 1, .minargs = 1, .rets = "bool" },
        },
        .nforms = 1,
        .doc = "Puts flat `v` on the typed queue for `T`; returns `false` at once without queueing when the queue is full.",
    },
    {
        .kind = B_QGET,
        .forms = {
            { .tyargs = "<T>", .nparams = 0, .minargs = 0, .rets = "T" },
            { .tyargs = "<T>", .params = { { .name = "ms", .type = "i64" } }, .nparams = 1, .minargs = 1, .rets = "T, bool" },
        },
        .nforms = 2,
        .doc = "Takes the next `T` from its typed queue, blocking or waiting at most `ms` milliseconds (returning `T`'s zero value and `false` on timeout).",
    },
    {
        .kind = B_QPOLL,
        .forms = {
            { .tyargs = "<T>", .nparams = 0, .minargs = 0, .rets = "T, bool" },
        },
        .nforms = 1,
        .doc = "Takes one `T` from its typed queue if available, else returns `T`'s zero value and `false`.",
    },
    {
        .kind = B_QLIMIT,
        .forms = {
            { .tyargs = "<T>", .params = { { .name = "n", .type = "i64" } }, .nparams = 1, .minargs = 1 },
        },
        .nforms = 1,
        .doc = "Sets the typed queue for `T` to hold at most `n` messages (`n > 0`).",
    },
    {
        .kind = B_DEFAULT,
        .forms = {
            { .tyargs = "<T>", .nparams = 0, .minargs = 0, .rets = "T" },
        },
        .nforms = 1,
        .doc = "Returns the value a fixed-size `T` has before anything is written to it, declared field defaults applied.",
    },
    {
        .kind = B_COPY,
        .forms = {
            { .params = { { .name = "x", .type = "T" } }, .nparams = 1, .minargs = 1, .rets = "T" },
        },
        .nforms = 1,
        .doc = "Returns a fresh copy of the stored value or reference `x` names.",
    },
    {
        .kind = B_MOVE_FROM,
        .forms = {
            { .params = { { .name = "dst", .type = "T[:]" }, { .name = "src", .type = "T[:]" } }, .nparams = 2, .minargs = 2 },
        },
        .nforms = 1,
        .doc = "Copies fixed-size elements from `src` into equal-length slice `dst` with overlap-safe ordering.",
    },
    {
        .kind = B_TO_BYTES,
        .forms = {
            { .params = { { .name = "a" } }, .nparams = 1, .minargs = 1, .rets = "u8[>..]" },
            { .params = { { .name = "a" }, { .name = "out", .type = "u8[>..]" } }, .nparams = 2, .minargs = 2 },
        },
        .nforms = 2,
        .doc = "Builds a framed image of array or slice `a` (a varint byte count then its element region), fresh as `u8[>..]` or appended to growable `u8` array `out`.",
    },
    {
        .kind = B_BYTES_OF,
        .forms = {
            { .params = { { .name = "a" } }, .nparams = 1, .minargs = 1, .rets = "const u8[:]" },
        },
        .nforms = 1,
        .doc = "Returns the element region of array or slice `a` alone as a read-only `const u8[:]` view without copying.",
    },
    {
        .kind = B_FROM_BYTES,
        .forms = {
            { .tyargs = "<A>", .params = { { .name = "b", .type = "u8[:]" } }, .nparams = 1, .minargs = 1, .rets = "A, bool" },
        },
        .nforms = 1,
        .doc = "Verifies untrusted image `b` and copies it into a fresh array `A` (`T[>..]`, `T[>..<]`, or `T[]`), or returns an empty array and `false`.",
    },
    {
        .kind = B_EMBED_SHADER,
        .forms = {
            { .params = { { .name = "path", .type = "const u8[:]" } }, .nparams = 1, .minargs = 1, .rets = "const u8[:]" },
            { .params = { { .name = "stage", .type = "const u8[:]" }, { .name = "source", .type = "const u8[:]", .variadic = true } }, .nparams = 2, .minargs = 2, .rets = "const u8[:]" },
        },
        .nforms = 2,
        .doc = "Compiles GLSL at compile time from file `path` or stage (`\"vert\"`, `\"frag\"`, `\"comp\"`) and `source` strings into a static `const u8[:]` shader blob.",
    },
    {
        .kind = B_EMBED_FILE,
        .forms = {
            { .params = { { .name = "path", .type = "const u8[:]" } }, .nparams = 1, .minargs = 1, .rets = "const u8[:]" },
        },
        .nforms = 1,
        .doc = "Reads file `path` (relative to the calling source file) at compile time into static `const u8[:]` data.",
    },
    {
        .kind = B_FILE,
        .forms = {
            { .nparams = 0, .minargs = 0, .rets = "const u8[:]" },
        },
        .nforms = 1,
        .doc = "Returns the base file name of the source file containing the call as `const u8[:]` (or of the caller when evaluated as an omitted default).",
    },
    {
        .kind = B_LINE,
        .forms = {
            { .nparams = 0, .minargs = 0, .rets = "i64" },
        },
        .nforms = 1,
        .doc = "Returns the 1-based line number of the call (or of the caller when evaluated as an omitted default).",
    },
    {
        .kind = B_LEN,
        .forms = {},
        .nforms = 0,
        .doc = "The element count of the array or slice.",
    },
    {
        .kind = B_CAP,
        .forms = {},
        .nforms = 0,
        .doc = "The capacity of the limited array (`T[..k]` or `T[..]`).",
    },
    {
        .kind = B_INDEX_OF,
        .forms = {
            { .params = { { .name = "a" }, { .name = "r", .type = "T&" } }, .nparams = 2, .minargs = 2, .rets = "i64" },
        },
        .nforms = 1,
        .doc = "Returns the index of the element `r` refers to in fixed-stride array `a` (proven rooted in `a`).",
    },
    {
        .kind = B_RANGE_INDEX,
        .forms = {
            { .params = { { .name = "a" }, { .name = "s", .type = "T[:]" } }, .nparams = 2, .minargs = 2, .rets = "i64" },
            { .params = { { .name = "a" }, { .name = "s", .type = "T[:]" }, { .name = "msg", .type = "const u8[:]" } }, .nparams = 3, .minargs = 3, .rets = "i64" },
        },
        .nforms = 2,
        .doc = "Returns the start index in fixed-stride array `a` of contained slice `s` (0 when empty), aborting with `msg` if out of range.",
    },
    {
        .kind = B_CHECK_LENGTH,
        .forms = {
            { .params = { { .name = "a" }, { .name = "n", .type = "i64" } }, .nparams = 2, .minargs = 2, .rets = "i64" },
            { .params = { { .name = "a" }, { .name = "n", .type = "i64" }, { .name = "msg", .type = "const u8[:]" } }, .nparams = 3, .minargs = 3, .rets = "i64" },
        },
        .nforms = 2,
        .doc = "Validates `n` against a data-stack reservation without changing `a`, returning `n` or aborting with `msg`.",
    },
    {
        .kind = B_PUSH,
        .forms = {
            { .params = { { .name = "a" }, { .name = "v", .type = "T" } }, .nparams = 2, .minargs = 2, .rets = "T&" },
        },
        .nforms = 1,
        .doc = "Constructs element `v` in place at the end of growable array `a` and returns a reference to it.",
    },
    {
        .kind = B_APPEND,
        .forms = {
            { .params = { { .name = "a" }, { .name = "src", .type = "T[:]" } }, .nparams = 2, .minargs = 2 },
        },
        .nforms = 1,
        .doc = "Appends the elements of array, slice, or array literal `src` into growable array `a`.",
    },
    {
        .kind = B_POP,
        .forms = {
            { .params = { { .name = "a" } }, .nparams = 1, .minargs = 1, .rets = "T" },
        },
        .nforms = 1,
        .doc = "Removes and returns the last element of `a`, aborting if empty.",
    },
    {
        .kind = B_POP_N,
        .forms = {
            { .params = { { .name = "a" }, { .name = "n", .type = "i64" } }, .nparams = 2, .minargs = 2 },
        },
        .nforms = 1,
        .doc = "Drops the last `n` elements of `a`, aborting if fewer than `n` exist.",
    },
    {
        .kind = B_RESIZE,
        .forms = {
            { .params = { { .name = "a" }, { .name = "n", .type = "i64" } }, .nparams = 2, .minargs = 2 },
            { .params = { { .name = "a" }, { .name = "n", .type = "i64" }, { .name = "v", .type = "T" } }, .nparams = 3, .minargs = 3 },
        },
        .nforms = 2,
        .doc = "Resizes `a` to length `n`, shrinking or growing with copies of `v`.",
    },
    {
        .kind = B_CLEAR,
        .forms = {
            { .params = { { .name = "a" } }, .nparams = 1, .minargs = 1 },
        },
        .nforms = 1,
        .doc = "Empties `a`, setting its length to 0.",
    },
    {
        .kind = B_ALLOC_INDEX,
        .forms = {
            { .params = { { .name = "pool", .type = "T[>..]&" }, { .name = "value", .type = "T" } }, .nparams = 2, .minargs = 2, .rets = "i64" },
        },
        .nforms = 1,
        .doc = "Allocates or reuses a slot in a `reusable` pool with `value` and returns its index. Deprecated: prefer `slot_pool<T>`.",
    },
    {
        .kind = B_ALLOC_REF,
        .forms = {
            { .params = { { .name = "pool", .type = "T[>..]&" }, { .name = "value", .type = "T" } }, .nparams = 2, .minargs = 2, .rets = "T&" },
        },
        .nforms = 1,
        .doc = "Allocates or reuses a slot in a `reusable` pool with `value` and returns a reference to it. Deprecated: prefer `slot_pool<T>`.",
    },
    {
        .kind = B_FREE,
        .forms = {
            { .params = { { .name = "pool", .type = "T[>..]&" }, { .name = "i", .type = "i64" } }, .nparams = 2, .minargs = 2 },
        },
        .nforms = 1,
        .doc = "Marks slot `i` available for reuse in a `reusable` pool. Deprecated: prefer `slot_pool<T>`.",
    },
    {
        .kind = B_ALLOC_SLICE,
        .forms = {
            { .params = { { .name = "pool", .type = "T[>..]&" }, { .name = "n", .type = "i64" } }, .nparams = 2, .minargs = 2, .rets = "T[:]" },
        },
        .nforms = 1,
        .doc = "Allocates or reuses a default-initialized run of `n` elements in a `reusable[]` pool. Deprecated: prefer `slice_pool<T>`.",
    },
    {
        .kind = B_REALLOC_SLICE,
        .forms = {
            { .params = { { .name = "pool", .type = "T[>..]&" }, { .name = "s", .type = "T[:]" }, { .name = "n", .type = "i64" } }, .nparams = 3, .minargs = 3, .rets = "T[:]" },
        },
        .nforms = 1,
        .doc = "Resizes pool slice `s` to `n` elements in a `reusable[]` pool. Deprecated: prefer `slice_pool<T>`.",
    },
    {
        .kind = B_FREE_SLICE,
        .forms = {
            { .params = { { .name = "pool", .type = "T[>..]&" }, { .name = "s", .type = "T[:]" } }, .nparams = 2, .minargs = 2 },
        },
        .nforms = 1,
        .doc = "Marks slice `s` available for reuse in a `reusable[]` pool. Deprecated: prefer `slice_pool<T>`.",
    },
};

constexpr bool CheckBuiltinDocs() {
    if (sizeof(builtindocs) / sizeof(builtindocs[0]) != B_COUNT) return false;
    for (int k = 0; k < B_COUNT; k++) {
        const auto &def = builtindefs[k];
        const auto &doc = builtindocs[k];
        if (doc.kind != k || !doc.doc || doc.doc[0] == '\0') return false;
        if (def.flags & BF_PROPERTY) {
            if (doc.nforms != 0) return false;
            continue;
        }
        if (doc.nforms < 1 || doc.nforms > 2) return false;
        int lo = doc.forms[0].minargs;
        int hi = doc.forms[0].MaxArgs();
        for (int i = 0; i < doc.nforms; i++) {
            const auto &f = doc.forms[i];
            if (f.nparams < 0 || f.nparams > 3) return false;
            if ((f.tyargs[0] != '\0') != ((def.flags & BF_TYARGS) != 0)) return false;
            if (f.minargs < lo) lo = f.minargs;
            if (f.MaxArgs() > hi) hi = f.MaxArgs();
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
