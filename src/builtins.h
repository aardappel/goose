// Goose compiler — the builtin function table. Builtins are ordinary
// functions reachable both as f(a, b) and, per UFCS, as a.f(b); the entries
// flagged BF_MEMBER are the array operations (§3.3, §5.4), which resolution
// tries before user functions at a.f() sites (§7.1) — their receiver is
// simply the first argument. BF_PROPERTY entries (.len/.cap) are field-like
// accesses, not calls.
//
// The table is one X-macro so the enum, name, arity, receiver rules, and
// signature can never drift apart. The generic checking code in typecheck.h
// (CheckBuiltin) validates the receiver kind/provenance and the signature;
// entries whose behavior it cannot express (polymorphic arguments, explicit
// type arguments, kind-dependent forms) carry "" / BF_CUSTOM parts and get
// dedicated code there, dispatched on the enum.
//
// Signature characters, applying to the arguments after the receiver for
// members: i int, f flt, b bool, e construct one element of the receiver's
// element type, a an array/slice of that element type. Return characters:
// i int, b bool, e element value, r reference to an element.
#pragma once

namespace goose {

enum BuiltinFlags {
    BF_MEMBER    = 1 << 0,  // First argument is an array receiver.
    BF_PROPERTY  = 1 << 1,  // .name access, not a call.
    BF_TYARGS    = 1 << 2,  // Takes an explicit <T> list.
    BF_WRITE     = 1 << 3,  // Receiver needs writable provenance (§9.5).
    BF_REUSABLE  = 1 << 4,  // Receiver must be a reusable pool (§5.4).
    BF_CUSTOM    = 1 << 5,  // Checked by dedicated code beyond the table.
};

// Receiver kind masks (members only): which array flavors have the member.
enum BuiltinRecv {
    BR_FIXED = 1 << 0, BR_VAR = 1 << 1, BR_LIMITED = 1 << 2,
    BR_GROW = 1 << 3, BR_GROWSHRINK = 1 << 4, BR_SLICE = 1 << 5,
    BR_ANY = BR_FIXED | BR_VAR | BR_LIMITED | BR_GROW | BR_GROWSHRINK | BR_SLICE,
    BR_GROWABLE = BR_LIMITED | BR_GROW | BR_GROWSHRINK,
    // Array kinds whose elements are at fixed strides *and* may be pointed
    // into, so a reference to one converts back to its index (§3.3). A
    // grow-shrink array admits no interior references at all (§5.2).
    BR_INDEXABLE = BR_FIXED | BR_LIMITED | BR_GROW | BR_GROWSHRINK,
    // A grow-only array shrinks too, where the checker can see that nothing is
    // rooted in it (§5.1, CheckGrowShrink in typecheck.h).
    BR_SHRINKABLE = BR_LIMITED | BR_GROWSHRINK | BR_GROW,
};

//        enum            name                min max  args  rets recv            flags
#define BUILTINS \
    F(B_PRINT,            "print",            0, 99,   "",   "",  0,              BF_CUSTOM) \
    F(B_STR,              "str",              0, 99,   "",   "",  0,              BF_CUSTOM) \
    F(B_FORMAT,           "format",           1, 99,   "",   "",  BR_GROWABLE,    BF_MEMBER | BF_WRITE | BF_CUSTOM) \
    F(B_ASSERT,           "assert",           1,  1,   "",   "",  0,              BF_CUSTOM) \
    F(B_ABORT,            "abort",            1,  1,   "",   "",  0,              BF_CUSTOM) \
    F(B_EXIT,             "exit",             1,  1,   "",   "",  0,              BF_CUSTOM) \
    F(B_HARDWARE_THREADS, "hardware_threads", 0,  0,   "",   "i", 0,              0) \
    F(B_THREAD_SPAWN,     "thread_spawn",     1, 99,   "",   "i", 0,              BF_CUSTOM) \
    F(B_THREAD_WAIT,      "thread_wait",      1,  1,   "i",  "",  0,              0) \
    F(B_QPUT,             "qput",             1,  1,   "",   "",  0,              BF_CUSTOM) \
    F(B_QGET,             "qget",             0,  0,   "",   "",  0,              BF_TYARGS | BF_CUSTOM) \
    F(B_QPOLL,            "qpoll",            0,  0,   "",   "",  0,              BF_TYARGS | BF_CUSTOM) \
    F(B_DEFAULT,          "default",          0,  0,   "",   "",  0,              BF_TYARGS | BF_CUSTOM) \
    F(B_LEN,              "len",              1,  1,   "",   "i", BR_ANY,         BF_MEMBER | BF_PROPERTY) \
    F(B_CAP,              "cap",              1,  1,   "",   "i", BR_LIMITED,     BF_MEMBER | BF_PROPERTY) \
    F(B_INDEX_OF,         "index_of",         2,  2,   "",   "i", BR_INDEXABLE,   BF_MEMBER | BF_CUSTOM) \
    F(B_PUSH,             "push",             2,  2,   "e",  "r", BR_GROWABLE,    BF_MEMBER | BF_WRITE | BF_CUSTOM) \
    F(B_APPEND,           "append",           2,  2,   "a",  "",  BR_GROWABLE,    BF_MEMBER | BF_WRITE) \
    F(B_POP,              "pop",              1,  1,   "",   "e", BR_SHRINKABLE,  BF_MEMBER | BF_WRITE) \
    F(B_RESIZE,           "resize",           2,  3,   "",   "",  BR_SHRINKABLE,  BF_MEMBER | BF_WRITE | BF_CUSTOM) \
    F(B_CLEAR,            "clear",            1,  1,   "",   "",  BR_SHRINKABLE,  BF_MEMBER | BF_WRITE | BF_CUSTOM) \
    F(B_ALLOC_INDEX,      "alloc_index",      2,  2,   "e",  "i", BR_GROW,        BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_ALLOC_REF,        "alloc_ref",        2,  2,   "e",  "r", BR_GROW,        BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_FREE,             "free",             2,  2,   "i",  "",  BR_GROW,        BF_MEMBER | BF_WRITE | BF_REUSABLE)

enum BuiltinKind {
    #define F(k, n, lo, hi, a, r, rv, fl) k,
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
    int flags;
};

inline const BuiltinDef builtindefs[] = {
    #define F(k, n, lo, hi, a, r, rv, fl) { k, n, lo, hi, a, r, rv, fl },
    BUILTINS
    #undef F
};

inline const BuiltinDef *LookupBuiltin(string_view name) {
    for (auto &d : builtindefs) if (name == d.name) return &d;
    return nullptr;
}

}  // namespace goose
