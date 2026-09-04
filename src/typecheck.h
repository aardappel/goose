// Goose compiler — the typechecker. Whole-program, call-graph order (§10.1):
// starting from global initializers and main, every function is checked per
// unique specialization of (argument types, reference roots and writability,
// bound function values), with generic parameters substituted (§7.7, §10.2).
// Each specialization gets a clone of the function body with all annotations
// (types, resolved symbols) filled in; those clones are what later phases
// (optimization/codegen) consume.
//
// References are transparent (§3.8): an expression denoting a reference
// behaves as its pointee in every value context (the checker "decays" it),
// except where the destination type is itself a reference — initialization,
// reference-typed parameters/fields, `.=` — which binds the reference value.
// Plain `=` through a reference writes the pointee; `.=` rebinds it.
//
// The lifetime system (§9) is implemented as: every reference/slice-typed
// value carries a static root (a VarDef, null = static data), an exactness
// bit saying whether that root owns the target or merely outlives it, and
// provenance bits (writable §9.5, reusable §5.4). Roots are compared by scope
// depth along the current compile-time call path; only rules that need the
// target's *identity* -- storing it as a relative reference (§3.9), and
// codegen's proof that two fat reference parameters are distinct stacks --
// consult the exactness bit, and fall back to the conservative path without
// it. A reference read out of a container is re-rooted by ReadBackRoot: the
// container bounds the lifetime, and the candidate variables of the enclosing
// scope that can hold the pointee by value say which storage it can be, so a
// sole candidate is exact (§9.5). Other deliberate v1 rules (now part of the
// spec, §9.2/§9.5): a read-back reference is writable regardless of its
// original provenance (writability launders through storage -- the language's
// const-cast loophole); a reference variable commits to one root depth for
// its whole life; all returns of one function agree on the returned
// reference's root. Inside a recursive cycle (§7.8) a reference may be stored
// only if it is rooted at a global or at a pool parameter -- a parameter root
// class whose members are all references to resizable-class values, which no
// cycle function can own (VarDef::poolclass); a back edge must then pass
// those pools exactly as the entry call did (ValidatePoolArgs). A cycle's
// *return* roots cannot come from its returns either, since a back edge
// reaches a function before those are checked, so they are predicted by a
// syntactic fixpoint over the cycle's returns before any body runs
// (CycleRoots, typecheck_cycles.h) and verified against the real returns as
// they are checked. Remaining conservatisms marked TODO: long-distance
// returns carry only global/static refs, and references rooted at a caller's
// fixed-size local are still pass-down-only inside a cycle.
//
// This file holds the TypeCheck class -- its state, the small utilities, and
// the driver -- with its members declared in the order they are defined
// across typecheck_types.h, typecheck_exprs.h, typecheck_flow.h,
// typecheck_calls.h and typecheck_builtins.h; the per-node Check overrides
// are typecheck_nodes.h.
#pragma once

namespace goose {

enum IterKind { IK_RANGE, IK_COUNT, IK_ARRAY, IK_SLICE };

// Type validation positions: what may be declared where.
enum ValidPos { VT_LOCAL, VT_GLOBAL, VT_PARAM, VT_RET, VT_FIELD, VT_ELEM, VT_POINTEE };

struct TypeCheck {
    Ast &ast;

    // (Val, the checked value of an expression, lives in ast.h: node Check
    // overrides return it.)

    // An assignable/addressable path: Ident, field, or element. Its
    // provenance names the storage's owner, and `writable` whether the whole
    // path admits writes.
    struct LVal : Prov {
        TypeExpr *type = nullptr;    // The location's own type (varints undecoded).
        VarDef *var = nullptr;       // Set when the path is a bare variable name.
        bool fromstorage = false;    // Reached by a field or element step, so a
                                     // reference read out of it is a read-back (§9.5).
        bool fotail = false;         // A frame object's resizable tail: has its own header (C.2).
        bool isvarint = false;       // varint field: read-only refs, not assignable.
    };

    // One level of the compile-time call path.
    struct Frame {
        SFunction *sf = nullptr;     // Null for the global-initializer frame.
        FnSpec *spec = nullptr;      // Owner of locals declared here (null at globals).
        FnSpec *lexspec = nullptr;   // Lexical env for generic bindings (differs for funvals).
        int lexframe = -1;           // Frame index for free-variable lookup chains.
        int scopebase = 0;           // First scope index belonging to this frame.
        int varbase = 0;             // First var index belonging to this frame.
        Line callline;               // Call site, for instantiation chain diagnostics.
        bool isfunval = false;
    };

    enum ScopeKind { SK_PLAIN, SK_FN, SK_LOOP, SK_BLOCK };
    struct Scope {
        int kind = SK_PLAIN;
        int varbase = 0;
        int fnbase = 0;              // Into localfns.
        Node *node = nullptr;        // The loop / `block` construct for SK_LOOP/SK_BLOCK.
        TypeExpr *breaktype = nullptr;
        bool hasbreak = false;
        bool valuelessbreak = false;
    };

    vector<Frame> frames;
    vector<Scope> scopes;
    vector<VarDef *> vars;                            // All in-scope variables, all frames.
    vector<pair<int, SFunction *>> localfns;          // Nested fns, with their scope index.
    bool reachable = true;
    // Inside a block/if/match/loop that produces a value, or a function-value
    // body: an enclosing expression may hold references it evaluated before
    // this point, which are in no variable and so invisible to the liveness
    // scan of CheckGrowShrink.
    bool invalue = false;
    bool inreturn = false;   // Checking a return's values: the function's own locals move.
    // The destination of the value under construction (for reference stores):
    // its root plus whether that root is the destination storage's owner.
    struct Dest {
        VarDef *root = nullptr;
        bool exact = false;
        bool varbind = false;  // A reference/slice variable itself: a binding, not a store.
    };
    Dest curdst;
    // The destination in force while a scope runs; the enclosing one returns
    // on exit, an error's throw included.
    struct DestScope {
        TypeCheck &tc;
        Dest saved;
        DestScope(TypeCheck &t, Dest d) : tc(t), saved(t.curdst) { tc.curdst = d; }
        ~DestScope() { tc.curdst = saved; }
    };
    VarDef *temproot = nullptr;  // Sentinel root for refs read out of temporaries.
    VarDef *cycleroot = nullptr; // Sentinel root for a back edge's result whose root
                                 // the cycle's returns do not determine (§7.8).
    TypeExpr *fntype = nullptr;  // Shared type of function values.
    TypeExpr *u8slice = nullptr; // The natural type of a string literal.
    TypeExpr *nulltype = nullptr;  // Placeholder type of a bare null literal.

    // ------------------------------------------------------------------
    // Errors, with the compile-time instantiation chain (§7.7).

    string Where(Line l) {
        if (l.fileidx < 0 || l.fileidx >= (int)ast.sources.size()) return "?";
        return cat(ast.sources[l.fileidx].first, ":", l.line);
    }

    [[noreturn]] void Error(Line l, const string &msg) {
        auto s = cat(Where(l), ": error: ", msg);
        // Show the offending source line with a caret-less underline context.
        if (l.fileidx >= 0 && l.fileidx < (int)ast.sources.size() && l.line > 0) {
            auto &src = *ast.sources[l.fileidx].second;
            auto p = src.c_str();
            for (auto ln = 1; *p && ln < l.line; p++) if (*p == '\n') ln++;
            auto end = p;
            while (*end && *end != '\n' && *end != '\r') end++;
            Append(s, "\n", string_view(p, (size_t)(end - p)));
        }
        for (auto i = (int)frames.size() - 1; i > 0; i--) {
            auto &f = frames[i];
            if (!f.sf || f.isfunval) continue;
            Append(s, "\n  in ", f.sf->isthread ? "thread_fn " : "fn ", f.sf->name, "(");
            if (f.spec) {
                for (size_t j = 0; j < f.spec->argtypes.size(); j++) {
                    if (j) s += ", ";
                    f.spec->argtypes[j]->Dump(s);
                }
            }
            Append(s, ") instantiated from ", Where(f.callline));
        }
        throw CompileError { s };
    }

    [[noreturn]] void Error(const Node *n, const string &msg) { Error(n->line, msg); }

    void Warn(const Node *n, const string &msg) {
        fprintf(stderr, "%s: warning: %s\n", Where(n->line).c_str(), msg.c_str());
    }

    string TypeStr(const TypeExpr *t) {
        string s;
        t->Dump(s);
        return s;
    }

    // ------------------------------------------------------------------
    // Constant expression evaluation: array sizes, match arm bounds, literal
    // fit. Understands literals, arithmetic, and `let` globals.

    bool ConstInt(Node *n, int64_t &v) {
        if (auto i = Is<IntLit>(n)) { v = i->val; return true; }
        if (auto u = Is<Unary>(n)) {
            int64_t c;
            if (!ConstInt(u->child, c)) return false;
            switch (u->op) {
                case T_MINUS:  v = -c; return true;
                case T_BITNOT: v = ~c; return true;
                default: return false;
            }
        }
        if (auto b = Is<Binary>(n)) {
            int64_t l, r;
            if (!ConstInt(b->left, l) || !ConstInt(b->right, r)) return false;
            switch (b->op) {
                case T_PLUS:   v = l + r; return true;
                case T_MINUS:  v = l - r; return true;
                case T_MUL:    v = l * r; return true;
                case T_DIV:    if (!r) Error(n, "constant division by zero"); v = l / r; return true;
                case T_MOD:    if (!r) Error(n, "constant division by zero");
                               v = EuclidMod(l, r); return true;
                case T_BITAND: v = l & r; return true;
                case T_BITOR:  v = l | r; return true;
                case T_XOR:    v = l ^ r; return true;
                case T_SHL:    v = l << (r & 63); return true;
                case T_SHR:    v = l >> (r & 63); return true;
                default: return false;
            }
        }
        if (auto id = Is<Ident>(n)) {
            // A `let` global with a constant initializer is a named constant.
            auto git = ast.globalmap.find(id->name);
            if (git == ast.globalmap.end()) return false;
            auto vd = git->second;
            if (vd->isvar || vd->inits.size() != 1) return false;
            return ConstInt(vd->inits[0], v);
        }
        return false;
    }

    int64_t ConstIntOrError(Node *n, const char *context) {
        int64_t v;
        if (!ConstInt(n, v)) Error(n, cat("constant integer expression expected for ", context));
        return v;
    }

    // Evaluated A_FIXED size / A_LIMITED capacity, cached in the shared detail.
    int64_t ArraySize(TypeArray *a) {
        if (a->size < 0 && a->sizeexpr) {
            auto v = ConstIntOrError(a->sizeexpr, "array size");
            if (v < 0) Error(a->sizeexpr, "array size cannot be negative");
            a->size = v;
        }
        return a->size;
    }

    // ------------------------------------------------------------------
    // Type equality on concrete (post-substitution) types.

    bool TypeEq(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (a->kind != b->kind) return false;
        switch (a->kind) {
            case TY_INT:  return a->intstorage == b->intstorage;
            case TY_FLT:  return a->fltstorage == b->fltstorage;
            case TY_BOOL: case TY_VOID: case TY_FN: return true;
            case TY_STRUCT: {
                if (a->struc->st != b->struc->st) return false;
                return TypeArgsEq(a->struc->args, b->struc->args);
            }
            case TY_ENUM: {
                if (a->enu->en != b->enu->en || a->enu->varmode != b->enu->varmode) return false;
                return TypeArgsEq(a->enu->args, b->enu->args);
            }
            case TY_ARRAY: {
                auto &x = *a->arr, &y = *b->arr;
                if (x.akind != y.akind || !TypeEq(x.sub, y.sub)) return false;
                switch (x.akind) {
                    case A_FIXED:   return ArraySize(a->arr) == ArraySize(b->arr);
                    case A_VAR: {
                        auto ls = [](int s) { return s < 0 ? IS_U32 : (IntStorage)s; };
                        return ls(x.lenstorage) == ls(y.lenstorage);
                    }
                    case A_LIMITED: {
                        auto xs = x.sizeexpr ? ArraySize(a->arr) : -1;
                        auto ys = y.sizeexpr ? ArraySize(b->arr) : -1;
                        return xs == ys;
                    }
                    default: return true;
                }
            }
            case TY_SLICE: return TypeEq(a->sub, b->sub);
            case TY_REF:
                // The pool is part of a relative reference's identity: offsets
                // measured from different bases are different encodings (§3.9).
                return TypeEq(a->ref->sub, b->ref->sub) && a->ref->optional == b->ref->optional &&
                       a->ref->lenstorage == b->ref->lenstorage && a->ref->pool == b->ref->pool;
            case TY_VARIANT:
                return a->var->variant == b->var->variant && TypeEq(a->var->adt, b->var->adt);
            case TY_GENERIC: return a->named->name == b->named->name;
            default: assert(false); return false;
        }
    }

    bool TypeArgsEq(vector<TypeExpr *> &a, vector<TypeExpr *> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) if (!TypeEq(a[i], b[i])) return false;
        return true;
    }

    // ------------------------------------------------------------------
    // Generic substitution. Bindings are searched lexically: the current
    // frame's lexical spec, then its lexical parents (nested fns see the
    // enclosing function's generics).

    TypeExpr *LookupBinding(string_view name) {
        for (auto sp = frames.back().lexspec; sp; sp = sp->lexparent)
            for (auto &[n, t] : sp->bindings) if (n == name) return t;
        return nullptr;
    }

    const FnValBind *LookupFnVal(string_view name) {
        for (auto sp = frames.back().lexspec; sp; sp = sp->lexparent)
            for (auto &[n, fv] : sp->fnvals) if (n == name) return &fv;
        return nullptr;
    }

    // Substitutes generic parameter names in t using the current lexical
    // bindings; returns t itself when nothing changed.
    TypeExpr *Subst(TypeExpr *t) {
        switch (t->kind) {
            case TY_GENERIC: {
                auto b = LookupBindingOuter(t->named->name);
                if (!b) return t;  // Unknown name; ValidateType reports it.
                if (!t->named->args.empty())
                    Error(t->line, cat("generic parameter ", t->named->name,
                                       " takes no type arguments"));
                if (t->named->varmode) {
                    if (b->kind != TY_ENUM)
                        Error(t->line, cat("variable mode (..) requires an ADT type, not ",
                                           TypeStr(b)));
                    if (b->enu->varmode) return b;
                    auto n = ast.NewType(TY_ENUM, t->line);
                    n->enu = ast.NewDetail<TypeEnum>();
                    n->enu->en = b->enu->en;
                    n->enu->args = b->enu->args;
                    n->enu->varmode = true;
                    return n;
                }
                return b;
            }
            case TY_STRUCT: {
                auto args = SubstArgs(t->struc->args);
                if (!args) return t;
                auto n = ast.NewType(TY_STRUCT, t->line);
                n->struc = ast.NewDetail<TypeStruct>();
                n->struc->st = t->struc->st;
                n->struc->args = std::move(*args);
                return n;
            }
            case TY_ENUM: {
                auto args = SubstArgs(t->enu->args);
                if (!args) return t;
                auto n = ast.NewType(TY_ENUM, t->line);
                n->enu = ast.NewDetail<TypeEnum>();
                n->enu->en = t->enu->en;
                n->enu->args = std::move(*args);
                n->enu->varmode = t->enu->varmode;
                return n;
            }
            case TY_ARRAY: {
                auto sub = Subst(t->arr->sub);
                if (sub == t->arr->sub) return t;
                auto n = ast.NewType(TY_ARRAY, t->line);
                n->arr = ast.NewDetail<TypeArray>();
                *n->arr = *t->arr;
                n->arr->sub = sub;
                return n;
            }
            case TY_SLICE: {
                auto sub = Subst(t->sub);
                if (sub == t->sub) return t;
                auto n = ast.NewType(TY_SLICE, t->line);
                n->sub = sub;
                return n;
            }
            case TY_REF: {
                auto sub = Subst(t->ref->sub);
                if (sub == t->ref->sub) return t;
                auto n = ast.NewType(TY_REF, t->line);
                n->ref = ast.NewDetail<TypeRef>();
                *n->ref = *t->ref;
                n->ref->sub = sub;
                return n;
            }
            case TY_VARIANT: {
                auto adt = Subst(t->var->adt);
                if (adt == t->var->adt) return t;
                if (adt->kind != TY_ENUM)
                    Error(t->line, cat("variant type of non-ADT type ", TypeStr(adt)));
                auto n = ast.NewType(TY_VARIANT, t->line);
                n->var = ast.NewDetail<TypeVariant>();
                n->var->adt = adt;
                // The template kept the name form when the ADT was generic.
                auto name = t->var->adt->kind == TY_ENUM ? t->var->variant->name : t->var->name;
                SVariant *found = nullptr;
                for (auto &v : adt->enu->en->variants) if (v.name == name) { found = &v; break; }
                if (!found)
                    Error(t->line, cat("enum ", adt->enu->en->name, " has no variant named ", name));
                n->var->variant = found;
                return n;
            }
            default: return t;
        }
    }

    // Returns nullopt-style: null when unchanged.
    unique_ptr<vector<TypeExpr *>> SubstArgs(vector<TypeExpr *> &args) {
        auto changed = false;
        vector<TypeExpr *> out;
        out.reserve(args.size());
        for (auto a : args) {
            auto s = Subst(a);
            changed |= s != a;
            out.push_back(s);
        }
        if (!changed) return nullptr;
        return make_unique<vector<TypeExpr *>>(std::move(out));
    }

    // ------------------------------------------------------------------
    // Struct/enum instantiation, size classes (§1.1), and placement rules
    // (§3.4). Field types are substituted with the instance's own bindings
    // only (bindonly), so a stray name in a declaration errors cleanly.

    vector<pair<string_view, TypeExpr *>> *extrabindings = nullptr;
    bool bindonly = false;
    // While unifying a call's parameter types, the callee's own generics must
    // stay unbound even when an enclosing function uses the same name (the
    // recursive-generic case).
    const vector<GenericParam> *ownexclude = nullptr;

    TypeExpr *LookupBindingOuter(string_view name);
    void BindGenerics(vector<GenericParam> &generics, vector<TypeExpr *> &args, string_view what,
                      string_view name, Line l, vector<pair<string_view, TypeExpr *>> &out);

    // Runs f with only the given bindings visible to Subst.
    template<typename F> void WithBindings(vector<pair<string_view, TypeExpr *>> &b, F f) {
        auto saveb = extrabindings;
        auto saveo = bindonly;
        extrabindings = &b;
        bindonly = true;
        f();
        extrabindings = saveb;
        bindonly = saveo;
    }

    StructInst *GetStructInst(TypeExpr *t);
    EnumInst *GetEnumInst(TypeExpr *t);
    void CheckFieldDefaults(vector<Field> &fields, vector<TypeExpr *> &ftypes, vector<Node *> &out,
                            vector<pair<string_view, TypeExpr *>> &bindings);
    SizeClass ClassOf(TypeExpr *t);
    bool IsFlat(TypeExpr *t);
    int VariantIndex(SEnum *en, SVariant *v);
    bool HasDefault(TypeExpr *t, string &why);

    // Is this a type an uninitialized `var x: T;` may have: fixed-size, so a
    // later whole-value assignment fully constructs it.
    bool UninitOK(TypeExpr *t) { return ClassOf(t) == SC_FIXED; }

    // ------------------------------------------------------------------
    // `var out = [];` (§4.2): a grow-only array whose element type is still
    // to be learned. The placeholder element is a private void type, so the
    // pending array is recognizable by kind alone; the first push, append or
    // assignment into the variable overwrites it in place, which completes the
    // type everywhere it was already recorded (the VarDef, every Ident
    // checked so far, the literal itself), since all of them share the one
    // TypeExpr object.

    TypeExpr *PendingArray(Line l);
    bool IsPendingArray(TypeExpr *t);
    TypeExpr *PendingElemFrom(const Val &av, Node *at);
    void CompletePending(TypeExpr *arrt, TypeExpr *elem, Line l);
    void RequireComplete(TypeExpr *t, Line l);
    void ValidateType(TypeExpr *t, Line l, int pos);

    // ------------------------------------------------------------------
    // Scopes, variables, and flow state (definite assignment + optional
    // narrowing, merged at control-flow joins).

    // Marks the statements of a construct that is producing a value for an
    // enclosing expression (see `invalue`); nests, and restores itself on the
    // throw an error does.
    struct ValueRegion {
        TypeCheck &tc;
        bool saved;
        ValueRegion(TypeCheck &t, bool wantvalue) : tc(t), saved(t.invalue) {
            tc.invalue = tc.invalue || wantvalue;
        }
        ~ValueRegion() { tc.invalue = saved; }
    };

    void PushScope(int kind, Node *node = nullptr);
    void PopScope();

    int CurDepth() { return (int)scopes.size(); }
    static int Depth(VarDef *v) { return v ? v->depth : 0; }
    static VarDef *CanonRoot(VarDef *v);

    // The root of the reference a variable holds; a null-initialized optional
    // has no commitment yet and reads as the temp sentinel, which no store
    // outlives (conservative).
    VarDef *RefRootOf(VarDef *vd) { return vd->refrootknown ? vd->ref.root : temproot; }

    bool ContainsGrowShrink(TypeExpr *t);
    bool IsGrowShrinkRoot(VarDef *r);
    bool RefExactOf(VarDef *vd);
    void BindProv(VarDef *vd, const Prov &p);
    void BindRefProvenance(VarDef *vd, const Val &v);
    Prov RefProvOf(VarDef *vd);
    VarDef *NewVar(string_view name, TypeExpr *type, Line l, bool isvar);
    VarDef *LookupVar(string_view name);
    int FrameOfSpec(FnSpec *sp);
    SFunction *LookupLocalFn(string_view name);

    // Snapshot of assigned/narrowed for every variable currently in scope.
    struct FlowState {
        vector<pair<bool, TypeExpr *>> st;
        bool reachable = true;
    };

    FlowState SaveFlow();
    void RestoreFlow(const FlowState &f);
    void MergeFlow(const FlowState &a, const FlowState &b);
    void NarrowCond(Node *cond, bool sense);

    void KillNarrow(VarDef *vd) { vd->narrowed = nullptr; }

    void CollectAssignedNames(Node *n, set<string_view> &out);
    void KillNarrowingsAssignedIn(Node *body);

    // ------------------------------------------------------------------
    // Small type constructors and views.

    TypeExpr *RefTo(TypeExpr *t, Line l);
    TypeExpr *SliceOf(TypeExpr *t, Line l);

    bool IsIntT(TypeExpr *t) { return t->kind == TY_INT && t->intstorage != IS_VARINT; }
    bool IsFltT(TypeExpr *t) { return t->kind == TY_FLT; }
    bool IsF32(TypeExpr *t) { return t->kind == TY_FLT && t->fltstorage == FS_F32; }
    bool IsOptional(TypeExpr *t) { return t->kind == TY_REF && t->ref->optional; }
    bool IsPlainRef(TypeExpr *t);
    bool IsArrayKind(TypeExpr *t, ArrayKind k);
    bool IsU8(TypeExpr *t) { return t->kind == TY_INT && t->intstorage == IS_U8; }

    TypeExpr *LoadType(TypeExpr *t);
    static bool ImplicitInt(IntStorage from, IntStorage to);
    TypeExpr *DerefType(TypeExpr *t);
    Val CheckIntAny(Node *n);
    bool HasRelRefT(TypeExpr *t);
    void NoRelRefCopy(Node *n, TypeExpr *t);

    // ------------------------------------------------------------------
    // Pool-relative references (§3.9). `T&<u32 in pool>` names a global pool
    // at the declaration, so nothing has to be discovered per call site: the
    // base is that global's, everywhere.

    void ResolvePools();

    // The globals some relative reference type measures offsets from. Empty
    // for a program without the feature, which is what keeps `RootArg::pool`
    // from splitting any specialization such a program would not have split.
    set<VarDef *> poolglobals;

    VarDef *PoolOf(VarDef *r);
    void ValidatePool(TypeExpr *t);

    // ------------------------------------------------------------------
    // Read-back roots (§9.5): what a reference or slice loaded out of a
    // container points into.
    //
    // The container names a scope the pointee outlives, not the storage that
    // owns it, so the checker re-derives the owner from the one thing it does
    // know about that scope: which variables in it can hold the pointee type
    // by value. A variable that only holds *references* to it cannot be its
    // owner. Where exactly one such candidate exists the read-back is that
    // variable, and rules that need identity (a relative-reference store,
    // §3.9) may use it; otherwise the candidates only bound the lifetime.

    bool CanContain(TypeExpr *t, TypeExpr *of);
    TypeExpr *PointeeOf(TypeExpr *t);
    void VisibleVars(const function<void(VarDef *)> &f);
    bool StaticCanContain(TypeExpr *of);
    void RootCandidates(TypeExpr *of, int d, bool globalsonly, vector<VarDef *> &out,
                        bool &hasstatic);

    // What the read-back rule makes of one load.
    struct ReadBack {
        VarDef *root = nullptr;
        bool exact = false;
        VarDef *from = nullptr;   // The container, where candidates were enumerated.
    };

    ReadBack ReadBackRoot(TypeExpr *rt, VarDef *croot, bool cexact);
    string ReadBackWhy(TypeExpr *rt, VarDef *from);

    // ------------------------------------------------------------------
    // Lvalue paths: names, fields, elements, optionally through references.

    LVal CheckLValue(Node *n);
    LVal LValueBase(Node *n);
    void DerefLValue(LVal &lv, Node *at);
    void RequireAssigned(VarDef *vd, Node *at);
    void SliceProvenance(LVal &lv, Node *at);
    void ReadBackLVal(LVal &lv);
    Val ContainerRead(LVal lv);
    void ResolveMemberLValue(LVal &lv, Dot *d);

    // ------------------------------------------------------------------
    // Values: the per-node dispatch plus the implicit-conversion rules.

    // The raw per-node check: virtual dispatch; the value may still denote a
    // reference. Consumers go through CheckValue/CheckArg/Operand, which
    // apply reference transparency.
    Val CheckV(Node *n, TypeExpr *expected) { return n->Check(*this, expected); }

    Val DecayRef(Val v);
    bool KeepsRef(Val &v, TypeExpr *dt);
    Node *AutoRef(Node *n, Val &v);
    bool BindsRef(const Val &v, TypeExpr *dt);
    bool IsNonFixedLValue(const Val &v);
    bool Referenceable(Node *n, const Val &v);
    bool UserRefOf(Node *n);
    void RequireCopyable(const Val &v, Node *n, TypeExpr *dt);
    void WriteBackArgs(Call *c, Dot *d, vector<Node *> &argnodes);
    void UnwrapCopy(Node *&n);
    Val CheckValue(Node *&n, TypeExpr *expected, bool callsite = false);

    Val CheckArg(Node *&n, TypeExpr *expected) { return CheckValue(n, expected, true); }

    Val CheckValueAt(Node *&n, TypeExpr *expected, Dest d, bool callsite = false);
    Val Operand(Node *n);

    string fitfail;  // A specific reason from the last failing FitsAt, if any.

    void MustFit(Val &v, Node *n, TypeExpr *dt, bool callsite);
    bool FitsAt(Val &v, TypeExpr *dt, bool callsite);
    static bool FitsIntStorage(int64_t v, bool uns, IntStorage s);
    static string ConstStr(const Val &v);
    Val CheckCond(Node *n);
    TypeExpr *UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue);
    Val VoidVal();
    TypeExpr *FixedArrayOf(TypeExpr *elem, int64_t count, Line l);
    Val CheckRefOf(Unary *x);
    static bool AddOv(int64_t a, int64_t b, int64_t &r);
    static bool SubOv(int64_t a, int64_t b, int64_t &r);
    static bool MulOv(int64_t a, int64_t b, int64_t &r);
    static int64_t EuclidMod(int64_t a, int64_t b);
    void FoldInt(TType op, Val &l, Val &r, Val &out, Node *at);
    TypeExpr *UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt, TypeExpr *rt,
                           bool cmp = false);
    void RetypeOperands(Node *left, Node *right, Val &lv, Val &rv, TypeExpr *ct);
    bool ElementwiseOK(TypeExpr *t);
    Val CheckVariantConst(Dot *d, SEnum *en);
    Val MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at, bool wantvalue);
    Val CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue);
    Val CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue, int scopekind,
                      Node *scopenode = nullptr);
    Val CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue);
    TypeExpr *VariantTypeOf(TypeExpr *enumtype, SVariant *v, Line l);
    Val CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue);
    Val CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue);
    void CheckWhile(While *x);
    void CheckFor(ForLoop *x);
    void CheckGuard(Guard *g);
    void ImplicitEmptyReturn(Node *at);
    Frame &CurRealFrame();
    int FindBreakScope(bool forcontinue);
    void CheckBreak(Break *b);
    void CheckContinue(Node *n);

    // ------------------------------------------------------------------
    // Calls: builtin members, builtins, UFCS, overload resolution with
    // generic inference (§7.1, §7.7), and case-function tag dispatch (§8.2).

    // Return values of the most recent call, for `let a, b = f();`.
    vector<Val> lastcallrets;

    struct MatchInfo {
        SFunction *sf = nullptr;
        int tier = 0;  // 0 = exact, 1 = generic binding, 2 = coercions.
        vector<pair<string_view, TypeExpr *>> bindings;
        vector<pair<string_view, FnValBind>> fnvals;
        vector<TypeExpr *> paramtypes;  // Concrete, one per declared parameter.
        FnSpec *env = nullptr;          // Lexical parent for nested functions.
    };

    Val CheckCall(Call *c);
    Val CheckNamedCall(Call *c, Ident *id);
    SFunction *LookupLocalFnEnv(string_view name, FnSpec *&env);
    Val CheckUfcsCall(Call *c, Dot *d);
    Val ResolveCall(Call *c, vector<SFunction *> &cands, FnSpec *env, string_view name, Val *preval,
                    Node *&prenode, bool *nomatch = nullptr);
    bool TryMatch(SFunction *sf, Call *c, vector<Val> &argvals, MatchInfo &mi, string &why);
    TypeExpr *NaturalType(const Val &av);
    TypeExpr *UnifyArg(TypeExpr *pt, Val &av, vector<pair<string_view, TypeExpr *>> &b, int &tier);
    TypeExpr *UnifyArgRaw(TypeExpr *pt, Val &av, vector<pair<string_view, TypeExpr *>> &b,
                          int &tier);
    bool HasGenerics(TypeExpr *t);
    bool BindTypes(TypeExpr *pt, TypeExpr *at, vector<pair<string_view, TypeExpr *>> &b);
    TypeExpr *SubstOwn(TypeExpr *pt, vector<pair<string_view, TypeExpr *>> &b);
    Val TryDispatch(Call *c, vector<SFunction *> &cands, vector<Node *> &argnodes,
                    vector<Val> &argvals, string_view name);

    // ------------------------------------------------------------------
    // Specialization: find or create the FnSpec for a resolved call and
    // check its body (once) in call-graph order.

    FnSpec *GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode);
    void ValidateCycle(FnSpec *spec, Node *callnode);
    static VarDef *UltimateRoot(VarDef *v);
    void ValidatePoolArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode);
    void ValidateNeeds(FnSpec *spec, Node *callnode);
    CycleRoots Cycles();
    bool ExternValueOk(TypeExpr *t, string &why);
    bool ExternParamOk(TypeExpr *t, string &why);
    void CheckExternSpec(FnSpec *spec);
    void CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline);
    void RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at);
    Val CallResult(Call *c, FnSpec *spec, vector<Val> &argvals);
    void CheckReturn(Return *r);

    // ------------------------------------------------------------------
    // Struct and variant literals (§4.2). The per-node entry is
    // StructLit::Check at the end of this file.

    void CheckInits(StructLit *sl, vector<Field> &fields, vector<TypeExpr *> &ftypes,
                    string_view what, TypeExpr *selft);
    void CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft);

    // ------------------------------------------------------------------
    // Statements.

    void CheckStmt(Node *n);
    void CheckStmtExpr(Node *n);
    void CheckVarDecl(VarDecl *vd, bool global);
    void NoteNonfixedLocal(TypeExpr *t, Line l, bool global);
    void AssignableClassCheck(TypeExpr *t, Node *at);
    void CheckAssign(Assign *a);
    void CheckRebind(Assign *a, LVal &lv);
    bool PointeeWritable(LVal &lv, Node *at);
    void PointeeAssign(Assign *a, LVal &lv);
    void CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv);
    void CompoundAssign(Assign *a, TypeExpr *st, bool writable);
    void CheckIncDec(IncDec *x);

    // ------------------------------------------------------------------
    // Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

    Val CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv);
    void CheckPrintable(Call *c, const char *what, Node *a);
    void CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at,
                         vector<TypeExpr *> &seen);
    FnSpec *UserFormat(Call *c, TypeExpr *t);
    void CheckGrowShrink(Node *at, bool standalone, const char *op, Node *recv, TypeExpr *rtype);
    void GrowOnlyShrinkAt(Node *c, bool standalone, const char *op, VarDef *vd);

    string ExprStr(Node *n) {
        string s;
        n->Dump(s, 0);
        return s;
    }

    void CheckShrinkHolders(Node *at, const string &op, VarDef *root, const string &what);
    void NoteShrink(VarDef *root);
    void ShrinkGrowShrink(Node *at, const string &op, VarDef *root, const string &what);
    void ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name);
    void ElemArg(Node *&n, TypeExpr *elem, Val &rv);
    FnSpec *EnsureThreadSpec(SFunction *sf, Line l);

    // ------------------------------------------------------------------
    // Calling a function value F(a): the body is cloned and checked inline
    // in the lexical environment it was written in (§7.6).

    Val CheckFunValCall(Call *c, const FnValBind &fb);
    TypeExpr *SubstEnv(TypeExpr *t, FnSpec *env);

    // ------------------------------------------------------------------
    // The driver: globals in order, then main, then thread entry points.

    TypeCheck(Ast &_ast) : ast(_ast) {
        temproot = ast.NewVarDef();
        temproot->name = "<temporary>";
        temproot->depth = INT32_MAX;
        cycleroot = ast.NewVarDef();
        cycleroot->name = "<recursive result>";
        cycleroot->depth = INT32_MAX;
        fntype = ast.NewType(TY_FN, Line {});
        fntype->fn = ast.NewDetail<TypeFn>();
        u8slice = SliceOf(ast.inttypes[IS_U8], Line {});
        nulltype = ast.NewType(TY_REF, Line {});
        nulltype->ref = ast.NewDetail<TypeRef>();
        nulltype->ref->sub = ast.voidtype;
        nulltype->ref->optional = true;
        Frame f;
        frames.push_back(f);
        // Global VarDefs exist up front so names resolve in any order; reads
        // before their initializer ran are caught by the assigned flag.
        for (auto g : ast.globals) {
            for (auto name : g->names) {
                auto vd = ast.NewVarDef();
                vd->name = name;
                vd->line = g->line;
                vd->isvar = g->isvar;
                vd->isglobal = true;
                vd->reusable = g->reusable;
                g->defs.push_back(vd);
            }
        }
        ResolvePools();
        // Validate all non-generic type declarations up front: clearer errors
        // than at first use, and unused decls get checked too.
        for (auto st : ast.structs) {
            if (!st->generics.empty()) continue;
            auto t = ast.NewType(TY_STRUCT, st->line);
            t->struc = ast.NewDetail<TypeStruct>();
            t->struc->st = st;
            GetStructInst(t);
        }
        for (auto en : ast.enums) {
            if (!en->generics.empty()) continue;
            auto t = ast.NewType(TY_ENUM, en->line);
            t->enu = ast.NewDetail<TypeEnum>();
            t->enu->en = en;
            t->enu->varmode = true;
            GetEnumInst(t);
        }
        for (auto g : ast.globals) {
            CheckVarDecl(g, true);
            for (auto d : g->defs) d->assigned = true;
        }
        // Concrete ones only; a pointee still spelled with a type parameter
        // gets here again once a specialization substitutes it.
        for (auto t : ast.alltypes)
            if (t->kind == TY_REF && t->ref->pool && !HasGenerics(t->ref->sub)) ValidatePool(t);
        auto mit = ast.functionmap.find("main");
        if (mit == ast.functionmap.end() || mit->second.size() != 1)
            throw CompileError { "program needs exactly one fn main()" };
        auto mainsf = mit->second[0];
        if (!mainsf->params.empty() || mainsf->has_rets || !mainsf->generics.empty() ||
            mainsf->isthread)
            Error(mainsf->line, "fn main() takes no parameters and returns nothing");
        auto mainspec = ast.NewFnSpec();
        mainspec->sf = mainsf;
        mainsf->specs.push_back(mainspec);
        CheckSpecBody(mainspec, nullptr, mainsf->line);
        // Thread entry points compile as separate programs (§11.2); check any
        // that no spawn reached.
        for (auto sf : ast.functions)
            if (sf->isthread) EnsureThreadSpec(sf, sf->line);
        // Thread programs may not touch globals: that would be shared mutable
        // memory between programs (§11.2).
        for (auto sf : ast.functions)
            if (sf->isthread && !sf->specs.empty()) CheckThreadGlobals(sf->specs[0]);
        // NOTE: in this compilation model, code no call reaches would never be
        // typechecked at all. That is right for generic functions (they need a
        // caller's types), but silently skipping plain dead code makes for a
        // confusing experience, so leftover non-generic functions are checked
        // standalone here with permissive assumptions (every reference
        // parameter distinct-rooted, writable, reusable where it could be).
        // Errors these produce are real; a lack of errors is weaker than for
        // reached code, since no call-site facts were available.
        for (auto sf : ast.functions) CheckUnreached(sf);
        SettleParamRootExactness();
    }

    // Checking is over, so every call site of every specialization has been
    // seen. A root class named one array inside its body whatever the call
    // site (GetOrCreateSpec keeps an inexactly rooted argument out of every
    // other argument's class), but *which* array only a call site knows, and
    // the later passes ask that question instead: whether two classes are two
    // arrays. Record the answer where they read it.
    void SettleParamRootExactness() {
        for (auto spec : ast.fnspecs) {
            auto ri = 0;
            for (size_t i = 0; i < spec->params.size() && i < spec->argtypes.size(); i++) {
                auto t = spec->argtypes[i];
                if (t->kind != TY_REF && t->kind != TY_SLICE) continue;
                if (ri < (int)spec->roots.size() && !spec->roots[ri].exact)
                    spec->params[i]->ref.rootexact = false;
                ri++;
            }
        }
    }

    void CheckUnreached(SFunction *sf) {
        if (!sf->specs.empty() || sf->isthread || sf->isnested) return;
        if (!sf->generics.empty()) return;
        for (auto &p : sf->params) if (!p.type) return;
        set<string_view> names = { sf->name };
        auto foreign = false;
        if (sf->body) ScanForeignFrom(sf->body, names, foreign);
        if (foreign) return;
        auto spec = ast.NewFnSpec();
        spec->sf = sf;
        for (auto &p : sf->params) {
            auto t = Subst(p.type);
            ValidateType(t, sf->line, VT_PARAM);
            spec->argtypes.push_back(t);
            if (t->kind == TY_REF || t->kind == TY_SLICE) {
                RootArg ra;
                ra.cls = 0;
                ra.writable = true;
                ra.reusable = t->kind == TY_REF && IsArrayKind(t->ref->sub, A_GROW) &&
                              ClassOf(t->ref->sub->arr->sub) == SC_FIXED;
                spec->roots.push_back(ra);
            }
        }
        sf->specs.push_back(spec);
        CheckSpecBody(spec, nullptr, sf->line);
    }

    // Walks a thread program's call graph rejecting global accesses (§11.2).
    void CheckThreadGlobals(FnSpec *entry) {
        set<FnSpec *> seen;
        function<void(FnSpec *)> rec = [&](FnSpec *sp) {
            if (!sp || !sp->body || !seen.insert(sp).second) return;
            function<void(Node *)> walk = [&](Node *n) {
                if (!n) return;
                if (auto id = Is<Ident>(n)) {
                    if (id->vdef && id->vdef->isglobal)
                        Error(n, cat("thread programs may not access globals (§11.2): ",
                                     id->name, " (reached from thread_fn ",
                                     entry->sf->name, ")"));
                }
                if (auto c = Is<Call>(n)) {
                    rec(c->spec);
                    for (auto d : c->dispatch) rec(d);
                    walk(c->fvbody);
                }
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
        };
        rec(entry);
    }

    // Does the body contain `return ... from f` for an f not declared within?
    void ScanForeignFrom(Node *n, set<string_view> &names, bool &foreign) {
        if (!n || foreign) return;
        if (auto r = Is<Return>(n)) {
            if (!r->from.empty() && !names.count(r->from)) foreign = true;
        } else if (auto fd = Is<FnDecl>(n)) {
            // Nested fns count as in-scope targets; their bodies are not
            // Children, so recurse explicitly.
            names.insert(fd->sf->name);
            if (fd->sf->body) ScanForeignFrom(fd->sf->body, names, foreign);
            return;
        }
        n->Children([&](Node *c) { ScanForeignFrom(c, names, foreign); });
    }
};

// Runs the whole pass; errors throw CompileError.
inline void TypeCheckProgram(Ast &ast) { TypeCheck tc(ast); }

}  // namespace goose
