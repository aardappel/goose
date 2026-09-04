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
#pragma once

namespace goose {

enum IterKind { IK_RANGE, IK_COUNT, IK_ARRAY, IK_SLICE };

// Type validation positions: what may be declared where.
enum ValidPos { VT_LOCAL, VT_GLOBAL, VT_PARAM, VT_RET, VT_FIELD, VT_ELEM, VT_POINTEE };

struct TypeCheck {
    Ast &ast;

    // (Val, the checked value of an expression, lives in ast.h: node Check
    // overrides return it.)

    // An assignable/addressable path: Ident, field, or element.
    struct LVal {
        TypeExpr *type = nullptr;    // The location's own type (varints undecoded).
        VarDef *var = nullptr;       // Set when the path is a bare variable name.
        VarDef *root = nullptr;      // Owner of the storage (null = static).
        bool rootexact = false;      // See Val::rootexact.
        VarDef *rootfrom = nullptr;  // See Val::rootfrom.
        bool fromstorage = false;    // Reached by a field or element step, so a
                                     // reference read out of it is a read-back (§9.5).
        bool writable = false;       // Whole path admits writes.
        bool reusable = false;
        bool sequential = false;     // Variable-size element: not addressable.
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
        if (auto b = Is<BoolLit>(n)) { (void)b; return false; }
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

    bool IsFnValName(string_view name) {
        for (auto sp = frames.back().lexspec; sp; sp = sp->lexparent)
            for (auto &[n, fv] : sp->fnvals) if (n == name) return true;
        return false;
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

    TypeExpr *LookupBindingOuter(string_view name) {
        if (extrabindings)
            for (auto &[n, t] : *extrabindings) if (n == name) return t;
        if (ownexclude)
            for (auto &g : *ownexclude) if (g.name == name) return nullptr;
        if (bindonly || frames.empty()) return nullptr;
        return LookupBinding(name);
    }

    void BindGenerics(vector<GenericParam> &generics, vector<TypeExpr *> &args,
                      string_view what, string_view name, Line l,
                      vector<pair<string_view, TypeExpr *>> &out) {
        if (args.size() != generics.size())
            Error(l, cat(what, " ", name, " takes ", (int64_t)generics.size(),
                         " type argument(s), ", (int64_t)args.size(), " given"));
        for (size_t i = 0; i < generics.size(); i++)
            out.push_back({ generics[i].name, args[i] });
    }

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

    StructInst *GetStructInst(TypeExpr *t) {
        auto st = t->struc->st;
        if (t->struc->inst) return t->struc->inst;
        for (auto inst : st->insts)
            if (TypeArgsEq(inst->args, t->struc->args)) return t->struc->inst = inst;
        auto inst = ast.NewStructInst();
        inst->st = st;
        inst->args = t->struc->args;
        st->insts.push_back(inst);
        t->struc->inst = inst;
        vector<pair<string_view, TypeExpr *>> bindings;
        BindGenerics(st->generics, inst->args, "struct", st->name, t->line, bindings);
        WithBindings(bindings, [&]() {
            for (auto &f : st->fields)
                inst->ftypes.push_back(f.ispad ? nullptr : Subst(f.type));
        });
        // Placement (§3.4): a resizable field only as the tail, making the
        // struct itself resizable; any variable part makes it variable.
        auto lastreal = -1;
        for (auto i = 0; i < (int)st->fields.size(); i++) if (!st->fields[i].ispad) lastreal = i;
        for (auto i = 0; i < (int)st->fields.size(); i++) {
            if (st->fields[i].ispad) continue;
            auto ft = inst->ftypes[i];
            ValidateType(ft, st->line, VT_FIELD);
            auto c = ClassOf(ft);
            if (c == SC_RESIZABLE) {
                if (i != lastreal)
                    Error(st->line, cat("resizable field ", st->fields[i].name, " of struct ",
                                        st->name, " must be the final field"));
                inst->sclass = SC_RESIZABLE;
            } else if (c == SC_VARIABLE && inst->sclass == SC_FIXED) {
                inst->sclass = SC_VARIABLE;
            }
            inst->flat = inst->flat && IsFlat(ft);
        }
        if (inst->sclass == SC_RESIZABLE) {
            auto fo = true;
            for (auto i = 0; i < (int)st->fields.size(); i++) {
                if (st->fields[i].ispad) continue;
                auto ft = inst->ftypes[i];
                if (i == lastreal)
                    fo &= ft->kind == TY_ARRAY ||
                          (ft->kind == TY_STRUCT && GetStructInst(ft)->frameobj);
                else
                    fo &= ClassOf(ft) == SC_FIXED && !HasRelRefT(ft);
            }
            inst->frameobj = fo;
        }
        inst->validated = true;
        CheckFieldDefaults(st->fields, inst->ftypes, inst->defaults, bindings);
        return inst;
    }

    EnumInst *GetEnumInst(TypeExpr *t) {
        auto en = t->enu->en;
        if (t->enu->inst) return t->enu->inst;
        for (auto inst : en->insts)
            if (TypeArgsEq(inst->args, t->enu->args)) return t->enu->inst = inst;
        auto inst = ast.NewEnumInst();
        inst->en = en;
        inst->args = t->enu->args;
        en->insts.push_back(inst);
        t->enu->inst = inst;
        vector<pair<string_view, TypeExpr *>> bindings;
        BindGenerics(en->generics, inst->args, "enum", en->name, t->line, bindings);
        WithBindings(bindings, [&]() {
            for (auto &v : en->variants) {
                inst->vftypes.emplace_back();
                for (auto &f : v.fields)
                    inst->vftypes.back().push_back(f.ispad ? nullptr : Subst(f.type));
            }
        });
        for (size_t vi = 0; vi < en->variants.size(); vi++) {
            auto &v = en->variants[vi];
            auto lastreal = -1;
            for (auto i = 0; i < (int)v.fields.size(); i++) if (!v.fields[i].ispad) lastreal = i;
            for (auto i = 0; i < (int)v.fields.size(); i++) {
                if (v.fields[i].ispad) continue;
                auto ft = inst->vftypes[vi][i];
                ValidateType(ft, en->line, VT_FIELD);
                auto c = ClassOf(ft);
                if (c == SC_RESIZABLE) {
                    if (i != lastreal)
                        Error(en->line, cat("resizable field ", v.fields[i].name, " of variant ",
                                            en->name, ".", v.name, " must be the final field"));
                    inst->varclass = SC_RESIZABLE;
                }
                if (c != SC_FIXED) inst->allfixed = false;
                inst->flat = inst->flat && IsFlat(ft);
            }
        }
        inst->validated = true;
        vector<pair<string_view, TypeExpr *>> b2 = bindings;
        for (size_t vi = 0; vi < en->variants.size(); vi++) {
            inst->vdefaults.emplace_back();
            CheckFieldDefaults(en->variants[vi].fields, inst->vftypes[vi],
                               inst->vdefaults.back(), b2);
        }
        return inst;
    }

    // Field defaults are checked once per instance, on clones, in a pristine
    // frame that sees only globals (plus the instance's generic bindings).
    void CheckFieldDefaults(vector<Field> &fields, vector<TypeExpr *> &ftypes,
                            vector<Node *> &out, vector<pair<string_view, TypeExpr *>> &bindings) {
        auto any = false;
        for (auto &f : fields) any |= f.defaultval != nullptr;
        if (!any) {
            out.resize(fields.size(), nullptr);
            return;
        }
        auto savereach = reachable;
        auto savedst = curdst;
        reachable = true;
        curdst = Dest {};
        auto sp = ast.NewFnSpec();  // Bindings holder for the pseudo frame.
        sp->bindings = bindings;
        Frame f;
        f.lexspec = sp;
        f.scopebase = (int)scopes.size();
        f.varbase = (int)vars.size();
        frames.push_back(f);
        for (size_t i = 0; i < fields.size(); i++) {
            if (!fields[i].defaultval) { out.push_back(nullptr); continue; }
            auto clone = fields[i].defaultval->Clone(ast);
            CheckValue(clone, ftypes[i]);
            out.push_back(clone);
        }
        frames.pop_back();
        reachable = savereach;
        curdst = savedst;
    }

    SizeClass ClassOf(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT:  return t->intstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
            case TY_REF:
                // A varint-width relative reference is varint-encoded storage,
                // so it makes its container variable-class like any varint (§3.6).
                return t->ref->lenstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
            case TY_FLT: case TY_BOOL: case TY_SLICE: return SC_FIXED;
            case TY_STRUCT: {
                auto inst = GetStructInst(t);
                // Still being validated = the struct (transitively) contains
                // itself by value; references to self are fine (fixed class).
                if (!inst->validated)
                    Error(t->line, cat("struct ", inst->st->name, " contains itself by value"));
                return inst->sclass;
            }
            case TY_ENUM: {
                if (!t->enu->varmode) return SC_FIXED;
                auto inst = GetEnumInst(t);
                if (!inst->validated)
                    Error(t->line, cat("enum ", inst->en->name, " contains itself by value"));
                return inst->varclass;
            }
            case TY_ARRAY:
                switch (t->arr->akind) {
                    case A_FIXED:   return SC_FIXED;
                    case A_VAR:     return SC_VARIABLE;
                    case A_LIMITED: return ArraySize(t->arr) >= 0 ? SC_FIXED : SC_VARIABLE;
                    default:        return SC_RESIZABLE;
                }
            case TY_VARIANT: {
                auto inst = GetEnumInst(t->var->adt);
                auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
                auto c = SC_FIXED;
                for (auto ft : inst->vftypes[vi])
                    if (ft) c = std::max(c, ClassOf(ft));
                return c;
            }
            default: return SC_FIXED;
        }
    }

    // Flat (§1.1): no references, slices, or relative references at any depth.
    bool IsFlat(TypeExpr *t) {
        switch (t->kind) {
            case TY_REF: case TY_SLICE: return false;
            case TY_STRUCT: return GetStructInst(t)->flat;
            case TY_ENUM:   return GetEnumInst(t)->flat;
            case TY_ARRAY:  return IsFlat(t->arr->sub);
            case TY_VARIANT: {
                auto inst = GetEnumInst(t->var->adt);
                auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
                for (auto ft : inst->vftypes[vi]) if (ft && !IsFlat(ft)) return false;
                return true;
            }
            default: return true;
        }
    }

    int VariantIndex(SEnum *en, SVariant *v) {
        for (size_t i = 0; i < en->variants.size(); i++)
            if (&en->variants[i] == v) return (int)i;
        assert(false);
        return 0;
    }

    // Does a fixed-size type have a default value (§4.2)? Everything does
    // except a non-optional reference, which has nothing to point at, and so
    // anything containing one without a declared field default.
    bool HasDefault(TypeExpr *t, string &why) {
        auto fields = [&](const vector<Field> &fs, const vector<TypeExpr *> &fts) {
            for (size_t i = 0; i < fs.size(); i++) {
                if (fs[i].ispad || fs[i].defaultval) continue;
                if (!HasDefault(fts[i], why)) {
                    why = cat("field ", fs[i].name, " has no declared default and ", why);
                    return false;
                }
            }
            return true;
        };
        switch (t->kind) {
            case TY_INT: case TY_FLT: case TY_BOOL: case TY_SLICE: return true;
            case TY_REF:
                if (t->ref->optional) return true;
                why = cat(TypeStr(t), " is a non-optional reference");
                return false;
            case TY_STRUCT: {
                auto inst = GetStructInst(t);
                return fields(inst->st->fields, inst->ftypes);
            }
            case TY_ENUM: {
                // Variant 0 is the default variant.
                auto inst = GetEnumInst(t);
                return fields(inst->en->variants[0].fields, inst->vftypes[0]);
            }
            case TY_VARIANT: {
                auto inst = GetEnumInst(t->var->adt);
                auto vi = VariantIndex(inst->en, t->var->variant);
                return fields(t->var->variant->fields, inst->vftypes[vi]);
            }
            case TY_ARRAY:
                if (t->arr->akind == A_LIMITED) return true;   // Empty.
                return HasDefault(t->arr->sub, why);
            default:
                why = cat(TypeStr(t), " has no default value");
                return false;
        }
    }

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

    TypeExpr *PendingArray(Line l) {
        auto t = ast.NewType(TY_ARRAY, l);
        t->arr = ast.NewDetail<TypeArray>();
        t->arr->sub = ast.NewType(TY_VOID, l);
        t->arr->akind = A_GROW;
        return t;
    }

    // Distinct from an empty array literal's `void[0]`, which is fixed-size.
    bool IsPendingArray(TypeExpr *t) {
        return t && t->kind == TY_ARRAY && t->arr->akind == A_GROW && t->arr->sub->kind == TY_VOID;
    }

    // The element type an argument value supplies to a pending array: a
    // string literal makes it an array of owned strings (u8[]), the natural
    // element to be pushing literals into; [] and null say nothing.
    TypeExpr *PendingElemFrom(const Val &av, Node *at) {
        if (av.emptyarr || av.isnull || av.type->kind == TY_VOID || av.type == fntype)
            Error(at, "cannot infer the element type of this array from this value");
        if (av.strlit) {
            auto t = ast.NewType(TY_ARRAY, at->line);
            t->arr = ast.NewDetail<TypeArray>();
            t->arr->sub = ast.inttypes[IS_U8];
            t->arr->akind = A_VAR;
            return t;
        }
        return av.type;
    }

    void CompletePending(TypeExpr *arrt, TypeExpr *elem, Line l) {
        arrt->arr->sub = elem;
        ValidateType(arrt, l, VT_LOCAL);
    }

    void RequireComplete(TypeExpr *t, Line l) {
        if (IsPendingArray(t))
            Error(l, "the element type of this array is not known yet (it was declared "
                     "with `= []`): push or append into it first, or annotate the "
                     "declaration");
    }

    void ValidateType(TypeExpr *t, Line l, int pos) {
        switch (t->kind) {
            case TY_GENERIC:
                Error(l, cat("unknown type: ", t->named->name));
            case TY_UNRESOLVED:
                assert(false);
                return;
            case TY_INT: {
                // varint is encoded storage: it exists only inside compound
                // types (fields, elements) and behind references (§3.6).
                auto compound = pos == VT_FIELD || pos == VT_ELEM || pos == VT_POINTEE;
                if (t->intstorage == IS_VARINT && !compound)
                    Error(l, "varint is a storage type: only fields and array elements");
                return;
            }
            case TY_FLT: return;
            case TY_BOOL: return;
            case TY_VOID:
                Error(l, "expression has no value here");
            case TY_FN:
                Error(l, "function value types are compile-time only and cannot be stored");
            case TY_STRUCT: GetStructInst(t); return;
            case TY_ENUM: {
                auto inst = GetEnumInst(t);
                if (!inst->validated && !t->enu->varmode)
                    Error(l, cat("enum ", t->enu->en->name, " contains itself by value"));
                if (inst->validated && !t->enu->varmode && !inst->allfixed)
                    Error(l, cat("enum ", t->enu->en->name, " has non-fixed-size payloads and "
                                 "can only be used in variable mode (",
                                 t->enu->en->name, "..)"));
                return;
            }
            case TY_VARIANT:
                if (t->var->adt->kind != TY_ENUM)
                    Error(l, cat("variant type of non-ADT type ", TypeStr(t->var->adt)));
                GetEnumInst(t->var->adt);
                return;
            case TY_ARRAY: {
                RequireComplete(t, l);
                ValidateType(t->arr->sub, l, VT_ELEM);
                auto ec = ClassOf(t->arr->sub);
                switch (t->arr->akind) {
                    case A_FIXED:
                        ArraySize(t->arr);
                        if (ec != SC_FIXED)
                            Error(l, cat("fixed array elements must be fixed-size: ",
                                         TypeStr(t->arr->sub)));
                        break;
                    case A_LIMITED:
                        if (t->arr->sizeexpr) ArraySize(t->arr);
                        if (ec != SC_FIXED)
                            Error(l, cat("limited array elements must be fixed-size: ",
                                         TypeStr(t->arr->sub)));
                        break;
                    case A_GROWSHRINK:
                        if (ec != SC_FIXED)
                            Error(l, cat("grow-shrink array elements must be fixed-size: ",
                                         TypeStr(t->arr->sub)));
                        break;
                    case A_VAR: case A_GROW:
                        if (ec == SC_RESIZABLE)
                            Error(l, cat("array elements may not be resizable: ",
                                         TypeStr(t->arr->sub)));
                        break;
                }
                return;
            }
            case TY_SLICE: ValidateType(t->sub, l, VT_ELEM); return;
            case TY_REF:
                // A pool's own type is only known once the globals are
                // checked; the driver revisits every concrete in-pool type
                // then, and this catches the ones substitution makes later.
                if (t->ref->pool && t->ref->pool->type && !HasGenerics(t->ref->sub))
                    ValidatePool(t);
                ValidateType(t->ref->sub, l, VT_POINTEE);
                return;
        }
    }

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

    void PushScope(int kind, Node *node = nullptr) {
        Scope s;
        s.kind = kind;
        s.varbase = (int)vars.size();
        s.fnbase = (int)localfns.size();
        s.node = node;
        scopes.push_back(s);
    }

    void PopScope() {
        auto &s = scopes.back();
        // A `var x = []` that nothing ever pushed into has no type to give
        // codegen; the scope ending is the last chance to say so.
        for (auto i = s.varbase; i < (int)vars.size(); i++)
            if (IsPendingArray(vars[i]->type))
                Error(vars[i]->line, cat("the element type of ", vars[i]->name,
                                         " was never determined: nothing was pushed or "
                                         "appended into it, and no array was assigned to it"));
        vars.resize(s.varbase);
        localfns.resize(s.fnbase);
        scopes.pop_back();
    }

    int CurDepth() { return (int)scopes.size(); }
    static int Depth(VarDef *v) { return v ? v->depth : 0; }
    static VarDef *CanonRoot(VarDef *v) {
        while (v && v->rootalias) v = v->rootalias;
        return v;
    }

    // The root of the reference a variable holds; a null-initialized optional
    // has no commitment yet and reads as the temp sentinel, which no store
    // outlives (conservative).
    VarDef *RefRootOf(VarDef *vd) { return vd->refrootknown ? vd->refroot : temproot; }

    // A grow-shrink array anywhere in a value of type t: the array itself, or
    // the tail of a struct (elements are never resizable, §3.3).
    bool ContainsGrowShrink(TypeExpr *t) {
        switch (t->kind) {
            case TY_ARRAY: return t->arr->akind == A_GROWSHRINK;
            case TY_STRUCT: {
                auto inst = GetStructInst(t);
                for (auto ft : inst->ftypes)
                    if (ft && ContainsGrowShrink(ft)) return true;
                return false;
            }
            default: return false;
        }
    }

    // Whether references rooted at r may point into a grow-shrink array
    // (§5.2): r holds one, or stands for a call-site root that does.
    bool IsGrowShrinkRoot(VarDef *r) {
        return r && (r->growshrink || (r->type && ContainsGrowShrink(r->type)));
    }

    // Reading a reference variable's root as an identity. A loop body is
    // checked once, so a read here sees the value a later rebind in the same
    // loop leaves behind; noting the read lets that rebind reject the root
    // change instead of silently invalidating this one.
    bool RefExactOf(VarDef *vd) {
        if (!vd->refrootknown || !vd->refrootexact) return false;
        for (auto i = (int)scopes.size() - 1; i >= vd->depth; i--)
            if (scopes[i].kind == SK_LOOP) { vd->refidentityused = true; break; }
        return true;
    }

    // First non-null binding of a reference variable fixes its provenance.
    void BindRefProvenance(VarDef *vd, const Val &v) {
        if (v.isnull) return;
        vd->refroot = CanonRoot(v.root);
        vd->refrootknown = true;
        vd->refrootexact = v.rootexact;
        vd->refrootfrom = v.rootfrom;
        vd->refwritable = v.writable;
        vd->refreusable = v.reusable;
    }

    VarDef *NewVar(string_view name, TypeExpr *type, Line l, bool isvar) {
        auto vd = ast.NewVarDef();
        vd->name = name;
        vd->type = type;
        vd->line = l;
        vd->isvar = isvar;
        vd->depth = CurDepth();
        vd->ownerspec = frames.back().spec;
        vars.push_back(vd);
        return vd;
    }

    // Name lookup: current frame's scopes innermost-out, then the lexical
    // parent chain (free variables of nested fns / function values, §7.5),
    // then globals.
    VarDef *LookupVar(string_view name) {
        for (auto fi = (int)frames.size() - 1; fi >= 0;) {
            auto &f = frames[fi];
            auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                      : frames[fi + 1].varbase;
            for (auto i = limit - 1; i >= f.varbase; i--) {
                if (vars[i]->name == name) {
                    if (fi != (int)frames.size() - 1) vars[i]->captured = true;
                    return vars[i];
                }
            }
            fi = f.lexframe;
        }
        auto git = ast.globalmap.find(name);
        if (git != ast.globalmap.end() && !git->second->defs.empty())
            return git->second->defs[0];
        return nullptr;
    }

    // The frame whose vars the above may address next: used to find a spec's
    // frame index for lexparent chains.
    int FrameOfSpec(FnSpec *sp) {
        for (auto i = (int)frames.size() - 1; i >= 0; i--)
            if (frames[i].spec == sp && !frames[i].isfunval) return i;
        return -1;
    }

    SFunction *LookupLocalFn(string_view name) {
        // A frame's scopes are [f.scopebase, next frame's scopebase).
        for (auto fi = (int)frames.size() - 1; fi >= 0;) {
            auto &f = frames[fi];
            auto scopelimit = fi == (int)frames.size() - 1 ? (int)scopes.size()
                                                           : frames[fi + 1].scopebase;
            for (auto i = (int)localfns.size() - 1; i >= 0; i--) {
                auto &[si, sf] = localfns[i];
                if (si >= f.scopebase && si < scopelimit && sf->name == name) return sf;
            }
            fi = f.lexframe;
        }
        return nullptr;
    }

    // Snapshot of assigned/narrowed for every variable currently in scope.
    struct FlowState {
        vector<pair<bool, TypeExpr *>> st;
        bool reachable = true;
    };

    FlowState SaveFlow() {
        FlowState f;
        f.st.reserve(vars.size());
        for (auto v : vars) f.st.push_back({ v->assigned, v->narrowed });
        // Globals' narrowing participates too (assignment in branches).
        f.reachable = reachable;
        return f;
    }

    void RestoreFlow(const FlowState &f) {
        for (size_t i = 0; i < f.st.size() && i < vars.size(); i++) {
            vars[i]->assigned = f.st[i].first;
            vars[i]->narrowed = f.st[i].second;
        }
        reachable = f.reachable;
    }

    // Joins two branch end states into the current state: a fact holds after
    // the join iff it holds in every reachable branch.
    void MergeFlow(const FlowState &a, const FlowState &b) {
        for (size_t i = 0; i < vars.size(); i++) {
            auto aa = i < a.st.size() ? a.st[i] : pair<bool, TypeExpr *> { false, nullptr };
            auto bb = i < b.st.size() ? b.st[i] : pair<bool, TypeExpr *> { false, nullptr };
            vars[i]->assigned = (a.reachable ? aa.first : true) &&
                                (b.reachable ? bb.first : true);
            TypeExpr *n = nullptr;
            if (!a.reachable) n = bb.second;
            else if (!b.reachable) n = aa.second;
            else if (aa.second && bb.second) n = aa.second;
            vars[i]->narrowed = n;
        }
        reachable = a.reachable || b.reachable;
    }

    // Optional narrowing (§3.8): a bare optional variable as a condition, and
    // the obvious compositions. `sense` = the region where cond is true.
    void NarrowCond(Node *cond, bool sense) {
        if (auto id = Is<Ident>(cond)) {
            if (!id->vdef) return;
            auto t = id->vdef->type;
            if (t && t->kind == TY_REF && t->ref->optional && sense && !id->vdef->narrowed) {
                auto r = ast.NewType(TY_REF, cond->line);
                r->ref = ast.NewDetail<TypeRef>();
                r->ref->sub = t->ref->sub;
                id->vdef->narrowed = r;
            }
            return;
        }
        if (auto u = Is<Unary>(cond)) {
            if (u->op == T_NOT) NarrowCond(u->child, !sense);
            return;
        }
        if (auto b = Is<Binary>(cond)) {
            if ((b->op == T_ANDAND && sense) || (b->op == T_OROR && !sense)) {
                NarrowCond(b->left, sense);
                NarrowCond(b->right, sense);
                return;
            }
            // o != null narrows where true; o == null narrows where false.
            if (b->op == T_EQ || b->op == T_NEQ) {
                auto other = Is<NullLit>(b->left) ? b->right
                                                  : Is<NullLit>(b->right) ? b->left : nullptr;
                if (other) NarrowCond(other, b->op == T_NEQ ? sense : !sense);
            }
            return;
        }
    }

    void KillNarrow(VarDef *vd) { vd->narrowed = nullptr; }

    // Names assigned or rebound anywhere below n: loop bodies clear these
    // narrowings up front, since iteration 2 sees the assignment.
    void CollectAssignedNames(Node *n, set<string_view> &out) {
        if (!n) return;
        if (auto a = Is<Assign>(n))
            if (auto id = Is<Ident>(a->lval)) out.insert(id->name);
        n->Children([&](Node *c) { CollectAssignedNames(c, out); });
    }

    void KillNarrowingsAssignedIn(Node *body) {
        set<string_view> names;
        CollectAssignedNames(body, names);
        for (auto v : vars) if (names.count(v->name)) v->narrowed = nullptr;
    }

    // ------------------------------------------------------------------
    // Small type constructors and views.

    TypeExpr *RefTo(TypeExpr *t, Line l) {
        auto r = ast.NewType(TY_REF, l);
        r->ref = ast.NewDetail<TypeRef>();
        r->ref->sub = t;
        return r;
    }

    TypeExpr *SliceOf(TypeExpr *t, Line l) {
        auto s = ast.NewType(TY_SLICE, l);
        s->sub = t;
        return s;
    }

    bool IsIntT(TypeExpr *t) { return t->kind == TY_INT && t->intstorage != IS_VARINT; }
    bool IsFltT(TypeExpr *t) { return t->kind == TY_FLT; }
    bool IsF32(TypeExpr *t) { return t->kind == TY_FLT && t->fltstorage == FS_F32; }
    bool IsOptional(TypeExpr *t) { return t->kind == TY_REF && t->ref->optional; }
    bool IsPlainRef(TypeExpr *t) {
        return t->kind == TY_REF && !t->ref->optional && t->ref->lenstorage < 0;
    }
    bool IsArrayKind(TypeExpr *t, ArrayKind k) {
        return t->kind == TY_ARRAY && t->arr->akind == k;
    }
    bool IsU8(TypeExpr *t) { return t->kind == TY_INT && t->intstorage == IS_U8; }

    // The value type a load from storage yields: numeric types load as
    // themselves, varint decodes to i64 (§3.6), and relative references load
    // as ordinary references (§3.9).
    TypeExpr *LoadType(TypeExpr *t) {
        if (t->kind == TY_INT && t->intstorage == IS_VARINT) return ast.inttypes[IS_I64];
        if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
            auto r = ast.NewType(TY_REF, t->line);
            r->ref = ast.NewDetail<TypeRef>();
            r->ref->sub = t->ref->sub;
            r->ref->optional = t->ref->optional;
            return r;
        }
        return t;
    }

    // The implicit numeric widenings (§6.3): conversions that can never
    // change a value — to a wider type of the same signedness, or from an
    // unsigned type to any strictly wider signed type; f32 to f64.
    static bool ImplicitInt(IntStorage from, IntStorage to) {
        if (from == IS_VARINT || to == IS_VARINT) return false;
        if (from == to) return true;
        if (IntBits(from) >= IntBits(to)) return false;
        return IsUnsigned(to) ? IsUnsigned(from) : true;
    }

    // The pointee type when v is a (non-optional) reference, else null.
    TypeExpr *DerefType(TypeExpr *t) {
        if (t->kind != TY_REF) return nullptr;
        if (t->ref->optional) return nullptr;
        return t->ref->sub;
    }

    // Fixed-size element check for indexable access.
    bool SequentialElems(TypeExpr *arr) {
        return ClassOf(arr->arr->sub) != SC_FIXED;
    }

    // Positions that accept any integer type (indices, slice bounds, sizes,
    // counts): the value is used at 64 bits internally; a u64 above i64.max
    // is out of range for every such use and the bounds check catches it.
    Val CheckIntAny(Node *n) {
        auto v = Operand(n);
        if (!IsIntT(v.type))
            Error(n, cat("an integer is expected here, got ", TypeStr(v.type)));
        return v;
    }

    // Does this type embed self-relative references at the value level (not
    // behind plain references/slices)? Those are the offsets that depend on
    // where the value sits, so a copy would carry the wrong ones; an
    // `in pool` offset is measured from the pool and copies fine (§3.9).
    bool HasRelRefT(TypeExpr *t) {
        switch (t->kind) {
            case TY_REF: return t->ref->lenstorage >= 0 && !t->ref->pool;
            case TY_STRUCT: {
                auto inst = GetStructInst(t);
                for (size_t i = 0; i < inst->ftypes.size(); i++)
                    if (inst->ftypes[i] && HasRelRefT(inst->ftypes[i])) return true;
                return false;
            }
            case TY_ENUM: {
                auto inst = GetEnumInst(t);
                for (auto &vf : inst->vftypes)
                    for (auto ft : vf)
                        if (ft && HasRelRefT(ft)) return true;
                return false;
            }
            case TY_VARIANT: {
                auto inst = GetEnumInst(t->var->adt);
                auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
                for (auto ft : inst->vftypes[vi]) if (ft && HasRelRefT(ft)) return true;
                return false;
            }
            case TY_ARRAY: return HasRelRefT(t->arr->sub);
            default: return false;
        }
    }

    // A copy of a value containing self-relative references would carry
    // offsets measured from the source location; only in-place construction
    // (a literal) is allowed for now. TODO: track the region a relative
    // reference ranges over so whole-region copies can be permitted.
    void NoRelRefCopy(Node *n, TypeExpr *t) {
        if (!reachable || !t) return;
        if (t->kind == TY_REF || t->kind == TY_SLICE || !HasRelRefT(t)) return;
        if (Is<StructLit>(n) || Is<ArrayLit>(n)) return;   // Constructed in place.
        Error(n, cat("copying a value of type ", TypeStr(t), ", which contains self-relative "
                     "references, is not supported; construct it in place"));
    }

    // ------------------------------------------------------------------
    // Pool-relative references (§3.9). `T&<u32 in pool>` names a global pool
    // at the declaration, so nothing has to be discovered per call site: the
    // base is that global's, everywhere.

    // Names resolve once, before any type is instantiated, so the pool is
    // part of the type's identity from the first comparison on.
    void ResolvePools() {
        for (auto t : ast.alltypes) {
            if (t->kind != TY_REF || t->ref->poolname.empty()) continue;
            auto git = ast.globalmap.find(t->ref->poolname);
            if (git == ast.globalmap.end() || git->second->defs.empty())
                Error(t->line, cat("in ", t->ref->poolname,
                                   ": a relative reference's pool must be a global variable; a "
                                   "local or parameter pool has no name at this declaration, so "
                                   "use the self-relative form ", TypeStr(t->ref->sub), "&<",
                                   IntStorageName(t->ref->lenstorage), "> instead (§3.9)"));
            t->ref->pool = git->second->defs[0];
            poolglobals.insert(t->ref->pool);
        }
    }

    // The globals some relative reference type measures offsets from. Empty
    // for a program without the feature, which is what keeps `RootArg::pool`
    // from splitting any specialization such a program would not have split.
    set<VarDef *> poolglobals;

    // The pool a reference rooted at `r` points into, or null. A global names
    // itself; a synthetic parameter class names what every call site that
    // reaches this specialization passed, which is part of its key.
    VarDef *PoolOf(VarDef *r) {
        r = CanonRoot(r);
        if (!r) return nullptr;
        if (r->isglobal) return poolglobals.count(r) ? r : nullptr;
        return r->classpool;
    }

    // The pool must be storage a `T` can live in, and one whose base never
    // moves: a grow-only resizable global grows by bumping its stack's top.
    void ValidatePool(TypeExpr *t) {
        auto pool = t->ref->pool;
        auto pt = pool->type;
        if (!pt || !IsArrayKind(pt, A_GROW))
            Error(t->line, cat("in ", pool->name, ": a relative reference's pool must be a "
                               "grow-only resizable global (", pool->name, ": T[>..]), not ",
                               pt ? TypeStr(pt) : string("an unresolved type"), " (§3.9)"));
        if (!pool->isvar)
            Error(t->line, cat("in ", pool->name, ": a relative reference's pool must be a "
                               "var (§3.9)"));
        if (!CanContain(pt, t->ref->sub))
            Error(t->line, cat("in ", pool->name, ": ", TypeStr(pool->type),
                               " cannot hold a value of type ", TypeStr(t->ref->sub),
                               ", so nothing in it can be pointed at (§3.9)"));
    }

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

    // Can a value of type `t` contain an `of` by value anywhere inside it? A
    // reference or slice field ends the search: what is behind one belongs to
    // its own root.
    bool CanContain(TypeExpr *t, TypeExpr *of) {
        if (!t) return false;
        if (TypeEq(LoadType(t), of)) return true;
        switch (t->kind) {
            case TY_ARRAY: return CanContain(t->arr->sub, of);
            case TY_STRUCT: {
                auto inst = GetStructInst(t);
                for (auto ft : inst->ftypes) if (CanContain(ft, of)) return true;
                return false;
            }
            case TY_ENUM: {
                auto inst = GetEnumInst(t);
                for (auto &vf : inst->vftypes)
                    for (auto ft : vf) if (CanContain(ft, of)) return true;
                return false;
            }
            case TY_VARIANT: {
                auto inst = GetEnumInst(t->var->adt);
                auto vi = VariantIndex(t->var->adt->enu->en, t->var->variant);
                for (auto ft : inst->vftypes[vi]) if (CanContain(ft, of)) return true;
                return false;
            }
            default: return false;
        }
    }

    // The pointee type a reference or slice type reaches: for a slice, its
    // elements (a candidate must hold a run of those).
    TypeExpr *PointeeOf(TypeExpr *t) {
        if (t->kind == TY_REF) return LoadType(t->ref->sub);
        if (t->kind == TY_SLICE) return t->sub;
        return nullptr;
    }

    // Every variable the body being checked can name, exactly the set
    // LookupVar reaches: this frame's scopes plus the lexical parent chain
    // (§7.5). Globals are enumerated separately.
    void VisibleVars(const function<void(VarDef *)> &f) {
        for (auto fi = (int)frames.size() - 1; fi >= 0;) {
            auto &fr = frames[fi];
            auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                      : frames[fi + 1].varbase;
            for (auto i = limit - 1; i >= fr.varbase; i--) f(vars[i]);
            fi = fr.lexframe;
        }
    }

    // A string literal is a run of u8s, so static data owns anything a
    // literal could supply.
    bool StaticCanContain(TypeExpr *of) {
        return of->kind == TY_INT && of->intstorage == IS_U8;
    }

    // The candidates for a pointee of type `of`: every global whose own
    // storage can hold one, static data where a literal could supply one and,
    // unless `globalsonly`, every named local at scope depth `d` or shallower
    // that can hold one plus the pointee of every reference/slice in scope (a
    // parameter's caller-side storage is reachable only through it).
    // Deduplicated by root; the caller picks the deepest.
    void RootCandidates(TypeExpr *of, int d, bool globalsonly, vector<VarDef *> &out,
                        bool &hasstatic) {
        hasstatic = false;
        auto add = [&](VarDef *r) {
            r = CanonRoot(r);
            if (!r) { hasstatic = true; return; }
            for (auto o : out) if (o == r) return;
            out.push_back(r);
        };
        auto consider = [&](VarDef *v, int rd) {
            if (!v->type) return;
            if (v->type->kind == TY_REF || v->type->kind == TY_SLICE) {
                if (!v->refrootknown) return;   // No commitment yet; nothing stored from it.
                auto r = CanonRoot(v->refroot);
                if (Depth(r) > rd) return;
                auto pt = PointeeOf(v->type);
                if (pt && CanContain(pt, of)) add(r);
            } else if (Depth(v) <= rd && CanContain(v->type, of)) {
                add(v);
            }
        };
        if (!globalsonly) VisibleVars([&](VarDef *v) { if (!v->isglobal) consider(v, d); });
        for (auto g : ast.globals) for (auto gd : g->defs) consider(gd, 0);
        if (StaticCanContain(of)) hasstatic = true;
    }

    // What the read-back rule makes of one load.
    struct ReadBack {
        VarDef *root = nullptr;
        bool exact = false;
        VarDef *from = nullptr;   // The container, where candidates were enumerated.
    };

    // The root of a reference/slice of type `rt` loaded out of a container
    // whose own root is (croot, cexact).
    ReadBack ReadBackRoot(TypeExpr *rt, VarDef *croot, bool cexact) {
        ReadBack rb;
        croot = CanonRoot(croot);
        rb.root = croot;
        // A relative reference points within its own root array by
        // construction (§3.9), so it inherits the container's root outright —
        // unless it named a pool, in which case the pool *is* the root, and
        // exactly, wherever the container sits.
        if (rt->kind == TY_REF && rt->ref->lenstorage >= 0) {
            if (rt->ref->pool) { rb.root = rt->ref->pool; rb.exact = true; return rb; }
            rb.exact = cexact;
            return rb;
        }
        auto of = PointeeOf(rt);
        if (!of || !croot || croot == temproot || croot == cycleroot) return rb;
        // Case 3: the container came from a caller, or its own root is only a
        // bound -- storage this function cannot enumerate may be behind it.
        auto global = croot->isglobal;
        if (!global && (!cexact || croot->ownerspec != CurRealFrame().spec)) return rb;
        // Only globals outlive globals (§11.1), so a global container's
        // pointee is owned by a global or by static data, whatever local scope
        // is open here. A local container's was reachable from this frame and
        // had to outlive the container, so its owner is a candidate at the
        // container's depth or shallower.
        vector<VarDef *> cands;
        auto hasstatic = false;
        RootCandidates(of, Depth(croot), global, cands, hasstatic);
        rb.from = croot;
        if (cands.empty()) {
            // Static data alone: null is its root, and it outlives everything.
            if (hasstatic) { rb.root = nullptr; rb.exact = true; }
            return rb;
        }
        // The deepest candidate is the conservative choice: the owner is that
        // one or one further out, so its depth bounds every possibility.
        rb.root = cands[0];
        for (auto c : cands) if (Depth(c) > Depth(rb.root)) rb.root = c;
        rb.exact = cands.size() == 1 && !hasstatic;
        return rb;
    }

    // "was read out of `slots` and may point into `pool` or `spare`": the
    // candidates a read-back could not choose between, for the diagnostics of
    // the rules that need one (§3.9).
    string ReadBackWhy(TypeExpr *rt, VarDef *from) {
        auto of = PointeeOf(rt);
        if (!from || !of) return {};
        vector<VarDef *> cands;
        auto hasstatic = false;
        RootCandidates(of, Depth(from), from->isglobal, cands, hasstatic);
        string s = cat("it was read out of ", from->name, " and may point into ");
        auto n = cands.size() + (hasstatic ? 1 : 0);
        for (size_t i = 0; i < cands.size(); i++) {
            if (i) s += i + 1 == n ? " or " : ", ";
            s += cands[i]->name;
        }
        if (hasstatic) { if (n > 1) s += " or "; s += "static data"; }
        return s;
    }

    // ------------------------------------------------------------------
    // Lvalue paths: names, fields, elements, optionally through references.

    LVal CheckLValue(Node *n) {
        if (auto id = Is<Ident>(n)) {
            auto vd = LookupVar(id->name);
            if (!vd) Error(n, cat("unknown variable: ", id->name));
            id->vdef = vd;
            LVal lv;
            lv.type = vd->narrowed ? vd->narrowed : vd->type;
            lv.var = vd;
            lv.root = vd;
            lv.rootexact = true;
            lv.writable = vd->isvar;
            lv.reusable = vd->reusable;
            n->exprtype = vd->type;
            return lv;
        }
        if (auto d = Is<Dot>(n)) {
            auto lv = LValueBase(d->obj);
            DerefLValue(lv, d->obj);
            ResolveMemberLValue(lv, d);
            n->exprtype = lv.type;
            return lv;
        }
        if (auto ix = Is<Index>(n)) {
            auto lv = LValueBase(ix->obj);
            DerefLValue(lv, ix->obj);
            SliceProvenance(lv, ix->obj);
            if (lv.type->kind == TY_SLICE) {
                if (ClassOf(lv.type->sub) != SC_FIXED)
                    Error(n, "slices of variable-size elements cannot be indexed, only iterated");
                CheckIntAny(ix->idx);
                lv.type = lv.type->sub;
                lv.var = nullptr;
                lv.fromstorage = true;
                n->exprtype = lv.type;
                return lv;
            }
            if (lv.type->kind != TY_ARRAY)
                Error(n, cat("cannot index a value of type ", TypeStr(lv.type)));
            RequireComplete(lv.type, n->line);
            if (SequentialElems(lv.type))
                Error(n, "arrays of variable-size elements cannot be indexed, only iterated");
            CheckIntAny(ix->idx);
            lv.type = lv.type->arr->sub;
            lv.var = nullptr;
            lv.fromstorage = true;
            if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT) lv.isvarint = true;
            n->exprtype = lv.type;
            return lv;
        }
        Error(n, "not an assignable location");
    }

    // The base of a path: itself a path, or any other expression (a call
    // result, a string literal, ...) whose value is then addressed. A null
    // root means static data; temporaries carry the temproot sentinel.
    LVal LValueBase(Node *n) {
        if (Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n)) return CheckLValue(n);
        auto v = CheckV(n, nullptr);
        n->exprtype = v.type;
        LVal lv;
        lv.type = v.type;
        lv.root = v.root;
        lv.rootexact = v.rootexact;
        lv.rootfrom = v.rootfrom;
        lv.writable = v.writable;
        lv.reusable = v.reusable;
        return lv;
    }

    // Crossing a reference in a path (auto-deref, §3.8): the storage owner
    // becomes the reference's root, writability its provenance.
    void DerefLValue(LVal &lv, Node *at) {
        if (lv.type->kind != TY_REF) return;
        if (lv.type->ref->optional)
            Error(at, "optional value must be narrowed (if/guard/assert) before use");
        // Reading a reference variable requires it to have a value.
        if (lv.var) {
            RequireAssigned(lv.var, at);
            lv.writable = lv.var->refwritable;
            lv.root = RefRootOf(lv.var);
            lv.rootexact = RefExactOf(lv.var);
            lv.rootfrom = lv.var->refrootfrom;
            lv.reusable = lv.var->refreusable;
        } else if (lv.fromstorage) {
            // Crossing a reference the path read out of a container: it is a
            // read-back, so where it points is re-derived (§9.5). A relative
            // one loads as an ordinary reference into the same pool, so it
            // keeps the container's root.
            ReadBackLVal(lv);
        }
        lv.type = lv.type->ref->sub;
        lv.var = nullptr;
        if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT) lv.isvarint = true;
    }

    void RequireAssigned(VarDef *vd, Node *at) {
        if (!vd->assigned)
            Error(at, cat("variable ", vd->name, " may be used before it is assigned"));
    }

    // Accessing through a slice variable: writes and roots follow the slice
    // value's provenance, not the variable's own var-ness.
    void SliceProvenance(LVal &lv, Node *at) {
        if (lv.type->kind != TY_SLICE) return;
        if (!lv.var) {
            if (lv.fromstorage) ReadBackLVal(lv);
            return;
        }
        RequireAssigned(lv.var, at);
        lv.writable = lv.var->refwritable;
        lv.root = RefRootOf(lv.var);
        lv.rootexact = RefExactOf(lv.var);
        lv.rootfrom = lv.var->refrootfrom;
        lv.reusable = lv.var->refreusable;
        lv.var = nullptr;
    }

    // A location whose own type is a reference or slice: the value loaded out
    // of it is a read-back, so its root is re-derived (§9.5).
    void ReadBackLVal(LVal &lv) {
        if (lv.type->kind != TY_REF && lv.type->kind != TY_SLICE) return;
        auto rb = ReadBackRoot(lv.type, lv.root, lv.rootexact);
        lv.root = rb.root;
        lv.rootexact = rb.exact;
        lv.rootfrom = rb.from;
    }

    // Field / builtin-property resolution on an lvalue path.
    void ResolveMemberLValue(LVal &lv, Dot *d) {
        auto t = lv.type;
        if (t->kind == TY_STRUCT) {
            auto inst = GetStructInst(t);
            auto st = inst->st;
            for (auto i = 0; i < (int)st->fields.size(); i++) {
                auto &f = st->fields[i];
                if (!f.ispad && f.name == d->name) {
                    d->fieldidx = i;
                    if (f.isconst) lv.writable = false;
                    lv.type = inst->ftypes[i];
                    lv.var = nullptr;
                    lv.fromstorage = true;
                    lv.fotail = inst->frameobj && ClassOf(lv.type) == SC_RESIZABLE;
                    if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT)
                        lv.isvarint = true;
                    return;
                }
            }
            Error(d, cat("struct ", st->name, " has no field ", d->name));
        }
        if (t->kind == TY_VARIANT) {
            auto inst = GetEnumInst(t->var->adt);
            auto vi = VariantIndex(inst->en, t->var->variant);
            auto &fields = t->var->variant->fields;
            for (auto i = 0; i < (int)fields.size(); i++) {
                auto &f = fields[i];
                if (!f.ispad && f.name == d->name) {
                    d->fieldidx = i;
                    if (f.isconst) lv.writable = false;
                    lv.type = inst->vftypes[vi][i];
                    lv.var = nullptr;
                    lv.fromstorage = true;
                    if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT)
                        lv.isvarint = true;
                    return;
                }
            }
            Error(d, cat("variant ", inst->en->name, ".", t->var->variant->name,
                         " has no field ", d->name));
        }
        Error(d, cat("no field access on a value of type ", TypeStr(t)));
    }

    // ------------------------------------------------------------------
    // Values: the per-node dispatch plus the implicit-conversion rules.

    // The raw per-node check: virtual dispatch; the value may still denote a
    // reference. Consumers go through CheckValue/CheckArg/Operand, which
    // apply reference transparency.
    Val CheckV(Node *n, TypeExpr *expected) { return n->Check(*this, expected); }

    // References are transparent: load the pointee unless the destination
    // wants the reference itself. Optionals never decay (narrow first).
    Val DecayRef(Val v) {
        if (!IsPlainRef(v.type)) return v;
        Val r;
        r.type = LoadType(v.type->ref->sub);
        r.root = v.root;  // Compound pointee values: container info, harmless.
        r.rootexact = v.rootexact;
        r.rootfrom = v.rootfrom;
        return r;
    }

    // Does dt consume a reference value as-is (so no decay before fitting)?
    bool KeepsRef(Val &v, TypeExpr *dt) {
        if (!IsPlainRef(v.type)) return true;  // Nothing to decay.
        if (dt->kind == TY_REF) return true;   // Binding (plain/optional/relative).
        // Whole-(pointee-)array argument to a slice parameter (§3.10).
        if (dt->kind == TY_SLICE && v.type->ref->sub->kind == TY_ARRAY) return true;
        return false;
    }

    // An lvalue of a reference destination's pointee type binds by reference
    // (§4.1): the node becomes `&node`, as if written, so every later pass
    // sees an ordinary reference argument.
    Node *AutoRef(Node *n, Val &v) {
        if (!Referenceable(n, v))
            Error(n, "cannot reference a resizable value nested in a variable-size prefix "
                     "or an ADT payload; reference the owning variable instead");
        auto u = ast.New<Unary>(n->line, T_BITAND, n);
        u->synth = true;
        v.type = RefTo(v.type, n->line);
        v.lvalue = false;
        u->exprtype = v.type;
        return u;
    }

    bool BindsRef(const Val &v, TypeExpr *dt) {
        return dt->kind == TY_REF && v.lvalue && v.type->kind != TY_REF && !v.isnull &&
               TypeEq(v.type, dt->ref->sub);
    }

    bool IsNonFixedLValue(const Val &v) {
        return v.lvalue && v.type->kind != TY_REF && v.type->kind != TY_SLICE &&
               ClassOf(v.type) != SC_FIXED;
    }

    // Whether a resizable-valued path has a header of its own to reference
    // (C.2): a variable, or the tail of a frame object.
    bool Referenceable(Node *n, const Val &v) {
        if (ClassOf(v.type) != SC_RESIZABLE) return true;
        if (Is<Ident>(n)) return true;
        auto d = Is<Dot>(n);
        if (!d || !d->obj->exprtype) return false;
        auto ot = d->obj->exprtype;
        if (ot->kind == TY_REF) ot = ot->ref->sub;
        return ot->kind == TY_STRUCT && GetStructInst(ot)->frameobj;
    }

    bool UserRefOf(Node *n) {
        auto u = Is<Unary>(n);
        return u && u->op == T_BITAND && !u->synth;
    }

    // A non-fixed value reaches a value destination only as an rvalue or an
    // explicit copy (§4.1): an lvalue, or a reference to one, is never copied
    // implicitly. A function's own local is moved by `return`.
    void RequireCopyable(const Val &v, Node *n, TypeExpr *dt) {
        if (!reachable) return;
        if (dt->kind == TY_REF || dt->kind == TY_SLICE || dt->kind == TY_VOID) return;
        if (ClassOf(dt) == SC_FIXED) return;
        auto src = IsPlainRef(v.type) ? v.type->ref->sub : v.type;
        if (ClassOf(src) == SC_FIXED) return;
        if (!v.lvalue && !IsPlainRef(v.type)) return;
        if (inreturn && v.lvalue)
            if (auto id = Is<Ident>(n); id && id->vdef && !id->vdef->isglobal &&
                                        id->vdef->ownerspec == frames.back().spec)
                return;
        auto u = Is<Unary>(n);
        auto what = ExprStr(u && u->op == T_BITAND ? u->child : n);
        Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1): pass copy(",
                     what, ") for a copy, or bind it by reference"));
    }

    // Argument nodes the checker rebound (by reference, or a copy() unwrapped)
    // replace the originals: the receiver, then the call's own arguments.
    void WriteBackArgs(Call *c, Dot *d, vector<Node *> &argnodes) {
        d->obj = argnodes[0];
        for (size_t i = 0; i < c->args.size(); i++) c->args[i] = argnodes[i + 1];
    }

    // copy(x) checked: the node becomes x itself, the stored value codegen
    // copies at the destination like any lvalue source.
    void UnwrapCopy(Node *&n) {
        if (auto c = Is<Call>(n); c && c->builtin == B_COPY) n = c->args[0];
    }

    Val CheckValue(Node *&n, TypeExpr *expected) {
        auto v = CheckV(n, expected);
        UnwrapCopy(n);
        if (!expected || expected->kind == TY_VOID) {
            v = DecayRef(v);
        } else {
            if (expected->kind == TY_REF && UserRefOf(n))
                Warn(n, cat("redundant &: ", ExprStr(Is<Unary>(n)->child),
                            " binds by reference here without it (§4.1)"));
            if (BindsRef(v, expected)) n = AutoRef(n, v);
            RequireCopyable(v, n, expected);
            if (!KeepsRef(v, expected)) v = DecayRef(v);
            MustFit(v, n, expected, false);
            NoRelRefCopy(n, expected);
        }
        n->exprtype = v.type;
        return v;
    }

    // Argument position: additionally allows the array→slice coercion (§3.10).
    Val CheckArg(Node *&n, TypeExpr *expected) {
        auto v = CheckV(n, expected);
        UnwrapCopy(n);
        if (expected) {
            if (BindsRef(v, expected)) n = AutoRef(n, v);
            RequireCopyable(v, n, expected);
            if (!KeepsRef(v, expected)) v = DecayRef(v);
            MustFit(v, n, expected, true);
            NoRelRefCopy(n, expected);
        } else {
            v = DecayRef(v);
        }
        n->exprtype = v.type;
        return v;
    }

    // An operand of an operator: always the pointee.
    Val Operand(Node *n) {
        auto v = DecayRef(CheckV(n, nullptr));
        n->exprtype = v.type;
        return v;
    }

    string fitfail;  // A specific reason from the last failing FitsAt, if any.

    void MustFit(Val &v, Node *n, TypeExpr *dt, bool callsite) {
        if (!reachable) return;  // A diverging operand fits anything.
        fitfail.clear();
        if (!FitsAt(v, dt, callsite)) {
            if (!fitfail.empty()) Error(n, fitfail);
            Error(n, cat("expected a value of type ", TypeStr(dt), ", got ", TypeStr(v.type),
                         v.type->kind == TY_INT && dt->kind == TY_INT
                             ? " (narrowing and sign changes require an explicit `as`)"
                             : ""));
        }
    }

    // The implicit adaptations legal at construction/assignment sites (§6.3,
    // §3.1, §3.7, §3.10). On success v.type becomes dt. Also the enforcement
    // point of the store rule (§9.2): a reference/slice stored into storage
    // owned by curdst must be rooted at least as shallow (call-site argument
    // slots pass curdst null: parameters always die before their arguments'
    // roots).
    bool FitsAt(Val &v, TypeExpr *dt, bool callsite) {
        auto t = v.type;
        // An lvalue at a reference destination is the reference to it (§4.1).
        if (BindsRef(v, dt)) {
            t = v.type = RefTo(t, dt->line);
            v.lvalue = false;
        }
        // The null literal fits any optional (plain or relative).
        if (v.isnull) {
            if (dt->kind == TY_REF && dt->ref->optional) { v.type = dt; return true; }
            fitfail = cat("null is only a value of optional types, not ", TypeStr(dt));
            return false;
        }
        if ((dt->kind == TY_REF || dt->kind == TY_SLICE) &&
            (t->kind == TY_REF || t->kind == TY_SLICE) && !callsite && curdst.root) {
            auto root = CanonRoot(v.root);
            if (root && root == cycleroot) {
                fitfail = "storing the result of a recursive call whose returned "
                          "reference's root the cycle's returns do not determine "
                          "(§7.8); it may only be passed down";
                return false;
            }
            if (Depth(root) > Depth(CanonRoot(curdst.root))) {
                fitfail = cat("storing a reference rooted at ",
                              root ? root->name : string_view("static data"),
                              ", which does not outlive the destination (§9.2)");
                return false;
            }
            if (!curdst.varbind && IsGrowShrinkRoot(root)) {
                fitfail = cat("storing a reference into ", root->name,
                              ", which holds a grow-shrink array: such a reference lives in a "
                              "variable, is passed down or returned, and is never stored (§5.2)");
                return false;
            }
            auto spec = CurRealFrame().spec;
            if (root && !root->isglobal && !root->poolclass && spec &&
                (spec->incycle || spec->sf->isrec)) {
                fitfail = "references may only be passed down, not stored, inside a "
                          "recursive cycle (§7.8)";
                return false;
            }
        }
        if (TypeEq(t, dt)) { v.type = dt; return true; }
        switch (dt->kind) {
            case TY_INT:
                if (t->kind != TY_INT) return false;
                // A constant adapts to any integer type its value fits.
                if (v.ck == CK_INT) {
                    if (FitsIntStorage(v.ival, v.uns, dt->intstorage)) {
                        v.type = dt;
                        return true;
                    }
                    fitfail = cat("constant ", ConstStr(v), " does not fit ", TypeStr(dt));
                    return false;
                }
                if (dt->intstorage == IS_VARINT) {
                    // varint stores hold the full i64 value range: every
                    // integer type embeds except u64 (§3.6).
                    if (t->intstorage != IS_U64 && t->intstorage != IS_VARINT) {
                        v.type = dt;
                        return true;
                    }
                    return false;
                }
                if (ImplicitInt(t->intstorage, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                return false;
            case TY_FLT:
                if (t->kind != TY_FLT) return false;
                // Literals adapt to f32; f32 widens to f64.
                if (IsF32(dt)) { if (v.ck != CK_FLT) return false; }
                else if (!IsF32(t)) return false;
                v.type = dt;
                return true;
            case TY_ARRAY: {
                // Construction of an array from another array/slice of the
                // same element type (copies, §3.7/§4.2). Fixed destinations
                // need a statically known length, so only [] adapts.
                TypeExpr *selem = nullptr;
                if (t->kind == TY_ARRAY) selem = t->arr->sub;
                else if (t->kind == TY_SLICE) selem = t->sub;
                else return false;
                if (v.emptyarr) {
                    if (dt->arr->akind == A_FIXED && ArraySize(dt->arr) != 0) return false;
                    v.type = dt;
                    return true;
                }
                if (dt->arr->akind == A_FIXED) return false;
                if (!TypeEq(selem, dt->arr->sub)) return false;
                v.type = dt;
                return true;
            }
            case TY_SLICE: {
                // Whole-array argument to a slice parameter, call sites only;
                // through a reference the pointee array is sliced in place.
                auto at = t;
                if (IsPlainRef(t) && t->ref->sub->kind == TY_ARRAY) at = t->ref->sub;
                if (!callsite || at->kind != TY_ARRAY) return false;
                if (!TypeEq(at->arr->sub, dt->sub)) return false;
                v.type = dt;
                return true;
            }
            case TY_REF: {
                if (t->kind != TY_REF) return false;
                if (!TypeEq(t->ref->sub, dt->ref->sub)) return false;
                if (t->ref->lenstorage >= 0) return false;  // Values are never relative.
                if (dt->ref->lenstorage >= 0) {
                    // Storing an ordinary reference into a relative reference
                    // location (§3.9). A self-relative one needs both ends in
                    // the same root array; an `in pool` one needs the value in
                    // the pool, and takes the destination wherever it is.
                    if (t->ref->optional && !dt->ref->optional) return false;
                    // The target must be the *same* array, so a root that only
                    // bounds the pointee's lifetime will not do (§9.5).
                    auto want = dt->ref->pool ? dt->ref->pool : CanonRoot(curdst.root);
                    auto have = dt->ref->pool ? PoolOf(v.root) : CanonRoot(v.root);
                    if (!want || (!dt->ref->pool && !curdst.exact) || !v.rootexact ||
                        have != want) {
                        auto why = v.rootexact ? string() : ReadBackWhy(v.type, v.rootfrom);
                        fitfail = cat(dt->ref->pool
                                          ? cat("a relative reference in ", want->name,
                                                " must point into ", want->name, " (§3.9); ")
                                          : string("a relative reference must point within the "
                                                   "same root as its location (§3.9); "),
                                      !why.empty() ? why
                                      : !v.rootexact
                                          ? cat("this reference's root is not known exactly, "
                                                "only that it outlives ",
                                                v.root ? CanonRoot(v.root)->name
                                                       : string_view("static data"))
                                      : cat("this reference is rooted at ",
                                            v.root ? CanonRoot(v.root)->name
                                                   : string_view("static data")));
                        return false;
                    }
                    v.type = dt;
                    return true;
                }
                // T& widens to T?.
                if (dt->ref->optional && !t->ref->optional) { v.type = dt; return true; }
                return false;
            }
            case TY_ENUM: {
                // Mode adaptation copies at construction (fixed <-> variable);
                // a variant value constructs its enum (the tag is static).
                if (t->kind == TY_VARIANT) {
                    auto adt = t->var->adt;
                    if (adt->enu->en != dt->enu->en) return false;
                    if (!TypeArgsEq(adt->enu->args, dt->enu->args)) return false;
                } else if (t->kind == TY_ENUM) {
                    if (t->enu->en != dt->enu->en) return false;
                    if (!TypeArgsEq(t->enu->args, dt->enu->args)) return false;
                } else {
                    return false;
                }
                if (!dt->enu->varmode && !GetEnumInst(dt)->allfixed) return false;
                v.type = dt;
                return true;
            }
            default: return false;
        }
    }

    // Whether the constant (value bits v, u64-flavored when uns) fits the
    // given integer type. varint holds the full i64 value range.
    static bool FitsIntStorage(int64_t v, bool uns, IntStorage s) {
        if (uns) return s == IS_U64;   // Above i64.max: only u64 holds it.
        switch (s) {
            case IS_I8:  return v >= -128 && v <= 127;
            case IS_I16: return v >= -32768 && v <= 32767;
            case IS_I32: return v >= INT32_MIN && v <= INT32_MAX;
            case IS_U8:  return v >= 0 && v <= 255;
            case IS_U16: return v >= 0 && v <= 65535;
            case IS_U32: return v >= 0 && v <= UINT32_MAX;
            case IS_U64: return v >= 0;
            case IS_I64: case IS_VARINT: return true;
        }
        return false;
    }

    static string ConstStr(const Val &v) {
        return v.uns ? cat((uint64_t)v.ival) : cat(v.ival);
    }

    // Conditions: bool, or an optional (§3.8 truthiness + narrowing). Plain
    // references decay (a bool& condition reads its pointee); an already
    // narrowed optional stays a valid (trivially true) test.
    Val CheckCond(Node *n) {
        auto v = CheckV(n, nullptr);
        auto id = Is<Ident>(n);
        auto narrowedopt = id && id->vdef && IsOptional(id->vdef->type) && id->vdef->narrowed;
        if (!narrowedopt) v = DecayRef(v);
        n->exprtype = v.type;
        if (!narrowedopt && v.type->kind != TY_BOOL && !IsOptional(v.type))
            Error(n, cat("condition must be bool or an optional, got ", TypeStr(v.type)));
        return v;
    }

    // Unifies two branch values (literal/f32/root adaptations); null = branch
    // diverged (bottom). Errors when both produce values of unrelated types
    // and a value is wanted.
    TypeExpr *UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue) {
        if (!a) return b;
        if (!b) return a;
        if (TypeEq(a, b)) return a;
        if (a->kind == TY_INT && b->kind == TY_INT) {
            if (ImplicitInt(a->intstorage, b->intstorage)) return b;
            if (ImplicitInt(b->intstorage, a->intstorage)) return a;
        }
        if (a->kind == TY_FLT && b->kind == TY_FLT) return ast.flttypes[FS_F64];
        if (!wantvalue) return ast.voidtype;
        Error(at, cat("branches have mismatched types: ", TypeStr(a), " vs ", TypeStr(b)));
    }

    Val VoidVal() {
        Val v;
        v.type = ast.voidtype;
        return v;
    }

    TypeExpr *FixedArrayOf(TypeExpr *elem, int64_t count, Line l) {
        auto t = ast.NewType(TY_ARRAY, l);
        t->arr = ast.NewDetail<TypeArray>();
        t->arr->sub = elem;
        t->arr->akind = A_FIXED;
        t->arr->size = count;
        return t;
    }

    // &lvalue: reference creation (§3.8) with its restrictions. On a location
    // that itself holds a reference (a reference variable or field), yields
    // the stored reference — there are no references to references.
    Val CheckRefOf(Unary *x) {
        auto lv = CheckLValue(x->child);
        if (lv.var) RequireAssigned(lv.var, x);
        // A reference to a resizable value points at its header (C.2): a whole
        // variable's, or a frame object's tail's; a resizable nested in any
        // other shape (a variable-size prefix, an ADT payload) has none.
        if (!lv.var && !lv.fotail && lv.type->kind != TY_REF &&
            ClassOf(lv.type) == SC_RESIZABLE)
            Error(x, "cannot reference a resizable value nested in a variable-size prefix "
                     "or an ADT payload; reference the owning variable instead");
        if (lv.type->kind == TY_REF) {
            Val v;
            v.type = LoadType(lv.type);  // Relative refs load as plain (§3.9).
            if (lv.var) {
                v.root = RefRootOf(lv.var);
                v.rootexact = RefExactOf(lv.var);
                v.rootfrom = lv.var->refrootfrom;
                v.writable = lv.var->refwritable;
                v.reusable = lv.var->refreusable;
            } else {
                // Container-read: laundered writable by design (§9.5), and
                // rooted by the read-back rule rather than at the container.
                ReadBackLVal(lv);
                v.root = lv.root;
                v.rootexact = lv.rootexact;
                v.rootfrom = lv.rootfrom;
                v.writable = true;
            }
            return v;
        }
        Val v;
        v.type = RefTo(lv.type, x->line);
        v.root = lv.root;
        v.rootexact = lv.rootexact;
        v.rootfrom = lv.rootfrom;
        v.writable = lv.writable && !lv.isvarint;
        v.reusable = lv.reusable;
        return v;
    }

    // Wrap-free signed 64-bit arithmetic, reporting overflow.
    static bool AddOv(int64_t a, int64_t b, int64_t &r) {
        r = (int64_t)((uint64_t)a + (uint64_t)b);
        return ((a ^ r) & (b ^ r)) < 0;
    }
    static bool SubOv(int64_t a, int64_t b, int64_t &r) {
        r = (int64_t)((uint64_t)a - (uint64_t)b);
        return ((a ^ b) & (a ^ r)) < 0;
    }
    static bool MulOv(int64_t a, int64_t b, int64_t &r) {
        r = (int64_t)((uint64_t)a * (uint64_t)b);
        if (a == 0 || b == 0) return false;
        if (a == -1) return b == INT64_MIN;
        if (b == -1) return a == INT64_MIN;
        return r / b != a;
    }

    // Signed `%` is Euclidean (§6.2): the result is in [0, |b|), never
    // negative. Callers check b != 0 first. The adjustment is computed
    // unsigned so that b == i64.min (whose negation is unrepresentable) and
    // the i64.min % -1 case both work out; the latter's exact remainder is 0,
    // which is why it needs no hardware division.
    static int64_t EuclidMod(int64_t a, int64_t b) {
        if (b == -1) return 0;
        auto r = a % b;
        if (r < 0) r = (int64_t)((uint64_t)r + (b < 0 ? 0u - (uint64_t)b : (uint64_t)b));
        return r;
    }

    // Folds a constant binary op at the width and signedness of out.type. An
    // operation whose result does not fit is left for the runtime (overflow
    // aborts in debug builds, §6.2); division by a constant zero is a
    // compile error. Operand values fit out.type (the unify rules ensured it).
    void FoldInt(TType op, Val &l, Val &r, Val &out, Node *at) {
        if (l.ck != CK_INT || r.ck != CK_INT) return;
        auto s = out.type->intstorage;
        if (s == IS_VARINT) return;
        auto bits = IntBits(s);
        if (IsUnsigned(s)) {
            // Unsigned arithmetic wraps modulo 2^width by definition (§6.2),
            // so every operation folds exactly.
            auto a = (uint64_t)l.ival, b = (uint64_t)r.ival;
            auto max = bits == 64 ? UINT64_MAX : (1ull << bits) - 1;
            uint64_t res = 0;
            switch (op) {
                case T_PLUS:   res = (a + b) & max; break;
                case T_MINUS:  res = (a - b) & max; break;
                case T_MUL:    res = (a * b) & max; break;
                case T_DIV:    if (!b) Error(at, "constant division by zero"); res = a / b; break;
                case T_MOD:    if (!b) Error(at, "constant division by zero"); res = a % b; break;
                case T_BITAND: res = a & b; break;
                case T_BITOR:  res = a | b; break;
                case T_XOR:    res = a ^ b; break;
                case T_SHL:    res = (a << (b & (bits - 1))) & max; break;
                case T_SHR:    res = (a & max) >> (b & (bits - 1)); break;
                default:       return;
            }
            out.ck = CK_INT;
            out.ival = (int64_t)res;
            out.uns = s == IS_U64 && out.ival < 0;
            return;
        }
        auto a = l.ival, b = r.ival;
        int64_t res = 0;
        switch (op) {
            case T_PLUS:   if (AddOv(a, b, res)) return; break;
            case T_MINUS:  if (SubOv(a, b, res)) return; break;
            case T_MUL:    if (MulOv(a, b, res)) return; break;
            case T_DIV:
                if (!b) Error(at, "constant division by zero");
                if (a == INT64_MIN && b == -1) return;
                res = a / b;
                break;
            case T_MOD:
                if (!b) Error(at, "constant division by zero");
                res = EuclidMod(a, b);
                break;
            case T_BITAND: res = a & b; break;
            case T_BITOR:  res = a | b; break;
            case T_XOR:    res = a ^ b; break;
            case T_SHL:    res = (int64_t)((uint64_t)a << (b & (bits - 1))); break;
            case T_SHR:    res = a >> (b & (bits - 1)); break;
            default:       return;
        }
        if (!FitsIntStorage(res, false, s)) return;
        out.ck = CK_INT;
        out.ival = res;
    }

    // The operand/result type of a binary numeric operator: equal types
    // stand; a constant adapts to the other operand's type; otherwise the
    // operand that implicitly widens into the other picks the wider type
    // (§6.1). Returns null for non-numeric or int/float-mixed pairs.
    TypeExpr *UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt, TypeExpr *rt,
                           bool cmp = false) {
        if (IsIntT(lt) && IsIntT(rt)) {
            if (TypeEq(lt, rt)) return lt;
            if (lv.ck == CK_INT && rv.ck == CK_INT) {
                if (lv.uns || rv.uns) {
                    if ((!lv.uns && lv.ival < 0) || (!rv.uns && rv.ival < 0))
                        Error(at, "constant operands have no common type (one is above "
                                  "i64.max, the other negative)");
                    return ast.inttypes[IS_U64];
                }
                return ast.inttypes[IS_I64];
            }
            if (lv.ck == CK_INT) {
                if (!FitsIntStorage(lv.ival, lv.uns, rt->intstorage))
                    Error(at, cat("constant ", ConstStr(lv), " does not fit ", TypeStr(rt)));
                return rt;
            }
            if (rv.ck == CK_INT) {
                if (!FitsIntStorage(rv.ival, rv.uns, lt->intstorage))
                    Error(at, cat("constant ", ConstStr(rv), " does not fit ", TypeStr(lt)));
                return lt;
            }
            if (ImplicitInt(lt->intstorage, rt->intstorage)) return rt;
            if (ImplicitInt(rt->intstorage, lt->intstorage)) return lt;
            // §6.1: a comparison produces bool, so it has no result type to
            // pick and the mathematical answer across signedness is never in
            // doubt. u64 is the one unsigned type with no signed supertype;
            // it may meet a signed operand the compiler knows is
            // non-negative, and the compare is then a single unsigned one.
            // Without that knowledge the conversion could change the value,
            // so the cast has to be written (and thought about).
            if (cmp) {
                auto isu64 = [](TypeExpr *t) { return t->intstorage == IS_U64; };
                if (isu64(lt) != isu64(rt)) {
                    auto &sv = isu64(lt) ? rv : lv;
                    auto st = isu64(lt) ? rt : lt;
                    if (!IsUnsigned(st->intstorage)) {
                        if (sv.nonneg) return ast.inttypes[IS_U64];
                        Error(at, cat("comparing ", TypeStr(lt), " with ", TypeStr(rt),
                                      " needs the signed side to be known non-negative "
                                      "(a literal, .len/.cap, or a `let` bound to one); "
                                      "convert it with `as` otherwise"));
                    }
                }
            }
            Error(at, cat("operands of ", TName(op), " have no common type: ", TypeStr(lt),
                          " and ", TypeStr(rt), " (convert one with `as`)"));
        }
        if (lt->kind == TY_FLT && rt->kind == TY_FLT) {
            if (TypeEq(lt, rt)) return lt;
            // One side is f32, the other f64: a literal adapts to the typed
            // side, otherwise f32 widens (§6.3).
            if (lv.ck == CK_FLT) return rt;
            if (rv.ck == CK_FLT) return lt;
            return ast.flttypes[FS_F64];
        }
        return nullptr;
    }

    // Re-types both operands to the unified type ct: adapted constants and
    // implicitly widened operands emit at ct downstream.
    void RetypeOperands(Node *left, Node *right, Val &lv, Val &rv, TypeExpr *ct) {
        lv.type = ct;
        rv.type = ct;
        left->exprtype = ct;
        right->exprtype = ct;
    }

    // All scalar leaves integers, or all floats; only structs and fixed
    // arrays compose; the value must be fixed-size (a constructed result).
    bool ElementwiseOK(TypeExpr *t) {
        int isint = -1;
        function<bool(TypeExpr *)> rec = [&](TypeExpr *t2) -> bool {
            switch (t2->kind) {
                case TY_INT:
                    if (t2->intstorage == IS_VARINT) return false;
                    if (isint == 0) return false;
                    isint = 1;
                    return true;
                case TY_FLT:
                    if (isint == 1) return false;
                    isint = 0;
                    return true;
                case TY_STRUCT: {
                    auto inst = GetStructInst(t2);
                    for (size_t i = 0; i < inst->ftypes.size(); i++)
                        if (inst->ftypes[i] && !rec(inst->ftypes[i])) return false;
                    return true;
                }
                case TY_ARRAY:
                    return t2->arr->akind == A_FIXED && rec(t2->arr->sub);
                default:
                    return false;
            }
        };
        return (t->kind == TY_STRUCT || t->kind == TY_ARRAY) && rec(t);
    }

    Val CheckVariantConst(Dot *d, SEnum *en) {
        if (!en->generics.empty())
            Error(d, cat("generic enum ", en->name, " needs type arguments to name a variant"));
        auto t = ast.NewType(TY_ENUM, d->line);
        t->enu = ast.NewDetail<TypeEnum>();
        t->enu->en = en;
        auto inst = GetEnumInst(t);
        SVariant *found = nullptr;
        for (auto &var : en->variants) if (var.name == d->name) { found = &var; break; }
        if (!found) Error(d, cat("enum ", en->name, " has no variant named ", d->name));
        if (found->has_payload)
            Error(d, cat("variant ", en->name, ".", d->name,
                         " has a payload; construct it with ", en->name, ".", d->name, " { ... }"));
        d->variantconst = found;
        d->einst = inst;
        if (!inst->allfixed) t->enu->varmode = true;
        Val v;
        v.type = t;
        return v;
    }

    // Merges the values of two branches (for roots: the deeper — i.e. more
    // conservative — root wins; writability must hold in both).
    Val MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at, bool wantvalue) {
        if (!areach) return b;
        if (!breach) return a;
        Val v;
        v.type = UnifyBranch(a.type, b.type, at, wantvalue);
        v.root = Depth(a.root) >= Depth(b.root) ? a.root : b.root;
        v.rootexact = a.rootexact && b.rootexact && CanonRoot(a.root) == CanonRoot(b.root);
        v.rootfrom = a.rootfrom ? a.rootfrom : b.rootfrom;
        v.writable = a.writable && b.writable;
        v.reusable = a.reusable && b.reusable;
        return v;
    }

    Val CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue) {
        CheckCond(x->cond);
        auto entry = SaveFlow();
        NarrowCond(x->cond, true);
        auto tv = CheckBlockVal(x->thenb, expected, wantvalue, SK_PLAIN);
        auto aflow = SaveFlow();
        RestoreFlow(entry);
        Val ev = VoidVal();
        FlowState bflow;
        if (x->elseb) {
            NarrowCond(x->cond, false);
            if (auto ei = Is<IfExpr>(x->elseb)) ev = CheckIf(ei, expected, wantvalue);
            else ev = CheckBlockVal((Block *)x->elseb, expected, wantvalue, SK_PLAIN);
            x->elseb->exprtype = ev.type;
            bflow = SaveFlow();
            RestoreFlow(entry);
        } else {
            if (wantvalue)
                Error(x, "an if used as a value requires an else branch");
            NarrowCond(x->cond, false);
            bflow = SaveFlow();
            RestoreFlow(entry);
        }
        MergeFlow(aflow, bflow);
        if (!wantvalue) return VoidVal();
        return MergeVals(tv, aflow.reachable, ev, bflow.reachable, x, wantvalue);
    }

    Val CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue, int scopekind,
                      Node *scopenode = nullptr) {
        ValueRegion vr(*this, wantvalue);
        PushScope(scopekind, scopenode);
        for (auto st : b->stmts) CheckStmt(st);
        Val v = VoidVal();
        if (b->tail) {
            if (wantvalue) v = CheckValue(b->tail, expected);
            else CheckStmtExpr(b->tail);
        } else if (wantvalue && reachable && expected && expected->kind != TY_VOID) {
            Error(b, "block used as a value must end in an expression");
        }
        if (!reachable) v.type = nullptr;  // Bottom: the block never produces.
        PopScope();
        b->exprtype = v.type ? v.type : ast.voidtype;
        return v;
    }

    Val CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue) {
        auto sv = CheckV(m->scrutinee, nullptr);
        m->scrutinee->exprtype = sv.type;
        auto st = sv.type;
        TypeExpr *enumtype = nullptr;
        if (st->kind == TY_REF) {
            if (st->ref->optional)
                Error(m, "optional value must be narrowed (if/guard/assert), not matched");
            if (st->ref->sub->kind == TY_ENUM) enumtype = st->ref->sub;
        } else if (st->kind == TY_ENUM) {
            enumtype = st;
        }
        auto entry = SaveFlow();
        Val result;
        auto resultreach = false;
        auto first = true;
        FlowState acc;
        auto DoArm = [&](MatchArm &arm, VarDef *binder) {
            RestoreFlow(entry);
            PushScope(SK_PLAIN);
            if (binder) vars.push_back(binder);
            Val av;
            if (wantvalue) av = CheckValue(arm.body, expected);
            else CheckStmtExpr(arm.body);
            auto aflow = SaveFlow();
            if (!reachable) av.type = nullptr;
            PopScope();
            if (first) {
                result = av;
                resultreach = aflow.reachable;
                acc = aflow;
                first = false;
            } else {
                result = MergeVals(result, resultreach, av, aflow.reachable, m, wantvalue);
                resultreach = resultreach || aflow.reachable;
                // Accumulate the join of all arms' flow.
                auto save = SaveFlow();
                MergeFlow(acc, aflow);
                acc = SaveFlow();
                RestoreFlow(save);
            }
        };
        if (enumtype) {
            auto inst = GetEnumInst(enumtype);
            auto en = inst->en;
            vector<bool> covered(en->variants.size(), false);
            auto haswild = false;
            for (auto &arm : m->arms) {
                if (arm.pat.kind == P_WILDCARD) {
                    haswild = true;
                    DoArm(arm, nullptr);
                    continue;
                }
                if (arm.pat.kind != P_VARIANT)
                    Error(arm.body, "ADT match arms are variant names (or _)");
                SVariant *found = nullptr;
                int vi = 0;
                for (auto i = 0; i < (int)en->variants.size(); i++)
                    if (en->variants[i].name == arm.pat.variant) { found = &en->variants[i]; vi = i; break; }
                if (!found)
                    Error(arm.body, cat("enum ", en->name, " has no variant named ",
                                        arm.pat.variant));
                if (covered[vi])
                    Error(arm.body, cat("duplicate match arm for variant ", arm.pat.variant));
                covered[vi] = true;
                arm.variant = found;
                VarDef *binder = nullptr;
                if (!arm.pat.binder.empty()) {
                    if (!found->has_payload && found->fields.empty())
                        Error(arm.body, cat("variant ", arm.pat.variant, " has no payload to bind"));
                    auto vt = VariantTypeOf(enumtype, found, m->line);
                    binder = ast.NewVarDef();
                    binder->name = arm.pat.binder;
                    binder->line = m->line;
                    binder->depth = CurDepth() + 1;
                    binder->ownerspec = frames.back().spec;
                    binder->assigned = true;
                    if (arm.pat.byref) {
                        // `Variant &b`: only variable-mode payloads may be
                        // bound by reference — a fixed-mode value may be
                        // overwritten with another variant, so references
                        // into its payload are illegal (§3.5, §8.1).
                        if (!enumtype->enu->varmode)
                            Error(arm.body, cat("cannot bind the payload of fixed-mode ",
                                                enumtype->enu->en->name, " by reference "
                                                "(§3.5); bind by value: ", arm.pat.variant,
                                                " ", arm.pat.binder));
                        binder->type = RefTo(vt, m->line);
                        binder->refroot = CanonRoot(sv.root);
                        binder->refrootknown = true;
                        binder->refrootexact = sv.rootexact;
                        binder->refrootfrom = sv.rootfrom;
                        binder->refwritable = sv.writable;
                    } else {
                        if (HasRelRefT(vt))
                            Error(arm.body, cat("payload of ", arm.pat.variant, " contains "
                                                "relative references; bind it by reference "
                                                "(&", arm.pat.binder, ")"));
                        binder->type = vt;  // Payload copy, any mode (§8.1).
                        binder->isvar = false;
                        NoteNonfixedLocal(vt, m->line, !frames.back().spec);
                    }
                    arm.binder = binder;
                }
                DoArm(arm, binder);
            }
            if (!haswild)
                for (size_t i = 0; i < en->variants.size(); i++)
                    if (!covered[i])
                        Error(m, cat("match does not cover variant ", en->name, ".",
                                     en->variants[i].name));
        } else if (IsIntT(LoadType(st))) {
            auto sit = LoadType(st)->intstorage;
            auto haswild = false;
            for (auto &arm : m->arms) {
                if (arm.pat.kind == P_WILDCARD) { haswild = true; DoArm(arm, nullptr); continue; }
                if (arm.pat.kind == P_VARIANT)
                    Error(arm.body, cat("unknown pattern ", arm.pat.variant,
                                        " in an integer match"));
                arm.lo = ConstIntOrError(arm.pat.lo, "match pattern");
                arm.hi = arm.pat.kind == P_RANGE
                             ? ConstIntOrError(arm.pat.hi, "match pattern")
                             : arm.lo + 1;
                // Pattern values must fit the scrutinee's type (a u64
                // scrutinee accepts any 64-bit pattern).
                if (sit != IS_U64) {
                    auto ul = Is<IntLit>(arm.pat.lo) && Is<IntLit>(arm.pat.lo)->uns;
                    auto uh = arm.pat.hi && Is<IntLit>(arm.pat.hi) &&
                              Is<IntLit>(arm.pat.hi)->uns;
                    if (!FitsIntStorage(arm.lo, ul, sit) ||
                        (arm.pat.kind == P_RANGE && !FitsIntStorage(arm.hi, uh, sit)))
                        Error(arm.body, cat("match pattern does not fit the scrutinee "
                                            "type ", TypeStr(st)));
                }
                if (arm.hi <= arm.lo && !(sit == IS_U64 && (arm.lo < 0 || arm.hi < 0)))
                    Error(arm.body, "empty range in match pattern");
                DoArm(arm, nullptr);
            }
            if (!haswild) Error(m, "integer match requires a _ arm");
        } else {
            Error(m, cat("cannot match on a value of type ", TypeStr(st)));
        }
        if (!first) {
            auto save = SaveFlow();
            (void)save;
            RestoreFlow(acc);
        }
        reachable = resultreach;
        if (!wantvalue) return VoidVal();
        if (!result.type) result.type = ast.voidtype;
        return result;
    }

    TypeExpr *VariantTypeOf(TypeExpr *enumtype, SVariant *v, Line l) {
        auto t = ast.NewType(TY_VARIANT, l);
        t->var = ast.NewDetail<TypeVariant>();
        // Variant types are mode-neutral; drop varmode from the adt type.
        if (enumtype->enu->varmode) {
            auto base = ast.NewType(TY_ENUM, l);
            base->enu = ast.NewDetail<TypeEnum>();
            base->enu->en = enumtype->enu->en;
            base->enu->args = enumtype->enu->args;
            t->var->adt = base;
        } else {
            t->var->adt = enumtype;
        }
        t->var->variant = v;
        return t;
    }

    Val CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue) {
        ValueRegion vr(*this, wantvalue);
        PushScope(SK_BLOCK, x);
        for (auto st : x->body->stmts) CheckStmt(st);
        Val v = VoidVal();
        if (x->body->tail) {
            if (wantvalue) v = CheckValue(x->body->tail, expected);
            else CheckStmtExpr(x->body->tail);
        }
        if (!reachable) v.type = nullptr;
        auto sc = scopes.back();
        PopScope();
        x->body->exprtype = v.type ? v.type : ast.voidtype;
        reachable = reachable || sc.hasbreak;  // Exits via the tail or any break.
        if (!wantvalue) return VoidVal();
        Val r = v;
        r.type = UnifyBranch(v.type, sc.breaktype, x, wantvalue);
        if (!r.type) r.type = ast.voidtype;
        return r;
    }

    Val CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue) {
        (void)expected;
        ValueRegion vr(*this, wantvalue);
        KillNarrowingsAssignedIn(x->body);
        auto entry = SaveFlow();
        PushScope(SK_LOOP, x);
        for (auto st : x->body->stmts) CheckStmt(st);
        if (x->body->tail) CheckStmtExpr(x->body->tail);
        auto sc = scopes.back();
        PopScope();
        RestoreFlow(entry);
        KillNarrowingsAssignedIn(x->body);
        reachable = sc.hasbreak;  // A loop only exits via break.
        Val v;
        if (wantvalue && sc.breaktype) v.type = sc.breaktype;
        else v.type = ast.voidtype;
        return v;
    }

    void CheckWhile(While *x) {
        CheckCond(x->cond);
        auto entry = SaveFlow();
        NarrowCond(x->cond, true);
        KillNarrowingsAssignedIn(x->body);
        PushScope(SK_LOOP, x);
        for (auto st : x->body->stmts) CheckStmt(st);
        if (x->body->tail) CheckStmtExpr(x->body->tail);
        auto sc = scopes.back();
        PopScope();
        RestoreFlow(entry);
        KillNarrowingsAssignedIn(x->body);
        if (sc.breaktype)
            Error(x, "break with a value exits loop/block only, not while");
    }

    void CheckFor(ForLoop *x) {
        TypeExpr *bindtype = nullptr;
        VarDef *iterroot = nullptr;
        auto iterexact = false;
        VarDef *iterfrom = nullptr;
        auto iterwritable = false;
        if (auto r = Is<RangeExpr>(x->iter)) {
            auto lo = CheckIntAny(r->lo);
            auto hi = CheckIntAny(r->hi);
            auto ct = UnifyNumeric(r, T_DOTDOT, lo, hi, lo.type, hi.type);
            RetypeOperands(r->lo, r->hi, lo, hi, ct);
            r->exprtype = ct;
            x->iterkind = IK_RANGE;
            if (x->byref) Error(x, "cannot iterate an integer range by reference");
            bindtype = ct;
        } else {
            auto iv = CheckV(x->iter, nullptr);
            x->iter->exprtype = iv.type;
            auto t = iv.type;
            iterroot = iv.root;
            iterexact = iv.rootexact;
            iterfrom = iv.rootfrom;
            iterwritable = iv.writable;
            if (t->kind == TY_REF && !t->ref->optional) t = t->ref->sub;  // Iterate through refs.
            t = LoadType(t);
            RequireComplete(t, x->line);
            if (IsIntT(t)) {
                x->iterkind = IK_COUNT;
                if (x->byref) Error(x, "cannot iterate an integer count by reference");
                bindtype = t;
            } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
                auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
                x->iterkind = t->kind == TY_ARRAY ? IK_ARRAY : IK_SLICE;
                // Non-fixed elements bind by reference either way (§4.1).
                if (!x->byref && ClassOf(elem) != SC_FIXED) {
                    x->byref = true;
                } else if (x->byref && ClassOf(elem) != SC_FIXED) {
                    Warn(x, "redundant &: elements of this type bind by reference without it "
                            "(§4.1)");
                }
                if (x->byref) {
                    bindtype = RefTo(elem, x->line);
                } else {
                    if (HasRelRefT(elem))
                        Error(x, "elements containing relative references require the "
                                 "&x binding form (copies are not supported)");
                    bindtype = LoadType(elem);
                }
            } else {
                Error(x, cat("cannot iterate a value of type ", TypeStr(t)));
            }
        }
        auto entry = SaveFlow();
        KillNarrowingsAssignedIn(x->body);
        PushScope(SK_LOOP, x);
        auto vd = NewVar(x->var, bindtype, x->line, false);
        vd->assigned = true;
        if (bindtype->kind == TY_REF) {
            vd->refroot = CanonRoot(iterroot);
            vd->refrootknown = true;
            vd->refrootexact = iterexact;
            vd->refrootfrom = iterfrom;
            vd->refwritable = iterwritable;
        }
        x->vdef = vd;
        if (!x->idxvar.empty()) {
            auto idx = NewVar(x->idxvar, ast.inttypes[IS_I64], x->line, false);
            idx->assigned = true;
            x->idxdef = idx;
        }
        for (auto st : x->body->stmts) CheckStmt(st);
        if (x->body->tail) CheckStmtExpr(x->body->tail);
        auto sc = scopes.back();
        PopScope();
        RestoreFlow(entry);
        KillNarrowingsAssignedIn(x->body);
        if (sc.breaktype)
            Error(x, "break with a value exits loop/block only, not for");
    }

    void CheckGuard(Guard *g) {
        CheckCond(g->cond);
        auto entry = SaveFlow();
        if (g->elseb) {
            NarrowCond(g->cond, false);
            CheckBlockVal(g->elseb, nullptr, false, SK_PLAIN);
            if (reachable)
                Error(g, "guard else block must diverge (return, break, continue, or abort)");
        } else {
            auto si = FindBreakScope(false);
            if (si >= 0) {
                auto &sc = scopes[si];
                if (sc.breaktype)
                    Error(g, "bare guard exits a construct that requires a break value");
                sc.valuelessbreak = true;
                sc.hasbreak = true;
                g->implicitexit = 1;
            } else {
                ImplicitEmptyReturn(g);
                g->implicitexit = 2;
            }
        }
        RestoreFlow(entry);
        NarrowCond(g->cond, true);
    }

    void ImplicitEmptyReturn(Node *at) {
        auto &f = CurRealFrame();
        if (!f.sf) Error(at, "guard shorthand cannot exit the top level");
        auto spec = f.spec;
        if (spec->retsknown) {
            if (!spec->rets.empty())
                Error(at, cat("bare guard would return without the required value(s) of ",
                              f.sf->name));
        } else {
            spec->rets.clear();
            spec->retsknown = true;
        }
    }

    Frame &CurRealFrame() {
        for (auto i = (int)frames.size() - 1; i >= 0; i--)
            if (!frames[i].isfunval) return frames[i];
        return frames[0];
    }

    int FindBreakScope(bool forcontinue) {
        for (auto i = (int)scopes.size() - 1; i >= frames.back().scopebase; i--) {
            if (scopes[i].kind == SK_LOOP) return i;
            if (!forcontinue && scopes[i].kind == SK_BLOCK) return i;
            if (scopes[i].kind == SK_FN) break;
        }
        return -1;
    }

    void CheckBreak(Break *b) {
        auto si = FindBreakScope(false);
        if (si < 0) Error(b, "break outside of a loop or block");
        auto &sc = scopes[si];
        if (b->val) {
            if (!Is<LoopExpr>(sc.node) && !Is<EarlyBlock>(sc.node))
                Error(b, "break with a value exits loop/block only");
            if (sc.valuelessbreak)
                Error(b, "this construct mixes valueless and valued breaks");
            auto v = CheckValue(b->val, scopes[si].breaktype);
            auto &sc2 = scopes[si];  // CheckValue may not reallocate, but be safe.
            if (!sc2.breaktype) sc2.breaktype = v.type;
            sc2.hasbreak = true;
        } else {
            if (sc.breaktype)
                Error(b, "this construct mixes valueless and valued breaks");
            sc.valuelessbreak = true;
            sc.hasbreak = true;
        }
        reachable = false;
    }

    void CheckContinue(Node *n) {
        if (FindBreakScope(true) < 0) Error(n, "continue outside of a loop");
        reachable = false;
    }

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

    Val CheckCall(Call *c, TypeExpr *expected) {
        (void)expected;
        // A node may be re-checked in argument phase 2; reset annotations.
        c->spec = nullptr;
        c->dispatch.clear();
        c->builtin = -1;
        c->rettypes.clear();
        lastcallrets.clear();
        // Arguments construct into parameter slots, not whatever destination
        // encloses this call; member ops re-set curdst for element pushes.
        auto savedst = curdst;
        curdst = Dest {};
        Val v;
        if (auto d = Is<Dot>(c->callee)) v = CheckUfcsCall(c, d);
        else if (auto id = Is<Ident>(c->callee)) v = CheckNamedCall(c, id);
        else Error(c, "this expression cannot be called");
        curdst = savedst;
        return v;
    }

    Val CheckNamedCall(Call *c, Ident *id) {
        if (LookupVar(id->name))
            Error(c, cat(id->name, " is a variable, not a function"));
        if (auto fb = LookupFnVal(id->name)) {
            id->vdef = nullptr;
            return CheckFunValCall(c, *fb);
        }
        FnSpec *env = nullptr;
        vector<SFunction *> cands;
        if (auto nf = LookupLocalFnEnv(id->name, env)) {
            cands.push_back(nf);
        } else {
            auto fit = ast.functionmap.find(id->name);
            if (fit != ast.functionmap.end()) cands = fit->second;
        }
        if (!cands.empty()) {
            for (auto sf : cands)
                if (sf->isthread)
                    Error(c, cat("thread_fn ", id->name, " is spawned with thread_spawn, "
                                 "not called"));
            Node *nopre = nullptr;
            return ResolveCall(c, cands, env, id->name, nullptr, nopre);
        }
        auto bd = LookupBuiltin(id->name);
        if (!bd) Error(c, cat("unknown function: ", id->name));
        if (bd->flags & BF_PROPERTY)
            Error(c, cat(id->name, " is a property (use a.", id->name, "), not a call"));
        return CheckBuiltin(c, *bd, c->args, nullptr);
    }

    SFunction *LookupLocalFnEnv(string_view name, FnSpec *&env) {
        for (auto fi = (int)frames.size() - 1; fi >= 0;) {
            auto &f = frames[fi];
            auto scopelimit = fi == (int)frames.size() - 1 ? (int)scopes.size()
                                                           : frames[fi + 1].scopebase;
            for (auto i = (int)localfns.size() - 1; i >= 0; i--) {
                auto &[si, sf] = localfns[i];
                if (si >= f.scopebase && si < scopelimit && sf->name == name) {
                    env = f.lexspec;
                    return sf;
                }
            }
            fi = f.lexframe;
        }
        return nullptr;
    }

    Val CheckUfcsCall(Call *c, Dot *d) {
        auto ov = CheckV(d->obj, nullptr);
        d->obj->exprtype = ov.type;
        auto rt = ov.type;
        if (rt->kind == TY_REF) {
            if (rt->ref->optional)
                Error(c, "optional value must be narrowed (if/guard/assert) before use");
            rt = rt->ref->sub;
        }
        // Built-in members first (§7.1), then free functions, then the
        // remaining builtins (a.f(b) is exactly f(a, b)).
        auto bd = LookupBuiltin(d->name);
        if (bd && (bd->flags & BF_MEMBER) && !(bd->flags & BF_PROPERTY) &&
            rt->kind == TY_ARRAY) {
            vector<Node *> argnodes = { d->obj };
            for (auto a : c->args) argnodes.push_back(a);
            d->member = bd->kind;
            auto v = CheckBuiltin(c, *bd, argnodes, &ov);
            WriteBackArgs(c, d, argnodes);
            return v;
        }
        if (rt->kind == TY_STRUCT) {
            for (auto &f : rt->struc->st->fields)
                if (!f.ispad && f.name == d->name)
                    Error(c, cat("field ", d->name, " is not callable"));
        }
        FnSpec *env = nullptr;
        vector<SFunction *> cands;
        if (auto nf = LookupLocalFnEnv(d->name, env)) {
            cands.push_back(nf);
        } else {
            auto fit = ast.functionmap.find(d->name);
            if (fit != ast.functionmap.end()) cands = fit->second;
        }
        if (!cands.empty()) return ResolveCall(c, cands, env, d->name, &ov, d->obj);
        if (bd && !(bd->flags & BF_PROPERTY)) {
            vector<Node *> argnodes = { d->obj };
            for (auto a : c->args) argnodes.push_back(a);
            auto v = CheckBuiltin(c, *bd, argnodes, &ov);
            WriteBackArgs(c, d, argnodes);
            return v;
        }
        if (bd) Error(c, cat(".", d->name, " is a property, not a call"));
        Error(c, cat("unknown function or member: ", d->name));
    }

    // Phase 1 checks arguments bottom-up for resolution; phase 2 re-checks
    // each against its concrete parameter type (adapting literals etc.).
    Val ResolveCall(Call *c, vector<SFunction *> &cands, FnSpec *env, string_view name,
                    Val *preval, Node *&prenode) {
        vector<Node *> argnodes;
        vector<Val> argvals;
        // A non-fixed lvalue argument passes by reference (§4.1): it becomes
        // `&a` before any candidate sees it. The user's own `&` on one is
        // redundant.
        auto byref = [&](Node *&a, Val &v) {
            // A resizable without a header of its own (C.2) stays a value,
            // which a slice parameter still takes whole.
            if (IsNonFixedLValue(v) && Referenceable(a, v)) a = AutoRef(a, v);
            else if (UserRefOf(a) && IsPlainRef(v.type) && ClassOf(v.type->ref->sub) != SC_FIXED)
                Warn(a, cat("redundant &: ", ExprStr(Is<Unary>(a)->child),
                            " is passed by reference without it (§4.1)"));
        };
        if (prenode) {
            auto v = *preval;
            byref(prenode, v);
            argnodes.push_back(prenode);
            argvals.push_back(v);
        }
        for (auto &a : c->args) {
            auto v = CheckV(a, nullptr);
            RequireComplete(v.type, a->line);
            byref(a, v);
            argnodes.push_back(a);
            a->exprtype = v.type;
            argvals.push_back(v);
        }
        MatchInfo best;
        auto bestcount = 0;
        string failures;
        for (auto sf : cands) {
            MatchInfo mi;
            mi.sf = sf;
            mi.env = env;
            string why;
            if (!TryMatch(sf, c, argvals, mi, why)) {
                Append(failures, "\n  candidate ", name, " at ", Where(sf->line), ": ", why);
                continue;
            }
            if (!bestcount || mi.tier < best.tier) {
                best = mi;
                bestcount = 1;
            } else if (mi.tier == best.tier) {
                bestcount++;
            }
        }
        if (bestcount > 1)
            Error(c, cat("ambiguous call to ", name, ": multiple overloads match equally well"));
        if (!bestcount) {
            // Tag dispatch (§8.2): the match-as-overload-set form.
            auto v = TryDispatch(c, cands, argnodes, argvals, name);
            if (v.type) return v;
            Error(c, cat("no matching overload for call to ", name, failures));
        }
        auto spec = GetOrCreateSpec(best, argvals, c);
        ApplyCalleeShrinks(c, spec, argvals, name);
        // Phase 2: re-check arguments against the resolved parameter types.
        // Arguments construct into fresh parameter slots, not curdst.
        {
            auto savedst = curdst;
            curdst = Dest {};
            for (size_t i = 0; i < best.paramtypes.size(); i++) {
                // A slice parameter takes the array itself: the reference the
                // argument loop made of it is undone, so the coercion is the
                // plain array-to-slice one.
                if (best.paramtypes[i]->kind == TY_SLICE)
                    if (auto u = Is<Unary>(argnodes[i]); u && u->synth) argnodes[i] = u->child;
                // A `&` at a parameter declared as a reference is redundant
                // (§4.1) -- unless it picked this overload.
                auto &p = best.sf->params[i];
                if (UserRefOf(argnodes[i]) && cands.size() == 1 && p.type &&
                    !HasGenerics(p.type) && p.type->kind == TY_REF &&
                    ClassOf(p.type->ref->sub) == SC_FIXED)
                    Warn(argnodes[i], cat("redundant &: ", ExprStr(Is<Unary>(argnodes[i])->child),
                                          " is passed by reference without it (§4.1)"));
                CheckArg(argnodes[i], best.paramtypes[i]);
            }
            curdst = savedst;
        }
        // Arguments the re-check rebound by reference replace the originals.
        {
            size_t off = 0;
            if (prenode) { prenode = argnodes[0]; off = 1; }
            for (size_t i = 0; i < c->args.size(); i++) c->args[i] = argnodes[i + off];
        }
        c->spec = spec;
        return CallResult(c, spec, argvals, best.paramtypes);
    }

    bool TryMatch(SFunction *sf, Call *c, vector<Val> &argvals, MatchInfo &mi, string &why) {
        auto P = sf->params.size();
        auto N = argvals.size();
        if (N < P) { why = "too few arguments"; return false; }
        for (auto i = P; i < N; i++) {
            if (argvals[i].type != fntype) { why = "too many arguments"; return false; }
        }
        if (c->tyargs.size() > sf->generics.size()) {
            why = "too many explicit type arguments";
            return false;
        }
        for (size_t i = 0; i < c->tyargs.size(); i++) {
            auto t = Subst(c->tyargs[i]);
            ValidateType(t, c->line, VT_LOCAL);
            mi.bindings.push_back({ sf->generics[i].name, t });
        }
        mi.paramtypes.resize(P);
        // The callee's own generic names must not resolve to same-named
        // enclosing bindings while unifying (the recursive-generic case).
        auto saveex = ownexclude;
        ownexclude = &sf->generics;
        auto paramsok = [&]() {
            for (size_t i = 0; i < P; i++) {
                auto &p = sf->params[i];
                auto &av = argvals[i];
                if (!p.type) {
                    // Untyped parameter: an anonymous type variable (§7.1),
                    // bound to the argument's exact type — a reference
                    // argument binds as a reference, like `<T>` does.
                    if (av.type == fntype) {
                        why = "function values bind to generic parameters, not untyped ones";
                        return false;
                    }
                    auto nt = NaturalType(av);
                    if (!nt) {
                        why = cat("cannot infer a type for argument ", (int64_t)i + 1);
                        return false;
                    }
                    mi.paramtypes[i] = nt;
                    mi.tier = std::max(mi.tier, 1);
                    continue;
                }
                auto ct = UnifyArg(p.type, av, mi.bindings, mi.tier);
                if (!ct) {
                    why = cat("argument ", (int64_t)i + 1, ": cannot pass ", TypeStr(av.type),
                              " as ", TypeStr(p.type));
                    return false;
                }
                mi.paramtypes[i] = ct;
            }
            return true;
        }();
        ownexclude = saveex;
        if (!paramsok) return false;
        // Leftover generics bind function values, in order (§7.6).
        vector<string_view> unbound;
        for (auto &g : sf->generics) {
            auto found = false;
            for (auto &[n, t] : mi.bindings) found |= n == g.name;
            if (!found) unbound.push_back(g.name);
        }
        auto need = (N - P) + (c->trailing ? 1 : 0);
        if (unbound.size() != need) {
            why = need ? cat((int64_t)need, " function value(s) for ",
                             (int64_t)unbound.size(), " unbound generic parameter(s)")
                       : "cannot infer all generic parameters (use explicit <...>)";
            return false;
        }
        for (size_t k = 0; P + k < N; k++)
            mi.fnvals.push_back({ unbound[k], argvals[P + k].fnv });
        if (c->trailing) {
            FnValBind fb;
            fb.fv = c->trailing;
            fb.env = frames.back().lexspec;
            mi.fnvals.push_back({ unbound.back(), fb });
        }
        return true;
    }

    // The type an argument value contributes to inference; null when it has
    // none ([] and null literals).
    TypeExpr *NaturalType(const Val &av) {
        if (av.emptyarr || av.isnull) return nullptr;
        if (av.strlit) return u8slice;
        return av.type;
    }

    // Makes parameter type pt concrete against argument av, binding this
    // call's own generics into b. Returns null when it cannot match. A
    // reference argument that fails to match as a reference retries as its
    // pointee (transparency).
    TypeExpr *UnifyArg(TypeExpr *pt, Val &av, vector<pair<string_view, TypeExpr *>> &b,
                       int &tier) {
        auto nbind = b.size();
        if (auto ct = UnifyArgRaw(pt, av, b, tier)) return ct;
        if (IsPlainRef(av.type)) {
            b.resize(nbind);  // Discard bindings from the failed attempt.
            auto dv = DecayRef(av);
            if (auto ct = UnifyArgRaw(pt, dv, b, tier)) return ct;
        }
        return nullptr;
    }

    TypeExpr *UnifyArgRaw(TypeExpr *pt, Val &av, vector<pair<string_view, TypeExpr *>> &b,
                          int &tier) {
        pt = Subst(pt);  // Enclosing functions' generics are already bound.
        if (av.isnull) {
            auto ct = SubstOwn(pt, b);
            if (!ct || HasGenerics(ct) || !IsOptional(ct)) return nullptr;
            tier = std::max(tier, 2);
            return ct;
        }
        if (av.emptyarr) {
            auto ct = SubstOwn(pt, b);
            if (!ct || HasGenerics(ct) || ct->kind != TY_ARRAY) return nullptr;
            tier = std::max(tier, 2);
            return ct;
        }
        // An lvalue meeting a reference parameter binds by reference (§4.1),
        // so a generic pointee unifies with the argument's own type.
        if (av.lvalue && pt->kind == TY_REF && pt->ref->lenstorage < 0 &&
            av.type->kind != TY_REF) {
            Val rv = av;
            rv.type = RefTo(av.type, pt->line);
            rv.lvalue = false;
            return UnifyArgRaw(pt, rv, b, tier);
        }
        auto at = NaturalType(av);
        auto hadgen = HasGenerics(pt);
        if (hadgen) {
            if (!BindTypes(pt, at, b)) {
                // The one shape-changing coercion generics see through:
                // whole-array arguments to slice parameters (§3.10).
                if (pt->kind == TY_SLICE && at->kind == TY_ARRAY)
                    BindTypes(pt->sub, at->arr->sub, b);
            }
        }
        auto ct = SubstOwn(pt, b);
        if (!ct || HasGenerics(ct)) return nullptr;
        if (TypeEq(at, ct)) {
            if (hadgen) tier = std::max(tier, 1);
            return ct;
        }
        Val tmp = av;
        if (!FitsAt(tmp, ct, true)) return nullptr;
        tier = std::max(tier, 2);
        return ct;
    }

    bool HasGenerics(TypeExpr *t) {
        switch (t->kind) {
            case TY_GENERIC: return true;
            case TY_STRUCT: for (auto a : t->struc->args) if (HasGenerics(a)) return true; return false;
            case TY_ENUM:   for (auto a : t->enu->args) if (HasGenerics(a)) return true; return false;
            case TY_ARRAY:  return HasGenerics(t->arr->sub);
            case TY_SLICE:  return HasGenerics(t->sub);
            case TY_REF:    return HasGenerics(t->ref->sub);
            case TY_VARIANT: return HasGenerics(t->var->adt);
            default: return false;
        }
    }

    // Structural binding of pt's generic leaves against concrete at. Loose:
    // the caller re-validates with FitsAt after substitution.
    bool BindTypes(TypeExpr *pt, TypeExpr *at, vector<pair<string_view, TypeExpr *>> &b) {
        switch (pt->kind) {
            case TY_GENERIC: {
                auto name = pt->named->name;
                auto bindto = at;
                if (pt->named->varmode) {
                    if (at->kind != TY_ENUM) return false;
                    if (at->enu->varmode) {
                        auto base = ast.NewType(TY_ENUM, at->line);
                        base->enu = ast.NewDetail<TypeEnum>();
                        base->enu->en = at->enu->en;
                        base->enu->args = at->enu->args;
                        bindto = base;
                    }
                }
                for (auto &[n, t] : b)
                    if (n == name) return TypeEq(t, bindto);
                b.push_back({ name, bindto });
                return true;
            }
            case TY_STRUCT:
                if (at->kind != TY_STRUCT || at->struc->st != pt->struc->st ||
                    at->struc->args.size() != pt->struc->args.size()) return false;
                for (size_t i = 0; i < pt->struc->args.size(); i++)
                    if (!BindTypes(pt->struc->args[i], at->struc->args[i], b)) return false;
                return true;
            case TY_ENUM:
                if (at->kind != TY_ENUM || at->enu->en != pt->enu->en ||
                    at->enu->args.size() != pt->enu->args.size()) return false;
                for (size_t i = 0; i < pt->enu->args.size(); i++)
                    if (!BindTypes(pt->enu->args[i], at->enu->args[i], b)) return false;
                return true;
            case TY_ARRAY:
                if (at->kind != TY_ARRAY || at->arr->akind != pt->arr->akind) return false;
                return BindTypes(pt->arr->sub, at->arr->sub, b);
            case TY_SLICE:
                if (at->kind != TY_SLICE) return false;
                return BindTypes(pt->sub, at->sub, b);
            case TY_REF:
                if (at->kind != TY_REF) return false;
                return BindTypes(pt->ref->sub, at->ref->sub, b);
            case TY_VARIANT: {
                // The template's union holds a bare name while its ADT is generic.
                auto ptname = pt->var->adt->kind == TY_ENUM ? pt->var->variant->name
                                                            : pt->var->name;
                if (at->kind != TY_VARIANT || at->var->variant->name != ptname) return false;
                return BindTypes(pt->var->adt, at->var->adt, b);
            }
            default:
                return TypeEq(pt, at);
        }
    }

    TypeExpr *SubstOwn(TypeExpr *pt, vector<pair<string_view, TypeExpr *>> &b) {
        TypeExpr *r = nullptr;
        WithBindings(b, [&]() { r = Subst(pt); });
        return r;
    }

    // Case-function tag dispatch (§8.2): calling an overload set of variant
    // types with the ADT (or a reference to it) dispatches on the tag.
    // Returns a Val with null type when no dispatch position exists.
    Val TryDispatch(Call *c, vector<SFunction *> &cands, vector<Node *> &argnodes,
                    vector<Val> &argvals, string_view name) {
        Val novv;
        novv.type = nullptr;
        auto found = -1;
        vector<MatchInfo> matches;  // Per variant, for the found position.
        TypeExpr *enumtype = nullptr;
        auto byref = false;
        for (size_t pos = 0; pos < argvals.size(); pos++) {
            auto at = argvals[pos].type;
            TypeExpr *et = nullptr;
            auto isref = false;
            if (at->kind == TY_ENUM) et = at;
            else if (IsPlainRef(at) && at->ref->sub->kind == TY_ENUM) { et = at->ref->sub; isref = true; }
            if (!et) continue;
            // Fixed-mode payloads pass by copy even through a reference, for
            // the same soundness reason as match binders (§3.5).
            isref = isref && et->enu->varmode;
            auto en = et->enu->en;
            vector<MatchInfo> vm;
            auto allok = true;
            for (auto &var : en->variants) {
                auto vt = VariantTypeOf(et, &var, c->line);
                auto argt = isref ? RefTo(vt, c->line) : vt;
                Val vv = argvals[pos];
                vv.type = argt;
                auto saved = argvals[pos];
                argvals[pos] = vv;
                MatchInfo onlymatch;
                auto count = 0;
                for (auto sf : cands) {
                    MatchInfo mi;
                    mi.sf = sf;
                    string why;
                    if (TryMatch(sf, c, argvals, mi, why)) {
                        onlymatch = mi;
                        count++;
                    }
                }
                argvals[pos] = saved;
                if (count != 1) { allok = false; break; }
                vm.push_back(onlymatch);
            }
            if (!allok) continue;
            if (found >= 0)
                Error(c, cat("call to ", name, " could dispatch on more than one argument "
                             "(v1 allows a single dispatch position)"));
            found = (int)pos;
            matches = std::move(vm);
            enumtype = et;
            byref = isref;
        }
        if (found < 0) return novv;
        // Specialize every arm; return types and the other parameters must
        // agree across the set.
        c->dispatcharg = found;
        vector<Val> armvals = argvals;
        auto en = enumtype->enu->en;
        FnSpec *first = nullptr;
        for (size_t vi = 0; vi < en->variants.size(); vi++) {
            auto &mi = matches[vi];
            auto vt = VariantTypeOf(enumtype, &en->variants[vi], c->line);
            armvals[found] = argvals[found];
            armvals[found].type = byref ? RefTo(vt, c->line) : vt;
            auto spec = GetOrCreateSpec(mi, armvals, c);
            ApplyCalleeShrinks(c, spec, armvals, name);
            if (first) {
                if (spec->rets.size() != first->rets.size())
                    Error(c, cat("case functions of ", name, " disagree on return counts"));
                for (size_t r = 0; r < spec->rets.size(); r++)
                    if (!TypeEq(spec->rets[r], first->rets[r]))
                        Error(c, cat("case functions of ", name, " disagree on return types: ",
                                     TypeStr(spec->rets[r]), " vs ", TypeStr(first->rets[r])));
                for (size_t p = 0; p < matches[vi].paramtypes.size(); p++)
                    if ((int)p != found &&
                        !TypeEq(matches[vi].paramtypes[p], matches[0].paramtypes[p]))
                        Error(c, cat("case functions of ", name,
                                     " disagree on non-dispatch parameter types"));
            } else {
                first = spec;
            }
            c->dispatch.push_back(spec);
        }
        // Phase 2 for the non-dispatch args (before CallResult, so nested
        // calls cannot clobber lastcallrets); the dispatch arg keeps its type.
        {
            auto savedst = curdst;
            curdst = Dest {};
            for (size_t i = 0; i < matches[0].paramtypes.size(); i++)
                if ((int)i != found) CheckArg(argnodes[i], matches[0].paramtypes[i]);
            curdst = savedst;
        }
        return CallResult(c, first, argvals, matches[0].paramtypes);
    }

    // ------------------------------------------------------------------
    // Specialization: find or create the FnSpec for a resolved call and
    // check its body (once) in call-graph order.

    FnSpec *GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode) {
        auto sf = mi.sf;
        // Root classes: distinct roots of ref/slice args ordered by depth.
        vector<RootArg> roots;
        vector<VarDef *> distinct;
        for (size_t i = 0; i < mi.paramtypes.size(); i++) {
            auto pt = mi.paramtypes[i];
            if (pt->kind != TY_REF && pt->kind != TY_SLICE) continue;
            auto r = CanonRoot(argvals[i].root);
            RootArg ra;
            ra.writable = argvals[i].writable;
            ra.reusable = argvals[i].reusable;
            ra.exact = argvals[i].rootexact;
            ra.growshrink = IsGrowShrinkRoot(r);
            if (ra.exact) ra.pool = PoolOf(r);
            if (!r) {
                ra.cls = 0;
            } else {
                auto idx = -1;
                // Sharing a class says the two arguments point into the same
                // array, which an inexactly rooted one does not establish: it
                // names a scope its pointee outlives, not the storage that
                // owns it (§9.5). Such an argument gets a class to itself, so
                // a class is always one array whatever the call site.
                if (ra.exact)
                    for (size_t k = 0; k < distinct.size(); k++)
                        if (distinct[k] == r) idx = (int)k;
                if (idx < 0) {
                    // Keep distinct ordered by depth so classes mean outlives-rank.
                    auto ins = distinct.size();
                    while (ins > 0 && Depth(distinct[ins - 1]) > Depth(r)) ins--;
                    distinct.insert(distinct.begin() + ins, r);
                    for (auto &rr : roots) if (rr.cls > (int)ins) rr.cls++;
                    idx = (int)ins;
                }
                ra.cls = idx + 1;
            }
            roots.push_back(ra);
        }
        for (auto spec : sf->specs) {
            if (spec->lexparent != mi.env) continue;
            if (!TypeArgsEq(spec->argtypes, mi.paramtypes)) continue;
            if (spec->fnvals.size() != mi.fnvals.size()) continue;
            auto fvok = true;
            for (size_t i = 0; i < mi.fnvals.size(); i++)
                fvok &= spec->fnvals[i].second == mi.fnvals[i].second;
            if (!fvok) continue;
            auto rootsok = spec->roots.size() == roots.size();
            for (size_t i = 0; rootsok && i < roots.size(); i++)
                rootsok &= spec->roots[i] == roots[i];
            // A back edge must reuse the in-progress spec whatever the roots
            // (§7.8): inside a cycle, references rooted at cycle locals may
            // not be stored or returned, so their identity is irrelevant, and
            // the pool parameters that may be stored are checked below to be
            // the same ones the entry call passed.
            if (!rootsok && !spec->inprogress) continue;
            // Exactness is not part of the key, so what the specialization
            // records is what every call site that reaches it agrees on.
            for (size_t i = 0; i < roots.size() && i < spec->roots.size(); i++)
                spec->roots[i].exact = spec->roots[i].exact && roots[i].exact;
            if (spec->inprogress) {
                ValidateCycle(spec, callnode);
                ValidatePoolArgs(spec, argvals, callnode);
            }
            ValidateNeeds(spec, callnode);
            return spec;
        }
        auto spec = ast.NewFnSpec();
        spec->sf = sf;
        spec->lexparent = mi.env;
        spec->argtypes = mi.paramtypes;
        spec->roots = roots;
        spec->fnvals = mi.fnvals;
        spec->bindings = mi.bindings;
        sf->specs.push_back(spec);
        CheckSpecBody(spec, &argvals, callnode->line);
        return spec;
    }

    void ValidateCycle(FnSpec *spec, Node *callnode) {
        if (!spec->sf->isrec)
            Error(callnode, cat("recursive call cycle through ", spec->sf->name,
                                ", which is not declared `recursive fn` (§7.8)"));
        auto fi = FrameOfSpec(spec);
        if (fi < 0) fi = 0;
        for (auto i = fi; i < (int)frames.size(); i++) {
            auto s = frames[i].spec;
            if (!s || !frames[i].sf || frames[i].isfunval) continue;
            s->incycle = true;
            for (auto &p : frames[i].sf->params)
                if (!p.type)
                    Error(callnode, cat("function ", frames[i].sf->name, " is in a recursive "
                                        "cycle and needs fully explicit parameter types"));
            if (s->has_nonfixed_local)
                Error(s->nonfixedline, cat("function ", frames[i].sf->name, " is in a "
                                           "recursive cycle and may not own non-fixed-size "
                                           "locals (§7.8)"));
        }
        // A cycle function without an explicit return type is committed to
        // returning nothing at the back edge; a later `return v` then errors
        // with a mismatch (return-type inference cannot cross the back edge).
        if (!spec->retsknown) spec->retsknown = true;
    }

    // The root a synthetic parameter class stands for, followed back through
    // the call sites that created it to a real variable (or null for static
    // data).
    static VarDef *UltimateRoot(VarDef *v) {
        while (v && v->classfrom) v = CanonRoot(v->classfrom);
        return v;
    }

    // References in a pool class (VarDef::poolclass) may be stored inside a
    // recursive cycle, which is sound only while every activation's pool
    // parameters name the pools the entry call passed: the reused spec's
    // stores were proven against those. A back edge that passes a different
    // pool, or swaps two, is rejected here.
    void ValidatePoolArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode) {
        for (size_t i = 0; i < spec->params.size() && i < argvals.size(); i++) {
            auto pr = spec->params[i]->refroot;
            if (!pr) continue;
            // A named pool is part of the specialization key everywhere else,
            // but a back edge reuses the in-progress spec whatever its roots.
            if (pr->classpool &&
                (!argvals[i].rootexact || PoolOf(CanonRoot(argvals[i].root)) != pr->classpool))
                Error(callnode, cat("recursive call passes ", spec->params[i]->name,
                                    " rooted outside ", pr->classpool->name,
                                    ", which the cycle's entry call rooted there (§3.9)"));
            if (!pr->poolclass) continue;
            if (!argvals[i].rootexact ||
                UltimateRoot(pr) != UltimateRoot(CanonRoot(argvals[i].root)))
                Error(callnode, cat("recursive call passes ", spec->params[i]->name,
                                    " rooted differently from the cycle's entry call, which "
                                    "stored references into it (§7.8)"));
        }
    }

    // `return from` targets recorded by a callee must be live on every
    // compile-time call path (§7.9); cached reuse re-validates here.
    void ValidateNeeds(FnSpec *spec, Node *callnode) {
        for (auto t : spec->needs) {
            auto found = -1;
            for (auto i = (int)frames.size() - 1; i >= 0; i--)
                if (frames[i].sf == t && !frames[i].isfunval) { found = i; break; }
            if (found < 0)
                Error(callnode, cat("call to ", spec->sf->name, " requires an enclosing call "
                                    "of ", t->name, " (it does `return ... from ", t->name,
                                    "`)"));
            for (auto i = found + 1; i < (int)frames.size(); i++)
                if (frames[i].spec) frames[i].spec->needs.insert(t);
        }
    }

    // The §7.8 cycle return-root analysis, handed the one piece of checker
    // state it cannot derive from the syntax: the root a variable holds.
    CycleRoots Cycles() {
        return CycleRoots(ast, cycleroot, [this](VarDef *vd, bool isref) {
            return CanonRoot(isref ? RefRootOf(vd) : vd);
        });
    }

    // A fixed-size value C takes by value (§7.10): a scalar, bool, or a flat
    // fixed struct or array (packed, no references).
    bool ExternValueOk(TypeExpr *t, string &why) {
        switch (t->kind) {
            case TY_INT:
                if (t->intstorage == IS_VARINT) { why = "varints have no C form"; return false; }
                return true;
            case TY_FLT: case TY_BOOL: return true;
            case TY_REF: case TY_SLICE: why = "nested references and slices do not cross"; return false;
            default: break;
        }
        if (ClassOf(t) != SC_FIXED) { why = "only fixed-size values cross by value"; return false; }
        if (!IsFlat(t)) { why = "values holding references do not cross"; return false; }
        return true;
    }

    bool ExternParamOk(TypeExpr *t, string &why) {
        if (t->kind == TY_REF) {
            if (t->ref->optional || t->ref->lenstorage >= 0) {
                why = "only plain references cross";
                return false;
            }
            auto s = t->ref->sub;
            if (s->kind == TY_ARRAY && s->arr->akind == A_GROW && IsU8(s->arr->sub)) return true;
            return ExternValueOk(s, why);
        }
        if (t->kind == TY_SLICE) return ExternValueOk(t->sub, why);
        return ExternValueOk(t, why);
    }

    // An extern declaration (§7.10) has typed parameters of C-crossing
    // shapes, at most one fixed-size return, and no body to check.
    void CheckExternSpec(FnSpec *spec) {
        auto sf = spec->sf;
        if (!sf->generics.empty())
            Error(sf->line, cat("extern fn ", sf->name, " cannot be generic"));
        for (size_t i = 0; i < sf->params.size(); i++) {
            auto &p = sf->params[i];
            if (!p.type) Error(sf->line, cat("extern fn ", sf->name, ": parameter ", p.name,
                                             " needs a type"));
            auto pt = spec->argtypes[i];
            ValidateType(pt, sf->line, VT_PARAM);
            string why;
            if (!ExternParamOk(pt, why))
                Error(sf->line, cat("extern fn ", sf->name, ": parameter ", p.name, " of type ",
                                    TypeStr(pt), " cannot cross to C: ", why));
            auto vd = ast.NewVarDef();
            vd->name = p.name;
            vd->type = pt;
            vd->line = sf->line;
            vd->isparam = true;
            vd->ownerspec = spec;
            spec->params.push_back(vd);
        }
        if (sf->rets.size() > 1)
            Error(sf->line, cat("extern fn ", sf->name, " returns at most one value"));
        spec->rets.clear();
        for (auto rt : sf->rets) {
            auto t = Subst(rt);
            ValidateType(t, sf->line, VT_LOCAL);
            string why;
            if (!ExternValueOk(t, why))
                Error(sf->line, cat("extern fn ", sf->name, " cannot return ", TypeStr(t), ": ",
                                    why));
            spec->rets.push_back(t);
        }
        spec->retsknown = true;
        spec->checkedreturn = true;
        spec->checked = true;
        spec->inprogress = false;
    }

    void CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline) {
        auto sf = spec->sf;
        if (sf->isextern) { CheckExternSpec(spec); return; }
        spec->inprogress = true;
        Frame f;
        f.sf = sf;
        f.spec = spec;
        f.lexspec = spec;
        f.lexframe = spec->lexparent ? FrameOfSpec(spec->lexparent) : -1;
        f.scopebase = (int)scopes.size();
        f.varbase = (int)vars.size();
        f.callline = callline;
        frames.push_back(f);
        auto savereach = reachable;
        auto savedst = curdst;
        reachable = true;
        curdst = Dest {};
        PushScope(SK_FN);
        // Parameters. For reference/slice parameters, a synthetic root
        // VarDef per call-site root class carries the caller-side depth.
        vector<VarDef *> classroots(spec->roots.size() + 1, nullptr);
        auto rootidx = 0;
        for (size_t i = 0; i < sf->params.size(); i++) {
            auto &p = sf->params[i];
            auto pt = spec->argtypes[i];
            ValidateType(pt, sf->line, VT_PARAM);
            auto vd = NewVar(p.name, pt, sf->line, p.isvar);
            vd->isparam = true;
            vd->assigned = true;
            if (pt->kind == TY_REF || pt->kind == TY_SLICE) {
                auto &ra = spec->roots[rootidx++];
                if (ra.cls == 0) {
                    vd->refroot = nullptr;  // Static data.
                } else {
                    if (!classroots[ra.cls]) {
                        auto rv = ast.NewVarDef();
                        rv->name = p.name;
                        rv->depth = argvals ? Depth(CanonRoot((*argvals)[i].root)) : 0;
                        rv->classfrom = argvals ? CanonRoot((*argvals)[i].root) : nullptr;
                        rv->poolclass = true;
                        rv->classpool = ra.pool;
                        rv->growshrink = ra.growshrink;
                        classroots[ra.cls] = rv;
                    }
                    // Members of one class share a root, so they agree on the
                    // pool; a member that names none settles it for all.
                    if (!ra.pool) classroots[ra.cls]->classpool = nullptr;
                    // A pool class holds only references to resizable-class
                    // values; a slice or a reference to anything smaller may
                    // point at a cycle function's own storage.
                    if (pt->kind != TY_REF || ClassOf(pt->ref->sub) != SC_RESIZABLE ||
                        !ra.exact)
                        classroots[ra.cls]->poolclass = false;
                    vd->refroot = classroots[ra.cls];
                }
                vd->refrootknown = true;
                // Every member of a class points into one array (see
                // GetOrCreateSpec), so within this body the class names that
                // array. Whether it is the array some *other* class names is a
                // different question, and only ra.exact answers it.
                vd->refrootexact = true;
                vd->refwritable = ra.writable;
                vd->refreusable = ra.reusable;
            }
            spec->params.push_back(vd);
        }
        if (sf->has_rets) {
            for (auto rt : sf->rets) {
                auto ct = Subst(rt);
                ValidateType(ct, sf->line, VT_RET);
                spec->rets.push_back(ct);
            }
            spec->retsknown = true;
        }
        spec->retroots.resize(16, nullptr);
        spec->retrootexact.resize(16, false);
        spec->retwritable.resize(16, false);
        spec->retrootseeded.resize(16, false);
        spec->retrootset.resize(16, false);
        // A cycle's back edges reach this specialization before any of its own
        // returns are checked, so predict their roots first (§7.8).
        if (sf->isrec && spec->retsknown) Cycles().Seed(spec);
        spec->body = (Block *)sf->body->Clone(ast);
        // The body: statements plus a value-producing tail (treated exactly
        // like `return tail`). An else-less if tail cannot be a value, so a
        // void function may end in one.
        for (auto st : spec->body->stmts) CheckStmt(st);
        if (spec->body->tail) {
            auto tail = spec->body->tail;
            auto asvalue = !(spec->retsknown && spec->rets.empty());
            if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) asvalue = false;
            if (auto g = Is<Guard>(tail)) { (void)g; asvalue = false; }
            if (!asvalue) {
                CheckStmtExpr(tail);
                if (reachable && spec->retsknown && !spec->rets.empty())
                    Error(tail, cat("function ", sf->name, " must return value(s)"));
            } else {
                auto expected = spec->retsknown && spec->rets.size() == 1 ? spec->rets[0]
                                                                          : nullptr;
                auto tv = CheckValue(spec->body->tail, expected);
                tail = spec->body->tail;
                if (reachable) {
                    if (tv.type->kind == TY_VOID) {
                        if (spec->retsknown && !spec->rets.empty())
                            Error(tail, cat("function ", sf->name, " must return value(s)"));
                    } else {
                        vector<Val> vals = { tv };
                        RecordReturn(spec, vals, tail);
                        reachable = false;
                    }
                }
            }
        }
        if (reachable) {
            if (spec->retsknown && !spec->rets.empty())
                Error(sf->body, cat("function ", sf->name,
                                    " can fall off the end without returning value(s)"));
            if (!spec->retsknown) spec->retsknown = true;  // No returns at all: void.
        }
        if (!spec->retsknown) spec->retsknown = true;
        PopScope();
        frames.pop_back();
        reachable = savereach;
        curdst = savedst;
        spec->inprogress = false;
        spec->checked = true;
    }

    // Shared by `return` statements and body tails: agree the values with
    // the target's return types (setting them on first sight), and record
    // reference roots for the caller to map (§9.2).
    void RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at) {
        // A single call forwards all its return values.
        vector<TypeExpr *> types;
        for (auto &v : vals) types.push_back(v.type);
        if (!tspec->retsknown) {
            for (auto &v : vals) {
                if (v.type->kind == TY_VOID)
                    Error(at, "cannot return a valueless expression");
                tspec->rets.push_back(v.type);
            }
            tspec->retsknown = true;
        } else {
            if (vals.size() != tspec->rets.size())
                Error(at, cat("returning ", (int64_t)vals.size(), " value(s), function ",
                              tspec->sf ? tspec->sf->name : string_view("?"), " has ",
                              (int64_t)tspec->rets.size()));
        }
        for (size_t i = 0; i < vals.size(); i++) {
            auto rt = tspec->rets[i];
            auto rk = rt->kind;
            if (rk != TY_REF && rk != TY_SLICE) continue;
            // A null return names no root: it agrees with every other return.
            if (vals[i].isnull) continue;
            auto root = CanonRoot(vals[i].root);
            // Anything whose storage the callee's frame owns dies on return;
            // reference parameters' pointee roots are synthetic per-class
            // VarDefs (no ownerspec), so they pass and map at the call site.
            if (root && root->ownerspec == tspec)
                Error(at, cat("returning a reference rooted in ", root->name,
                              ", which dies with this function (§9.2)"));
            if (root && root == temproot)
                Error(at, "returning a reference into a temporary");
            if (root && root == cycleroot)
                Error(at, "returning the result of a recursive call whose returned "
                          "reference's root the cycle's returns do not determine (§7.8)");
            if (i < tspec->retroots.size() && i < tspec->retrootseeded.size()) {
                if (tspec->retrootset[i] && tspec->retroots[i] != root)
                    Error(at, "returns disagree on the returned reference's root "
                              "(not yet supported; use one source)");
                tspec->retrootset[i] = true;
                if (auto bad = Cycles().ReturnConflict(tspec, i, root, vals[i].rootexact);
                    !bad.empty())
                    Error(at, bad);
                tspec->retroots[i] = root;
                tspec->retrootexact[i] = vals[i].rootexact;
                tspec->retwritable[i] = vals[i].writable;
                tspec->retrootseeded[i] = false;
            }
        }
        tspec->checkedreturn = true;
    }

    Val CallResult(Call *c, FnSpec *spec, vector<Val> &argvals, vector<TypeExpr *> &paramtypes) {
        c->rettypes = spec->rets;
        lastcallrets.clear();
        for (size_t i = 0; i < spec->rets.size(); i++) {
            Val v;
            v.type = spec->rets[i];
            if (v.type->kind == TY_REF || v.type->kind == TY_SLICE) {
                auto rr = i < spec->retroots.size() ? spec->retroots[i] : nullptr;
                auto rex = i < spec->retrootexact.size() && spec->retrootexact[i];
                v.writable = i < spec->retwritable.size() && spec->retwritable[i];
                if (!rr) {
                    // A back edge whose target has neither recorded nor
                    // predicted this root: unknown, which is not static data.
                    auto known = spec->checkedreturn ||
                                 (i < spec->retrootseeded.size() && spec->retrootseeded[i]);
                    v.root = spec->inprogress && !known ? cycleroot : nullptr;
                } else if (!rr->isglobal && !rr->ownerspec) {
                    // A synthetic per-class parameter root: map back to this
                    // call site's argument root.
                    v.root = rr;
                    v.rootexact = rex;
                    for (size_t p = 0; p < spec->params.size(); p++) {
                        if (spec->params[p]->refroot == rr) {
                            if (p < argvals.size()) {
                                v.root = CanonRoot(argvals[p].root);
                                v.rootexact = rex && argvals[p].rootexact;
                                v.rootfrom = argvals[p].rootfrom;
                                v.writable = argvals[p].writable;
                            }
                            break;
                        }
                    }
                } else {
                    v.root = rr;  // A global or a captured outer local.
                    v.rootexact = rex;
                }
            } else {
                v.root = temproot;
                v.writable = false;
            }
            lastcallrets.push_back(v);
        }
        (void)paramtypes;
        if (spec->rets.empty()) return VoidVal();
        return lastcallrets[0];
    }

    void CheckReturn(Return *r) {
        // Which function does this exit? `from f` names one on the current
        // compile-time path; a plain return inside a function value exits the
        // lexically enclosing named function (§7.6, §7.9).
        auto tf = -1;
        if (!r->from.empty()) {
            for (auto i = (int)frames.size() - 1; i >= 1; i--)
                if (!frames[i].isfunval && frames[i].sf && frames[i].sf->name == r->from) {
                    tf = i;
                    break;
                }
            if (tf < 0)
                Error(r, cat("return from ", r->from, ": no enclosing call of ", r->from,
                             " on this compile-time call path"));
        } else if (frames.back().isfunval) {
            tf = FrameOfSpec(frames.back().lexspec);
            if (tf < 0) Error(r, "cannot resolve the enclosing function of this value");
        } else {
            tf = (int)frames.size() - 1;
            if (!frames[tf].sf) Error(r, "return outside of a function");
        }
        auto tspec = frames[tf].spec;
        r->target = frames[tf].sf;
        for (auto i = tf + 1; i < (int)frames.size(); i++)
            if (frames[i].spec) frames[i].spec->needs.insert(frames[tf].sf);
        // Values.
        vector<Val> vals;
        auto expectone = [&](size_t i) -> TypeExpr * {
            return tspec->retsknown && i < tspec->rets.size() ? tspec->rets[i] : nullptr;
        };
        inreturn = true;
        if (r->vals.size() == 1) {
            auto v = CheckValue(r->vals[0], tspec->retsknown && tspec->rets.size() == 1
                                                ? tspec->rets[0] : nullptr);
            if (auto call = Is<Call>(r->vals[0]); call && call->rettypes.size() > 1) {
                vals = lastcallrets;  // Forward a multi-value call.
            } else {
                if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
                vals.push_back(v);
            }
        } else {
            for (size_t i = 0; i < r->vals.size(); i++) {
                auto v = CheckValue(r->vals[i], expectone(i));
                if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
                vals.push_back(v);
            }
        }
        inreturn = false;
        if (vals.empty() && tspec->retsknown && !tspec->rets.empty())
            Error(r, cat("function ", frames[tf].sf->name, " must return value(s)"));
        if (!vals.empty() || !tspec->retsknown) {
            // For long-distance returns, references must not be rooted in
            // frames that unwind; conservatively require globals/static.
            if (tf != (int)frames.size() - 1 && !frames.back().isfunval) {
                for (auto &v : vals) {
                    auto rk = v.type->kind;
                    if ((rk == TY_REF || rk == TY_SLICE) && v.root && !v.root->isglobal)
                        Error(r, "a long-distance return may only carry references to "
                                 "globals or static data");
                }
            }
            RecordReturn(tspec, vals, r);
        }
        reachable = false;
    }

    // ------------------------------------------------------------------
    // Struct and variant literals (§4.2). The per-node entry is
    // StructLit::Check at the end of this file.

    // `selft` is the type of the value this literal constructs (the enum type
    // for a variant literal in fixed enum mode), which is what `self` names.
    void CheckInits(StructLit *sl, vector<Field> &fields, vector<TypeExpr *> &ftypes,
                    string_view what, TypeExpr *selft) {
        auto named = !sl->inits.empty() && !sl->inits[0].name.empty();
        vector<bool> got(fields.size(), false);
        auto pos = 0;
        for (auto &fi : sl->inits) {
            auto idx = -1;
            if (named) {
                for (auto i = 0; i < (int)fields.size(); i++)
                    if (!fields[i].ispad && fields[i].name == fi.name) { idx = i; break; }
                if (idx < 0) Error(fi.val, cat(what, " has no field ", fi.name));
                if (got[idx]) Error(fi.val, cat("duplicate initializer for field ", fi.name));
                // Declaration order is required (S4.2): values construct
                // front-to-back, so out-of-order names would obfuscate either
                // evaluation order or cost.
                for (auto i = idx + 1; i < (int)fields.size(); i++)
                    if (got[i])
                        Error(fi.val, cat("field initializers must follow declaration "
                                          "order: ", fi.name, " comes before ",
                                          fields[i].name));
            } else {
                while (pos < (int)fields.size() && fields[pos].ispad) pos++;
                if (pos >= (int)fields.size())
                    Error(fi.val, cat("too many initializers for ", what));
                idx = pos++;
            }
            got[idx] = true;
            sl->fieldindices.push_back(idx);
            if (Is<SelfRef>(fi.val)) { CheckSelfInit(fi.val, ftypes[idx], selft); continue; }
            CheckValue(fi.val, ftypes[idx]);
        }
        for (auto i = 0; i < (int)fields.size(); i++) {
            if (fields[i].ispad || got[i]) continue;
            // Optional fields default to null (there is no null literal to
            // spell it with); anything else needs a declared default.
            if (!fields[i].defaultval && !IsOptional(ftypes[i]))
                Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                              " (it has no default)"));
        }
    }

    // `self` in a field initializer: the field must hold a non-optional
    // relative reference to the very value being constructed (§3.9), which is
    // the one reference to it that exists before the value does. Optional
    // relative references are excluded because offset 0 is their null.
    void CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft) {
        if (ft->kind != TY_REF || ft->ref->lenstorage < 0)
            Error(n, cat("self initializes relative-reference fields (T&<u32> and friends), "
                         "not ", TypeStr(ft)));
        if (ft->ref->optional)
            Error(n, cat("self cannot initialize the optional relative reference ",
                         TypeStr(ft), ": offset 0 is its null (§3.9)"));
        if (!TypeEq(ft->ref->sub, selft))
            Error(n, cat("self here is a value of type ", TypeStr(selft), ", which does not "
                         "fit a field of type ", TypeStr(ft)));
        // An `in pool` self is the value's own offset in the pool, so unlike a
        // self-relative one it only means anything where the literal is being
        // built: inside that pool.
        if (ft->ref->pool && (!curdst.exact || PoolOf(curdst.root) != ft->ref->pool))
            Error(n, cat("self initializes ", TypeStr(ft), " only in a literal being built "
                         "inside ", ft->ref->pool->name, " (a push, an alloc, or an element "
                         "store), since it stores the value's own offset in it (§3.9)"));
        // A resizable pointee needs a header the offset cannot carry; the root
        // rule keeps every other relative reference away from one, but a
        // self-reference satisfies that rule by construction.
        if (ClassOf(selft) == SC_RESIZABLE)
            Error(n, cat("self cannot be stored relative: ", TypeStr(selft),
                         " is resizable, and a relative reference is an offset alone (§3.9)"));
        n->exprtype = ft;
    }

    // ------------------------------------------------------------------
    // Statements.

    void CheckStmt(Node *n) {
        if (auto vd = Is<VarDecl>(n)) { CheckVarDecl(vd, false); return; }
        if (auto a = Is<Assign>(n)) { CheckAssign(a); return; }
        if (auto x = Is<IncDec>(n)) { CheckIncDec(x); return; }
        if (auto fd = Is<FnDecl>(n)) {
            // Nested function: visible from here to the end of the scope;
            // checked when called, specialized per caller (§7.5).
            localfns.push_back({ (int)scopes.size() - 1, fd->sf });
            return;
        }
        CheckStmtExpr(n);
    }

    // An expression in statement position: control constructs want no value;
    // other values are computed and discarded.
    void CheckStmtExpr(Node *n) {
        if (auto x = Is<IfExpr>(n)) { CheckIf(x, nullptr, false); n->exprtype = ast.voidtype; return; }
        if (auto x = Is<MatchExpr>(n)) { CheckMatch(x, nullptr, false); n->exprtype = ast.voidtype; return; }
        if (auto x = Is<EarlyBlock>(n)) { CheckEarlyBlock(x, nullptr, false); n->exprtype = ast.voidtype; return; }
        if (auto x = Is<LoopExpr>(n)) { CheckLoop(x, nullptr, false); n->exprtype = ast.voidtype; return; }
        CheckValue(n, nullptr);
    }

    void CheckVarDecl(VarDecl *vd, bool global) {
        TypeExpr *ann = nullptr;
        if (vd->type) {
            ann = Subst(vd->type);
            ValidateType(ann, vd->line, global ? VT_GLOBAL : VT_LOCAL);
        }
        auto MakeDef = [&](size_t i) -> VarDef * {
            if (global) return vd->defs[i];  // Pre-created by the driver.
            auto d = ast.NewVarDef();
            d->name = vd->names[i];
            d->line = vd->line;
            d->isvar = vd->isvar;
            d->reusable = vd->reusable;
            d->depth = CurDepth();
            d->ownerspec = frames.back().spec;
            return d;
        };
        auto Finish = [&](VarDef *d, TypeExpr *t, const Val *v) {
            if (t->kind == TY_VOID) Error(vd, "initializer has no value");
            if (t->kind == TY_FN)
                Error(vd, "function values are compile-time only and cannot be stored (§7.6)");
            d->type = t;
            if (v && (t->kind == TY_REF || t->kind == TY_SLICE)) BindRefProvenance(d, *v);
            if (vd->reusable) {
                if (!vd->isvar) Error(vd, "reusable requires var");
                if (!IsArrayKind(t, A_GROW) || ClassOf(t->arr->sub) != SC_FIXED)
                    Error(vd, "reusable applies to grow-only arrays of fixed-size "
                              "elements (§5.4)");
            }
            NoteNonfixedLocal(t, vd->line, global);
            if (!global) {
                vd->defs.push_back(d);
                vars.push_back(d);
            }
        };
        if (vd->inits.empty()) {
            if (!ann) Error(vd, "a declaration without an initializer needs a type");
            if (!UninitOK(ann))
                Error(vd, cat("a value of type ", TypeStr(ann),
                              " must be constructed at its declaration (§4.2)"));
            for (size_t i = 0; i < vd->names.size(); i++) {
                auto d = MakeDef(i);
                d->assigned = false;
                Finish(d, ann, nullptr);
            }
            return;
        }
        if (vd->inits.size() == 1 && vd->names.size() > 1) {
            // let a, b = f();
            if (ann) Error(vd, "a type annotation is not supported on multi-value bindings");
            CheckValue(vd->inits[0], nullptr);
            auto call = Is<Call>(vd->inits[0]);
            if (!call || call->rettypes.size() != vd->names.size())
                Error(vd, cat((int64_t)vd->names.size(), " names need that many values"));
            auto rets = lastcallrets;
            for (size_t i = 0; i < vd->names.size(); i++) {
                auto d = MakeDef(i);
                d->assigned = true;
                // Reference returns decay in inference, like everywhere.
                auto rv = DecayRef(rets[i]);
                Finish(d, rv.type, &rv);
            }
            return;
        }
        if (vd->inits.size() != vd->names.size())
            Error(vd, cat((int64_t)vd->names.size(), " name(s) with ",
                          (int64_t)vd->inits.size(), " initializer(s)"));
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = MakeDef(i);
            auto savedst = curdst;
            curdst = Dest { d, true };
            curdst.varbind = ann && (ann->kind == TY_REF || ann->kind == TY_SLICE);
            Val v;
            auto refinit = Is<Unary>(vd->inits[i]);
            if (!ann && refinit && refinit->op == T_BITAND) {
                // `let r = &x;` keeps the reference (an explicit &); every
                // other un-annotated initializer decays to the pointee.
                v = CheckV(vd->inits[i], nullptr);
                vd->inits[i]->exprtype = v.type;
                if (IsPlainRef(v.type) && ClassOf(v.type->ref->sub) != SC_FIXED)
                    Warn(vd->inits[i], cat("redundant &: ", ExprStr(refinit->child),
                                           " binds by reference without it (§4.1)"));
            } else {
                v = CheckValue(vd->inits[i], ann);
                // An un-annotated binding of a non-fixed lvalue is a reference
                // to it (§4.1), like an untyped parameter's.
                if (!ann && IsNonFixedLValue(v)) vd->inits[i] = AutoRef(vd->inits[i], v);
            }
            curdst = savedst;
            if (v.emptyarr && !ann) {
                // `var out = [];` -- a grow-only array whose element type the
                // first push, append or assignment into it supplies (§4.2).
                if (!vd->isvar)
                    Error(vd->inits[i], "a let bound to [] can never receive elements; "
                                        "annotate its type, or make it a var");
                v.type = PendingArray(vd->inits[i]->line);
                vd->inits[i]->exprtype = v.type;
                v.emptyarr = false;
            }
            if (v.isnull && !ann)
                Error(vd->inits[i], "null needs an annotated optional type");
            if (!ann) NoRelRefCopy(vd->inits[i], v.type);
            d->assigned = true;
            // A `let` has exactly one value, so its initializer's
            // non-negativity is the name's for good (§6.1). A `var` can be
            // assigned anything later.
            d->nonneg = !vd->isvar && v.nonneg;
            Finish(d, ann ? ann : v.type, &v);
        }
    }

    void NoteNonfixedLocal(TypeExpr *t, Line l, bool global) {
        if (global) return;
        if (ClassOf(t) == SC_FIXED) return;
        auto spec = frames.back().spec;
        if (!spec) return;
        if (!spec->has_nonfixed_local) {
            spec->has_nonfixed_local = true;
            spec->nonfixedline = l;
        }
        if (spec->incycle || spec->sf->isrec)
            Error(l, cat("function ", spec->sf->name, " is (in) a recursive cycle and may "
                         "not own non-fixed-size locals (§7.8)"));
    }

    // §4.4: which lvalues accept `=` after construction.
    void AssignableClassCheck(TypeExpr *t, Node *at) {
        auto cls = ClassOf(t);
        if (cls == SC_VARIABLE && !(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED))
            Error(at, cat("a value of type ", TypeStr(t),
                          " is frozen at construction (§4.4); rebuild its container instead"));
    }

    void CheckAssign(Assign *a) {
        auto lv = CheckLValue(a->lval);
        if (a->op == T_DOTASSIGN) { CheckRebind(a, lv); return; }
        if (lv.isvarint)
            Error(a, "varint fields are written only at construction (§3.6)");
        if (lv.type->kind == TY_REF && lv.type->ref->lenstorage == IS_VARINT)
            Error(a, "varint-width relative references are written only at construction");
        // References are transparent: `=` through a reference-typed location
        // (a narrowed optional included, since lv.type is the narrowed type)
        // writes the pointee; rebinding is `.=`.
        if (IsPlainRef(lv.type)) {
            if (a->op == T_ASSIGN) PointeeAssign(a, lv);
            else CompoundAssign(a, lv, lv.type->ref->sub, PointeeWritable(lv, a));
            a->pointee = true;
            return;
        }
        if (IsOptional(lv.type))
            Error(a, "optional value must be narrowed before writing through it, "
                     "or rebound with .=");
        if (a->op != T_ASSIGN) {
            CompoundAssign(a, lv, lv.type, lv.writable);
            if (lv.var) RequireAssigned(lv.var, a);
            return;
        }
        // Plain value assignment.
        auto target = lv.var ? lv.var->type : lv.type;
        if (lv.var && !lv.var->assigned) {
            // First assignment of an uninitialized local constructs it.
        } else {
            if (!lv.writable)
                Error(a, "cannot assign through this path (let, or non-writable "
                         "provenance, §9.5)");
            AssignableClassCheck(target, a);
        }
        if (IsPendingArray(target)) {
            // `var x = []; x = other;` completes x from the assigned array.
            auto av = DecayRef(CheckV(a->rhs, nullptr));
            auto t2 = av.type;
            TypeExpr *selem = nullptr;
            if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
            else if (t2->kind == TY_SLICE) selem = t2->sub;
            if (av.strlit) selem = ast.inttypes[IS_U8];
            if (!selem || av.emptyarr)
                Error(a, "cannot infer the element type of this array from this value");
            CompletePending(target, selem, a->line);
        }
        // Assigning a resizable array whole replaces its elements: a shrink
        // to anything referring into it (§5.1, §5.2).
        if (target->kind == TY_ARRAY && (!lv.var || lv.var->assigned)) {
            auto root = lv.var ? lv.var : CanonRoot(lv.root);
            if (target->arr->akind == A_GROW) {
                if (!root || !root->type)
                    Error(a, "cannot assign a grow-only array through a reference: a shrink of "
                             "a grow-only array applies to a local of the function that owns "
                             "it (§5.1)");
                GrowOnlyShrinkAt(a, true, "assign", root);
            } else if (target->arr->akind == A_GROWSHRINK) {
                ShrinkGrowShrink(a, cat("assign ", ExprStr(a->lval)), root, ExprStr(a->lval));
            }
        }
        auto savedst = curdst;
        curdst = Dest { lv.root, lv.rootexact };
        curdst.varbind = lv.var && (target->kind == TY_REF || target->kind == TY_SLICE);
        auto v = CheckValue(a->rhs, target);
        curdst = savedst;
        if (v.type->kind == TY_VOID && reachable)
            Error(a, "the right-hand side has no value");
        if (lv.var) {
            // Slice variables carry their value's provenance (refs use .=).
            if (target->kind == TY_SLICE) {
                if (!lv.var->refrootknown) BindRefProvenance(lv.var, v);
                else CheckRefRebindRoot(a, lv.var, v);
            }
            lv.var->assigned = true;
            KillNarrow(lv.var);
        }
    }

    // `.=`: rebinds the reference stored at the location (§3.8).
    void CheckRebind(Assign *a, LVal &lv) {
        auto target = lv.var ? lv.var->type : lv.type;
        if (target->kind != TY_REF)
            Error(a, cat(".= rebinds references; the target has type ", TypeStr(target)));
        // A varint-width relative reference cannot be re-encoded in place
        // (its byte length could change, §3.6/§3.9).
        if (target->ref->lenstorage == IS_VARINT)
            Error(a, "varint-width relative references are written only at construction");
        if (lv.var) {
            if (!lv.var->isvar && lv.var->assigned)
                Error(a, cat("cannot rebind let ", lv.var->name));
        } else if (!lv.writable) {
            Error(a, "cannot assign through this path (let, or non-writable "
                     "provenance, §9.5)");
        }
        auto savedst = curdst;
        curdst = lv.var ? Dest { lv.var, true } : Dest { lv.root, lv.rootexact };
        curdst.varbind = lv.var != nullptr;
        auto v = CheckV(a->rhs, target);
        auto wasplain = IsPlainRef(v.type);
        MustFit(v, a->rhs, target, false);
        a->rhs->exprtype = v.type;
        curdst = savedst;
        if (lv.var) {
            if (!lv.var->refrootknown) BindRefProvenance(lv.var, v);
            else if (!v.isnull) CheckRefRebindRoot(a, lv.var, v);
            lv.var->assigned = true;
            // Rebinding an optional settles its nullness — narrowed only when
            // the new value is provably non-null (a plain reference).
            if (target->ref->optional) {
                if (!v.isnull && wasplain) {
                    auto r = ast.NewType(TY_REF, a->line);
                    r->ref = ast.NewDetail<TypeRef>();
                    r->ref->sub = target->ref->sub;
                    lv.var->narrowed = r;
                } else {
                    lv.var->narrowed = nullptr;
                }
            }
        }
    }

    // Provenance for writing the pointee of the reference at lv.
    bool PointeeWritable(LVal &lv, Node *at) {
        if (lv.var) {
            RequireAssigned(lv.var, at);
            return lv.var->refwritable;
        }
        // Container-read: laundered writable by design (§9.5).
        return true;
    }

    void PointeeAssign(Assign *a, LVal &lv) {
        auto pt = lv.type->ref->sub;
        if (!PointeeWritable(lv, a))
            Error(a, "cannot write through this reference: non-writable provenance (§9.5)");
        if (pt->kind == TY_INT && pt->intstorage == IS_VARINT)
            Error(a, "varint fields are written only at construction (§3.6)");
        AssignableClassCheck(pt, a);
        if (pt->kind == TY_ARRAY && pt->arr->akind == A_GROW)
            Error(a, "cannot assign a grow-only array through a reference: a shrink of a "
                     "grow-only array applies to a local of the function that owns it (§5.1)");
        if (pt->kind == TY_ARRAY && pt->arr->akind == A_GROWSHRINK)
            ShrinkGrowShrink(a, cat("assign ", ExprStr(a->lval)),
                             CanonRoot(lv.var ? RefRootOf(lv.var) : lv.root), ExprStr(a->lval));
        auto savedst = curdst;
        curdst = lv.var ? Dest { RefRootOf(lv.var), RefExactOf(lv.var) }
                        : Dest { lv.root, lv.rootexact };
        auto v = CheckValue(a->rhs, pt);
        curdst = savedst;
        if (v.type->kind == TY_VOID && reachable)
            Error(a, "the right-hand side has no value");
    }

    // A reference variable keeps one root for its whole life (see header
    // note): re-assignments must carry the same root, or one at the same
    // scope depth (which is equivalent for the outlives check).
    void CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv) {
        auto nr = CanonRoot(rv.root);
        if (nr != vd->refroot && Depth(nr) != Depth(vd->refroot))
            Error(at, cat("re-binding ", vd->name, " with a reference rooted at a different "
                          "scope depth is not supported; declare a new variable"));
        if (nr == vd->refroot) {
            // The root is unchanged, so only the new value's own exactness can
            // weaken what the variable stands for.
            if (!rv.rootexact) { vd->refrootexact = false; vd->refrootfrom = rv.rootfrom; }
            return;
        }
        // A same-depth rebind keeps the lifetime bound but moves the pointee to
        // other storage, so the variable no longer names one array. A read
        // earlier in an enclosing loop has already seen this value, and cannot
        // be revisited, so its claim has to be rejected here.
        if (vd->refidentityused)
            Error(at, cat("re-binding ", vd->name, " to storage rooted at ",
                          nr ? nr->name : string_view("static data"), " after its root ",
                          vd->refroot ? vd->refroot->name : string_view("static data"),
                          " was used as the identity of a relative reference (§3.9)"));
        vd->refroot = nr;
        vd->refrootexact = false;
        vd->refrootfrom = rv.rootfrom;
    }

    void CompoundAssign(Assign *a, LVal &lv, TypeExpr *st, bool writable) {
        (void)lv;
        if (!writable)
            Error(a, "cannot assign through this path (let, or non-writable "
                     "provenance, §9.5)");
        auto isbit = a->op == T_ANDEQ || a->op == T_OREQ || a->op == T_XOREQ;
        if (st->kind == TY_INT && st->intstorage != IS_VARINT) {
            // The update computes at the target's type; the operand must
            // reach it implicitly (literal fit or widening, §6.3).
            CheckValue(a->rhs, st);
        } else if (st->kind == TY_FLT && !isbit) {
            CheckValue(a->rhs, st);
        } else if (st->kind == TY_INT) {
            Error(a, "varint fields are written only at construction (§3.6)");
        } else {
            Error(a, cat("operator ", TName(a->op), " cannot be applied to ", TypeStr(st)));
        }
    }

    void CheckIncDec(IncDec *x) {
        auto lv = CheckLValue(x->lval);
        auto st = lv.type;
        auto writable = lv.writable;
        if (IsPlainRef(lv.type)) {
            st = lv.type->ref->sub;
            writable = PointeeWritable(lv, x);
        } else if (lv.var) {
            RequireAssigned(lv.var, x);
        }
        if (!writable) Error(x, "cannot modify through this path (let, or non-writable "
                                "provenance, §9.5)");
        if (!IsIntT(st))
            Error(x, cat(TName(x->op), " requires an integer lvalue, got ", TypeStr(st)));
    }

    // ------------------------------------------------------------------
    // Builtins (§12) and array members (§3.3, §5.4).

    // One entry for every builtin (builtins.h), for both spellings — f(a, b)
    // and a.f(b) arrive with a uniform argument list (receiver first). The
    // table drives arity, receiver kind/provenance, and simple signatures;
    // BF_CUSTOM entries get dedicated code below.
    Val CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv) {
        c->builtin = d.kind;
        if (c->trailing) Error(c, cat(d.name, " takes no function value"));
        if (!(d.flags & BF_TYARGS) && !c->tyargs.empty())
            Error(c, cat(d.name, " takes no type arguments"));
        if ((int)args.size() < d.minargs || (int)args.size() > d.maxargs)
            Error(c, cat(d.name, " takes ", (int64_t)d.minargs,
                         d.minargs == d.maxargs ? string() : cat("-", (int64_t)d.maxargs),
                         " argument(s), ", (int64_t)args.size(), " given"));
        // The fully custom builtins first.
        switch (d.kind) {
            case B_PRINT:
                for (auto a : args) CheckPrintable(c, d.name, a);
                return VoidVal();
            case B_STR: {
                // str(a, b, ...): a fresh u8[>..] holding the arguments' text,
                // built at the destination like any resizable result (§7.3).
                for (auto a : args) CheckPrintable(c, d.name, a);
                auto t = ast.NewType(TY_ARRAY, c->line);
                t->arr = ast.NewDetail<TypeArray>();
                t->arr->sub = ast.inttypes[IS_U8];
                t->arr->akind = A_GROW;
                c->rettypes.push_back(t);
                Val v;
                v.type = t;
                v.root = temproot;
                return v;
            }
            case B_ASSERT:
                CheckCond(args[0]);
                NarrowCond(args[0], true);  // assert(r) narrows onwards (§3.8).
                return VoidVal();
            case B_ABORT: case B_EXIT:
                // Both end the program (§9.3), so the code after them is
                // unreachable: this is what lets a `guard ... else` diverge
                // with an abort (§6.4).
                if (d.kind == B_ABORT) CheckArg(args[0], u8slice);
                else CheckIntAny(args[0]);
                reachable = false;
                return VoidVal();
            case B_THREAD_SPAWN: {
                auto wid = Is<Ident>(args[0]);
                SFunction *wsf = nullptr;
                if (wid) {
                    auto fit = ast.functionmap.find(wid->name);
                    if (fit != ast.functionmap.end())
                        for (auto sf : fit->second) if (sf->isthread) wsf = sf;
                }
                if (!wsf) Error(c, "thread_spawn's first argument names a thread_fn");
                wid->fnref = wsf;
                wid->exprtype = fntype;
                auto spec = EnsureThreadSpec(wsf, c->line);
                if (args.size() != 1 + spec->argtypes.size())
                    Error(c, cat("thread_spawn(", wsf->name, ", ...) takes ",
                                 (int64_t)spec->argtypes.size(), " worker argument(s)"));
                auto savedst = curdst;
                curdst = Dest {};
                for (size_t i = 0; i < spec->argtypes.size(); i++)
                    CheckArg(args[1 + i], spec->argtypes[i]);
                curdst = savedst;
                c->spec = spec;
                Val v;
                v.type = ast.inttypes[IS_I64];
                return v;
            }
            case B_QPUT: {
                auto av = CheckValue(args[0], nullptr);
                if (av.type->kind == TY_VOID || av.type->kind == TY_FN || !IsFlat(av.type))
                    Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(av.type)));
                return VoidVal();
            }
            case B_QGET: case B_QPOLL: {
                if (c->tyargs.size() != 1)
                    Error(c, cat(d.name, "<T>() needs exactly one explicit type argument"));
                auto t = Subst(c->tyargs[0]);
                ValidateType(t, c->line, VT_LOCAL);
                if (!IsFlat(t))
                    Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(t)));
                c->rettypes.push_back(t);
                Val first;
                first.type = t;
                first.root = temproot;
                lastcallrets.clear();
                lastcallrets.push_back(first);
                if (d.kind == B_QPOLL) {
                    Val b2;
                    b2.type = ast.booltype;
                    c->rettypes.push_back(ast.booltype);
                    lastcallrets.push_back(b2);
                }
                return first;
            }
            case B_COPY: {
                // copy(x): a fresh value from stored one (§4.1), for the
                // destinations that never copy implicitly.
                auto av = CheckV(args[0], nullptr);
                args[0]->exprtype = av.type;
                if (av.type->kind == TY_SLICE)
                    Error(c, "copy takes a value or a reference, not a slice");
                if (!av.lvalue && !IsPlainRef(av.type))
                    Error(c, "copy of a temporary: the value is fresh already");
                auto v = DecayRef(av);
                v.lvalue = false;
                c->rettypes.push_back(v.type);
                return v;
            }
            case B_DEFAULT: {
                // default<T>(): the value a T has before anything is written
                // to it, declared field defaults applied (§4.2).
                if (c->tyargs.size() != 1)
                    Error(c, "default<T>() needs exactly one explicit type argument");
                auto t = Subst(c->tyargs[0]);
                ValidateType(t, c->line, VT_LOCAL);
                if (ClassOf(t) != SC_FIXED)
                    Error(c, cat("default<T>() needs a fixed-size type, not ", TypeStr(t)));
                string why;
                if (!HasDefault(t, why))
                    Error(c, cat("default<", TypeStr(t), ">() does not exist: ", why));
                c->rettypes.push_back(t);
                Val v;
                v.type = t;
                v.rootexact = true;   // A null optional or an empty slice: static.
                return v;
            }
            default: break;
        }
        // Member receiver validation, from the table.
        Val rv;
        TypeExpr *elem = nullptr;
        auto ak = A_FIXED;
        if (d.flags & BF_MEMBER) {
            if (precv) {
                rv = *precv;
            } else {
                rv = CheckV(args[0], nullptr);
                args[0]->exprtype = rv.type;
            }
            auto rt = rv.type;
            if (IsPlainRef(rt)) rt = rt->ref->sub;
            auto got = 0;
            if (rt->kind == TY_ARRAY) {
                ak = rt->arr->akind;
                elem = rt->arr->sub;
                switch (ak) {
                    case A_FIXED:      got = BR_FIXED; break;
                    case A_VAR:        got = BR_VAR; break;
                    case A_LIMITED:    got = BR_LIMITED; break;
                    case A_GROW:       got = BR_GROW; break;
                    case A_GROWSHRINK: got = BR_GROWSHRINK; break;
                }
            } else if (rt->kind == TY_SLICE) {
                got = BR_SLICE;
                elem = rt->sub;
            }
            if (!(got & d.recv))
                Error(c, cat(".", d.name, " is not available on ", TypeStr(rv.type)));
            if ((d.flags & BF_WRITE) && !rv.writable)
                Error(c, cat("cannot .", d.name, " through a non-writable value "
                             "(let, or non-writable provenance, §9.5)"));
            if ((d.flags & BF_REUSABLE) && !rv.reusable)
                Error(c, cat(".", d.name, " exists on reusable pools only (§5.4)"));
        }
        // A pending `var x = []` receiver learns its element type from what
        // is first pushed or appended into it (§4.2).
        if (elem && elem->kind == TY_VOID) {
            auto rt = rv.type;
            if (IsPlainRef(rt)) rt = rt->ref->sub;
            if (d.kind == B_PUSH || d.kind == B_ALLOC_INDEX || d.kind == B_ALLOC_REF) {
                auto av = DecayRef(CheckV(args[1], nullptr));
                CompletePending(rt, PendingElemFrom(av, args[1]), c->line);
            } else if (d.kind == B_APPEND || d.kind == B_FORMAT) {
                TypeExpr *selem = nullptr;
                if (d.kind == B_FORMAT) {
                    selem = ast.inttypes[IS_U8];
                } else {
                    auto av = DecayRef(CheckV(args[1], nullptr));
                    auto t2 = av.type;
                    if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
                    else if (t2->kind == TY_SLICE) selem = t2->sub;
                    if (av.strlit) selem = ast.inttypes[IS_U8];
                    if (!selem || av.emptyarr)
                        Error(c, "cannot infer the element type of this array from this value");
                }
                CompletePending(rt, selem, c->line);
            } else {
                RequireComplete(rt, c->line);
            }
            elem = rt->arr->sub;
        }
        // format(out, a, b, ...): the arguments' text appended to a growable
        // u8 array (§3.7).
        if (d.kind == B_FORMAT) {
            if (!IsU8(elem))
                Error(c, cat(".format appends text to u8 arrays, not ", TypeStr(rv.type)));
            for (size_t i = 1; i < args.size(); i++) CheckPrintable(c, d.name, args[i]);
            return VoidVal();
        }
        // A grow-only array shrinks only where nothing can still be rooted in
        // it (§5.1); pop and resize also need an element the shrink can find,
        // which a sequential array has not got.
        if (ak == A_GROW && (d.kind == B_POP || d.kind == B_RESIZE || d.kind == B_CLEAR)) {
            CheckGrowShrink(c, c->standalone, d.name, args[0], rv.type);
            if (d.kind != B_CLEAR && ClassOf(elem) != SC_FIXED)
                Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                             " is sequential (§3.3)"));
        }
        // A grow-shrink array shrinks from anywhere, provided nothing in scope
        // refers into it (§5.2).
        if (ak == A_GROWSHRINK && (d.kind == B_POP || d.kind == B_RESIZE || d.kind == B_CLEAR))
            ShrinkGrowShrink(c, cat(d.name, " ", ExprStr(args[0])), CanonRoot(rv.root),
                             ExprStr(args[0]));
        // resize has two forms (§3.3); a target below zero is caught at runtime.
        if (d.kind == B_RESIZE) {
            CheckIntAny(args[1]);
            if (args.size() == 3) ElemArg(args[2], elem, rv);
            return VoidVal();
        }
        // index_of recovers the element index a reference stands for (§3.3).
        // The reference must be an element of this very array, which is what
        // an exact root at the receiver says (§9.2); the distance is then a
        // whole number of elements and inside the length, so the division is
        // exact and nothing has to be bounds-checked.
        if (d.kind == B_INDEX_OF) {
            if (ClassOf(elem) != SC_FIXED)
                Error(c, cat(".index_of needs fixed-size elements: ", TypeStr(rv.type),
                             " is sequential (§3.3)"));
            auto av = CheckV(args[1], nullptr);
            if (av.lvalue && av.type->kind != TY_REF && TypeEq(av.type, elem))
                args[1] = AutoRef(args[1], av);
            else if (UserRefOf(args[1]))
                Warn(args[1], cat("redundant &: ", ExprStr(Is<Unary>(args[1])->child),
                                  " is passed by reference without it (§4.1)"));
            args[1]->exprtype = av.type;
            if (!IsPlainRef(av.type) || !TypeEq(av.type->ref->sub, elem))
                Error(c, cat(".index_of takes a reference to an element of ", TypeStr(rv.type),
                             ", got ", TypeStr(av.type)));
            if (!av.rootexact || CanonRoot(av.root) != CanonRoot(rv.root)) {
                auto why = av.rootexact ? string() : ReadBackWhy(av.type, av.rootfrom);
                Error(c, cat(".index_of needs a reference rooted at the array itself (§3.3); ",
                             !why.empty() ? why
                             : cat("this one is rooted at ",
                                   av.root ? CanonRoot(av.root)->name
                                           : string_view("static data"))));
            }
        }
        // Signature-driven arguments.
        auto base = (d.flags & BF_MEMBER) ? 1 : 0;
        for (auto i = 0; d.args[i]; i++) {
            auto &an = args[base + i];
            switch (d.args[i]) {
                case 'i': CheckIntAny(an); break;
                case 'f': CheckValue(an, ast.flttypes[FS_F64]); break;
                case 'b': CheckValue(an, ast.booltype); break;
                case 'e': ElemArg(an, elem, rv); break;
                case 'a': {  // An array/slice of the receiver's element type.
                    auto av = CheckV(an, nullptr);
                    an->exprtype = av.type;
                    auto t2 = av.type;
                    if (IsPlainRef(t2)) t2 = t2->ref->sub;
                    TypeExpr *selem = nullptr;
                    if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
                    if (t2->kind == TY_SLICE) selem = t2->sub;
                    if (av.strlit) selem = ast.inttypes[IS_U8];
                    if (!selem || !TypeEq(selem, elem))
                        Error(c, cat(".", d.name, " takes an array or slice of ",
                                     TypeStr(elem), ", got ", TypeStr(av.type)));
                    break;
                }
                default: assert(false);
            }
        }
        // Returns, from the table.
        Val v = VoidVal();
        switch (d.rets[0]) {
            case 0: break;
            case 'i': v.type = ast.inttypes[IS_I64]; break;
            case 'b': v.type = ast.booltype; break;
            case 'e':
                v.type = LoadType(elem);
                v.root = temproot;
                break;
            case 'r':
                v.type = RefTo(elem, c->line);
                v.root = rv.root;
                v.rootexact = rv.rootexact;
                v.writable = rv.writable;
                break;
            default: assert(false);
        }
        return v;
    }

    // An argument of print/str/format: something with a text form (§3.7) --
    // for now the scalars, bool, and u8 arrays and slices.
    void CheckPrintable(Call *c, const char *what, Node *a) {
        auto av = CheckValue(a, nullptr);
        auto t = av.type;
        auto ok = t->kind == TY_INT || t->kind == TY_FLT || t->kind == TY_BOOL;
        if (t->kind == TY_ARRAY) ok = IsU8(t->arr->sub);
        if (t->kind == TY_SLICE) ok = IsU8(t->sub);
        if (!ok) Error(c, cat(what, " takes scalars and u8 arrays/slices, not ", TypeStr(t)));
    }

    // A shrink (`pop`, `resize` down, `clear`) of a grow-only array (§5.1).
    // Everything below the stack top belongs to the array's elements for as
    // long as it lives, so handing part of the region back is safe exactly
    // when nothing can still point into it: the receiver is a local of the
    // function being checked (not a global, not a field, not reached through a
    // reference -- those have holders this pass cannot see); the call stands on
    // its own (a statement, an initializer, or the right-hand side of an
    // assignment to a variable), so no reference taken earlier in the same
    // expression outlives it; and no live value can hold a reference or slice
    // rooted at the array. Roots are only known per variable, so the scan is
    // conservative wherever they are: a value whose type contains references
    // at all counts as a possible holder, and so does a `var` reference the
    // same-depth rebinding rule (§9.2) could retarget into the array.
    void CheckGrowShrink(Node *at, bool standalone, const char *op, Node *recv,
                         TypeExpr *rtype) {
        auto id = Is<Ident>(recv);
        auto vd = id ? id->vdef : nullptr;
        if (!vd || rtype->kind != TY_ARRAY)
            Error(at, cat(op, " on a grow-only array names the array's own variable, not a "
                          "reference or an element of another value (§5.1)"));
        GrowOnlyShrinkAt(at, standalone, op, vd);
    }

    // A shrink of the grow-only array (or value holding one) that local vd
    // owns: only where nothing in scope can still refer into it (§5.1).
    void GrowOnlyShrinkAt(Node *c, bool standalone, const char *op, VarDef *vd) {
        if (vd->isglobal || vd->isparam || !frames.back().spec ||
            vd->ownerspec != frames.back().spec)
            Error(c, cat("cannot ", op, " ", vd->name,
                         ": a shrink of a grow-only array applies to a local of the function "
                         "that owns it (§5.1)"));
        if (vd->reusable)
            Error(c, cat("cannot ", op, " reusable pool ", vd->name,
                         ": its slots stay live for the freelist (§5.4)"));
        if (!standalone)
            Error(c, cat("cannot ", op, " ", vd->name,
                         " inside a larger expression: a reference taken earlier in it may "
                         "still be live, so bind the result first (§5.1)"));
        if (invalue)
            Error(c, cat("cannot ", op, " ", vd->name,
                         " inside a value-producing expression: references taken earlier in "
                         "it may still be live (§5.1)"));
        for (auto v : vars) {
            if (v == vd || !v->type) continue;
            auto t = v->type;
            if (t->kind == TY_REF || t->kind == TY_SLICE) {
                // A recorded root is exact only while the variable keeps its
                // first binding: a `var` may since have been rebound to any
                // root at the same depth, and one not bound yet can still
                // commit to this array further down a loop body.
                auto root = RefRootOf(v);
                auto holds = root == vd || (v->isvar && Depth(root) == Depth(vd)) ||
                             (!v->refrootknown && Depth(v) >= Depth(vd));
                if (!holds) continue;
            } else {
                // Any other value holds references only where a store put
                // them, which the outlives rule permits only into storage the
                // array outlives (§9.2); flat types have no room for one.
                if (IsFlat(t) || Depth(v) < Depth(vd)) continue;
            }
            Error(c, cat("cannot ", op, " ", vd->name, " while ", v->name,
                         " is in scope: it may hold a reference or slice into it (§5.1)"));
        }
    }

    string ExprStr(Node *n) {
        string s;
        n->Dump(s, 0);
        return s;
    }

    // A shrink of the grow-shrink array rooted at root (§5.2): no variable in
    // scope may refer into it. Such references are held only by variables
    // (they cannot be stored), so the scan is exact, up to a `var` reference
    // the same-depth rebinding rule could have retargeted into it.
    void CheckShrinkHolders(Node *at, const string &op, VarDef *root, const string &what) {
        VisibleVars([&](VarDef *v) {
            if (v == root || !v->type) return;
            if (v->type->kind != TY_REF && v->type->kind != TY_SLICE) return;
            // A reference to the whole array (or the value holding it) is the
            // path to it, not something a shrink invalidates.
            if (v->type->kind == TY_REF && ContainsGrowShrink(v->type->ref->sub)) return;
            auto r = RefRootOf(v);
            auto holds = r == root || (v->isvar && Depth(r) == Depth(root)) ||
                         (!v->refrootknown && Depth(v) >= Depth(root));
            if (!holds) return;
            Error(at, cat("cannot ", op, " while ", v->name, " (bound at ", Where(v->line),
                          ") is in scope: it may refer into ", what, " (§5.2)"));
        });
    }

    // Records a shrink for the callers' sake (§5.2): of a global, on the
    // specialization being checked; through a parameter class, on the
    // specialization that owns those parameters (a function value's body may
    // shrink through its enclosing function's).
    void NoteShrink(VarDef *root) {
        if (root->isglobal) {
            if (auto spec = CurRealFrame().spec) spec->shrinkglobals.insert(root);
            return;
        }
        if (root->type) return;  // A local: its owner sees every shrink directly.
        for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
            auto spec = frames[fi].spec;
            if (!spec) continue;
            auto found = false;
            for (size_t i = 0; i < spec->params.size(); i++) {
                if (RefRootOf(spec->params[i]) != root) continue;
                spec->shrinkparams.insert((int)i);
                found = true;
            }
            if (found) return;
        }
    }

    void ShrinkGrowShrink(Node *at, const string &op, VarDef *root, const string &what) {
        if (!root) return;
        CheckShrinkHolders(at, op, root, what);
        NoteShrink(root);
    }

    // The callee's shrinks of grow-shrink arrays (§5.2) are the caller's:
    // nothing in scope may refer into an argument it shrinks through or a
    // global it shrinks, and both are recorded for the caller's own callers.
    // A back edge's summary is incomplete, so it counts as shrinking every
    // grow-shrink array it can reach.
    void ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name) {
        auto pending = spec->inprogress;
        for (size_t i = 0; i < argvals.size() && i < spec->argtypes.size(); i++) {
            auto pt = spec->argtypes[i];
            auto shrinks = pending ? pt->kind == TY_REF && ContainsGrowShrink(pt->ref->sub)
                                   : spec->shrinkparams.count((int)i) > 0;
            if (!shrinks) continue;
            auto root = CanonRoot(argvals[i].root);
            if (!root) continue;
            ShrinkGrowShrink(at, cat("call ", name, ", which shrinks ", root->name), root,
                             string(root->name));
        }
        if (pending) {
            for (auto g : ast.globals)
                for (auto vd : g->defs)
                    if (vd->type && ContainsGrowShrink(vd->type))
                        ShrinkGrowShrink(at, cat("call ", name, ", which may shrink ", vd->name),
                                         vd, string(vd->name));
        } else {
            for (auto vd : spec->shrinkglobals)
                ShrinkGrowShrink(at, cat("call ", name, ", which shrinks ", vd->name), vd,
                                 string(vd->name));
        }
    }

    // Element construction targets the array's storage (relative references
    // in the element must derive from the same root, §3.9).
    void ElemArg(Node *&n, TypeExpr *elem, Val &rv) {
        auto savedst = curdst;
        curdst = Dest { rv.root, rv.rootexact };
        CheckArg(n, elem);
        curdst = savedst;
    }

    FnSpec *EnsureThreadSpec(SFunction *sf, Line l) {
        if (!sf->specs.empty()) return sf->specs[0];
        if (!sf->generics.empty()) Error(l, cat("thread_fn ", sf->name, " cannot be generic"));
        if (sf->has_rets) Error(l, cat("thread_fn ", sf->name, " cannot return values"));
        auto spec = ast.NewFnSpec();
        spec->sf = sf;
        for (auto &p : sf->params) {
            if (!p.type)
                Error(l, cat("thread_fn ", sf->name, " needs fully typed parameters"));
            auto t = Subst(p.type);
            ValidateType(t, sf->line, VT_PARAM);
            if (!IsFlat(t))
                Error(l, cat("thread_fn parameters must be flat (§11.2), not ", TypeStr(t)));
            spec->argtypes.push_back(t);
        }
        sf->specs.push_back(spec);
        CheckSpecBody(spec, nullptr, l);
        return spec;
    }

    // ------------------------------------------------------------------
    // Calling a function value F(a): the body is cloned and checked inline
    // in the lexical environment it was written in (§7.6).

    Val CheckFunValCall(Call *c, const FnValBind &fb) {
        if (c->trailing)
            Error(c, "a function value call cannot itself take a trailing block");
        if (fb.named) {
            vector<SFunction *> cands = { fb.named };
            Node *nopre = nullptr;
            return ResolveCall(c, cands, fb.env, fb.named->name, nullptr, nopre);
        }
        auto fv = fb.fv;
        vector<Val> argvals;
        for (auto a : c->args) {
            auto v = CheckV(a, nullptr);
            a->exprtype = v.type;
            argvals.push_back(v);
        }
        vector<Param> params;
        if (fv->explicit_params) {
            params = fv->params;
            if (params.size() != argvals.size())
                Error(c, cat("this function value takes ", (int64_t)params.size(),
                             " argument(s), ", (int64_t)argvals.size(), " given"));
        } else if (argvals.size() == 1) {
            Param p;
            p.name = "it";
            params.push_back(p);
        } else if (!argvals.empty()) {
            Error(c, "a block with multiple arguments needs named parameters (x, y => ...)");
        }
        // Parameter types: annotations resolve in the defining environment.
        vector<TypeExpr *> ptypes;
        for (size_t i = 0; i < params.size(); i++) {
            if (params[i].type) {
                auto t = SubstEnv(params[i].type, fb.env);
                ValidateType(t, c->line, VT_PARAM);
                ptypes.push_back(t);
            } else {
                auto nt = NaturalType(argvals[i]);
                if (!nt || nt->kind == TY_VOID || nt == fntype)
                    Error(c->args[i], "cannot infer a type for this argument");
                ptypes.push_back(nt);
            }
        }
        {
            auto savedst = curdst;
            curdst = Dest {};
            for (size_t i = 0; i < ptypes.size(); i++) CheckArg(c->args[i], ptypes[i]);
            curdst = savedst;
        }
        // Check the body inline, with lookups chaining to the definer.
        Frame f;
        f.sf = fb.env ? fb.env->sf : CurRealFrame().sf;
        f.spec = CurRealFrame().spec;
        f.lexspec = fb.env;
        f.lexframe = fb.env ? FrameOfSpec(fb.env) : 0;
        f.scopebase = (int)scopes.size();
        f.varbase = (int)vars.size();
        f.callline = c->line;
        f.isfunval = true;
        frames.push_back(f);
        PushScope(SK_FN);
        c->fvparams.clear();
        for (size_t i = 0; i < params.size(); i++) {
            auto vd = NewVar(params[i].name, ptypes[i], c->line, params[i].isvar);
            vd->assigned = true;
            if (ptypes[i]->kind == TY_REF || ptypes[i]->kind == TY_SLICE)
                BindRefProvenance(vd, argvals[i]);
            c->fvparams.push_back(vd);
        }
        c->fvtarget = fb.env ? fb.env->sf : nullptr;
        c->fvbody = (Block *)fv->body->Clone(ast);
        ValueRegion vr(*this, true);   // The body runs inside this call's expression.
        for (auto st : c->fvbody->stmts) CheckStmt(st);
        Val v = VoidVal();
        if (auto tail = c->fvbody->tail) {
            auto fi = Is<IfExpr>(tail);
            if ((fi && !fi->elseb) || Is<Guard>(tail)) CheckStmtExpr(tail);
            else v = CheckValue(c->fvbody->tail, nullptr);
        }
        c->fvbody->exprtype = v.type;
        PopScope();
        frames.pop_back();
        return v;
    }

    TypeExpr *SubstEnv(TypeExpr *t, FnSpec *env) {
        Frame f;
        f.lexspec = env;
        f.lexframe = -1;
        f.scopebase = (int)scopes.size();
        f.varbase = (int)vars.size();
        frames.push_back(f);
        auto r = Subst(t);
        frames.pop_back();
        return r;
    }

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
                    spec->params[i]->refrootexact = false;
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

// ---------------------------------------------------------------------------
// The per-node typecheck implementations (the Check virtual): the pass reads
// top to bottom here; shared machinery (calls, control flow, literals'
// construction rules) lives in the TypeCheck methods above. Values may denote
// references; the CheckValue/CheckArg/Operand wrappers apply transparency.

inline Val IntLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.inttypes[uns ? IS_U64 : IS_I64];
    v.ck = CK_INT;
    v.ival = val;
    v.uns = uns;
    v.nonneg = uns || val >= 0;
    return v;
}

inline Val FltLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.flttypes[FS_F64];
    v.ck = CK_FLT;
    v.fval = val;
    return v;
}

inline Val BoolLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.booltype;
    return v;
}

inline Val NullLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    v.isnull = true;
    // Its own type only matters when nothing adapts it; FitsAt checks isnull.
    v.type = expected && expected->kind == TY_REF && expected->ref->optional
                 ? expected : tc.nulltype;
    return v;
}

// `self` is consumed by CheckInits in the only position that gives it a
// meaning, so reaching the general expression path means it is misplaced.
inline Val SelfRef::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "self is only valid as the initializer of a non-optional "
                   "relative-reference field of the literal containing it (§3.9)");
}

inline Val StrLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    v.strlit = true;
    v.rootexact = true;   // Static data owns what it holds.
    v.writable = false;
    if (expected) {
        // A string literal constructs any u8-element array type (§3.7).
        if (expected->kind == TY_ARRAY && tc.IsU8(expected->arr->sub)) {
            if (expected->arr->akind == A_FIXED &&
                tc.ArraySize(expected->arr) != (int64_t)val.size())
                tc.Error(this, cat("string literal of length ", (int64_t)val.size(),
                                   " does not fit ", tc.TypeStr(expected)));
            v.type = expected;
            return v;
        }
        if (expected->kind == TY_SLICE && tc.IsU8(expected->sub)) {
            v.type = expected;
            return v;
        }
    }
    v.type = tc.u8slice;
    return v;
}

inline Val Ident::Check(TypeCheck &tc, TypeExpr *) {
    if (auto vd = tc.LookupVar(name)) {
        vdef = vd;
        tc.RequireAssigned(vd, this);
        Val v;
        auto t = vd->narrowed ? vd->narrowed : vd->type;
        v.type = tc.LoadType(t);
        if (t->kind == TY_REF || t->kind == TY_SLICE) {
            v.root = tc.RefRootOf(vd);
            v.rootexact = tc.RefExactOf(vd);
            v.rootfrom = vd->refrootfrom;
            v.writable = vd->refwritable;
            v.reusable = vd->refreusable;
            v.lvalue = t->kind == TY_SLICE;   // A slice variable is storage; a reference is the pointee's path.
        } else {
            v.root = vd;
            v.rootexact = true;
            v.writable = vd->isvar;
            v.reusable = vd->reusable;
            v.nonneg = vd->nonneg;
            v.lvalue = true;
        }
        return v;
    }
    // A generic parameter bound to a function value, or a named function: a
    // compile-time function value (§7.6).
    if (auto fb = tc.LookupFnVal(name)) {
        Val v;
        v.type = tc.fntype;
        v.fnv = *fb;
        return v;
    }
    if (auto sf = tc.LookupLocalFn(name)) {
        fnref = sf;
        Val v;
        v.type = tc.fntype;
        v.fnv.named = sf;
        v.fnv.env = tc.frames.back().lexspec;
        return v;
    }
    auto fit = tc.ast.functionmap.find(name);
    if (fit != tc.ast.functionmap.end()) {
        if (fit->second.size() != 1)
            tc.Error(this, cat("overloaded function ", name, " cannot be a function value"));
        fnref = fit->second[0];
        Val v;
        v.type = tc.fntype;
        v.fnv.named = fit->second[0];
        return v;
    }
    tc.Error(this, cat("unknown identifier: ", name));
}

inline Val ArrayLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    if (capexpr) {
        // [..cap]: an empty limited array with a runtime capacity (§5.3).
        tc.CheckIntAny(capexpr);
        if (!expected || expected->kind != TY_ARRAY ||
            expected->arr->akind != A_LIMITED || expected->arr->sizeexpr)
            tc.Error(this, "[..cap] constructs a limited array of construction-time "
                           "capacity; it needs a T[..] destination");
        v.type = expected;
        return v;
    }
    TypeExpr *elem = nullptr;
    int64_t wantcount = -1;
    if (expected && expected->kind == TY_ARRAY) {
        elem = expected->arr->sub;
        if (expected->arr->akind == A_FIXED) wantcount = tc.ArraySize(expected->arr);
    } else if (expected && expected->kind == TY_SLICE) {
        elem = expected->sub;
    }
    if (fillval) {
        auto cnt = tc.ConstIntOrError(fillcount, "array fill count");
        if (cnt < 0) tc.Error(this, "array fill count cannot be negative");
        auto ev = tc.CheckValue(fillval, elem);
        if (!elem) elem = ev.type;
        if (wantcount >= 0 && cnt != wantcount)
            tc.Error(this, cat("fill count ", cnt, " does not match array size ", wantcount));
        v.type = expected && expected->kind == TY_ARRAY
                     ? expected : tc.FixedArrayOf(elem, cnt, line);
        return v;
    }
    if (elems.empty() && !elem) {
        // [] adapts to any array type; callers re-check with an expected type
        // or report the missing context.
        v.emptyarr = true;
        v.type = tc.FixedArrayOf(tc.ast.voidtype, 0, line);
        return v;
    }
    for (auto &e : elems) {
        auto ev = tc.CheckValue(e, elem);
        if (!elem) elem = ev.type;
    }
    if (elem->kind == TY_VOID) tc.Error(this, "cannot infer array element type");
    if (wantcount >= 0 && (int64_t)elems.size() != wantcount)
        tc.Error(this, cat((int64_t)elems.size(), " element(s) do not fill ",
                           tc.TypeStr(expected)));
    if (expected && (expected->kind == TY_ARRAY || expected->kind == TY_SLICE)) {
        // A literal in slice position materializes a temporary fixed array.
        v.type = expected->kind == TY_ARRAY
                     ? expected : tc.FixedArrayOf(elem, (int64_t)elems.size(), line);
        v.root = expected->kind == TY_SLICE ? tc.temproot : nullptr;
        return v;
    }
    v.type = tc.FixedArrayOf(elem, (int64_t)elems.size(), line);
    return v;
}

inline Val StructLit::Check(TypeCheck &tc, TypeExpr *expected) {
    fieldindices.clear();
    sinst = nullptr;
    einst = nullptr;
    variant = nullptr;
    auto t = tc.Subst(type);
    if (t->kind == TY_VARIANT) {
        if (t->var->adt->kind != TY_ENUM)
            tc.Error(this, cat("variant literal of non-ADT type ", tc.TypeStr(t->var->adt)));
        auto ei = tc.GetEnumInst(t->var->adt);
        auto var = t->var->variant;
        auto vi = tc.VariantIndex(ei->en, var);
        einst = ei;
        variant = var;
        Val v;
        // The mode comes from the receiving declaration (§3.5); a literal
        // adapts to either, or stands as a first-class variant value (§8.2).
        // Which it is decides what `self` inside it denotes, so settle the
        // type before the initializers are checked.
        v.type = expected && expected->kind == TY_ENUM && expected->enu->en == ei->en &&
                         tc.TypeArgsEq(expected->enu->args, t->var->adt->enu->args)
                     ? expected : t;
        tc.CheckInits(this, var->fields, ei->vftypes[vi], ei->en->name, v.type);
        return v;
    }
    if (t->kind == TY_STRUCT) {
        auto st = t->struc->st;
        if (t->struc->args.empty() && !st->generics.empty()) {
            // Infer the struct's generics from the field initializers.
            vector<pair<string_view, TypeExpr *>> b;
            auto named = !inits.empty() && !inits[0].name.empty();
            auto pos = 0;
            for (auto &fi : inits) {
                Field *field = nullptr;
                if (named) {
                    for (auto &f2 : st->fields)
                        if (!f2.ispad && f2.name == fi.name) { field = &f2; break; }
                } else {
                    while (pos < (int)st->fields.size() && st->fields[pos].ispad) pos++;
                    if (pos < (int)st->fields.size()) field = &st->fields[pos++];
                }
                if (!field) continue;  // Reported properly below.
                // `self` says nothing about the arguments (its type is the
                // literal's own), and has no meaning outside CheckInits.
                if (Is<SelfRef>(fi.val)) continue;
                auto av = tc.CheckV(fi.val, nullptr);
                fi.val->exprtype = av.type;
                auto nt = tc.NaturalType(av);
                if (nt) tc.BindTypes(field->type, nt, b);
            }
            auto nt2 = tc.ast.NewType(TY_STRUCT, line);
            nt2->struc = tc.ast.NewDetail<TypeStruct>();
            nt2->struc->st = st;
            for (auto &g : st->generics) {
                TypeExpr *bound = nullptr;
                for (auto &[n, bt] : b) if (n == g.name) bound = bt;
                if (!bound)
                    tc.Error(this, cat("cannot infer generic parameter ", g.name, " of ",
                                       st->name, "; use ", st->name, "<...> { }"));
                nt2->struc->args.push_back(bound);
            }
            t = nt2;
        }
        auto inst = tc.GetStructInst(t);
        sinst = inst;
        tc.CheckInits(this, st->fields, inst->ftypes, st->name, t);
        Val v;
        v.type = t;
        return v;
    }
    tc.Error(this, cat("cannot construct a value of type ", tc.TypeStr(t), " with a literal"));
}

inline Val Unary::Check(TypeCheck &tc, TypeExpr *) {
    if (op == T_BITAND) return tc.CheckRefOf(this);
    auto v = tc.Operand(child);
    auto t = tc.LoadType(v.type);
    Val r;
    switch (op) {
        case T_MINUS:
            if (tc.IsIntT(t)) {
                if (v.ck == CK_INT) {
                    // -(2^63) is exactly i64.min; any other u64-range value
                    // cannot be negated.
                    if (v.uns && v.ival != INT64_MIN)
                        tc.Error(this, "negated constant too large for i64");
                    r.type = tc.ast.inttypes[IS_I64];
                    r.ck = CK_INT;
                    r.ival = v.uns ? INT64_MIN : -v.ival;
                    child->exprtype = r.type;
                    return r;
                }
                if (IsUnsigned(t->intstorage))
                    tc.Error(this, cat("cannot negate a value of unsigned type ",
                                       tc.TypeStr(t), " (convert with `as`)"));
                r.type = t;
            } else if (t->kind == TY_FLT) {
                r.type = t;
                if (v.ck == CK_FLT) { r.ck = CK_FLT; r.fval = -v.fval; }
            } else {
                tc.Error(this, cat("cannot negate a value of type ", tc.TypeStr(t)));
            }
            return r;
        case T_NOT:
            // Optionals are testable like conditions (§3.8 truthiness).
            if (t->kind != TY_BOOL && !tc.IsOptional(t))
                tc.Error(this, cat("! requires bool, got ", tc.TypeStr(t)));
            r.type = tc.ast.booltype;
            return r;
        case T_BITNOT:
            if (!tc.IsIntT(t))
                tc.Error(this, cat("~ requires an integer, got ", tc.TypeStr(t)));
            if (v.ck == CK_INT) {
                r.type = tc.ast.inttypes[v.uns ? IS_U64 : IS_I64];
                r.ck = CK_INT;
                r.ival = ~v.ival;
                r.uns = v.uns && r.ival < 0;
                child->exprtype = r.type;
                return r;
            }
            r.type = t;
            return r;
        default:
            assert(false);
            return tc.VoidVal();
    }
}

inline Val Binary::Check(TypeCheck &tc, TypeExpr *) {
    if (op == T_ANDAND || op == T_OROR) {
        tc.CheckCond(left);
        auto snap = tc.SaveFlow();
        tc.NarrowCond(left, op == T_ANDAND);
        tc.CheckCond(right);
        tc.RestoreFlow(snap);
        Val v;
        v.type = tc.ast.booltype;
        return v;
    }
    auto lv = tc.Operand(left);
    auto rv = tc.Operand(right);
    auto lt = tc.LoadType(lv.type), rt = tc.LoadType(rv.type);
    Val v;
    switch (op) {
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ: {
            auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt, true);
            if (!ct)
                tc.Error(this, cat("ordering comparison requires numeric operands, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            tc.RetypeOperands(left, right, lv, rv, ct);
            v.type = tc.ast.booltype;
            return v;
        }
        case T_EQ: case T_NEQ: {
            // null tests: the other side must be an optional (an already
            // narrowed optional variable still counts).
            if (lv.isnull || rv.isnull) {
                auto othernode = lv.isnull ? right : left;
                auto &other = lv.isnull ? rt : lt;
                auto oid = Is<Ident>(othernode);
                auto narrowedopt = oid && oid->vdef && tc.IsOptional(oid->vdef->type);
                if (!tc.IsOptional(other) && !narrowedopt && !(lv.isnull && rv.isnull))
                    tc.Error(this, cat("only optionals compare against null, not ",
                                       tc.TypeStr(other)));
                v.type = tc.ast.booltype;
                return v;
            }
            if (auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt, true)) {
                tc.RetypeOperands(left, right, lv, rv, ct);
                v.type = tc.ast.booltype;
                return v;
            }
            if (lt->kind == TY_FN || lt->kind == TY_VOID)
                tc.Error(this, "these values cannot be compared");
            if (!tc.TypeEq(lt, rt))
                tc.Error(this, cat("== requires operands of the same type, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            v.type = tc.ast.booltype;
            return v;
        }
        case T_SHL: case T_SHR: {
            // Shifts: the left operand's type is the result type; the count
            // may be any integer type and is masked to the width (§6.2).
            if (!tc.IsIntT(lt) || !tc.IsIntT(rt))
                tc.Error(this, cat("shift requires integer operands, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            if (lv.ck == CK_INT)
                v.type = tc.ast.inttypes[lv.uns ? IS_U64 : IS_I64];
            else
                v.type = lt;
            left->exprtype = v.type;
            lv.type = v.type;
            tc.FoldInt(op, lv, rv, v, this);
            return v;
        }
        case T_BITAND: case T_BITOR: case T_XOR: {
            if (!tc.IsIntT(lt) || !tc.IsIntT(rt))
                tc.Error(this, cat("bitwise operator requires integer operands, got ",
                                   tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
            auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt);
            tc.RetypeOperands(left, right, lv, rv, ct);
            v.type = ct;
            tc.FoldInt(op, lv, rv, v, this);
            return v;
        }
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            if ((tc.IsIntT(lt) && tc.IsIntT(rt)) ||
                (lt->kind == TY_FLT && rt->kind == TY_FLT)) {
                auto ct = tc.UnifyNumeric(this, op, lv, rv, lt, rt);
                tc.RetypeOperands(left, right, lv, rv, ct);
                v.type = ct;
                if (ct->kind == TY_INT) {
                    tc.FoldInt(op, lv, rv, v, this);
                } else if (lv.ck == CK_FLT && rv.ck == CK_FLT && op != T_MOD) {
                    // % (fmod) is left to the runtime.
                    auto a = lv.fval, b = rv.fval;
                    if (tc.IsF32(ct)) { a = (float)a; b = (float)b; }
                    v.ck = CK_FLT;
                    switch (op) {
                        case T_PLUS:  v.fval = a + b; break;
                        case T_MINUS: v.fval = a - b; break;
                        case T_MUL:   v.fval = a * b; break;
                        default:      v.fval = b != 0 ? a / b : 0; break;
                    }
                    if (tc.IsF32(ct)) v.fval = (double)(float)v.fval;
                }
                return v;
            }
            // Elementwise math on identical struct / fixed array types whose
            // scalar leaves are uniformly int or float (§6.1).
            if (tc.TypeEq(lt, rt) && tc.ElementwiseOK(lt)) {
                v.type = lt;
                return v;
            }
            tc.Error(this, cat("operator ", TName(op), " cannot be applied to ",
                               tc.TypeStr(lt), " and ", tc.TypeStr(rt)));
        }
        default:
            assert(false);
            return tc.VoidVal();
    }
}

inline Val Dot::Check(TypeCheck &tc, TypeExpr *) {
    // EnumName.Variant: a payload-less variant constant (§3.5).
    if (auto id = Is<Ident>(obj)) {
        if (!tc.LookupVar(id->name) && !tc.IsFnValName(id->name)) {
            auto eit = tc.ast.enummap.find(id->name);
            if (eit != tc.ast.enummap.end()) return tc.CheckVariantConst(this, eit->second);
        }
    }
    auto ov = tc.CheckV(obj, nullptr);
    obj->exprtype = ov.type;
    auto t = ov.type;
    if (t->kind == TY_REF) {
        if (t->ref->optional)
            tc.Error(this, "optional value must be narrowed (if/guard/assert) before use");
        t = t->ref->sub;  // Auto-deref; ov.root is already the pointee's owner.
    }
    // Builtin properties (.len/.cap) from the table.
    if (auto bd = LookupBuiltin(name); bd && (bd->flags & BF_PROPERTY)) {
        auto got = 0;
        if (t->kind == TY_ARRAY) {
            switch (t->arr->akind) {
                case A_FIXED:      got = BR_FIXED; break;
                case A_VAR:        got = BR_VAR; break;
                case A_LIMITED:    got = BR_LIMITED; break;
                case A_GROW:       got = BR_GROW; break;
                case A_GROWSHRINK: got = BR_GROWSHRINK; break;
            }
        } else if (t->kind == TY_SLICE) {
            got = BR_SLICE;
        }
        if (got & bd->recv) {
            member = bd->kind;
            Val v;
            assert(bd->rets[0] == 'i');
            v.type = tc.ast.inttypes[IS_I64];
            v.nonneg = true;   // A length or capacity is in [0, 2^48] (§10.4).
            return v;
        }
        if (t->kind == TY_ARRAY || t->kind == TY_SLICE)
            tc.Error(this, cat(".", name, " is not available on ", tc.TypeStr(t)));
    }
    TypeCheck::LVal lv;
    lv.type = t;
    lv.root = ov.root;
    lv.rootexact = ov.rootexact;
    lv.rootfrom = ov.rootfrom;
    lv.writable = ov.writable;
    tc.ResolveMemberLValue(lv, this);
    // A reference read out of a container is re-rooted (§9.5) and writable by
    // design (§9.5 laundering; see the header note).
    tc.ReadBackLVal(lv);
    Val v;
    v.type = tc.LoadType(lv.type);
    v.root = lv.root;
    v.rootexact = lv.rootexact;
    v.rootfrom = lv.rootfrom;
    v.writable = v.type->kind == TY_REF || v.type->kind == TY_SLICE ? true : lv.writable;
    v.lvalue = v.type->kind != TY_REF;
    return v;
}

inline Val Call::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckCall(this, expected);
}

inline Val Index::Check(TypeCheck &tc, TypeExpr *) {
    auto lv = tc.CheckLValue(this);
    tc.ReadBackLVal(lv);
    Val v;
    v.type = tc.LoadType(lv.type);
    v.root = lv.root;
    v.rootexact = lv.rootexact;
    v.rootfrom = lv.rootfrom;
    // Container-read laundering, as for fields above (§9.5).
    v.writable = v.type->kind == TY_REF || v.type->kind == TY_SLICE ? true : lv.writable;
    v.reusable = lv.reusable;
    v.lvalue = v.type->kind != TY_REF;
    return v;
}

inline Val SliceExpr::Check(TypeCheck &tc, TypeExpr *) {
    auto lv = tc.LValueBase(obj);
    tc.DerefLValue(lv, obj);
    tc.SliceProvenance(lv, obj);
    TypeExpr *elem;
    if (lv.type->kind == TY_SLICE) {
        elem = lv.type->sub;
        if (tc.ClassOf(elem) != SC_FIXED && (lo || hi))
            tc.Error(this, "slices of variable-size elements can only be re-sliced "
                           "whole ([..])");
    } else if (lv.type->kind == TY_ARRAY) {
        elem = lv.type->arr->sub;
        if (tc.ClassOf(elem) != SC_FIXED && (lo || hi))
            tc.Error(this, "arrays of variable-size elements can only be sliced "
                           "whole ([..])");
    } else {
        tc.Error(this, cat("cannot slice a value of type ", tc.TypeStr(lv.type)));
    }
    if (lo) tc.CheckIntAny(lo);
    if (hi) tc.CheckIntAny(hi);
    Val v;
    v.type = tc.SliceOf(elem, line);
    v.root = lv.root;
    v.rootexact = lv.rootexact;
    v.rootfrom = lv.rootfrom;
    v.writable = lv.writable;
    return v;
}

inline Val AsCast::Check(TypeCheck &tc, TypeExpr *) {
    auto cv = tc.Operand(child);
    auto st = tc.LoadType(cv.type);
    if (!tc.IsIntT(st) && st->kind != TY_FLT)
        tc.Error(this, cat("as requires a numeric source, got ", tc.TypeStr(st)));
    auto tt = tc.Subst(type);
    Val v;
    if (tt->kind == TY_INT) {
        if (tt->intstorage == IS_VARINT)
            tc.Error(this, "cannot cast to varint (varints are written at construction only)");
        v.type = tt;
        return v;
    }
    if (tt->kind == TY_FLT) {
        v.type = tt;
        return v;
    }
    tc.Error(this, cat("as can only convert between numeric types, not to ", tc.TypeStr(tt)));
}

inline Val RangeExpr::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "range expressions are only valid in for headers");
}

inline Val Block::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckBlockVal(this, expected, true, TypeCheck::SK_PLAIN);
}

inline Val IfExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckIf(this, expected, true);
}

inline Val MatchExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckMatch(this, expected, true);
}

inline Val EarlyBlock::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckEarlyBlock(this, expected, true);
}

inline Val While::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckWhile(this);
    return tc.VoidVal();
}

inline Val LoopExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.CheckLoop(this, expected, true);
}

inline Val ForLoop::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckFor(this);
    return tc.VoidVal();
}

inline Val Guard::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckGuard(this);
    return tc.VoidVal();
}

inline Val Return::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckReturn(this);
    return tc.VoidVal();
}

inline Val Break::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckBreak(this);
    return tc.VoidVal();
}

inline Val Continue::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckContinue(this);
    return tc.VoidVal();
}

inline Val FunVal::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "function values can only be passed to calls (§7.6)");
}

inline Val VarDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val Assign::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "an assignment is a statement, not a value");
}

inline Val IncDec::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "an increment is a statement, not a value");
}

inline Val FnDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val StructDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val EnumDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val AliasDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val InlineBlock::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "internal: optimizer node reached the typechecker");
}

// Runs the whole pass; errors throw CompileError.
inline void TypeCheckProgram(Ast &ast) { TypeCheck tc(ast); }

}  // namespace goose
