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
//
// Display help: each entry carries structured `forms`, plus `ty` and `doc`,
// for the language server's hover, signature help and completion text. Each
// form has individual parameter labels, display return types ("" = void),
// and a variadic flag. Parameters use ident-call order, receiver first for
// members, including receiver-only property forms. Forms are ordered by
// increasing parameter count; optional arguments have separate forms so
// conditional returns are represented accurately. A variadic form has one
// explicit final tail parameter such as `args…`, accepting zero or more
// arguments. The checker still enforces the row's minargs/maxargs bounds.
// Display types are `i64` (any int), `bool`, `T` (element/value), `T&`,
// `T[:]`, `u8[:]`, `const u8[:]`, `u8[>..]`, `T[>..]`, `T[>..<]` and
// `T[]`; a bare name (`a`, `out`, `dst`, `src`, `x`, `v`, `c`, `worker`,
// `path`, `msg` for a growable u8 array) is used where no single type fits.
// `ty` gives explicit type arguments, and `doc` describes behavior and
// constraints. Both nested initializer lists are aggregate-initialized
// directly in the static builtin table, preserving their backing storage.
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

// Brace lists stay a single X-macro argument and initialize static storage.
#define BUILTIN_FORMS(...) { __VA_ARGS__ }

//        enum            name                min max  args  rets recv            grows          flags                                     forms, ty, doc
#define BUILTINS \
    F(B_PRINT,            "print",            0, 99,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"args…"}, "", true }), "", "Writes the arguments' text and a newline to standard output, as a single write, so lines from different threads never interleave. Takes 0..99 arguments of any type, inserting nothing between them; a value renders through its type's `format` overload where one exists.") \
    F(B_STR,              "str",              0, 99,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"args…"}, "u8[>..]", true }), "", "The arguments' text as a fresh `u8[>..]`, built directly at its destination. Takes 0..99 arguments of any type, inserting nothing between them.") \
    F(B_FORMAT,           "format",           1, 99,   "",   "",  BR_GROWABLE,    "format into", BF_MEMBER | BF_WRITE,                       BUILTIN_FORMS({ {"out", "args…"}, "", true }), "", "Appends the arguments' text to `out`, a growable `u8` array: limited, grow-only or grow-shrink. Takes 1..99 arguments in total, of any type after `out`, inserting nothing between them. A user `format` overload takes the calls it matches; the builtin takes the rest.") \
    F(B_ASSERT,           "assert",           1,  1,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"c"}, "" }), "", "Aborts unless `c` holds. `c` is a `bool` or an optional, which is narrowed past the call.") \
    F(B_ABORT,            "abort",            1,  1,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"msg: u8[:]"}, "" }), "", "Aborts with the message `msg`. Never returns, so the code after it is unreachable; not catchable.") \
    F(B_EXIT,             "exit",             1,  1,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"code: i64"}, "" }), "", "Ends the program with `code` as its exit code. Never returns; not catchable.") \
    F(B_HARDWARE_THREADS, "hardware_threads", 0,  0,   "",   "i", 0,              "",            0,                                          BUILTIN_FORMS({ {}, "i64" }), "", "A sensible worker count, never more than the data stacks allow.") \
    F(B_THREAD_SPAWN,     "thread_spawn",     1, 99,   "",   "i", 0,              "",            0,                                          BUILTIN_FORMS({ {"worker", "args…"}, "i64", true }), "", "Starts `worker`, a `thread_fn`, with `args…`; its worker id, never reused. The worker's arguments must be flat. Takes 1..99 arguments in total.") \
    F(B_THREAD_SPAWN_CONTAINED, "thread_spawn_contained", 1, 99, "", "i", 0,       "",            0,                                          BUILTIN_FORMS({ {"worker", "args…"}, "i64", true }), "", "Starts a worker whose aborts end only that worker; its worker id. A failed check or `abort` releases everything a return would, and the message goes to the first `thread_wait(id, msg)` instead of stderr. `exit`, stack overflow and main-thread aborts still end the program. Takes 1..99 arguments in total.") \
    F(B_THREAD_WAIT,      "thread_wait",      1,  2,   "",   "",  0,              "append to",   BF_OUTARG,                                  BUILTIN_FORMS({ {"id: i64"}, "bool" }, { {"id: i64", "msg"}, "bool" }), "", "Blocks until worker `id` has ended and its storage is released; `false` if it was contained and aborted. With `msg`, a growable `u8` array, the first such wait appends the contained worker's abort message, with its source location.") \
    F(B_QPUT,             "qput",             1,  1,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"v"}, "bool" }), "", "Puts `v` on the queue for its type; `false`, at once, if the queue is full. Queue elements must be flat. As a statement the result is ignored.") \
    F(B_QGET,             "qget",             0,  1,   "",   "",  0,              "",            BF_TYARGS,                                  BUILTIN_FORMS({ {}, "T" }, { {"ms: i64"}, "T, bool" }), "<T>", "Takes the next `T` from its typed queue, blocking. With `ms`, waits at most that many milliseconds, else `T`'s zero value and `false`; without it returns just the `T`. `T` is explicit and must be flat.") \
    F(B_QPOLL,            "qpoll",            0,  0,   "",   "",  0,              "",            BF_TYARGS,                                  BUILTIN_FORMS({ {}, "T, bool" }), "<T>", "Takes a `T` from its typed queue if there is one; else `T`'s zero value and `false`. `T` is explicit and must be flat.") \
    F(B_QLIMIT,           "qlimit",           1,  1,   "",   "",  0,              "",            BF_TYARGS,                                  BUILTIN_FORMS({ {"n: i64"}, "" }), "<T>", "Lets the queue for `T` hold at most `n` messages; past that `qput` returns `false` at once. `T` is explicit and must be flat.") \
    F(B_DEFAULT,          "default",          0,  0,   "",   "",  0,              "",            BF_TYARGS,                                  BUILTIN_FORMS({ {}, "T" }), "<T>", "The value a fixed-size `T` has before anything is written to it, declared field defaults applied.") \
    F(B_COPY,             "copy",             1,  1,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"x"}, "T" }), "", "A fresh copy of the stored value `x` names, for the destinations that never copy implicitly. `x` is a value or a reference, not a slice or a temporary; root owners cannot be copied.") \
    F(B_MOVE_FROM,        "move_from",        2,  2,   "",   "",  BR_SLICE,       "",            BF_MEMBER | BF_WRITE,                       BUILTIN_FORMS({ {"dst", "src: T[:]"}, "" }), "", "Copies `src` into `dst`, slices of equal length with fixed-size elements; aborts before writing if they differ. Overlap-safe: the original source values are copied even where the slices overlap; the source is not cleared. Unavailable for elements holding self-relative references.") \
    F(B_TO_BYTES,         "to_bytes",         1,  2,   "",   "",  BR_ANY,         "append to",   BF_MEMBER | BF_OUTARG,                      BUILTIN_FORMS({ {"a"}, "u8[>..]" }, { {"a", "out"}, "" }), "", "A framed image of `a`: a varint byte count, then its element region. One argument returns it as a fresh `u8[>..]`; with `out`, a growable `u8` array, appends it there and returns nothing.") \
    F(B_BYTES_OF,         "bytes_of",         1,  1,   "",   "",  BR_ANY,         "",            BF_MEMBER,                                  BUILTIN_FORMS({ {"a"}, "const u8[:]" }), "", "The element region of `a` alone, as a read-only view: nothing copied, no framing. The view is never writable, since bytes written through it would be links the checker never proved.") \
    F(B_FROM_BYTES,       "from_bytes",       1,  1,   "",   "",  0,              "",            BF_TYARGS,                                  BUILTIN_FORMS({ {"b: u8[:]"}, "A, bool" }), "<A>", "A verified array from the untrusted image `b`; `false` and an empty array if it is not one. `A` is the requested array type: `T[>..]`, `T[>..<]` or `T[]`, except a `T[>..<]` of elements holding relative references. Verifies safety, not integrity: edited bytes still describe a well-formed structure.") \
    F(B_EMBED_SHADER,     "embed_shader",     1, 99,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"path", "args…"}, "const u8[:]", true }), "", "`embed_shader(\"x.frag\")` compiles the shader file at that path, relative to the calling file; `embed_shader(\"stage\", source, …)` compiles the given GLSL, joined as lines, for stage `\"vert\"`, `\"frag\"` or `\"comp\"`. Either returns the compiled blob as `const u8[:]` into static data. Arguments are string literals, or `let` globals holding one; each distinct shader compiles once. Takes 1..99 arguments in total.") \
    F(B_EMBED_FILE,       "embed_file",       1,  1,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {"path"}, "const u8[:]" }), "", "The file at `path`, relative to the calling file, read when the program is compiled; its bytes as `const u8[:]` into static data. `path` is a string literal or a `let` global holding one; each distinct file is read once.") \
    F(B_FILE,             "src_file",         0,  0,   "",   "",  0,              "",            0,                                          BUILTIN_FORMS({ {}, "const u8[:]" }), "", "The base name of the source file containing the call, as `const u8[:]`. In a parameter default, the call that left the argument out is reported instead.") \
    F(B_LINE,             "src_line",         0,  0,   "",   "i", 0,              "",            0,                                          BUILTIN_FORMS({ {}, "i64" }), "", "The 1-based line of the call. In a parameter default, the call that left the argument out is reported instead.") \
    F(B_LEN,              "len",              1,  1,   "",   "i", BR_ANY,         "",            BF_MEMBER | BF_PROPERTY,                    BUILTIN_FORMS({ {"a"}, "i64" }), "", "The element count, of every array kind and slices. A property: `a.len`, not a call.") \
    F(B_CAP,              "cap",              1,  1,   "",   "i", BR_LIMITED,     "",            BF_MEMBER | BF_PROPERTY,                    BUILTIN_FORMS({ {"a"}, "i64" }), "", "The capacity, of `T[..k]` and `T[..]`. A property: `a.cap`, not a call.") \
    F(B_INDEX_OF,         "index_of",         2,  2,   "",   "i", BR_INDEXABLE,   "",            BF_MEMBER,                                  BUILTIN_FORMS({ {"a", "r: T&"}, "i64" }), "", "The index of the element `r` refers to, of arrays with fixed-size elements. `r` must provably point into this very array.") \
    F(B_RANGE_INDEX,      "range_index",      2,  3,   "",   "i", BR_INDEXABLE,   "",            BF_MEMBER,                                  BUILTIN_FORMS({ {"a", "s: T[:]"}, "i64" }, { {"a", "s: T[:]", "msg: u8[:]"}, "i64" }), "", "The start of `s`, a slice wholly contained in `a`, of arrays with fixed-size elements; checked at runtime where provenance does not prove it. An empty slice returns zero. `msg`, a string literal, replaces the runtime's abort message.") \
    F(B_CHECK_LENGTH,     "check_length",     2,  3,   "i",  "i", BR_GROW | BR_GROWSHRINK, "",     BF_MEMBER,                                  BUILTIN_FORMS({ {"a", "n: i64"}, "i64" }, { {"a", "n: i64", "msg: u8[:]"}, "i64" }), "", "Validates `n` against the data-stack reservation, of grow-only and grow-shrink arrays with fixed-size elements; returns it without changing the array. Rejects negative counts and counts whose elements alone would exceed the reservation. Neither allocates storage nor promises an append fits. `msg`, a string literal, replaces the runtime's abort message.") \
    F(B_PUSH,             "push",             2,  2,   "e",  "r", BR_GROWABLE,    "push into",   BF_MEMBER | BF_WRITE,                       BUILTIN_FORMS({ {"a", "v: T"}, "T&" }), "", "One element, constructed in place from `v`, of limited, grow-only and grow-shrink arrays; a reference to it. Exceeding a limited array's capacity aborts.") \
    F(B_APPEND,           "append",           2,  2,   "a",  "",  BR_GROWABLE,    "append to",   BF_MEMBER | BF_WRITE,                       BUILTIN_FORMS({ {"a", "src"}, "" }), "", "The elements of `src` — an array, slice or array literal of `T` — copied into `a`, of limited, grow-only and grow-shrink arrays. Exceeding a limited array's capacity aborts.") \
    F(B_POP,              "pop",              1,  1,   "",   "e", BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS,         BUILTIN_FORMS({ {"a"}, "T" }), "", "Removes and returns the last element, of limited and grow-shrink arrays, and grow-only ones where nothing is still rooted in them. Shrinking below empty aborts.") \
    F(B_POP_N,            "pop_n",            2,  2,   "i",  "",  BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS,         BUILTIN_FORMS({ {"a", "n: i64"}, "" }), "", "Drops the last `n` elements, of limited and grow-shrink arrays, and grow-only ones where nothing is still rooted in them. Shrinking below empty aborts.") \
    F(B_RESIZE,           "resize",           2,  3,   "",   "",  BR_SHRINKABLE,  "resize",      BF_MEMBER | BF_WRITE | BF_SHRINKS,         BUILTIN_FORMS({ {"a", "n: i64"}, "" }, { {"a", "n: i64", "v: T"}, "" }), "", "Grows with copies of `v`, or shrinks, of limited and grow-shrink arrays, and grow-only ones where nothing is still rooted in them. `resize(n)` shrinks only: growing without `v` aborts. A negative `n` aborts; exceeding a limited array's capacity aborts.") \
    F(B_CLEAR,            "clear",            1,  1,   "",   "",  BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS,         BUILTIN_FORMS({ {"a"}, "" }), "", "Empties the array, of limited and grow-shrink arrays, and grow-only ones where nothing is still rooted in them.") \
    F(B_ALLOC_INDEX,      "alloc_index",      2,  2,   "e",  "i", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_REUSABLE,        BUILTIN_FORMS({ {"a", "v: T"}, "i64" }), "", "Deprecated: use `slot_pool`. The index of a slot holding `v`, of `reusable` pools: a freed slot if there is one, else a fresh push.") \
    F(B_ALLOC_REF,        "alloc_ref",        2,  2,   "e",  "r", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_REUSABLE,        BUILTIN_FORMS({ {"a", "v: T"}, "T&" }), "", "Deprecated: use `slot_pool`. The same as `alloc_index`, as a reference.") \
    F(B_FREE,             "free",             2,  2,   "i",  "",  BR_GROW,        "",            BF_MEMBER | BF_WRITE | BF_REUSABLE,        BUILTIN_FORMS({ {"a", "i: i64"}, "" }), "", "Deprecated: use `slot_pool`. Records slot `i` for reuse; aborts if `i` is out of range. Releases no memory: every element stays a live value of its type.") \
    F(B_ALLOC_SLICE,      "alloc_slice",      2,  2,   "",   "s", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_SLICEPOOL,       BUILTIN_FORMS({ {"a", "n: i64"}, "T[:]" }), "", "Deprecated: use `slice_pool`. `n` elements from the first free span that holds them, else from the end of the array, which grows; every returned element is reset to its default.") \
    F(B_REALLOC_SLICE,    "realloc_slice",    3,  3,   "",   "s", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_SLICEPOOL,       BUILTIN_FORMS({ {"a", "s: T[:]", "n: i64"}, "T[:]" }), "", "Deprecated: use `slice_pool`. `s` resized to `n` elements, in place where it can be, else freed and copied. A grown slice may move, so elements holding self-relative references are rejected.") \
    F(B_FREE_SLICE,       "free_slice",       2,  2,   "",   "",  BR_GROW,        "",            BF_MEMBER | BF_WRITE | BF_SLICEPOOL,       BUILTIN_FORMS({ {"a", "s: T[:]"}, "" }), "", "Deprecated: use `slice_pool`. Records the run `s` for reuse.")

enum BuiltinKind {
    #define F(k, n, lo, hi, a, r, rv, gr, fl, forms, ty, doc) k,
    BUILTINS
    #undef F
    B_COUNT
};

struct BuiltinSignature {
    initializer_list<string_view> params;
    const char *ret;
    bool variadic = false;
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
    initializer_list<BuiltinSignature> forms; // Static display forms, shortest first.
    const char *ty;    // Display type arguments, e.g. "<T>"; "" = none.
    const char *doc;   // Markdown help: summary, then args/returns/constraints. Plain text, no `//` prefixes.
};

inline const BuiltinDef builtindefs[] = {
    #define F(k, n, lo, hi, a, r, rv, gr, fl, forms, ty, doc) { k, n, lo, hi, a, r, rv, gr, fl, forms, ty, doc },
    BUILTINS
    #undef F
};

#undef BUILTIN_FORMS

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

}  // namespace goose
