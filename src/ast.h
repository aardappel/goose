// Goose compiler — parsed program data: type expressions, AST nodes, and symbols.
// All objects are owned centrally by Ast (append-only tables of pointers); the
// trees just point at each other, so no per-node destructors are needed.
#pragma once

namespace goose {

struct Node;
struct Block;
struct FunVal;
struct TypeExpr;
struct SFunction;
struct SStruct;
struct SEnum;
struct SVariant;
struct SAlias;
struct Ast;
struct VarDef;
struct FnSpec;
struct StructInst;
struct EnumInst;
struct TypeCheck;
struct Optimizer;
struct Inliner;

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
    TY_VOID,        // Typecheck-created: the "type" of statements and valueless blocks.
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
    StructInst *inst = nullptr;      // Instantiation cache (typecheck; concrete args only).
};

struct TypeEnum : TypeDetail {       // TY_ENUM
    SEnum *en = nullptr;
    vector<TypeExpr *> args;
    bool varmode = false;            // T.. — variable-mode use (§3.5).
    EnumInst *inst = nullptr;        // Instantiation cache (typecheck; concrete args only).
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
    int64_t size = -1;               // Evaluated sizeexpr (typecheck); -1 = not yet / none.
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
    bool byref = false;         // `Variant &b`: bind the payload by reference (§8.1).
    Node *lo = nullptr;         // P_INT / P_RANGE bounds (const exprs).
    Node *hi = nullptr;
};

struct MatchArm {
    Pattern pat;
    Node *body = nullptr;
    // Filled by typecheck:
    SVariant *variant = nullptr;    // P_VARIANT arms.
    VarDef *binder = nullptr;       // P_VARIANT payload binding, if any.
    int64_t lo = 0, hi = 0;         // P_INT / P_RANGE evaluated bounds (hi exclusive).
};

// A function value bound to a generic parameter at some call (typecheck):
// either a literal FunVal (trailing block) or a named function, plus the
// specialization whose locals it captures.
struct FnValBind {
    const FunVal *fv = nullptr;
    SFunction *named = nullptr;
    FnSpec *env = nullptr;
    bool operator==(const FnValBind &o) const {
        return fv == o.fv && named == o.named && env == o.env;
    }
};

// The checked value of an expression (typecheck.h): its type plus, for
// reference/slice-typed values, the lifetime root and provenance, plus the
// constant folding used for literal fit and array sizes. Lives here because
// every node's Check override returns one.
enum ConstKind { CK_NONE, CK_INT, CK_FLT };
struct Val {
    TypeExpr *type = nullptr;
    VarDef *root = nullptr;      // Ref/slice: owner of the pointee (null = static data).
    bool writable = false;       // Ref/slice provenance (§9.5).
    bool reusable = false;       // Root is a reusable pool (§5.4).
    ConstKind ck = CK_NONE;
    int64_t ival = 0;
    double fval = 0;
    bool strlit = false;         // String literal: adaptable to u8 array types.
    bool emptyarr = false;       // [] with as yet unknown element type.
    bool isnull = false;         // The null literal: adaptable to any optional.
    FnValBind fnv;               // When type is TY_FN.
};

// ---------------------------------------------------------------------------
// AST nodes. One base, leaves per construct; Dump implementations live
// together in dump.h so that pass reads top to bottom.

struct Node {
    Line line;
    TypeExpr *exprtype = nullptr;   // Filled by typecheck (the value's type; TY_VOID for none).
    Node(Line _line) : line(_line) {}
    virtual ~Node() {}
    virtual void Dump(string &s, int ind) const = 0;
    // Deep copy of the tree (typecheck clones function bodies per specialization
    // so annotations are per-instantiation). TypeExprs are shared, not cloned.
    virtual Node *Clone(Ast &ast) const = 0;
    // Calls f on every direct child; generic tree walks build on this
    // (implementations in clone.h alongside Clone).
    virtual void Children(const function<void(Node *)> &f) const = 0;
    // The typecheck pass for this node as a value expression; statements are
    // dispatched separately by TypeCheck::CheckStmt. Implementations live
    // together at the end of typecheck.h.
    virtual Val Check(TypeCheck &tc, TypeExpr *expected) = 0;
    // The optimizer pass (both in optimize.h): Cp1 is the annotation-preserving
    // deep copy used for inlining (its Cp wrapper carries exprtype over); Opt
    // folds/propagates/inlines below this node and returns the possibly
    // replaced node. Statements go through Optimizer::OptStmt, which handles
    // VarDecl and statement removal itself.
    virtual Node *Cp1(Inliner &inl) const = 0;
    virtual Node *Opt(Optimizer &opt) = 0;
};

#define NODE(name) struct name : Node { \
    void Dump(string &s, int ind) const override; \
    Node *Clone(Ast &ast) const override; \
    void Children(const function<void(Node *)> &f) const override; \
    Val Check(TypeCheck &tc, TypeExpr *expected) override; \
    Node *Cp1(Inliner &inl) const override; \
    Node *Opt(Optimizer &opt) override;
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
    // Filled by typecheck: exactly one of these.
    VarDef *vdef = nullptr;         // A variable.
    SFunction *fnref = nullptr;     // A named function used as a function value.
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
    // Filled by typecheck:
    StructInst *sinst = nullptr;    // Struct literals.
    EnumInst *einst = nullptr;      // Variant literals.
    SVariant *variant = nullptr;    //   "
    vector<int> fieldindices;       // Per init, the target field index.
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
    // Filled by typecheck: field access, builtin property (.len/.cap), or a
    // payload-less variant constant (obj names the enum type).
    int fieldidx = -1;
    int member = -1;                // BuiltinKind, builtins.h.
    SVariant *variantconst = nullptr;
    EnumInst *einst = nullptr;
    Dot(Line l, Node *_obj, string_view _name) : Node(l), obj(_obj), name(_name) {}
NODE_END

struct FunVal;

NODE(Call)
    Node *callee;               // Ident, or Dot for UFCS; resolution is later.
    vector<TypeExpr *> tyargs;  // Explicit <T> list, normally empty (inferred).
    vector<Node *> args;
    FunVal *trailing = nullptr; // Trailing-block function value, if any.
    // Filled by typecheck: exactly one resolution among these.
    FnSpec *spec = nullptr;             // A direct call to one specialization.
    vector<FnSpec *> dispatch;          // Case-function tag dispatch, per variant.
    int dispatcharg = -1;               //   which argument dispatches.
    int builtin = -1;                   // BuiltinKind, builtins.h (members included).
    Block *fvbody = nullptr;            // Call of a function value: checked body instance.
    vector<VarDef *> fvparams;          //   its parameter bindings.
    SFunction *fvtarget = nullptr;      //   the named fn a plain `return` inside exits.
    vector<TypeExpr *> rettypes;        // All return values (exprtype is rettypes[0] or void).
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

NODE(NullLit)                   // The null optional; adapts to any T? (§3.8).
    NullLit(Line l) : Node(l) {}
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
    Node *Clone(Ast &ast) const override;
    void Children(const function<void(Node *)> &f) const override;
    Val Check(TypeCheck &tc, TypeExpr *expected) override;
    Node *Cp1(Inliner &inl) const override;
    Node *Opt(Optimizer &opt) override;
};

NODE(IfExpr)
    Node *cond;
    Block *thenb;
    Node *elseb;                // Block, IfExpr, or null (any expr after optimization).
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
    // Filled by typecheck:
    VarDef *vdef = nullptr;
    VarDef *idxdef = nullptr;
    int iterkind = 0;           // IterKind, typecheck.h.
    ForLoop(Line l, bool _byref, string_view _var, string_view _idxvar, Node *_iter, Block *_body)
        : Node(l), byref(_byref), var(_var), idxvar(_idxvar), iter(_iter), body(_body) {}
NODE_END

NODE(Guard)
    Node *cond;
    Block *elseb;               // Null for the bare "guard c;" shorthand.
    int implicitexit = 0;       // Bare form resolution (typecheck): 1 = break, 2 = return.
    Guard(Line l, Node *_cond, Block *_elseb) : Node(l), cond(_cond), elseb(_elseb) {}
NODE_END

NODE(Return)
    vector<Node *> vals;
    string_view from;           // "return ... from f"; empty if absent.
    SFunction *target = nullptr;  // Filled by typecheck (the fn this exits; `from` or own).
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
    Node *Clone(Ast &ast) const override;
    void Children(const function<void(Node *)> &f) const override;
    Val Check(TypeCheck &tc, TypeExpr *expected) override;
    Node *Cp1(Inliner &inl) const override;
    Node *Opt(Optimizer &opt) override;
};

// let/var declarations, local and global.
NODE(VarDecl)
    bool isvar;                 // var vs let.
    bool reusable = false;
    bool isglobal = false;
    vector<string_view> names;  // let a, b = f();
    TypeExpr *type = nullptr;
    vector<Node *> inits;       // Empty for uninitialized locals.
    vector<VarDef *> defs;      // Filled by typecheck, aligned with names.
    VarDecl(Line l, bool _isvar) : Node(l), isvar(_isvar) {}
NODE_END

NODE(Assign)
    TType op;                   // T_ASSIGN, T_PLUSEQ, ...
    Node *lval, *rhs;
    bool pointee = false;       // Typecheck: lval is a reference and this writes its pointee.
    Assign(Line l, TType _op, Node *_lval, Node *_rhs) : Node(l), op(_op), lval(_lval), rhs(_rhs) {}
NODE_END

NODE(IncDec)
    TType op;                   // T_INC / T_DEC.
    Node *lval;
    IncDec(Line l, TType _op, Node *_lval) : Node(l), op(_op), lval(_lval) {}
NODE_END

// Created by the optimizer (optimize.h), never by the parser: an inlined call
// body spliced into the caller. Parameter bindings are ordinary VarDecls at
// the top of body. Value semantics for codegen: a Return inside whose target
// == sf exits this block with its value(s) as the block's value; normal
// completion yields the body's tail value. Returns with other targets pass
// through (they exit an enclosing InlineBlock or the real function).
NODE(InlineBlock)
    SFunction *sf;
    FnSpec *spec;               // The specialization this body came from.
    Block *body;
    InlineBlock(Line l, SFunction *_sf, FnSpec *_spec, Block *_body)
        : Node(l), sf(_sf), spec(_spec), body(_body) {}
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
    vector<FnSpec *> specs;     // Specializations (typecheck), owned by Ast.
};

struct SStruct {
    string_view name;
    Line line;
    vector<GenericParam> generics;
    vector<Field> fields;
    vector<StructInst *> insts;  // Instantiations (typecheck), owned by Ast.
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
    vector<EnumInst *> insts;   // Instantiations (typecheck), owned by Ast.
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
// Typecheck data. Everything below is produced by typecheck.h; it lives here
// because later phases (optimization/codegen) consume it alongside the AST.

// Size classes (§1.1): the max over a compound's parts, subject to placement.
enum SizeClass { SC_FIXED, SC_VARIABLE, SC_RESIZABLE };

// One checked variable: a global, local, parameter, or binding (for/match/
// function value). Created per specialization, so generic code has concrete
// types here. Also carries the transient flow state (assigned/narrowed) used
// while its scope is being checked.
struct VarDef {
    string_view name;
    TypeExpr *type = nullptr;   // Concrete (post-substitution) declared type.
    Line line;
    bool isvar = false;         // Assignable, and derived references writable.
    bool isglobal = false;
    bool isparam = false;
    bool reusable = false;
    FnSpec *ownerspec = nullptr;  // Null for globals.
    // Lifetime depth for the outlives check (§9.2): globals 0, then one per
    // nested scope along the current compile-time call path. Only comparable
    // between variables simultaneously live on that path.
    int depth = 0;
    VarDef *rootalias = nullptr;  // Params: canonical VarDef when call-site roots coincide.
    // For variables of reference/slice type: the provenance of the reference
    // value they hold, fixed at first binding (see typecheck.h header note).
    // A null-initialized optional has no commitment yet (refrootknown false).
    VarDef *refroot = nullptr;
    bool refrootknown = false;
    bool refwritable = true;
    bool refreusable = false;
    // Flow state during checking:
    bool assigned = false;
    TypeExpr *narrowed = nullptr;  // T? narrowed to T& in the current region.
    bool captured = false;         // Accessed from a nested fn / function value.
};

// A struct type with concrete generic arguments: substituted field types plus
// the derived properties every user of the type needs.
struct StructInst {
    SStruct *st = nullptr;
    vector<TypeExpr *> args;
    vector<TypeExpr *> ftypes;     // Aligned with st->fields; null for pads.
    vector<Node *> defaults;       // Aligned; checked clones of field defaults (or null).
    SizeClass sclass = SC_FIXED;
    bool flat = true;
    bool validated = false;        // Guards against recursive by-value nesting.
};

// An enum type with concrete generic arguments.
struct EnumInst {
    SEnum *en = nullptr;
    vector<TypeExpr *> args;
    vector<vector<TypeExpr *>> vftypes;   // Per variant, per field.
    vector<vector<Node *>> vdefaults;     // Checked clones of field defaults (or null).
    bool allfixed = true;                 // Fixed-mode use is legal.
    SizeClass varclass = SC_VARIABLE;     // Class when used in variable mode.
    bool flat = true;
    bool validated = false;
};

// Call-site facts about one reference/slice parameter, part of the
// specialization key (§10.2): the relative-outlives class of its root among
// the call's reference arguments (0 = static, 1 = outermost, ...), and the
// provenance bits.
struct RootArg {
    int cls = 0;
    bool writable = true;
    bool reusable = false;
    bool operator==(const RootArg &o) const {
        return cls == o.cls && writable == o.writable && reusable == o.reusable;
    }
};

// One monomorphic specialization of a function: the unit of typechecking and
// of later codegen. Owns nothing; body is a clone with annotations filled.
struct FnSpec {
    SFunction *sf = nullptr;
    FnSpec *lexparent = nullptr;   // Defining specialization, for nested fns.
    vector<TypeExpr *> argtypes;   // Concrete parameter types (the key, with the below).
    vector<RootArg> roots;         // Per reference/slice-typed parameter.
    vector<pair<string_view, FnValBind>> fnvals;  // Generic name -> bound function value.
    vector<pair<string_view, TypeExpr *>> bindings;  // Generic name -> concrete type.
    Block *body = nullptr;         // Cloned, annotated copy of sf->body.
    vector<VarDef *> params;
    vector<TypeExpr *> rets;       // TY_VOID-free: empty = no return values.
    vector<VarDef *> retroots;     // Per ret: root if ref/slice-typed (param VarDef,
                                   // global, or null = static), else null.
    vector<bool> retwritable;
    bool retsknown = false;
    bool checkedreturn = false;    // A return with values has been recorded.
    bool checked = false;
    bool inprogress = false;
    bool incycle = false;          // Part of a recursive cycle (§7.8).
    bool has_nonfixed_local = false;
    Line nonfixedline;             // First nonfixed local, for cycle diagnostics.
    set<SFunction *> needs;        // `return from` targets that must enclose every call.
    int id = 0;                    // Unique, for diagnostics/codegen naming.
    // Filled by the optimizer (optimize.h):
    int uses = 0;                  // Call sites in live code (tag-dispatch entries included).
    int nodecount = 0;             // Body node count after its own optimization.
    bool live = false;             // Reachable from main / threads / global initializers.
    bool noinline = false;         // This body cannot be spliced into callers.
};

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

    // Typecheck products (owned here so later phases can rely on them).
    vector<VarDef *> vardefs;
    vector<StructInst *> structinsts;
    vector<EnumInst *> enuminsts;
    vector<FnSpec *> fnspecs;

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
    TypeExpr *voidtype;

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
        voidtype = NewType(TY_VOID, Line {});
    }

    ~Ast() {
        for (auto n : allnodes) delete n;
        for (auto t : alltypes) delete t;
        for (auto d : typedetails) delete d;
        for (auto f : functions) delete f;
        for (auto s : structs) delete s;
        for (auto e : enums) delete e;
        for (auto a : aliases) delete a;
        for (auto v : vardefs) delete v;
        for (auto i : structinsts) delete i;
        for (auto i : enuminsts) delete i;
        for (auto sp : fnspecs) delete sp;
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

    VarDef *NewVarDef()       { auto v = new VarDef();     vardefs.push_back(v);     return v; }
    StructInst *NewStructInst() { auto i = new StructInst(); structinsts.push_back(i); return i; }
    EnumInst *NewEnumInst()   { auto i = new EnumInst();   enuminsts.push_back(i);   return i; }
    FnSpec *NewFnSpec() {
        auto sp = new FnSpec();
        sp->id = (int)fnspecs.size();
        fnspecs.push_back(sp);
        return sp;
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
