// Goose compiler — parsed program data: type expressions, AST nodes, and symbols.
// All objects are owned centrally by Ast (append-only tables of pointers); the
// trees just point at each other, so no per-node destructors are needed.
#pragma once

namespace goose {

struct Node;
struct Block;
struct TypeExpr;
struct SFunction;
struct SStruct;
struct SEnum;
struct SVariant;
struct SAlias;

struct Line {
    int line = 0;
    int fileidx = 0;
};

// ---------------------------------------------------------------------------
// Type expressions. During parsing, a type name cannot generally be looked up
// yet (top-level declarations are order-independent), so names parse as
// TY_UNRESOLVED; ResolveTypeNames (resolve.h) then rewrites every one of them
// in place into TY_STRUCT / TY_ENUM / TY_GENERIC, and *substitutes* alias uses
// with the aliased type. After resolution no TY_UNRESOLVED remains and aliases
// don't exist as types at all, so typecheck answers "is this an int?" by
// comparing kind, nothing else.

enum TypeKind {
    TY_INT,         // The one integer type; intstorage picks the in-memory width (§3.1, §3.6).
    TY_FLT,         // The one float type; fltstorage picks flt/f32/f64.
    TY_BOOL,
    TY_STRUCT,      // A nominal struct, with any generic type arguments.
    TY_ENUM,        // A nominal ADT; fixed-mode use, or variable-mode T.. (varmode flag, §3.5).
    TY_GENERIC,     // A generic type parameter name (typecheck validates scope).
    TY_UNRESOLVED,  // Parse-time only: a name + args; none survive resolution.
    TY_FN,          // Static function value type; bare `fn` has has_sig false.
    TY_ARRAY,       // The array family minus slices; akind picks the flavor.
    TY_SLICE,       // T[:] — a reference + count; kin of TY_REF, not TY_ARRAY.
    TY_REF,         // T& / T&<w> (self-relative offset of width w) / optional T?.
    TY_VARIANT,     // T.Name — variant type of an ADT.
};

enum ArrayKind {
    A_FIXED,       // T[k]           sizeexpr = k, a constant expression
    A_VAR,         // T[] / T[u8]    lenstorage = length field storage, -1 = default
    A_LIMITED,     // T[..k] / T[..] sizeexpr = capacity, null = construction-time
    A_GROW,        // T[>..]
    A_GROWSHRINK,  // T[>..<]
};

// Integer storage types (§3.1). IS_INT is the only expression type; the others
// exist in memory and widen to it on load. IS_VARINT is LEB128 (§3.6).
enum IntStorage { IS_INT, IS_I8, IS_I16, IS_I32, IS_I64, IS_U8, IS_U16, IS_U32, IS_U64, IS_VARINT };

enum FltStorage { FS_FLT, FS_F32, FS_F64 };  // f64 is the storage spelling of flt.

inline const char *IntStorageName(int s) {
    static const char *names[] = { "int", "i8", "i16", "i32", "i64",
                                   "u8", "u16", "u32", "u64", "varint" };
    return names[s];
}

inline const char *FltStorageName(int s) {
    static const char *names[] = { "flt", "f32", "f64" };
    return names[s];
}

// Per-kind detail payloads. A kind that needs more than one field gets one of
// these behind its single union member; they are owned by Ast.typedetails.
struct TypeDetail { virtual ~TypeDetail() {} };

struct TypeStruct : TypeDetail {     // TY_STRUCT
    SStruct *st = nullptr;
    vector<TypeExpr *> args;         // Generic type arguments, possibly empty.
};

struct TypeEnum : TypeDetail {       // TY_ENUM
    SEnum *en = nullptr;
    vector<TypeExpr *> args;
    bool varmode = false;            // T.. — variable-mode use (§3.5).
};

struct TypeName : TypeDetail {       // TY_UNRESOLVED and TY_GENERIC
    string_view name;
    vector<TypeExpr *> args;         // Args on a TY_GENERIC are an error (typecheck).
    bool varmode = false;            // T.. as written; resolution moves it to TypeEnum.
};

struct TypeFn : TypeDetail {         // TY_FN
    vector<TypeExpr *> args;         // Parameter types.
    vector<TypeExpr *> rets;
    bool has_sig = false;            // Bare `fn` vs full signature.
};

struct TypeArray : TypeDetail {      // TY_ARRAY
    TypeExpr *sub = nullptr;         // Element type.
    ArrayKind akind = A_FIXED;
    Node *sizeexpr = nullptr;        // A_FIXED size / A_LIMITED capacity (const expr; null for `[..]`).
    int lenstorage = -1;             // A_VAR length field IntStorage; -1 = default (u32).
};

struct TypeRef : TypeDetail {        // TY_REF
    TypeExpr *sub = nullptr;         // Pointee.
    int lenstorage = -1;             // Relative offset IntStorage; -1 = plain address.
    bool optional = false;           // T? — nullable (`T&?` and `T?` are the same type).
};

struct TypeVariant : TypeDetail {    // TY_VARIANT
    TypeExpr *adt = nullptr;         // The ADT type this is a variant of.
    union {
        string_view name;            // The variant name as written — valid until adt
                                     // resolves to an enum...
        SVariant *variant;           // ...then resolution replaces it with the variant
                                     // itself (which carries the name).
    };
    TypeVariant() : name() {}
};

struct TypeExpr {
    TypeKind kind;
    Line line;
    union {                          // Active member selected by kind:
        IntStorage intstorage;       //   TY_INT
        FltStorage fltstorage;       //   TY_FLT
        TypeExpr *sub;               //   TY_SLICE (element type)
        TypeStruct *struc;           //   TY_STRUCT
        TypeEnum *enu;               //   TY_ENUM
        TypeName *named;             //   TY_UNRESOLVED, TY_GENERIC
        TypeFn *fn;                  //   TY_FN
        TypeArray *arr;              //   TY_ARRAY
        TypeRef *ref;                //   TY_REF
        TypeVariant *var;            //   TY_VARIANT
    };                               // (TY_BOOL uses none.)

    TypeExpr(TypeKind _kind, Line _line) : kind(_kind), line(_line) { named = nullptr; }
    void Dump(string &s) const;
};

// The primitive type keyword tokens, in a contiguous range.
inline bool IsPrimTypeToken(TType t) { return t >= T_TINT && t <= T_TF64; }

struct GenericParam {
    string_view name;
    TypeExpr *bound = nullptr;  // Optional ": fn(...)" style documentation bound.
};

struct Param {
    string_view name;
    TypeExpr *type = nullptr;   // Null = untyped, i.e. generic.
    bool isvar = false;
};

struct Field {
    string_view name;
    TypeExpr *type = nullptr;
    Node *defaultval = nullptr;
    bool isconst = false;       // "let" field: const after construction.
    bool ispad = false;         // pad n / bare pad (padsize -1 = align next field).
    int64_t padsize = -1;
};

struct FieldInit {
    string_view name;           // Empty for positional.
    Node *val = nullptr;
};

enum PatKind { P_WILDCARD, P_VARIANT, P_INT, P_RANGE };

struct Pattern {
    PatKind kind = P_WILDCARD;
    string_view variant;        // P_VARIANT: variant name.
    string_view binder;         // P_VARIANT: optional payload binding.
    Node *lo = nullptr;         // P_INT / P_RANGE bounds (const exprs).
    Node *hi = nullptr;
};

struct MatchArm {
    Pattern pat;
    Node *body = nullptr;
};

// ---------------------------------------------------------------------------
// AST nodes. One base, leaves per construct; Dump implementations live
// together in dump.h so that pass reads top to bottom.

struct Node {
    Line line;
    Node(Line _line) : line(_line) {}
    virtual ~Node() {}
    virtual void Dump(string &s, int ind) const = 0;
};

#define NODE(name) struct name : Node { \
    void Dump(string &s, int ind) const override;
#define NODE_END };

NODE(IntLit)
    int64_t val;
    string_view text;  // Original spelling, so hex/char literals dump readably.
    IntLit(Line l, int64_t _val, string_view _text = {}) : Node(l), val(_val), text(_text) {}
NODE_END

NODE(FltLit)
    double val;
    FltLit(Line l, double _val) : Node(l), val(_val) {}
NODE_END

NODE(BoolLit)
    bool val;
    BoolLit(Line l, bool _val) : Node(l), val(_val) {}
NODE_END

NODE(StrLit)
    string val;
    StrLit(Line l, string _val) : Node(l), val(std::move(_val)) {}
NODE_END

NODE(Ident)
    string_view name;
    Ident(Line l, string_view _name) : Node(l), name(_name) {}
NODE_END

NODE(ArrayLit)
    vector<Node *> elems;
    Node *fillval = nullptr;    // [v; n] fill form: fillval/fillcount, elems empty.
    Node *fillcount = nullptr;
    ArrayLit(Line l) : Node(l) {}
NODE_END

NODE(StructLit)
    TypeExpr *type;             // Named type or variant type.
    vector<FieldInit> inits;
    StructLit(Line l, TypeExpr *_type) : Node(l), type(_type) {}
NODE_END

NODE(Unary)
    TType op;                   // T_MINUS, T_NOT, T_BITNOT, T_BITAND (ref-of).
    Node *child;
    Unary(Line l, TType _op, Node *_child) : Node(l), op(_op), child(_child) {}
NODE_END

NODE(Binary)
    TType op;
    Node *left, *right;
    Binary(Line l, TType _op, Node *_l, Node *_r) : Node(l), op(_op), left(_l), right(_r) {}
NODE_END

NODE(Dot)
    Node *obj;
    string_view name;
    Dot(Line l, Node *_obj, string_view _name) : Node(l), obj(_obj), name(_name) {}
NODE_END

struct FunVal;

NODE(Call)
    Node *callee;               // Ident, or Dot for UFCS; resolution is later.
    vector<TypeExpr *> tyargs;  // Explicit <T> list, normally empty (inferred).
    vector<Node *> args;
    FunVal *trailing = nullptr; // Trailing-block function value, if any.
    Call(Line l, Node *_callee) : Node(l), callee(_callee) {}
NODE_END

NODE(Index)
    Node *obj, *idx;
    Index(Line l, Node *_obj, Node *_idx) : Node(l), obj(_obj), idx(_idx) {}
NODE_END

NODE(SliceExpr)
    Node *obj;
    Node *lo = nullptr, *hi = nullptr;      // Either may be absent.
    bool lo_from_end = false, hi_from_end = false;  // ^k bounds.
    SliceExpr(Line l, Node *_obj) : Node(l), obj(_obj) {}
NODE_END

NODE(AsCast)
    Node *child;
    TypeExpr *type;
    bool unchecked;             // as! vs as.
    AsCast(Line l, Node *_child, TypeExpr *_type, bool _unchecked)
        : Node(l), child(_child), type(_type), unchecked(_unchecked) {}
NODE_END

NODE(CopyExpr)
    Node *child;
    CopyExpr(Line l, Node *_child) : Node(l), child(_child) {}
NODE_END

NODE(RangeExpr)                 // Only inside for-headers.
    Node *lo, *hi;
    RangeExpr(Line l, Node *_lo, Node *_hi) : Node(l), lo(_lo), hi(_hi) {}
NODE_END

// A { stmt* expr? } sequence; tail is the value-producing trailing expression.
struct Block : Node {
    vector<Node *> stmts;
    Node *tail = nullptr;
    Block(Line l) : Node(l) {}
    void Dump(string &s, int ind) const override;
};

NODE(IfExpr)
    Node *cond;
    Block *thenb;
    Node *elseb;                // Block, IfExpr, or null.
    IfExpr(Line l, Node *_cond, Block *_thenb, Node *_elseb)
        : Node(l), cond(_cond), thenb(_thenb), elseb(_elseb) {}
NODE_END

NODE(MatchExpr)
    Node *scrutinee;
    vector<MatchArm> arms;
    MatchExpr(Line l, Node *_scrutinee) : Node(l), scrutinee(_scrutinee) {}
NODE_END

NODE(EarlyBlock)                // "block { }": breakable early-out construct.
    Block *body;
    EarlyBlock(Line l, Block *_body) : Node(l), body(_body) {}
NODE_END

NODE(While)
    Node *cond;
    Block *body;
    While(Line l, Node *_cond, Block *_body) : Node(l), cond(_cond), body(_body) {}
NODE_END

NODE(LoopExpr)
    Block *body;
    LoopExpr(Line l, Block *_body) : Node(l), body(_body) {}
NODE_END

NODE(ForLoop)
    bool byref;                 // for &x in ...
    string_view var;
    string_view idxvar;         // Optional second binding; empty if absent.
    Node *iter;                 // Expression or RangeExpr.
    Block *body;
    ForLoop(Line l, bool _byref, string_view _var, string_view _idxvar, Node *_iter, Block *_body)
        : Node(l), byref(_byref), var(_var), idxvar(_idxvar), iter(_iter), body(_body) {}
NODE_END

NODE(Guard)
    Node *cond;
    Block *elseb;               // Null for the bare "guard c;" shorthand.
    Guard(Line l, Node *_cond, Block *_elseb) : Node(l), cond(_cond), elseb(_elseb) {}
NODE_END

NODE(Return)
    vector<Node *> vals;
    string_view from;           // "return ... from f"; empty if absent.
    Return(Line l) : Node(l) {}
NODE_END

NODE(Break)
    Node *val;
    Break(Line l, Node *_val) : Node(l), val(_val) {}
NODE_END

NODE(Continue)
    Continue(Line l) : Node(l) {}
NODE_END

// A function value: trailing block or block with named params. Non-escaping,
// compile-time entity; participates in calls only.
struct FunVal : Node {
    vector<Param> params;       // Empty param list = implicit "it".
    bool explicit_params = false;
    Block *body;
    FunVal(Line l, Block *_body) : Node(l), body(_body) {}
    void Dump(string &s, int ind) const override;
};

// let/var declarations, local and global.
NODE(VarDecl)
    bool isvar;                 // var vs let.
    bool reusable = false;
    bool isglobal = false;
    vector<string_view> names;  // let a, b = f();
    TypeExpr *type = nullptr;
    vector<Node *> inits;       // Empty for uninitialized locals.
    VarDecl(Line l, bool _isvar) : Node(l), isvar(_isvar) {}
NODE_END

NODE(Assign)
    TType op;                   // T_ASSIGN, T_PLUSEQ, ...
    Node *lval, *rhs;
    Assign(Line l, TType _op, Node *_lval, Node *_rhs) : Node(l), op(_op), lval(_lval), rhs(_rhs) {}
NODE_END

NODE(IncDec)
    TType op;                   // T_INC / T_DEC.
    Node *lval;
    IncDec(Line l, TType _op, Node *_lval) : Node(l), op(_op), lval(_lval) {}
NODE_END

// ---------------------------------------------------------------------------
// Symbols (top-level declarations). Wrapped in decl nodes so a module's
// top-level items keep source order for dumping.

struct SFunction {
    string_view name;
    Line line;
    vector<GenericParam> generics;
    vector<Param> params;
    vector<TypeExpr *> rets;    // Empty + !has_rets = inferred/none.
    bool has_rets = false;
    bool isrec = false;         // Declared with `recursive`.
    bool isthread = false;
    bool isnested = false;
    Block *body = nullptr;
};

struct SStruct {
    string_view name;
    Line line;
    vector<GenericParam> generics;
    vector<Field> fields;
};

struct SVariant {
    string_view name;
    vector<Field> fields;
    bool has_payload = false;   // Distinguishes "Point" from "Point {}".
};

struct SEnum {
    string_view name;
    Line line;
    vector<GenericParam> generics;
    vector<SVariant> variants;  // Stable once parsing completes; pointed at by TY_VARIANT.
};

// Not a type: a name referring to a type. Uses are substituted away during
// resolution; the symbol remains for the declaration itself and lookups.
struct SAlias {
    string_view name;
    Line line;
    TypeExpr *type = nullptr;
};

NODE(FnDecl)
    SFunction *sf;
    FnDecl(Line l, SFunction *_sf) : Node(l), sf(_sf) {}
NODE_END

NODE(StructDecl)
    SStruct *st;
    StructDecl(Line l, SStruct *_st) : Node(l), st(_st) {}
NODE_END

NODE(EnumDecl)
    SEnum *en;
    EnumDecl(Line l, SEnum *_en) : Node(l), en(_en) {}
NODE_END

NODE(AliasDecl)
    SAlias *al;
    AliasDecl(Line l, SAlias *_al) : Node(l), al(_al) {}
NODE_END

#undef NODE
#undef NODE_END

// ---------------------------------------------------------------------------
// Ast: owner of everything produced by parsing.

struct Ast {
    // Source buffers stay alive here; all string_views point into them. The
    // contents are heap-boxed so vector growth never moves an SSO buffer.
    vector<pair<string, unique_ptr<string>>> sources;   // (filename, contents).

    vector<Node *> allnodes;
    vector<TypeExpr *> alltypes;
    vector<TypeDetail *> typedetails;
    vector<SFunction *> functions;
    vector<SStruct *> structs;
    vector<SEnum *> enums;
    vector<SAlias *> aliases;

    vector<Node *> topdecls;                        // In source/import order.
    vector<VarDecl *> globals;                      // Initialization order.

    // Name maps. Types (structs/enums/aliases) share one namespace; functions
    // overload; globals have their own namespace for now.
    unordered_map<string_view, SStruct *> structmap;
    unordered_map<string_view, SEnum *> enummap;
    unordered_map<string_view, SAlias *> aliasmap;
    unordered_map<string_view, vector<SFunction *>> functionmap;
    unordered_map<string_view, VarDecl *> globalmap;

    // Shared instances of the primitive types.
    TypeExpr *inttypes[IS_VARINT + 1];
    TypeExpr *flttypes[FS_F64 + 1];
    TypeExpr *booltype;

    Ast() {
        for (int s = IS_INT; s <= IS_VARINT; s++) {
            inttypes[s] = NewType(TY_INT, Line {});
            inttypes[s]->intstorage = (IntStorage)s;
        }
        for (int s = FS_FLT; s <= FS_F64; s++) {
            flttypes[s] = NewType(TY_FLT, Line {});
            flttypes[s]->fltstorage = (FltStorage)s;
        }
        booltype = NewType(TY_BOOL, Line {});
    }

    ~Ast() {
        for (auto n : allnodes) delete n;
        for (auto t : alltypes) delete t;
        for (auto d : typedetails) delete d;
        for (auto f : functions) delete f;
        for (auto s : structs) delete s;
        for (auto e : enums) delete e;
        for (auto a : aliases) delete a;
    }

    TypeExpr *NewType(TypeKind kind, Line line) {
        auto t = new TypeExpr(kind, line);
        alltypes.push_back(t);
        return t;
    }

    template<typename T> T *NewDetail() {
        auto d = new T();
        typedetails.push_back(d);
        return d;
    }

    template<typename T, typename... Args> T *New(Args &&...args) {
        auto n = new T(std::forward<Args>(args)...);
        allnodes.push_back(n);
        return n;
    }

    TypeExpr *PrimTypeForToken(TType t) {
        switch (t) {
            case T_TINT:    return inttypes[IS_INT];
            case T_TI8:     return inttypes[IS_I8];
            case T_TI16:    return inttypes[IS_I16];
            case T_TI32:    return inttypes[IS_I32];
            case T_TI64:    return inttypes[IS_I64];
            case T_TU8:     return inttypes[IS_U8];
            case T_TU16:    return inttypes[IS_U16];
            case T_TU32:    return inttypes[IS_U32];
            case T_TU64:    return inttypes[IS_U64];
            case T_TVARINT: return inttypes[IS_VARINT];
            case T_TFLT:    return flttypes[FS_FLT];
            case T_TF32:    return flttypes[FS_F32];
            case T_TF64:    return flttypes[FS_F64];
            case T_TBOOL:   return booltype;
            default:        assert(false); return inttypes[IS_INT];
        }
    }

    bool TypeNameExists(string_view name) {
        return structmap.count(name) || enummap.count(name) || aliasmap.count(name);
    }

    void Dump(string &s) const;  // In dump.h.
};

}  // namespace goose
