// Goose compiler — C code generation. Consumes the optimized specialization
// bodies (FnSpec::body) plus globals and emits one self-contained C file (the
// runtime files from src/runtime/ are prepended by the driver).
//
// Representation (Appendix C, with v1 simplifications noted in the docs):
// * Fixed-size types become packed C types (#pragma pack(1)): scalars, packed
//   structs, fixed/limited arrays wrapped in structs, fixed-mode ADTs as
//   tag + union, references as pointers, slices as { data, len }.
// * Variable and resizable values ("bytes" values) are self-describing byte
//   images on a data stack, held as a uint8_t* to the value start. A resizable
//   value is [int64 len][elements] uniformly (also when outermost, unlike
//   C.2's frame header — a deliberate v1 simplification): growth bumps the
//   stack top and increments the inline length; the base pointer never moves.
// * References to resizable-class values are fat (gs_rref: address + stack);
//   references with reusable-pool provenance additionally carry the hidden
//   freelist (gs_pref). Which form a parameter uses comes from the
//   specialization's RootArg info, not the type.
// * Stack assignment (§10.3) is fully dynamic: every stack-using function
//   takes a hidden `int64_t gs_sp` argument, its nonfixed locals/temps use
//   gs_stks[gs_sp + k] with per-function constant k, and callees get
//   gs_sp + <locals in use>. §10.3 explicitly permits this strategy. Globals
//   own dedicated stacks outside the sp-indexed block, so thread programs
//   (which get a fresh block) share all generated functions.
// * Nonfixed return values are constructed directly at a destination stack
//   passed as a hidden gs_stack* argument (guaranteed in-place, §4.3/§7.3);
//   the C function returns nothing for them. `v.append(f())` calls f at v's
//   top and then slides the result's 8-byte length header out (see EmitCall).
// * `return ... from` (§7.9) uses a hidden int32* discriminant out-argument
//   on every function on a propagation path, plus per-target thread-local
//   channels for the in-flight fixed values (nonfixed ones land directly on
//   the target's destination stack, recorded thread-locally at target entry).
//
// Scope exits restore data-stack watermarks: every nonfixed local's own base
// pointer doubles as the watermark to restore, so exits (fallthrough, break,
// continue, return, propagate) emit `stack->top = base;` runs in reverse
// declaration order.
#pragma once

namespace goose {

struct CodeGen {
    Ast &ast;

    // Output sections, concatenated by Run() in this order (after the
    // driver-prepended runtime): predefs go before the runtime paste.
    string predefs;
    string tdecls;      // Packed typedefs.
    string pdata;       // Packed static data (string literals).
    string data;        // Queues, long-distance return channels, globals.
    string protos;
    string code;        // Function bodies, size/eq helpers, thunks, main.
    bool usesthreads = false;

    [[noreturn]] void Fail(Line l, const string &msg) {
        auto s = cat("codegen: ", msg);
        if (l.fileidx >= 0 && l.fileidx < (int)ast.sources.size())
            s = cat(ast.sources[l.fileidx].first, ":", l.line, ": ", s);
        throw CompileError { s };
    }

    string Where(Line l) {
        if (l.fileidx < 0 || l.fileidx >= (int)ast.sources.size()) return "?";
        auto f = ast.sources[l.fileidx].first;
        for (auto &c : f) if (c == '\\') c = '/';
        return cat(f, ":", l.line);
    }

    // A C string literal (quotes included) for arbitrary bytes; non-printables
    // as 3-digit octal so following characters can never extend an escape.
    static string CStr(string_view v) {
        string s = "\"";
        for (auto c : v) {
            auto u = (uint8_t)c;
            if (c == '"' || c == '\\') { s += '\\'; s += c; }
            else if (u >= 32 && u < 127) s += c;
            else {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\%03o", u);
                s += buf;
            }
        }
        s += '"';
        return s;
    }

    string WhereStr(Line l) { return CStr(Where(l)); }

    // ------------------------------------------------------------------
    // Type utilities on concrete (post-typecheck) types. Sizes of fixed and
    // limited arrays were evaluated during checking; assert rather than
    // re-evaluate.

    static IntStorage CanI(IntStorage s) { return TypeCheck::CanonI(s); }
    static FltStorage CanF(FltStorage s) { return TypeCheck::CanonF(s); }

    int64_t ArrSize(TypeArray *a) {
        assert(a->size >= 0 || !a->sizeexpr);
        return a->size;
    }

    bool TEq(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (a->kind != b->kind) return false;
        switch (a->kind) {
            case TY_INT:  return CanI(a->intstorage) == CanI(b->intstorage);
            case TY_FLT:  return CanF(a->fltstorage) == CanF(b->fltstorage);
            case TY_BOOL: case TY_VOID: case TY_FN: return true;
            case TY_STRUCT: return SI(a) == SI(b);
            case TY_ENUM:   return EIOf(a) == EIOf(b) && a->enu->varmode == b->enu->varmode;
            case TY_ARRAY: {
                auto &x = *a->arr, &y = *b->arr;
                if (x.akind != y.akind || !TEq(x.sub, y.sub)) return false;
                switch (x.akind) {
                    case A_FIXED:   return ArrSize(a->arr) == ArrSize(b->arr);
                    case A_VAR:     return LenStore(a->arr) == LenStore(b->arr);
                    case A_LIMITED: return (x.sizeexpr ? ArrSize(a->arr) : -1) ==
                                           (y.sizeexpr ? ArrSize(b->arr) : -1);
                    default:        return true;
                }
            }
            case TY_SLICE: return TEq(a->sub, b->sub);
            case TY_REF:
                return TEq(a->ref->sub, b->ref->sub) && a->ref->optional == b->ref->optional &&
                       a->ref->lenstorage == b->ref->lenstorage;
            case TY_VARIANT:
                return a->var->variant == b->var->variant && TEq(a->var->adt, b->var->adt);
            default: assert(false); return false;
        }
    }

    // Instantiations: the typechecker filled the per-TypeExpr caches for every
    // type it touched; fall back to an argument-equality search for clones.
    StructInst *SI(TypeExpr *t) {
        assert(t->kind == TY_STRUCT);
        if (t->struc->inst) return t->struc->inst;
        for (auto inst : t->struc->st->insts) {
            if (inst->args.size() != t->struc->args.size()) continue;
            auto ok = true;
            for (size_t i = 0; i < inst->args.size(); i++)
                ok &= TEq(inst->args[i], t->struc->args[i]);
            if (ok) return t->struc->inst = inst;
        }
        assert(false);
        return nullptr;
    }

    EnumInst *EIOf(TypeExpr *t) {
        assert(t->kind == TY_ENUM);
        if (t->enu->inst) return t->enu->inst;
        for (auto inst : t->enu->en->insts) {
            if (inst->args.size() != t->enu->args.size()) continue;
            auto ok = true;
            for (size_t i = 0; i < inst->args.size(); i++)
                ok &= TEq(inst->args[i], t->enu->args[i]);
            if (ok) return t->enu->inst = inst;
        }
        assert(false);
        return nullptr;
    }

    EnumInst *EIVar(TypeExpr *t) {   // For TY_VARIANT.
        assert(t->kind == TY_VARIANT);
        return EIOf(t->var->adt);
    }

    int VarIdx(SEnum *en, SVariant *v) {
        for (size_t i = 0; i < en->variants.size(); i++)
            if (&en->variants[i] == v) return (int)i;
        assert(false);
        return 0;
    }

    SizeClass Cls(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT: return t->intstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
            case TY_REF: return t->ref->lenstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
            case TY_STRUCT: return SI(t)->sclass;
            case TY_ENUM: return t->enu->varmode ? EIOf(t)->varclass : SC_FIXED;
            case TY_ARRAY:
                switch (t->arr->akind) {
                    case A_FIXED:   return SC_FIXED;
                    case A_VAR:     return SC_VARIABLE;
                    case A_LIMITED: return ArrSize(t->arr) >= 0 ? SC_FIXED : SC_VARIABLE;
                    default:        return SC_RESIZABLE;
                }
            case TY_VARIANT: {
                auto inst = EIVar(t);
                auto vi = VarIdx(inst->en, t->var->variant);
                auto c = SC_FIXED;
                for (auto ft : inst->vftypes[vi]) if (ft) c = std::max(c, Cls(ft));
                return c;
            }
            default: return SC_FIXED;
        }
    }

    bool IsFix(TypeExpr *t)  { return Cls(t) == SC_FIXED; }
    bool IsResz(TypeExpr *t) { return Cls(t) == SC_RESIZABLE; }
    bool IsBytesT(TypeExpr *t) { return Cls(t) != SC_FIXED; }
    bool IsFatPointee(TypeExpr *t) { return IsResz(t); }
    bool IsFatRef(TypeExpr *t) {
        return t->kind == TY_REF && t->ref->lenstorage < 0 && IsFatPointee(t->ref->sub);
    }
    bool IsOpt(TypeExpr *t) { return t->kind == TY_REF && t->ref->optional; }
    bool IsVoidT(TypeExpr *t) { return !t || t->kind == TY_VOID; }
    bool IsU8T(TypeExpr *t) { return t->kind == TY_INT && t->intstorage == IS_U8; }

    // The stored length-field type of an array (A_VAR: declared or u32
    // default; A_LIMITED with static capacity: smallest fitting type).
    IntStorage LenStore(TypeArray *a) {
        switch (a->akind) {
            case A_VAR:
                return a->lenstorage < 0 ? IS_U32 : (IntStorage)a->lenstorage;
            case A_LIMITED: {
                auto k = ArrSize(a);
                assert(k >= 0);
                return k <= 255 ? IS_U8 : k <= 65535 ? IS_U16 :
                       k <= (int64_t)UINT32_MAX ? IS_U32 : IS_U64;
            }
            default: assert(false); return IS_U32;
        }
    }

    static int64_t IntSize(IntStorage s) {
        switch (CanI(s)) {
            case IS_I8: case IS_U8: return 1;
            case IS_I16: case IS_U16: return 2;
            case IS_I32: case IS_U32: return 4;
            default: return 8;
        }
    }

    static const char *IntCT(IntStorage s) {
        switch (CanI(s)) {
            case IS_I8:  return "int8_t";
            case IS_I16: return "int16_t";
            case IS_I32: return "int32_t";
            case IS_U8:  return "uint8_t";
            case IS_U16: return "uint16_t";
            case IS_U32: return "uint32_t";
            case IS_U64: return "uint64_t";
            default:     return "int64_t";
        }
    }

    // The signed C type behind a relative reference's unsigned width spelling.
    static const char *RelCT(IntStorage s) {
        switch (IntSize(s)) {
            case 1: return "int8_t";
            case 2: return "int16_t";
            case 4: return "int32_t";
            default: return "int64_t";
        }
    }

    IntStorage TagStore(SEnum *en) { return en->variants.size() <= 256 ? IS_U8 : IS_U16; }
    int64_t TagSize(SEnum *en) { return IntSize(TagStore(en)); }

    // Packed layout of a run of fields: byte offsets aligned with the fields
    // vector (pads get their own offset), plus the total size. Fixed types
    // only; the same code computes the static prefix of variable structs.
    struct Layout {
        vector<int64_t> offs;
        int64_t size = 0;
    };
    map<pair<const void *, int>, Layout> layouts;   // StructInst / (EnumInst, variant).

    // The alignment `pad` (bare form) gives the next field: its scalar width.
    int64_t PadAlign(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT: return t->intstorage == IS_VARINT ? 1 : IntSize(t->intstorage);
            case TY_FLT: return t->fltstorage == FS_F32 ? 4 : 8;
            case TY_REF: return t->ref->lenstorage >= 0 ?
                                (t->ref->lenstorage == IS_VARINT ? 1
                                                                 : IntSize((IntStorage)t->ref->lenstorage))
                                : 8;
            case TY_SLICE: return 8;
            default: return 1;
        }
    }

    Layout LayoutFields(const vector<Field> &fields, const vector<TypeExpr *> &ftypes) {
        Layout lo;
        int64_t off = 0;
        for (size_t i = 0; i < fields.size(); i++) {
            auto &f = fields[i];
            if (f.ispad) {
                lo.offs.push_back(off);
                if (f.padsize >= 0) {
                    off += f.padsize;
                } else {
                    // Bare pad: align the next real field to its own size.
                    TypeExpr *nt = nullptr;
                    for (auto j = i + 1; j < fields.size(); j++)
                        if (!fields[j].ispad) { nt = ftypes[j]; break; }
                    auto a = nt ? PadAlign(nt) : 1;
                    off = (off + a - 1) / a * a;
                }
                continue;
            }
            lo.offs.push_back(off);
            off += FixedSize(ftypes[i]);
        }
        lo.size = off;
        return lo;
    }

    const Layout &StructLayout(StructInst *si) {
        auto key = pair<const void *, int>(si, -1);
        auto it = layouts.find(key);
        if (it != layouts.end()) return it->second;
        return layouts[key] = LayoutFields(si->st->fields, si->ftypes);
    }

    const Layout &VariantLayout(EnumInst *ei, int vi) {
        auto key = pair<const void *, int>(ei, vi);
        auto it = layouts.find(key);
        if (it != layouts.end()) return it->second;
        return layouts[key] = LayoutFields(ei->en->variants[vi].fields, ei->vftypes[vi]);
    }

    int64_t FixedSize(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT:  assert(t->intstorage != IS_VARINT); return IntSize(t->intstorage);
            case TY_FLT:  return t->fltstorage == FS_F32 ? 4 : 8;
            case TY_BOOL: return 1;
            case TY_REF:
                if (t->ref->lenstorage >= 0) {
                    assert(t->ref->lenstorage != IS_VARINT);
                    return IntSize((IntStorage)t->ref->lenstorage);
                }
                return IsFatPointee(t->ref->sub) ? 16 : 8;
            case TY_SLICE: return 16;
            case TY_STRUCT: return StructLayout(SI(t)).size;
            case TY_ENUM: {
                assert(!t->enu->varmode);
                auto ei = EIOf(t);
                int64_t maxp = 0;
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++)
                    maxp = std::max(maxp, VariantLayout(ei, (int)vi).size);
                return TagSize(ei->en) + maxp;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                return VariantLayout(ei, VarIdx(ei->en, t->var->variant)).size;
            }
            case TY_ARRAY: {
                auto &a = *t->arr;
                switch (a.akind) {
                    case A_FIXED:   return ArrSize(t->arr) * FixedSize(a.sub);
                    case A_LIMITED: return IntSize(LenStore(t->arr)) +
                                           ArrSize(t->arr) * FixedSize(a.sub);
                    default: assert(false); return 0;
                }
            }
            default: assert(false); return 0;
        }
    }

    // ------------------------------------------------------------------
    // Naming: one global identifier space for types, functions, globals, and
    // static data; per-function spaces for locals seeded from it.

    set<string> used;

    static bool CReserved(const string &s) {
        static const set<string> words = {
            "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
            "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
            "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
            "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "main",
            "bool", "true", "false", "NULL", "memcpy", "memmove", "memset", "printf", "free",
            "malloc", "abort", "exit",
        };
        return words.count(s) != 0;
    }

    string Sanitize(string_view name) {
        string s(name);
        if (s.empty()) s = "_";
        if (CReserved(s) || s.compare(0, 3, "gs_") == 0 || s.compare(0, 3, "GS_") == 0)
            s += "_";
        return s;
    }

    string Unique(string base) {
        if (!used.count(base)) { used.insert(base); return base; }
        for (auto n = 2;; n++) {
            auto s = cat(base, "_", n);
            if (!used.count(s)) { used.insert(s); return s; }
        }
    }

    // ------------------------------------------------------------------
    // Mangled type identities and on-demand C type emission. The mangle keys
    // every per-type artifact (typedef, size fn, eq fn); the C name is the
    // uniquified mangle.

    string Mangle(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT:
                switch (CanI(t->intstorage)) {
                    case IS_I8: return "i8"; case IS_I16: return "i16"; case IS_I32: return "i32";
                    case IS_U8: return "u8"; case IS_U16: return "u16"; case IS_U32: return "u32";
                    case IS_U64: return "u64"; case IS_VARINT: return "vi";
                    default: return "i";
                }
            case TY_FLT: return t->fltstorage == FS_F32 ? "f32" : "f";
            case TY_BOOL: return "b";
            case TY_STRUCT: {
                auto s = Sanitize(t->struc->st->name);
                for (auto a : t->struc->args) Append(s, "_", Mangle(a));
                return s;
            }
            case TY_ENUM: {
                auto s = Sanitize(t->enu->en->name);
                for (auto a : t->enu->args) Append(s, "_", Mangle(a));
                if (t->enu->varmode) s += "_vm";
                return s;
            }
            case TY_VARIANT: {
                auto s = Mangle(t->var->adt);
                Append(s, "_", t->var->variant->name, "_p");
                return s;
            }
            case TY_ARRAY: {
                auto &a = *t->arr;
                switch (a.akind) {
                    case A_FIXED:   return cat("a", ArrSize(t->arr), "_", Mangle(a.sub));
                    case A_VAR:     return cat("v", IntStorageName(LenStore(t->arr)), "_",
                                               Mangle(a.sub));
                    case A_LIMITED: return ArrSize(t->arr) >= 0
                                        ? cat("l", ArrSize(t->arr), "_", Mangle(a.sub))
                                        : cat("lv_", Mangle(a.sub));
                    case A_GROW:    return cat("gw_", Mangle(a.sub));
                    default:        return cat("gx_", Mangle(a.sub));
                }
            }
            case TY_SLICE: return cat("sl_", Mangle(t->sub));
            case TY_REF: {
                auto &r = *t->ref;
                string s = r.optional ? "o" : "r";
                if (r.lenstorage >= 0) Append(s, "l", IntStorageName(r.lenstorage));
                Append(s, "_", Mangle(r.sub));
                return s;
            }
            default: assert(false); return "void";
        }
    }

    unordered_map<string, string> ctypes;   // mangle -> emitted C type name.
    bool corebuiltins = false;

    void EmitCoreTypes() {
        if (corebuiltins) return;
        corebuiltins = true;
        tdecls +=
            "typedef struct { uint8_t *addr; gs_stack *stk; } gs_rref;\n"
            "typedef struct { uint8_t *addr; gs_stack *stk; uint8_t *fl; gs_stack *flstk; } "
            "gs_pref;\n\n";
        used.insert("gs_rref");
        used.insert("gs_pref");
    }

    // The C type of a fixed-class value (also refs/slices to anything). Emits
    // the typedef on first use. Never valid for bytes-class values.
    string CT(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT:  assert(t->intstorage != IS_VARINT); return IntCT(t->intstorage);
            case TY_FLT:  return t->fltstorage == FS_F32 ? "float" : "double";
            case TY_BOOL: return "uint8_t";
            case TY_VOID: return "void";
            case TY_REF: {
                auto &r = *t->ref;
                if (r.lenstorage >= 0) {
                    assert(r.lenstorage != IS_VARINT);
                    return IntCT((IntStorage)r.lenstorage);   // Stored offset form.
                }
                EmitCoreTypes();
                if (IsFatPointee(r.sub)) return "gs_rref";
                if (IsBytesT(r.sub)) return "uint8_t *";
                return cat(CT(r.sub), " *");
            }
            default: break;
        }
        auto m = Mangle(t);
        auto it = ctypes.find(m);
        if (it != ctypes.end()) return it->second;
        EmitCoreTypes();
        auto name = Unique(m);
        ctypes[m] = name;   // Before the body: recursion terminates via refs.
        string d;
        switch (t->kind) {
            case TY_SLICE: {
                auto e = IsBytesT(t->sub) ? string("uint8_t") : CT(t->sub);
                Append(d, "typedef struct { ", e, " *data; int64_t len; } ", name, ";\n");
                break;
            }
            case TY_ARRAY: {
                auto &a = *t->arr;
                auto e = CT(a.sub);
                if (a.akind == A_FIXED) {
                    auto k = ArrSize(t->arr);
                    Append(d, "typedef struct { ", e, " e[", k ? k : 1, "]; } ", name, ";\n");
                } else {
                    assert(a.akind == A_LIMITED && ArrSize(t->arr) >= 0);
                    Append(d, "typedef struct { ", IntCT(LenStore(t->arr)), " len; ", e, " e[",
                           ArrSize(t->arr) ? ArrSize(t->arr) : 1, "]; } ", name, ";\n");
                }
                break;
            }
            case TY_STRUCT: {
                auto si = SI(t);
                Append(d, "typedef struct {\n");
                EmitCFields(d, si->st->fields, si->ftypes);
                Append(d, "} ", name, ";\n");
                break;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                auto &v = ei->en->variants[vi];
                Append(d, "typedef struct {\n");
                if (v.fields.empty()) Append(d, "    uint8_t gs_empty;\n");
                else EmitCFields(d, v.fields, ei->vftypes[vi]);
                Append(d, "} ", name, ";\n");
                break;
            }
            case TY_ENUM: {
                assert(!t->enu->varmode);
                auto ei = EIOf(t);
                EnsureTagEnum(ei, t);
                Append(d, "typedef struct {\n    ", IntCT(TagStore(ei->en)), " tag;\n");
                string members;
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                    if (ei->en->variants[vi].fields.empty()) continue;
                    auto vt = VariantType(t, (int)vi);
                    Append(members, "        ", CT(vt), " v_",
                           Sanitize(ei->en->variants[vi].name), ";\n");
                }
                if (!members.empty()) Append(d, "    union {\n", members, "    } u;\n");
                Append(d, "} ", name, ";\n");
                break;
            }
            default: assert(false);
        }
        tdecls += d;
        tdecls += "\n";
        return name;
    }

    void EmitCFields(string &d, const vector<Field> &fields, const vector<TypeExpr *> &ftypes) {
        auto lo = LayoutFields(fields, ftypes);
        auto npad = 0;
        for (size_t i = 0; i < fields.size(); i++) {
            auto &f = fields[i];
            if (f.ispad) {
                auto next = i + 1 < fields.size() ? lo.offs[i + 1] : lo.size;
                auto n = next - lo.offs[i];
                if (n > 0) Append(d, "    uint8_t gs_pad", npad++, "[", n, "];\n");
                continue;
            }
            Append(d, "    ", CT(ftypes[i]), " ", Sanitize(f.name), ";\n");
        }
    }

    // Tag constants, one enum per EnumInst: <Mangle>_<Variant>_k.
    set<EnumInst *> tagenums;
    unordered_map<EnumInst *, string> tagprefix;

    void EnsureTagEnum(EnumInst *ei, TypeExpr *anyt) {
        if (tagenums.count(ei)) return;
        tagenums.insert(ei);
        // Use the base (mode-less) mangle as the constant prefix.
        string base = Sanitize(ei->en->name);
        for (auto a : ei->args) Append(base, "_", Mangle(a));
        tagprefix[ei] = base;
        string d = "enum {\n";
        for (size_t vi = 0; vi < ei->en->variants.size(); vi++)
            Append(d, "    ", base, "_", Sanitize(ei->en->variants[vi].name), "_k = ", vi,
                   ",\n");
        d += "};\n\n";
        tdecls += d;
        (void)anyt;
    }

    string TagConst(EnumInst *ei, int vi) {
        if (!tagenums.count(ei)) EnsureTagEnum(ei, nullptr);
        return cat(tagprefix[ei], "_", Sanitize(ei->en->variants[vi].name), "_k");
    }

    // The TY_VARIANT type for (enum type, variant index); cached per pair.
    map<pair<EnumInst *, int>, TypeExpr *> varianttypes;

    TypeExpr *VariantType(TypeExpr *enumtype, int vi) {
        auto ei = EIOf(enumtype);
        auto key = pair<EnumInst *, int>(ei, vi);
        auto it = varianttypes.find(key);
        if (it != varianttypes.end()) return it->second;
        auto t = ast.NewType(TY_VARIANT, enumtype->line);
        t->var = ast.NewDetail<TypeVariant>();
        auto base = enumtype;
        if (enumtype->enu->varmode) {
            base = ast.NewType(TY_ENUM, enumtype->line);
            base->enu = ast.NewDetail<TypeEnum>();
            base->enu->en = enumtype->enu->en;
            base->enu->args = enumtype->enu->args;
            base->enu->inst = ei;
        }
        t->var->adt = base;
        t->var->variant = &ei->en->variants[vi];
        return varianttypes[key] = t;
    }

    // ------------------------------------------------------------------
    // Runtime size of a bytes-class value: a generated per-type walker,
    // gs_size_<mangle>(p). Static-size subruns collapse into constants.

    set<string> sizefns, eqfns;

    // A C expression for the size of the value of type t at ptr; emits the
    // walker function on demand for dynamic types.
    string SizeX(TypeExpr *t, const string &ptr) {
        if (IsFix(t)) return cat(FixedSize(t));
        return cat(SizeFn(t), "(", ptr, ")");
    }

    string SizeFn(TypeExpr *t) {
        auto m = Mangle(t);
        auto name = cat("gs_size_", m);
        if (sizefns.count(m)) return name;
        sizefns.insert(m);
        Append(protos, "static int64_t ", name, "(const uint8_t *p);\n");
        string b;
        Append(b, "static int64_t ", name, "(const uint8_t *p) {\n");
        Append(b, "    const uint8_t *q = p;\n");
        EmitSizeWalk(b, t, "q");
        Append(b, "    return q - p;\n}\n\n");
        code += b;
        return name;
    }

    // Appends statements advancing cursor `q` over one value of type t.
    void EmitSizeWalk(string &b, TypeExpr *t, const string &q) {
        if (IsFix(t)) {
            Append(b, "    ", q, " += ", FixedSize(t), ";\n");
            return;
        }
        switch (t->kind) {
            case TY_INT:   // varint
                Append(b, "    ", q, " += gs_uleb_size(", q, ");\n");
                return;
            case TY_REF:   // varint-width relative reference
                Append(b, "    ", q, " += gs_uleb_size(", q, ");\n");
                return;
            case TY_ARRAY: {
                auto &a = *t->arr;
                switch (a.akind) {
                    case A_VAR: {
                        auto ls = LenStore(t->arr);
                        if (ls == IS_VARINT)
                            Append(b, "    { int64_t n = gs_uleb_read(", q, "); ", q,
                                   " += gs_uleb_size(", q, ");\n");
                        else
                            Append(b, "    { int64_t n = (int64_t)*(", IntCT(ls), " *)", q,
                                   "; ", q, " += ", IntSize(ls), ";\n");
                        EmitSizeElems(b, a.sub, q);
                        Append(b, "    }\n");
                        return;
                    }
                    case A_LIMITED:   // Runtime capacity: [cap u32][len u32][cap slots].
                        Append(b, "    ", q, " += 8 + (int64_t)*(uint32_t *)", q, " * ",
                               FixedSize(a.sub), ";\n");
                        return;
                    default: {        // Resizable: [int64 len][elements].
                        Append(b, "    { int64_t n = *(int64_t *)", q, "; ", q, " += 8;\n");
                        EmitSizeElems(b, a.sub, q);
                        Append(b, "    }\n");
                        return;
                    }
                }
            }
            case TY_STRUCT: {
                auto si = SI(t);
                for (size_t i = 0; i < si->st->fields.size(); i++) {
                    auto &f = si->st->fields[i];
                    // Bytes layouts have no bare-pad alignment; explicit pads count.
                    if (f.ispad) {
                        if (f.padsize > 0) Append(b, "    ", q, " += ", f.padsize, ";\n");
                        continue;
                    }
                    EmitSizeWalk(b, si->ftypes[i], q);
                }
                return;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++) {
                    auto &f = ei->en->variants[vi].fields[i];
                    if (f.ispad) {
                        if (f.padsize > 0) Append(b, "    ", q, " += ", f.padsize, ";\n");
                        continue;
                    }
                    EmitSizeWalk(b, ei->vftypes[vi][i], q);
                }
                return;
            }
            case TY_ENUM: {
                auto ei = EIOf(t);
                auto ts = TagSize(ei->en);
                Append(b, "    switch (*(", IntCT(TagStore(ei->en)), " *)", q, ") {\n");
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                    Append(b, "    case ", vi, ": { ", q, " += ", ts, ";\n");
                    string inner;
                    EmitSizeWalk(inner, VariantType(t, (int)vi), q);
                    b += inner;
                    Append(b, "    } break;\n");
                }
                Append(b, "    }\n");
                return;
            }
            default: assert(false); return;
        }
    }

    void EmitSizeElems(string &b, TypeExpr *elem, const string &q) {
        if (IsFix(elem)) {
            Append(b, "    ", q, " += n * ", FixedSize(elem), ";\n");
        } else {
            Append(b, "    for (int64_t i = 0; i < n; i++) ", q, " += ", SizeFn(elem), "(",
                   q, ");\n");
        }
    }

    // The size of the all-zeroes minimal value of a type (arrays empty, ADTs
    // at variant 0): what a missed qpoll writes so the value stays valid.
    int64_t ZeroSize(TypeExpr *t) {
        if (IsFix(t)) return FixedSize(t);
        switch (t->kind) {
            case TY_INT: case TY_REF: return 1;   // varint / varint relref: one 0 byte.
            case TY_ARRAY:
                switch (t->arr->akind) {
                    case A_VAR:     return LenStore(t->arr) == IS_VARINT ? 1
                                                                         : IntSize(LenStore(t->arr));
                    case A_LIMITED: return 8;
                    default:        return 8;
                }
            case TY_STRUCT: {
                auto si = SI(t);
                int64_t n = 0;
                for (size_t i = 0; i < si->st->fields.size(); i++)
                    if (!si->st->fields[i].ispad) n += ZeroSize(si->ftypes[i]);
                return n;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                int64_t n = 0;
                for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++)
                    if (!ei->en->variants[vi].fields[i].ispad) n += ZeroSize(ei->vftypes[vi][i]);
                return n;
            }
            case TY_ENUM:
                return TagSize(EIOf(t)->en) + ZeroSize(VariantType(t, 0));
            default: assert(false); return 0;
        }
    }

    // ------------------------------------------------------------------
    // Structural equality (§4.5): gs_eq_<mangle>. Fixed values pass by value,
    // bytes values as pointers. Gap-free fixed types shortcut to memcmp.

    bool GapFree(TypeExpr *t) {
        if (!IsFix(t)) return false;
        switch (t->kind) {
            case TY_STRUCT: {
                auto si = SI(t);
                for (size_t i = 0; i < si->st->fields.size(); i++) {
                    if (si->st->fields[i].ispad) return false;
                    if (!GapFree(si->ftypes[i])) return false;
                }
                return true;
            }
            case TY_ENUM: return false;      // Uninitialized trailing payload area.
            case TY_ARRAY:
                if (t->arr->akind == A_LIMITED) return false;   // Uninitialized slots.
                return GapFree(t->arr->sub);
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++) {
                    if (ei->en->variants[vi].fields[i].ispad) return false;
                    if (!GapFree(ei->vftypes[vi][i])) return false;
                }
                return true;
            }
            default: return true;
        }
    }

    bool ScalarEq(TypeExpr *t) {
        return t->kind == TY_INT || t->kind == TY_FLT || t->kind == TY_BOOL ||
               (t->kind == TY_REF && t->ref->lenstorage < 0 && !IsFatRef(t));
    }

    // Equality of two values as a C expression; a/b are values (fixed) or
    // byte pointers (bytes-class). May emit an eq function.
    string EqX(TypeExpr *t, const string &a, const string &b) {
        if (ScalarEq(t)) return cat("(", a, " == ", b, ")");
        if (t->kind == TY_REF && IsFatRef(t))
            return cat("(", a, ".addr == ", b, ".addr)");
        if (t->kind == TY_SLICE && !IsBytesT(t->sub))
            return cat("(", a, ".data == ", b, ".data && ", a, ".len == ", b, ".len)");
        if (IsFix(t)) {
            if (GapFree(t))
                return cat("(memcmp(&", a, ", &", b, ", ", FixedSize(t), ") == 0)");
            return cat(EqFn(t), "(&", a, ", &", b, ")");
        }
        return cat(EqFn(t), "(", a, ", ", b, ")");
    }

    string EqFn(TypeExpr *t) {
        auto m = Mangle(t);
        auto name = cat("gs_eq_", m);
        if (eqfns.count(m)) return name;
        eqfns.insert(m);
        auto bytes = IsBytesT(t);
        auto sig = bytes ? cat("static uint8_t ", name, "(const uint8_t *a, const uint8_t *b)")
                         : cat("static uint8_t ", name, "(const ", CT(t), " *a, const ", CT(t),
                               " *b)");
        Append(protos, sig, ";\n");
        string bo;
        Append(bo, sig, " {\n");
        if (bytes) EmitEqBytes(bo, t);
        else EmitEqFixed(bo, t);
        Append(bo, "}\n\n");
        code += bo;
        return name;
    }

    void EmitEqFixed(string &bo, TypeExpr *t) {
        switch (t->kind) {
            case TY_STRUCT: {
                auto si = SI(t);
                for (size_t i = 0; i < si->st->fields.size(); i++) {
                    auto &f = si->st->fields[i];
                    if (f.ispad) continue;
                    auto fn = Sanitize(f.name);
                    Append(bo, "    if (!", EqX(si->ftypes[i], cat("a->", fn), cat("b->", fn)),
                           ") return 0;\n");
                }
                Append(bo, "    return 1;\n");
                return;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                auto &v = ei->en->variants[vi];
                for (size_t i = 0; i < v.fields.size(); i++) {
                    if (v.fields[i].ispad) continue;
                    auto fn = Sanitize(v.fields[i].name);
                    Append(bo, "    if (!", EqX(ei->vftypes[vi][i], cat("a->", fn),
                                                cat("b->", fn)), ") return 0;\n");
                }
                Append(bo, "    return 1;\n");
                return;
            }
            case TY_ENUM: {
                auto ei = EIOf(t);
                Append(bo, "    if (a->tag != b->tag) return 0;\n    switch (a->tag) {\n");
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                    if (ei->en->variants[vi].fields.empty()) continue;
                    auto un = cat("u.v_", Sanitize(ei->en->variants[vi].name));
                    Append(bo, "    case ", TagConst(ei, (int)vi), ": return ",
                           EqX(VariantType(t, (int)vi), cat("a->", un), cat("b->", un)),
                           ";\n");
                }
                Append(bo, "    default: return 1;\n    }\n");
                return;
            }
            case TY_ARRAY: {
                auto &a = *t->arr;
                if (a.akind == A_FIXED) {
                    Append(bo, "    for (int64_t i = 0; i < ", ArrSize(t->arr), "; i++)\n",
                           "        if (!", EqX(a.sub, "a->e[i]", "b->e[i]"), ") return 0;\n",
                           "    return 1;\n");
                } else {   // Limited, static capacity.
                    Append(bo, "    if (a->len != b->len) return 0;\n",
                           "    for (int64_t i = 0; i < (int64_t)a->len; i++)\n",
                           "        if (!", EqX(a.sub, "a->e[i]", "b->e[i]"), ") return 0;\n",
                           "    return 1;\n");
                }
                return;
            }
            default: assert(false); return;
        }
    }

    void EmitEqBytes(string &bo, TypeExpr *t) {
        // Canonical encodings (minimal varints) make bytewise comparison exact
        // for any pad-free bytes value; walk-compare covers the rest.
        auto padfree = [&](TypeExpr *tt) {
            // Bytes layouts have no bare-pad alignment, but explicit pads and
            // embedded fixed-mode ADTs still make bytes differ.
            function<bool(TypeExpr *)> rec = [&](TypeExpr *x) -> bool {
                if (IsFix(x)) return GapFree(x);
                switch (x->kind) {
                    case TY_INT: case TY_REF: return true;
                    case TY_ARRAY: return rec(x->arr->sub);
                    case TY_STRUCT: {
                        auto si = SI(x);
                        for (size_t i = 0; i < si->st->fields.size(); i++) {
                            if (si->st->fields[i].ispad) return false;
                            if (!rec(si->ftypes[i])) return false;
                        }
                        return true;
                    }
                    case TY_VARIANT: {
                        auto ei = EIVar(x);
                        auto vi = VarIdx(ei->en, x->var->variant);
                        for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++) {
                            if (ei->en->variants[vi].fields[i].ispad) return false;
                            if (!rec(ei->vftypes[vi][i])) return false;
                        }
                        return true;
                    }
                    case TY_ENUM: {
                        auto ei = EIOf(x);
                        for (size_t vi = 0; vi < ei->en->variants.size(); vi++)
                            if (!rec(VariantType(x, (int)vi))) return false;
                        return true;
                    }
                    default: return false;
                }
            };
            return rec(tt);
        };
        if (padfree(t)) {
            Append(bo, "    int64_t na = ", SizeX(t, "a"), ", nb = ", SizeX(t, "b"), ";\n",
                   "    return na == nb && memcmp(a, b, (size_t)na) == 0;\n");
            return;
        }
        // Structural walk with a cursor pair; limited-capacity arrays compare
        // length + live elements, everything else in field order.
        Append(bo, "    const uint8_t *pa = a, *pb = b;\n");
        EmitEqWalk(bo, t, "pa", "pb", 1);
        Append(bo, "    return 1;\n");
    }

    // Compares one value at *pa/*pb (advancing both); returns 0 on mismatch.
    void EmitEqWalk(string &bo, TypeExpr *t, const string &pa, const string &pb, int depth) {
        auto I = string("    ");
        if (IsFix(t)) {
            auto ct = CT(t);
            (void)ct;
            if (GapFree(t)) {
                Append(bo, I, "if (memcmp(", pa, ", ", pb, ", ", FixedSize(t),
                       ") != 0) return 0;\n");
            } else {
                Append(bo, I, "if (!", EqX(t, cat("(*(", CT(t), " *)", pa, ")"),
                                           cat("(*(", CT(t), " *)", pb, ")")), ") return 0;\n");
            }
            Append(bo, I, pa, " += ", FixedSize(t), "; ", pb, " += ", FixedSize(t), ";\n");
            return;
        }
        switch (t->kind) {
            case TY_INT: case TY_REF: {  // varint / varint relref: canonical bytes.
                Append(bo, I, "{ int64_t n = gs_uleb_size(", pa, ");\n",
                       I, "  if (gs_uleb_size(", pb, ") != n || memcmp(", pa, ", ", pb,
                       ", (size_t)n) != 0) return 0;\n",
                       I, "  ", pa, " += n; ", pb, " += n; }\n");
                return;
            }
            case TY_ARRAY: {
                auto &a = *t->arr;
                string lena, lenb;
                auto adv = [&](const string &p, string &out) {
                    if (a.akind == A_VAR) {
                        auto ls = LenStore(t->arr);
                        if (ls == IS_VARINT) {
                            Append(bo, I, "int64_t ", out, " = gs_uleb_read(", p, "); ", p,
                                   " += gs_uleb_size(", p, ");\n");
                        } else {
                            Append(bo, I, "int64_t ", out, " = (int64_t)*(", IntCT(ls), " *)",
                                   p, "; ", p, " += ", IntSize(ls), ";\n");
                        }
                    } else if (a.akind == A_LIMITED) {
                        Append(bo, I, "int64_t ", out, "cap = (int64_t)*(uint32_t *)", p,
                               "; int64_t ", out, " = (int64_t)*(uint32_t *)(", p, " + 4); ",
                               p, " += 8;\n");
                    } else {
                        Append(bo, I, "int64_t ", out, " = *(int64_t *)", p, "; ", p,
                               " += 8;\n");
                    }
                };
                lena = cat("na", depth);
                lenb = cat("nb", depth);
                Append(bo, I, "{\n");
                adv(pa, lena);
                adv(pb, lenb);
                Append(bo, I, "if (", lena, " != ", lenb, ") return 0;\n");
                auto iv = cat("i", depth);
                Append(bo, I, "for (int64_t ", iv, " = 0; ", iv, " < ", lena, "; ", iv,
                       "++) {\n");
                EmitEqWalk(bo, a.sub, pa, pb, depth + 1);
                Append(bo, I, "}\n");
                if (a.akind == A_LIMITED)
                    Append(bo, I, pa, " += (", lena, "cap - ", lena, ") * ", FixedSize(a.sub),
                           "; ", pb, " += (", lenb, "cap - ", lenb, ") * ", FixedSize(a.sub),
                           ";\n");
                Append(bo, I, "}\n");
                return;
            }
            case TY_STRUCT: {
                auto si = SI(t);
                for (size_t i = 0; i < si->st->fields.size(); i++)
                    if (!si->st->fields[i].ispad) EmitEqWalk(bo, si->ftypes[i], pa, pb, depth);
                return;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++)
                    if (!ei->en->variants[vi].fields[i].ispad)
                        EmitEqWalk(bo, ei->vftypes[vi][i], pa, pb, depth);
                return;
            }
            case TY_ENUM: {
                auto ei = EIOf(t);
                auto ts = TagSize(ei->en);
                auto tt = IntCT(TagStore(ei->en));
                Append(bo, I, "if (*(", tt, " *)", pa, " != *(", tt, " *)", pb,
                       ") return 0;\n");
                Append(bo, I, "switch (*(", tt, " *)", pa, ") {\n");
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                    Append(bo, I, "case ", vi, ": {\n");
                    Append(bo, I, pa, " += ", ts, "; ", pb, " += ", ts, ";\n");
                    EmitEqWalk(bo, VariantType(t, (int)vi), pa, pb, depth + 1);
                    Append(bo, I, "} break;\n");
                }
                Append(bo, I, "}\n");
                return;
            }
            default: assert(false); return;
        }
    }

    // ------------------------------------------------------------------
    // Per-specialization call interface. Signature shape (C.3 order):
    //   [declared params (resizable by-value ones add a gs_stack*)]
    //   [free-variable references, §7.5]
    //   [out-pointers for fixed returns after the first]
    //   [destination stacks for nonfixed returns]
    //   [int64_t gs_sp]  [int32_t *gs_rf].
    // The C return value is the first fixed return, else void.

    struct SpecInfo {
        string cname;
        vector<VarDef *> freevars;
        vector<int> refidx;          // Per param: index into spec->roots, or -1.
        bool needssp = false;
        bool hasrf = false;
        int cret = -1;               // Ret index returned as the C value.
    };
    unordered_map<FnSpec *, SpecInfo> sinfo;
    vector<FnSpec *> livespecs;

    // Long-distance return targets (§7.9): id, per-ret TLS channels.
    unordered_map<SFunction *, int> fromids;
    set<SFunction *> fromemitted;

    bool IsPoolParam(FnSpec *sp, size_t i) {
        auto &si = sinfo[sp];
        auto ri = si.refidx[i];
        return ri >= 0 && sp->roots[ri].reusable && sp->argtypes[i]->kind == TY_REF &&
               sp->argtypes[i]->ref->sub->kind == TY_ARRAY &&
               sp->argtypes[i]->ref->sub->arr->akind == A_GROW;
    }

    void CollectSpecs() {
        for (auto sp : ast.fnspecs)
            if (sp->live && sp->body) livespecs.push_back(sp);
        // Names: suffix whenever the goose name maps to more than one live
        // spec (overloads or multiple specializations).
        map<string_view, int> namecount;
        for (auto sp : livespecs) namecount[sp->sf->name]++;
        for (auto sp : livespecs) {
            auto &si = sinfo[sp];
            auto base = Sanitize(sp->sf->name);
            if (sp->sf->name == "main") base = "gs_main";
            if (namecount[sp->sf->name] > 1) Append(base, "_", sp->id);
            si.cname = Unique(base);
            si.hasrf = !sp->needs.empty();
            auto ri = 0;
            for (auto pt : sp->argtypes)
                si.refidx.push_back(pt->kind == TY_REF || pt->kind == TY_SLICE ? ri++ : -1);
            for (size_t i = 0; i < sp->rets.size(); i++)
                if (si.cret < 0 && IsFix(sp->rets[i])) si.cret = (int)i;
            for (auto t : sp->needs)
                if (!fromids.count(t)) fromids[t] = (int)fromids.size() + 1;
        }
        // Free variables: every referenced VarDef another spec owns, in first-
        // appearance order, then a fixpoint pulling in callees' lists.
        for (auto sp : livespecs) {
            auto &fv = sinfo[sp].freevars;
            set<const VarDef *> seen;
            function<void(Node *)> walk = [&](Node *n) {
                if (!n) return;
                auto add = [&](VarDef *v) {
                    if (v && v->ownerspec && v->ownerspec != sp && !v->isglobal &&
                        seen.insert(v).second)
                        fv.push_back(v);
                };
                if (auto id = Is<Ident>(n)) add(id->vdef);
                if (auto c = Is<Call>(n)) walk(c->fvbody);
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
        }
        for (auto changed = true; changed;) {
            changed = false;
            for (auto sp : livespecs) {
                auto &si = sinfo[sp];
                set<const VarDef *> seen(si.freevars.begin(), si.freevars.end());
                function<void(Node *)> walk = [&](Node *n) {
                    if (!n) return;
                    if (auto c = Is<Call>(n)) {
                        auto absorb = [&](FnSpec *k) {
                            if (!k || k == sp || !sinfo.count(k)) return;
                            // Copy: a recursive callee chain could reach our
                            // own list while we grow it.
                            auto kfv = sinfo[k].freevars;
                            for (auto v : kfv) {
                                if (v->ownerspec != sp && seen.insert(v).second) {
                                    si.freevars.push_back(v);
                                    changed = true;
                                }
                            }
                        };
                        if (c->builtin < 0) absorb(c->spec);
                        for (auto d : c->dispatch) absorb(d);
                        walk(c->fvbody);
                    }
                    n->Children([&](Node *ch) { walk(ch); });
                };
                walk(sp->body);
            }
        }
        // needssp: any nonfixed value anywhere in the body, or a callee that
        // needs it. Iterate to a fixpoint (recursion makes one pass short).
        auto ownneed = [&](FnSpec *sp) {
            for (auto pt : sp->argtypes) if (IsBytesT(pt)) return true;
            for (auto rt : sp->rets) if (IsBytesT(rt)) return true;
            auto need = false;
            function<void(Node *)> walk = [&](Node *n) {
                if (!n || need) return;
                if (n->exprtype && n->exprtype->kind != TY_VOID && n->exprtype->kind != TY_FN &&
                    n->exprtype->kind != TY_GENERIC && IsBytesT(n->exprtype)) {
                    need = true;
                    return;
                }
                if (auto c = Is<Call>(n)) walk(c->fvbody);
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
            return need;
        };
        for (auto sp : livespecs) sinfo[sp].needssp = ownneed(sp);
        for (auto changed = true; changed;) {
            changed = false;
            for (auto sp : livespecs) {
                if (sinfo[sp].needssp) continue;
                auto need = false;
                function<void(Node *)> walk = [&](Node *n) {
                    if (!n || need) return;
                    if (auto c = Is<Call>(n)) {
                        if (c->builtin < 0 && c->spec && sinfo.count(c->spec) &&
                            sinfo[c->spec].needssp) need = true;
                        for (auto d : c->dispatch) if (sinfo.count(d) && sinfo[d].needssp) need = true;
                        if (c->builtin == B_QGET || c->builtin == B_QPOLL)
                            if (c->rettypes.size() && IsBytesT(c->rettypes[0])) need = true;
                        walk(c->fvbody);
                    }
                    n->Children([&](Node *ch) { walk(ch); });
                };
                walk(sp->body);
                if (need) { sinfo[sp].needssp = true; changed = true; }
            }
        }
    }

    // The C parameter list of a spec, as (type, name) decl strings; also
    // registers the parameter VarDefs' names and stack expressions.
    string SigParams(FnSpec *sp, bool decls) {
        auto &si = sinfo[sp];
        string s;
        auto add = [&](const string &d) {
            if (!s.empty()) s += ", ";
            s += d;
        };
        for (size_t i = 0; i < sp->params.size(); i++) {
            auto vd = sp->params[i];
            auto pt = sp->argtypes[i];
            auto pn = decls ? LocalName(vd) : cat("p", i);
            if (IsPoolParam(sp, i)) {
                add(cat("gs_pref ", pn));
            } else if (IsBytesT(pt)) {
                add(cat("uint8_t *", pn));
                if (IsResz(pt)) {
                    add(cat("gs_stack *", pn, "_stk"));
                    if (decls) vstk[vd] = cat(pn, "_stk");
                }
            } else {
                add(cat(CT(pt), " ", pn));
            }
        }
        auto fvn = 0;
        for (auto fv : si.freevars) {
            auto fn = decls ? LocalName(const_cast<VarDef *>(fv)) : cat("fv", fvn++);
            auto ft = fv->type;
            if (fv->reusable) {
                add(cat("gs_pref ", fn));
            } else if (IsBytesT(ft)) {
                add(cat("uint8_t *", fn));
                if (IsResz(ft)) {
                    add(cat("gs_stack *", fn, "_stk"));
                    if (decls) vstk[fv] = cat(fn, "_stk");
                }
            } else {
                add(cat(VarCT(fv), " *", fn));
                if (decls) fvptr.insert(fv);
            }
        }
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (IsBytesT(sp->rets[i])) add(cat("gs_stack *gs_dst", i));
            else if ((int)i != si.cret) add(cat(CT(sp->rets[i]), " *gs_r", i));
        }
        if (si.needssp) add("int64_t gs_sp");
        if (si.hasrf) add("int32_t *gs_rf");
        if (s.empty()) s = "void";
        return s;
    }

    string SigRet(FnSpec *sp) {
        auto &si = sinfo[sp];
        return si.cret >= 0 ? CT(sp->rets[si.cret]) : "void";
    }

    // ------------------------------------------------------------------
    // Per-function generation state.

    FnSpec *curspec = nullptr;
    SpecInfo *curinfo = nullptr;
    string body;
    int ind = 1;
    int tmpn = 0;
    int stknext = 0, stkmax = 0;
    bool cursp = false;                  // The current context has a gs_sp value.
    string spexpr;                       // "gs_sp" inside functions, "0" at global init.
    set<string> fnused;                  // Local C identifiers.
    unordered_map<const VarDef *, string> vnames;
    unordered_map<const VarDef *, string> vstk;    // Stack expr per nonfixed local.
    unordered_map<const VarDef *, pair<string, string>> vpool;  // fl base name, fl stack expr.
    set<const VarDef *> fvptr;           // Captured fixed vars arriving as pointers.
    set<const VarDef *> nrvovars;        // Locals aliased to a return destination.
    vector<string> fdstsaves;            // Epilogue restores for gs_fdst_* saves.

    enum { SC_PLAIN, SC_FN, SC_LOOP, SC_BLOCK, SC_IB, SC_STMT };
    struct CScope {
        int kind;
        int stkbase;
        vector<pair<string, string>> saves;   // (stack expr, base var) to restore.
        SFunction *ibsf = nullptr;            // SC_IB: which returns exit here.
        string brklbl, cntlbl;
        bool usedbrk = false, usedcnt = false;
        int dstk = 0;                         // Break/IB value destination kind (Dst::k).
        string dsts;
    };
    vector<CScope> cscopes;

    template<typename... Ts> void L(const Ts &...args) {
        body.append((size_t)ind * 4, ' ');
        Append(body, args...);
        body += '\n';
    }

    string T() { return cat("t", tmpn++); }
    string Lbl() { return cat("L", tmpn++); }

    string SpIdx(int k) { return cat("GS(", spexpr, " + ", k, ")"); }
    string SpTop() { return cat(spexpr, " + ", stknext); }   // First free index.

    void PushSc(int kind) {
        CScope s;
        s.kind = kind;
        s.stkbase = stknext;
        cscopes.push_back(s);
    }

    void EmitRestores(const CScope &s) {
        for (auto i = (int)s.saves.size() - 1; i >= 0; i--)
            L(s.saves[i].first, "->top = ", s.saves[i].second, ";");
    }

    void PopSc() {
        EmitRestores(cscopes.back());
        stknext = cscopes.back().stkbase;
        cscopes.pop_back();
    }

    // Restores for exiting down to (and including) scope index `to`.
    void EmitExitRestores(int to) {
        for (auto i = (int)cscopes.size() - 1; i >= to; i--) EmitRestores(cscopes[i]);
    }

    // Allocates a data stack index; `forlocal` skips enclosing statement
    // scopes so the value survives to the end of the surrounding block.
    // Returns the stack expression; the caller emits `uint8_t *base = X->top;`
    // and registers it via SaveBase.
    string AllocStk(bool forlocal) {
        auto k = stknext++;
        stkmax = std::max(stkmax, stknext);
        if (forlocal)
            for (auto &s : cscopes)
                if (s.kind == SC_STMT && s.stkbase < stknext) s.stkbase = stknext;
        return SpIdx(k);
    }

    void SaveBase(bool forlocal, const string &stk, const string &basevar) {
        for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
            if (forlocal && cscopes[i].kind == SC_STMT) continue;
            cscopes[i].saves.push_back({ stk, basevar });
            return;
        }
        assert(false);
    }

    // Globals keep their names for the whole run; locals are named per
    // function (a captured variable gets an independent name as the hidden
    // parameter of each capturer — arguments are positional).
    unordered_map<const VarDef *, string> gnames;
    unordered_map<const VarDef *, string> gstks;
    unordered_map<const VarDef *, pair<string, string>> gpools;

    string LocalName(VarDef *vd) {
        assert(!vd->isglobal);
        auto it = vnames.find(vd);
        if (it != vnames.end()) return it->second;
        auto base = Sanitize(vd->name);
        auto name = base;
        for (auto n = 2; fnused.count(name) || used.count(name); n++) name = cat(base, "_", n);
        fnused.insert(name);
        return vnames[vd] = name;
    }

    string VName(const VarDef *vd) {
        auto it = vnames.find(vd);
        if (it != vnames.end()) return it->second;
        auto git = gnames.find(vd);
        assert(git != gnames.end());
        return git->second;
    }

    // ------------------------------------------------------------------
    // Locations: an assignable/addressable path resolved to either a typed C
    // lvalue (val) or a byte pointer (bytes values, packed dynamic layouts).
    // The root's data stack (and pool freelist) ride along for growth ops.

    struct Loc {
        string s;
        TypeExpr *t = nullptr;
        bool val = false;
        bool ispref = false;   // s is a gs_pref-typed lvalue (pool reference).
        string stk, fl, flstk;
    };

    // Reference variables with reusable-pool provenance are represented as
    // gs_pref so pool operations stay available through them (§5.4). This is
    // a property of the VarDef (provenance), shared by caller and callee.
    bool PrefVar(const VarDef *vd) {
        auto t = vd->type;
        return t && t->kind == TY_REF && t->ref->lenstorage < 0 && !t->ref->optional &&
               t->ref->sub->kind == TY_ARRAY && t->ref->sub->arr->akind == A_GROW &&
               vd->refreusable;
    }

    // Construction destination: 0 = discard, 1 = assign C lvalue, 2 =
    // construct bytes at the given gs_stack *'s top. `t` records the wanted
    // type where the receiver knows it: optimizer splices can leave a
    // reference-typed tree in a slot whose checked type already decayed, and
    // the pointee load then happens at the leaf against this type.
    struct Dst {
        int k = 0;
        string s;
        TypeExpr *t = nullptr;
    };

    // Whether a value of type `have` needs a pointee load to serve as `want`.
    bool NeedsDeref(TypeExpr *have, TypeExpr *want) {
        return have && want && have->kind == TY_REF && !have->ref->optional &&
               have->ref->lenstorage < 0 && want->kind != TY_REF && want->kind != TY_VOID;
    }

    Loc BytesLoc(const string &ptr, TypeExpr *t, const Loc &from) {
        Loc l;
        l.t = t;
        l.stk = from.stk;
        l.fl = from.fl;
        l.flstk = from.flstk;
        if (IsFix(t)) {
            l.val = true;
            l.s = cat("(*(", CT(t), " *)(", ptr, "))");
        } else {
            l.val = false;
            l.s = ptr;
        }
        return l;
    }

    // Loads the value of a loc holding a (plain or relative) reference and
    // steps to the pointee. Optional locs never get here (narrowing).
    void DerefLoc(Loc &lv, Line ln) {
        assert(lv.t->kind == TY_REF);
        auto &r = *lv.t->ref;
        if (r.lenstorage >= 0) {
            // Relative: base = the offset field's own address. The stored
            // offset is signed despite the unsigned width spelling (S3.9).
            string faddr, off;
            if (r.lenstorage == IS_VARINT) {
                assert(!lv.val);
                faddr = lv.s;
                off = cat("gs_zig_read(", lv.s, ")");
            } else if (lv.val) {
                faddr = cat("((uint8_t *)&", lv.s, ")");
                off = cat("(int64_t)(", RelCT((IntStorage)r.lenstorage), ")", lv.s);
            } else {
                faddr = lv.s;
                off = cat("(int64_t)*(", RelCT((IntStorage)r.lenstorage), " *)(", lv.s, ")");
            }
            auto p = T();
            L("uint8_t *", p, " = ", faddr, " + ", off, ";");
            auto nl = BytesLoc(p, r.sub, lv);
            lv = nl;
            return;
        }
        auto rv = lv.s;   // The lvalue text reads as the reference value.
        if (IsFatPointee(r.sub)) {
            Loc nl;
            nl.t = r.sub;
            nl.val = false;
            nl.s = cat(rv, ".addr");
            nl.stk = cat(rv, ".stk");
            // A pool reference (gs_pref) has the same leading members plus the
            // freelist; keep the companions reachable for pool operations.
            nl.fl = lv.ispref ? cat(rv, ".fl") : lv.fl;
            nl.flstk = lv.ispref ? cat(rv, ".flstk") : lv.flstk;
            lv = nl;
            return;
        }
        if (IsBytesT(r.sub)) {
            Loc nl;
            nl.t = r.sub;
            nl.val = false;
            nl.s = rv;
            lv = nl;
            return;
        }
        Loc nl;
        nl.t = r.sub;
        nl.val = true;
        nl.s = cat("(*", rv, ")");
        lv = nl;
        (void)ln;
    }

    Loc VarLoc(VarDef *vd) {
        Loc l;
        l.t = vd->type;
        auto name = VName(vd);
        if (vd->reusable) {
            // A reusable pool: locals/globals have companions; a captured
            // pool arrives as a gs_pref.
            auto pit = vpool.find(vd);
            auto git = gpools.find(vd);
            if (pit != vpool.end() || git != gpools.end()) {
                auto &p = pit != vpool.end() ? pit->second : git->second;
                l.val = false;
                l.s = name;
                l.stk = VStkOf(vd);
                l.fl = p.first;
                l.flstk = p.second;
            } else {
                l.val = false;
                l.s = cat(name, ".addr");
                l.stk = cat(name, ".stk");
                l.fl = cat(name, ".fl");
                l.flstk = cat(name, ".flstk");
            }
            return l;
        }
        if (IsBytesT(vd->type)) {
            l.val = false;
            l.s = name;
            l.stk = VStkOf(vd);
            return l;
        }
        l.val = true;
        l.s = fvptr.count(vd) ? cat("(*", name, ")") : name;
        if (PrefVar(vd)) {
            l.ispref = true;
            l.fl = cat(l.s, ".fl");
            l.flstk = cat(l.s, ".flstk");
        }
        return l;
    }

    // The C type a variable's storage uses (gs_pref for pool references).
    string VarCT(const VarDef *vd) {
        if (PrefVar(vd)) { EmitCoreTypes(); return "gs_pref"; }
        return CT(vd->type);
    }

    // The data stack a nonfixed variable's value lives on, if known here.
    string VStkOf(const VarDef *vd) {
        auto it = vstk.find(vd);
        if (it != vstk.end()) return it->second;
        auto git = gstks.find(vd);
        if (git != gstks.end()) return git->second;
        return "";
    }

    // A byte-pointer expression for field `fieldidx` within the bytes value at
    // `base` (fields/ftypes from a struct instance or a variant). Emits cursor
    // statements when a dynamic-size field precedes the target.
    string FieldPtr(const string &base, const vector<Field> &fields,
                    const vector<TypeExpr *> &ftypes, int fieldidx) {
        int64_t off = 0;
        string cur;   // Cursor variable once dynamic fields intervene.
        for (auto i = 0; i < fieldidx; i++) {
            auto &f = fields[i];
            if (f.ispad) { off += f.padsize > 0 ? f.padsize : 0; continue; }
            if (IsFix(ftypes[i])) { off += FixedSize(ftypes[i]); continue; }
            if (cur.empty()) {
                cur = T();
                L("uint8_t *", cur, " = ", base, " + ", off, ";");
            } else if (off) {
                L(cur, " += ", off, ";");
            }
            off = 0;
            L(cur, " += ", SizeX(ftypes[i], cur), ";");
        }
        if (cur.empty()) return off ? cat(base, " + ", off) : base;
        return off ? cat(cur, " + ", off) : cur;
    }

    // Elements pointer + length expression of an array-typed loc, all kinds.
    struct ArrView {
        string elems;      // Pointer expression (typed for val fixed/limited).
        string len;        // int64 length expression.
        string lenlv;      // Length lvalue for ops that change it (may be typed).
        TypeExpr *elem = nullptr;
        bool typedelems = false;   // elems is CT* (else uint8_t*).
    };

    ArrView ArrayView(const Loc &lv, Line ln) {
        auto t = lv.t;
        ArrView v;
        if (t->kind == TY_SLICE) {
            v.elem = t->sub;
            v.elems = cat(lv.s, ".data");
            v.len = cat(lv.s, ".len");
            v.typedelems = !IsBytesT(t->sub);
            return v;
        }
        assert(t->kind == TY_ARRAY);
        auto &a = *t->arr;
        v.elem = a.sub;
        switch (a.akind) {
            case A_FIXED:
                assert(lv.val);
                v.elems = cat(lv.s, ".e");
                v.len = cat(ArrSize(t->arr));
                v.typedelems = true;
                return v;
            case A_LIMITED:
                if (lv.val) {
                    v.elems = cat(lv.s, ".e");
                    v.len = cat("(int64_t)", lv.s, ".len");
                    v.lenlv = cat(lv.s, ".len");
                    v.typedelems = true;
                } else {
                    v.elems = cat("((", lv.s, ") + 8)");
                    v.len = cat("(int64_t)*(uint32_t *)((", lv.s, ") + 4)");
                    v.lenlv = cat("(*(uint32_t *)((", lv.s, ") + 4))");
                }
                return v;
            case A_VAR: {
                assert(!lv.val);
                auto ls = LenStore(t->arr);
                if (ls == IS_VARINT) {
                    // Sequential unless elements are fixed; length prefix is
                    // dynamic, so materialize both once.
                    auto p = T();
                    L("uint8_t *", p, " = (", lv.s, ") + gs_uleb_size(", lv.s, ");");
                    v.elems = p;
                    v.len = cat("gs_uleb_read(", lv.s, ")");
                } else {
                    v.elems = cat("((", lv.s, ") + ", IntSize(ls), ")");
                    v.len = cat("(int64_t)*(", IntCT(ls), " *)(", lv.s, ")");
                }
                return v;
            }
            default:   // Grow / grow-shrink: [int64 len][elements].
                assert(!lv.val);
                v.elems = cat("((", lv.s, ") + 8)");
                v.len = cat("(*(int64_t *)(", lv.s, ")", ")");
                v.lenlv = cat("(*(int64_t *)(", lv.s, ")", ")");
                return v;
        }
        (void)ln;
    }

    // A side-effect-free C expression for a node: literals and plain scalar
    // variables pass through, anything else lands in a temp first.
    string GenPure(Node *n) {
        if (auto i = Is<IntLit>(n)) return IntStr(i->val);
        if (auto id = Is<Ident>(n)) {
            if (id->vdef && !id->vdef->captured && !IsBytesT(id->vdef->type) &&
                id->vdef->type->kind != TY_REF && !fvptr.count(id->vdef))
                return GenX(n);
        }
        auto x = GenX(n);
        // Temps generated by GenX are already single identifiers.
        auto simple = !x.empty() && (isalpha((uint8_t)x[0]) || x[0] == '_');
        for (auto c : x) simple = simple && (isalnum((uint8_t)c) || c == '_');
        if (simple) return x;
        auto t = T();
        L(CT(n->exprtype), " ", t, " = ", x, ";");
        return t;
    }

    static string IntStr(int64_t v) {
        if (v == INT64_MIN) return "(-9223372036854775807LL - 1)";
        if (v > INT32_MAX || v < INT32_MIN) return cat(v, "LL");
        return cat(v);
    }

    static string FltStr(double v, bool f32) {
        string s;
        CatOne(s, v);
        if (s.find('.') == string::npos && s.find('e') == string::npos &&
            s.find("inf") == string::npos && s.find("nan") == string::npos)
            s += ".0";
        if (f32) s += "f";
        return s;
    }

    // Indexed element location with bounds check (elided for provably
    // in-range constant indices into fixed arrays).
    Loc IndexLoc(Loc lv, Node *idxnode, Line ln) {
        if (lv.t->kind == TY_REF) DerefLoc(lv, ln);
        auto v = ArrayView(lv, ln);
        auto idx = GenPure(idxnode);
        string ix;
        auto il = Is<IntLit>(idxnode);
        auto statlen = lv.t->kind == TY_ARRAY && lv.t->arr->akind == A_FIXED
                           ? ArrSize(lv.t->arr) : -1;
        if (il && statlen >= 0 && il->val >= 0 && il->val < statlen) ix = idx;
        else ix = cat("GS_IDX(", idx, ", ", v.len, ", ", WhereStr(ln), ")");
        if (v.typedelems) {
            Loc r;
            r.t = v.elem;
            r.val = true;
            r.s = cat(v.elems, "[", ix, "]");
            r.stk = lv.stk;
            r.fl = lv.fl;
            r.flstk = lv.flstk;
            return r;
        }
        assert(IsFix(v.elem));   // Variable elements are never indexed (TC).
        return BytesLoc(cat("(", v.elems, " + ", ix, " * ", FixedSize(v.elem), ")"), v.elem,
                        lv);
    }

    Loc GenLoc(Node *n) {
        if (auto id = Is<Ident>(n)) {
            assert(id->vdef);
            return VarLoc(id->vdef);
        }
        if (auto d = Is<Dot>(n)) {
            auto lv = GenLoc(d->obj);
            if (lv.t->kind == TY_REF) DerefLoc(lv, d->line);
            return MemberLoc(lv, d);
        }
        if (auto ix = Is<Index>(n)) {
            auto lv = GenLoc(ix->obj);
            if (lv.t->kind == TY_REF) DerefLoc(lv, ix->line);
            return IndexLoc(lv, ix->idx, ix->line);
        }
        // Any other expression: an addressed temporary (TC's LValueBase).
        Loc l;
        l.t = n->exprtype;
        if (IsBytesT(n->exprtype)) {
            string stk;
            l.s = GenPtr(n, &stk);
            l.stk = stk;
            l.val = false;
        } else {
            auto t = T();
            L(CT(n->exprtype), " ", t, " = ", GenX(n), ";");
            l.s = t;
            l.val = true;
        }
        return l;
    }

    Loc MemberLoc(Loc lv, Dot *d) {
        assert(d->fieldidx >= 0);
        if (lv.t->kind == TY_STRUCT) {
            auto si = SI(lv.t);
            auto ft = si->ftypes[d->fieldidx];
            if (lv.val) {
                Loc r = lv;
                r.t = ft;
                r.s = cat(lv.s, ".", Sanitize(si->st->fields[d->fieldidx].name));
                return r;
            }
            return BytesLoc(FieldPtr(lv.s, si->st->fields, si->ftypes, d->fieldidx), ft, lv);
        }
        assert(lv.t->kind == TY_VARIANT);
        auto ei = EIVar(lv.t);
        auto vi = VarIdx(ei->en, lv.t->var->variant);
        auto ft = ei->vftypes[vi][d->fieldidx];
        if (lv.val) {
            Loc r = lv;
            r.t = ft;
            r.s = cat(lv.s, ".", Sanitize(ei->en->variants[vi].fields[d->fieldidx].name));
            return r;
        }
        return BytesLoc(FieldPtr(lv.s, ei->en->variants[vi].fields, ei->vftypes[vi],
                                 d->fieldidx), ft, lv);
    }

    // ------------------------------------------------------------------
    // Expression values. GenX produces a C expression for fixed-class values
    // (possibly after emitting statements); GenPtr produces a byte pointer to
    // a bytes-class value. Both follow the node's checked exprtype, which
    // already encodes widening and reference decay.

    static bool IsCtl(Node *n) {
        return Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<Block>(n) || Is<EarlyBlock>(n) ||
               Is<LoopExpr>(n) || Is<InlineBlock>(n);
    }

    // The value of a location, as the given expression type expects: storage
    // widening is implicit in C, varints decode, relative references load,
    // and a reference loc decays to its pointee when the exprtype says so.
    string LoadLoc(Loc lv, TypeExpr *et, Line ln) {
        if (lv.t->kind == TY_REF && et->kind != TY_REF &&
            !(IsOpt(lv.t) && et->kind == TY_REF)) {
            DerefLoc(lv, ln);
            return LoadLoc(lv, et, ln);
        }
        if (lv.t->kind == TY_REF && lv.t->ref->lenstorage >= 0) {
            // Relative reference load: an ordinary (possibly fat) reference;
            // a zero offset is the null of the optional form (§3.9).
            auto &r = *lv.t->ref;
            string faddr, off;
            if (r.lenstorage == IS_VARINT) {
                assert(!lv.val);
                faddr = lv.s;
                off = cat("gs_zig_read(", lv.s, ")");
            } else if (lv.val) {
                faddr = cat("((uint8_t *)&", lv.s, ")");
                off = cat("(int64_t)(", RelCT((IntStorage)r.lenstorage), ")", lv.s);
            } else {
                faddr = lv.s;
                off = cat("(int64_t)*(", RelCT((IntStorage)r.lenstorage), " *)(", lv.s, ")");
            }
            auto ov = T(), p = T();
            L("int64_t ", ov, " = ", off, ";");
            L("uint8_t *", p, " = ", ov, " ? ", faddr, " + ", ov, " : NULL;");
            if (IsFatPointee(r.sub)) {
                auto t = T();
                L("gs_rref ", t, " = { ", p, ", ", lv.stk, " };");
                return t;
            }
            if (IsBytesT(r.sub)) return p;
            return cat("((", CT(r.sub), " *)", p, ")");
        }
        if (!lv.val) {
            assert(IsBytesT(lv.t));
            if (lv.t->kind == TY_INT)   // varint field read: widen to int.
                return cat("gs_zig_read(", lv.s, ")");
            if (et && IsFix(et) && !TEq(lv.t, et)) return AdaptToFixed(lv, et, ln);
            return lv.s;   // Bytes value: the pointer is the currency.
        }
        if (et && IsFix(et) && lv.val && lv.t->kind == TY_SLICE && et->kind == TY_ARRAY)
            return AdaptToFixed(lv, et, ln);
        if (et && et->kind == TY_SLICE && lv.t->kind == TY_ARRAY) {
            // Whole-array argument to a slice parameter (§3.10).
            auto v = ArrayView(lv, ln);
            auto t = T();
            auto dp = v.typedelems && IsBytesT(et->sub)
                          ? cat("(uint8_t *)(", v.elems, ")") : string(v.elems);
            if (!v.typedelems && !IsBytesT(et->sub))
                dp = cat("(", CT(et->sub), " *)(", v.elems, ")");
            L(CT(et), " ", t, " = { ", dp, ", ", v.len, " };");
            return t;
        }
        if (lv.ispref && et && et->kind == TY_REF) {
            // A pool reference read as a plain reference drops the freelist.
            auto t = T();
            L("gs_rref ", t, " = { ", lv.s, ".addr, ", lv.s, ".stk };");
            return t;
        }
        return lv.s;
    }

    // A fixed C value built from a differently-represented source: a
    // variable-mode ADT read into a fixed-mode context, or an array/slice
    // into a static-capacity limited array.
    string AdaptToFixed(Loc lv, TypeExpr *et, Line ln) {
        if (et->kind == TY_ENUM) {
            auto ei = EIOf(et);
            auto ts = TagSize(ei->en);
            auto tv = T();
            L(CT(et), " ", tv, ";");
            L(tv, ".tag = *(", IntCT(TagStore(ei->en)), " *)(", lv.s, ")", ";");
            L("switch (", tv, ".tag) {");
            for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                auto psz = VariantLayout(ei, (int)vi).size;
                L("case ", TagConst(ei, (int)vi), ":");
                ind++;
                if (psz)
                    L("memcpy(&", tv, ".u.v_", Sanitize(ei->en->variants[vi].name), ", ",
                      lv.s, " + ", ts, ", ", psz, ");");
                L("break;");
                ind--;
            }
            L("}");
            return tv;
        }
        assert(et->kind == TY_ARRAY && et->arr->akind == A_LIMITED);
        auto v = ArrayView(lv, ln);
        auto nn = T();
        L("int64_t ", nn, " = ", v.len, ";");
        L("if (", nn, " > ", ArrSize(et->arr),
          ") gs_abort(\"limited array capacity exceeded\", ", WhereStr(ln), ");");
        auto tv = T();
        L(CT(et), " ", tv, ";");
        L(tv, ".len = (", IntCT(LenStore(et->arr)), ")", nn, ";");
        L("memcpy(", tv, ".e, ", v.elems, ", (size_t)(", nn, " * ", FixedSize(et->arr->sub),
          "));");
        return tv;
    }

    string BytesAddrOf(const Loc &lv) {
        return lv.val ? cat("(uint8_t *)&", lv.s) : lv.s;
    }

    // &lvalue (§3.8): the reference value. On a location holding a reference,
    // yields the stored reference.
    string GenRefVal(Node *child, Line ln) {
        auto lv = GenLoc(child);
        if (lv.t->kind == TY_REF) {
            if (lv.t->ref->lenstorage >= 0) return LoadLoc(lv, nullptr, ln);
            return lv.s;
        }
        if (IsFatPointee(lv.t)) {
            assert(!lv.stk.empty());
            auto t = T();
            L("gs_rref ", t, " = { ", lv.s, ", ", lv.stk, " };");
            return t;
        }
        if (!lv.val) return lv.s;
        return cat("&", lv.s);
    }

    // GenX, loading the pointee when an optimizer splice left a reference
    // where the context's checked type had already decayed.
    string GenXD(Node *n, TypeExpr *want) {
        if (NeedsDeref(n->exprtype, want)) {
            auto sub = n->exprtype->ref->sub;
            auto x = GenX(n);
            if (IsFatPointee(sub) || IsBytesT(sub)) return x;   // Byte-pointer currency.
            return cat("(*", x, ")");
        }
        return GenX(n);
    }

    string GenTruth(Node *n) {
        auto x = GenX(n);
        auto t = n->exprtype;
        if (t->kind == TY_REF && IsFatPointee(t->ref->sub)) return cat("(", x, ".addr != 0)");
        return x;
    }

    string GenX(Node *n) {
        auto et = n->exprtype;
        if (auto i = Is<IntLit>(n)) return IntStr(i->val);
        if (auto f = Is<FltLit>(n))
            return FltStr(f->val, et && et->kind == TY_FLT && et->fltstorage == FS_F32);
        if (auto b = Is<BoolLit>(n)) return b->val ? "1" : "0";
        if (Is<NullLit>(n)) {
            if (et && et->kind == TY_REF && IsFatPointee(et->ref->sub)) {
                auto t = T();
                L("gs_rref ", t, " = { 0, 0 };");
                return t;
            }
            return "NULL";
        }
        if (auto s = Is<StrLit>(n)) return GenStrVal(s);
        if (auto id = Is<Ident>(n)) {
            assert(id->vdef);   // Named-function values never reach runtime.
            return LoadLoc(VarLoc(id->vdef), et, n->line);
        }
        if (auto u = Is<Unary>(n)) {
            if (u->op == T_BITAND) return GenRefVal(u->child, u->line);
            auto x = GenX(u->child);
            switch (u->op) {
                case T_MINUS:
                    if (u->child->exprtype->kind == TY_FLT) return cat("(-", x, ")");
                    return cat("GS_NEG(", x, ")");
                case T_NOT: {
                    auto ct = u->child->exprtype;
                    if (ct->kind == TY_REF && IsFatPointee(ct->ref->sub))
                        return cat("(", x, ".addr == 0)");
                    return cat("(uint8_t)(!", x, ")");
                }
                case T_BITNOT: return cat("(~", x, ")");
                default: assert(false); return x;
            }
        }
        if (auto b = Is<Binary>(n)) return GenBinary(b);
        if (auto d = Is<Dot>(n)) return GenDotX(d);
        if (auto ix = Is<Index>(n)) {
            auto lv = GenLoc(ix);
            return LoadLoc(lv, et, n->line);
        }
        if (auto se = Is<SliceExpr>(n)) return GenSlice(se);
        if (auto ac = Is<AsCast>(n)) return GenCast(ac);
        if (auto c = Is<Call>(n)) {
            auto rets = EmitCall(c, Dst {});
            assert(!rets.empty());
            return CallVal0(c, rets[0]);
        }
        if (auto sl = Is<StructLit>(n)) return GenFixedStructLit(sl);
        if (auto al = Is<ArrayLit>(n)) return GenFixedArrayLit(al);
        if (IsCtl(n)) {
            auto t = T();
            L(CT(et), " ", t, ";");
            GenAny(n, Dst { 1, t });
            return t;
        }
        Fail(n->line, "unsupported expression node");
    }

    // Fixed struct/variant/ADT literal as a C value. Relative-reference
    // fields store against the member's own address (only meaningful when the
    // value stays put; the typechecker's root rules keep this sane).
    string GenFixedStructLit(StructLit *sl) {
        auto et = sl->exprtype;
        auto tv = T();
        L(CT(et), " ", tv, ";");
        auto fieldset = [&](const string &base, const vector<Field> &fields,
                            const vector<TypeExpr *> &ftypes, const vector<Node *> &defaults) {
            for (size_t i = 0; i < fields.size(); i++) {
                if (fields[i].ispad) continue;
                Node *init = nullptr;
                for (size_t k = 0; k < sl->fieldindices.size(); k++)
                    if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
                if (!init && i < defaults.size() && defaults[i]) init = defaults[i];
                auto ft = ftypes[i];
                auto path = cat(base, ".", Sanitize(fields[i].name));
                if (!init) {   // Omitted optional: null.
                    assert(ft->kind == TY_REF);
                    L("memset(&", path, ", 0, sizeof(", path, "));");
                    continue;
                }
                if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
                    EmitRelStoreAt(cat("(uint8_t *)&", path), ft, GenX(init), sl->line);
                    continue;
                }
                GenAny(init, Dst { 1, path });
            }
        };
        if (et->kind == TY_STRUCT) {
            auto si = SI(et);
            fieldset(tv, si->st->fields, si->ftypes, si->defaults);
            return tv;
        }
        if (et->kind == TY_VARIANT) {
            auto ei = EIVar(et);
            auto vi = VarIdx(ei->en, et->var->variant);
            if (ei->en->variants[vi].fields.empty()) L("memset(&", tv, ", 0, sizeof(", tv, "));");
            fieldset(tv, ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi]);
            return tv;
        }
        assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
        auto ei = EIOf(et);
        auto vi = VarIdx(ei->en, sl->variant);
        L(tv, ".tag = ", TagConst(ei, vi), ";");
        if (!ei->en->variants[vi].fields.empty())
            fieldset(cat(tv, ".u.v_", Sanitize(ei->en->variants[vi].name)),
                     ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi]);
        return tv;
    }

    string GenFixedArrayLit(ArrayLit *al) {
        auto et = al->exprtype;
        if (et->kind == TY_SLICE) {
            // A literal in slice position: a temporary fixed array, sliced
            // whole (§3.10; rooted as a temporary by the checker).
            auto count = al->fillval ? ((IntLit *)al->fillcount)->val
                                     : (int64_t)al->elems.size();
            auto at = ast.NewType(TY_ARRAY, al->line);
            at->arr = ast.NewDetail<TypeArray>();
            at->arr->sub = et->sub;
            at->arr->akind = A_FIXED;
            at->arr->size = count;
            auto save = al->exprtype;
            al->exprtype = at;
            auto av = GenFixedArrayLit(al);
            al->exprtype = save;
            auto t = T();
            L(CT(et), " ", t, " = { ", av, ".e, ", count, " };");
            return t;
        }
        assert(et->kind == TY_ARRAY);
        auto tv = T();
        L(CT(et), " ", tv, ";");
        if (et->arr->akind == A_LIMITED) {
            auto count = al->fillval ? ((IntLit *)al->fillcount)->val
                                     : (int64_t)al->elems.size();
            L(tv, ".len = ", count, ";");
        }
        if (al->fillval) {
            auto fc = Is<IntLit>(al->fillcount);
            assert(fc);
            auto fv = GenPure(al->fillval);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", fc->val, "; ", iv, "++) ", tv, ".e[",
              iv, "] = ", fv, ";");
            return tv;
        }
        for (size_t i = 0; i < al->elems.size(); i++)
            GenAny(al->elems[i], Dst { 1, cat(tv, ".e[", i, "]") });
        return tv;
    }

    // Bytes-class value of an expression; optionally reports the data stack
    // it lives on (temporaries land on a fresh statement-scoped stack).
    string GenPtr(Node *n, string *stkout = nullptr) {
        if (stkout) stkout->clear();
        if (auto id = Is<Ident>(n)) {
            auto lv = VarLoc(id->vdef);
            if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
            if (stkout) *stkout = lv.stk;
            return lv.s;
        }
        if (Is<Dot>(n) || Is<Index>(n)) {
            auto lv = GenLoc(n);
            if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
            if (stkout) *stkout = lv.stk;
            assert(!lv.val);
            return lv.s;
        }
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
            // A reference decayed back to a bytes pointee.
            auto lv = GenLoc(u->child);
            if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
            if (stkout) *stkout = lv.stk;
            return lv.s;
        }
        if (auto s = Is<StrLit>(n)) return GenStrBytes(s);
        // Everything else constructs on a fresh temp stack.
        auto stk = AllocStk(false);
        auto base = T();
        L("uint8_t *", base, " = ", stk, "->top;");
        SaveBase(false, stk, base);
        GenAny(n, Dst { 2, stk });
        if (stkout) *stkout = stk;
        return base;
    }

    // ------------------------------------------------------------------
    // String literals: static byte data. As a slice: { data, len }. As a
    // u8[...] value: a static [lenfield][bytes] image (emitted writable, since
    // §9.5's laundering makes writes through read-back references legal).

    unordered_map<string, pair<string, string>> strdata;  // text -> (raw name, none).
    unordered_map<string, string> strval;                 // mangle+text -> value name.

    string StrRaw(const string &v) {
        auto it = strdata.find(v);
        if (it != strdata.end()) return it->second.first;
        auto name = Unique(cat("gs_str", strdata.size()));
        Append(pdata, "static uint8_t ", name, "[", v.size() ? v.size() : 1, "] = ", CStr(v),
               ";\n");
        strdata[v] = { name, "" };
        return name;
    }

    string GenStrVal(StrLit *s) {
        auto et = s->exprtype;
        if (et->kind == TY_SLICE) {
            auto t = T();
            L(CT(et), " ", t, " = { ", StrRaw(s->val), ", ", s->val.size(), " };");
            return t;
        }
        // A fixed u8[k] array value.
        assert(et->kind == TY_ARRAY && et->arr->akind == A_FIXED);
        auto t = T();
        L(CT(et), " ", t, ";");
        if (!s->val.empty())
            L("memcpy(", t, ".e, ", StrRaw(s->val), ", ", s->val.size(), ");");
        return t;
    }

    string GenStrBytes(StrLit *s) {
        auto et = s->exprtype;
        assert(et->kind == TY_ARRAY);
        auto key = cat(Mangle(et), "|", s->val);
        auto it = strval.find(key);
        if (it != strval.end()) return it->second;
        auto ls = LenStore(et->arr);
        auto name = Unique(cat("gs_strv", strval.size()));
        if (ls == IS_VARINT) {
            uint8_t lenbuf[10];
            uint64_t v = s->val.size();
            string enc;
            auto nn = 0;
            do {
                lenbuf[nn] = v & 0x7f;
                v >>= 7;
                if (v) lenbuf[nn] |= 0x80;
                nn++;
            } while (v);
            enc.assign((char *)lenbuf, nn);
            enc += s->val;
            Append(pdata, "static uint8_t ", name, "[", enc.size(), "] = ", CStr(enc),
                   ";\n");
        } else {
            Append(pdata, "static struct { ", IntCT(ls), " len; uint8_t s[",
                   s->val.size() ? s->val.size() : 1, "]; } ", name, " = { ",
                   s->val.size(), ", ", CStr(s->val), " };\n");
        }
        auto expr = cat("((uint8_t *)&", name, ")");
        strval[key] = expr;
        return expr;
    }

    // ------------------------------------------------------------------
    // Binary operators. Operand exprtypes are already decayed/widened.

    string GenBinary(Binary *b) {
        auto op = b->op;
        if (op == T_ANDAND || op == T_OROR) {
            // Short-circuit with left-to-right statement emission: the right
            // operand's statements may only run when the left allows.
            auto l = GenTruth(b->left);
            auto t = T();
            L("uint8_t ", t, " = (uint8_t)(", l, op == T_ANDAND ? " != 0);" : " != 0);");
            L("if (", op == T_ANDAND ? t : cat("!", t), ") {");
            ind++;
            auto r = GenTruth(b->right);
            L(t, " = (uint8_t)(", r, " != 0);");
            ind--;
            L("}");
            return t;
        }
        auto lt = OperandT(b->left->exprtype), rt = OperandT(b->right->exprtype);
        // Null tests (§3.8).
        if (op == T_EQ || op == T_NEQ) {
            auto lnull = Is<NullLit>(b->left) != nullptr, rnull = Is<NullLit>(b->right) != nullptr;
            if (lnull || rnull) {
                if (lnull && rnull) return op == T_EQ ? "1" : "0";
                auto other = lnull ? b->right : b->left;
                // A narrowed optional variable's exprtype may already be the
                // decayed pointee; the null test reads the reference itself.
                string x;
                TypeExpr *ot = other->exprtype;
                if (auto id = Is<Ident>(other); id && id->vdef && IsOpt(id->vdef->type)) {
                    auto lv = VarLoc(id->vdef);
                    x = lv.s;
                    ot = id->vdef->type;
                } else {
                    x = GenX(other);
                }
                auto addr = ot->kind == TY_REF && IsFatPointee(ot->ref->sub)
                                ? cat(x, ".addr") : x;
                return cat("(", addr, op == T_EQ ? " == NULL)" : " != NULL)");
            }
        }
        // Order of evaluation is left-to-right (§2): if the right operand
        // needs statements, the left must land in a temp first.
        auto leftfirst = HasStmts(b->right);
        string l, r;
        if (leftfirst) {
            l = GenPureVal(b->left);
            r = GenVal(b->right);
        } else {
            l = GenVal(b->left);
            r = GenVal(b->right);
        }
        auto isint = lt->kind == TY_INT;
        auto isflt = lt->kind == TY_FLT;
        auto f32 = b->exprtype && b->exprtype->kind == TY_FLT &&
                   b->exprtype->fltstorage == FS_F32;
        switch (op) {
            case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
                if (isflt) {
                    if (op == T_MOD) return cat(f32 ? "fmodf(" : "fmod(", l, ", ", r, ")");
                    const char *o = op == T_PLUS ? " + " : op == T_MINUS ? " - "
                                    : op == T_MUL ? " * " : " / ";
                    return cat("(", l, o, r, ")");
                }
                if (isint) {
                    switch (op) {
                        case T_PLUS:  return cat("GS_ADD(", l, ", ", r, ")");
                        case T_MINUS: return cat("GS_SUB(", l, ", ", r, ")");
                        case T_MUL:   return cat("GS_MUL(", l, ", ", r, ")");
                        case T_DIV:   return cat("gs_idiv(", l, ", ", r, ", ",
                                                 WhereStr(b->line), ")");
                        default:      return cat("gs_imod(", l, ", ", r, ", ",
                                                 WhereStr(b->line), ")");
                    }
                }
                // Elementwise math on identical struct/fixed-array types (§6.1).
                return GenElemwise(b, l, r);
            }
            case T_LT: case T_GT: case T_LTEQ: case T_GTEQ: {
                const char *o = op == T_LT ? " < " : op == T_GT ? " > "
                                : op == T_LTEQ ? " <= " : " >= ";
                return cat("(uint8_t)(", l, o, r, ")");
            }
            case T_EQ: case T_NEQ: {
                auto eq = GenEquality(lt, rt, b->left, b->right, l, r);
                return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
            }
            case T_BITAND: return cat("(", l, " & ", r, ")");
            case T_BITOR:  return cat("(", l, " | ", r, ")");
            case T_XOR:    return cat("(", l, " ^ ", r, ")");
            case T_SHL:    return cat("GS_SHL(", l, ", ", r, ")");
            case T_SHR:    return cat("GS_SHR(", l, ", ", r, ")");
            default: assert(false); return l;
        }
    }

    // Operand exprtype as an operator sees it: a spliced reference reads as
    // its pointee (widened like any load).
    TypeExpr *OperandT(TypeExpr *t) {
        if (t && t->kind == TY_REF && !t->ref->optional && t->ref->lenstorage < 0) {
            auto p2 = t->ref->sub;
            if (p2->kind == TY_INT && p2->intstorage != IS_VARINT &&
                CanI(p2->intstorage) != IS_INT) return ast.inttypes[IS_INT];
            if (p2->kind == TY_FLT && p2->fltstorage == FS_F64) return ast.flttypes[FS_FLT];
            return p2;
        }
        return t;
    }

    // GenX or GenPtr per the operand's rep; operators see spliced references
    // as their pointees.
    string GenVal(Node *n) {
        auto et = OperandT(n->exprtype);
        if (et != n->exprtype) return GenXD(n, et);   // Bytes pointees share the currency.
        return IsBytesT(et) ? GenPtr(n) : GenX(n);
    }
    string GenPureVal(Node *n) {
        auto et = OperandT(n->exprtype);
        if (et != n->exprtype) return GenXD(n, et);
        if (IsBytesT(et)) return GenPtr(n);
        return GenPure(n);
    }

    bool HasStmts(Node *n) {
        if (!n) return false;
        if (Is<Call>(n) || IsCtl(n) || Is<StrLit>(n)) return true;
        auto found = false;
        n->Children([&](Node *c) { found = found || HasStmts(c); });
        return found;
    }

    string GenEquality(TypeExpr *lt, TypeExpr *rt, Node *ln, Node *rn, const string &l,
                       const string &r) {
        (void)rt; (void)ln; (void)rn;
        if (ScalarEq(lt) || (lt->kind == TY_REF && IsFatRef(lt)) || lt->kind == TY_SLICE) {
            if (lt->kind == TY_SLICE) {
                // Slices compare structurally: length then elements (§4.5).
                return GenSliceEq(lt, l, r);
            }
            return EqX(lt, l, r);
        }
        if (IsBytesT(lt)) return cat(EqFn(lt), "(", l, ", ", r, ")");
        return EqX(lt, l, r);
    }

    string GenSliceEq(TypeExpr *st, const string &l, const string &r) {
        auto elem = st->sub;
        auto t = T();
        L("uint8_t ", t, " = ", l, ".len == ", r, ".len;");
        L("if (", t, ") {");
        ind++;
        if (ScalarEq(elem) && GapFree(elem)) {
            L(t, " = memcmp(", l, ".data, ", r, ".data, (size_t)(", l, ".len * ",
              FixedSize(elem), ")) == 0;");
        } else if (IsFix(elem)) {
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < ", l, ".len; ", iv, "++)");
            L("    ", t, " = ", EqX(elem, cat(l, ".data[", iv, "]"), cat(r, ".data[", iv, "]")),
              ";");
        } else {
            // Variable elements: parallel cursor walk.
            auto pa = T(), pb = T(), iv = T();
            L("const uint8_t *", pa, " = ", l, ".data, *", pb, " = ", r, ".data;");
            L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < ", l, ".len; ", iv, "++) {");
            ind++;
            L(t, " = ", EqFn(elem), "(", pa, ", ", pb, ");");
            L(pa, " += ", SizeX(elem, pa), "; ", pb, " += ", SizeX(elem, pb), ";");
            ind--;
            L("}");
        }
        ind--;
        L("}");
        return t;
    }

    // Elementwise arithmetic (§6.1): expand member by member into a temp.
    string GenElemwise(Binary *b, const string &l, const string &r) {
        auto t = b->exprtype;
        auto tv = T();
        L(CT(t), " ", tv, ";");
        auto lt = T(), rtv = T();
        L(CT(t), " ", lt, " = ", l, ";");
        L(CT(t), " ", rtv, " = ", r, ";");
        function<void(TypeExpr *, const string &)> rec = [&](TypeExpr *tt, const string &path) {
            switch (tt->kind) {
                case TY_INT: case TY_FLT: {
                    auto a = cat(lt, path), c = cat(rtv, path);
                    string x;
                    if (tt->kind == TY_FLT) {
                        switch (b->op) {
                            case T_PLUS:  x = cat("(", a, " + ", c, ")"); break;
                            case T_MINUS: x = cat("(", a, " - ", c, ")"); break;
                            case T_MUL:   x = cat("(", a, " * ", c, ")"); break;
                            case T_DIV:   x = cat("(", a, " / ", c, ")"); break;
                            default:      x = cat((tt->fltstorage == FS_F32 ? "fmodf(" : "fmod("),
                                                  a, ", ", c, ")"); break;
                        }
                        L(tv, path, " = ", x, ";");
                    } else {
                        switch (b->op) {
                            case T_PLUS:  x = cat("GS_ADD(", a, ", ", c, ")"); break;
                            case T_MINUS: x = cat("GS_SUB(", a, ", ", c, ")"); break;
                            case T_MUL:   x = cat("GS_MUL(", a, ", ", c, ")"); break;
                            case T_DIV:   x = cat("gs_idiv(", a, ", ", c, ", ",
                                                  WhereStr(b->line), ")"); break;
                            default:      x = cat("gs_imod(", a, ", ", c, ", ",
                                                  WhereStr(b->line), ")"); break;
                        }
                        L(tv, path, " = (", CT(tt), ")", x, ";");
                    }
                    return;
                }
                case TY_STRUCT: {
                    auto si = SI(tt);
                    for (size_t i = 0; i < si->st->fields.size(); i++)
                        if (!si->st->fields[i].ispad)
                            rec(si->ftypes[i],
                                cat(path, ".", Sanitize(si->st->fields[i].name)));
                    return;
                }
                case TY_ARRAY: {
                    auto iv = T();
                    L("for (int64_t ", iv, " = 0; ", iv, " < ", ArrSize(tt->arr), "; ", iv,
                      "++) {");
                    ind++;
                    rec(tt->arr->sub, cat(path, ".e[", iv, "]"));
                    ind--;
                    L("}");
                    return;
                }
                default: assert(false); return;
            }
        };
        // Struct field paths start at the value; array paths at .e — handled
        // uniformly since the outer type is one of the two.
        rec(t, "");
        return tv;
    }

    // Fixed array member paths need ".e" prefixes; adjust: the recursion above
    // treats a top-level fixed array via TY_ARRAY (paths ".e[i]"), fine.

    // `as` range-checks in debug builds (GS_RANGE and friends are identity
    // casts unless the C is compiled with -DGS_DEBUG=1); `as!` always wraps
    // or truncates (§6.3).
    string GenCast(AsCast *ac) {
        auto x = GenX(ac->child);
        auto st = ac->child->exprtype;
        auto tt = ac->exprtype;
        if (tt->kind == TY_INT) {
            auto is = tt->intstorage;
            auto iv = st->kind == TY_FLT
                          ? (ac->unchecked ? cat("gs_f2iwrap(", x, ")")
                                           : cat("GS_F2I(", x, ")"))
                          : cat("(", x, ")");
            if (ac->unchecked || IntSize(is) == 8)
                return cat("(", IntCT(is), ")", iv);
            int64_t lo = 0, hi = 0;
            switch (CanI(is)) {
                case IS_I8:  lo = -128; hi = 127; break;
                case IS_I16: lo = -32768; hi = 32767; break;
                case IS_I32: lo = INT32_MIN; hi = INT32_MAX; break;
                case IS_U8:  hi = 255; break;
                case IS_U16: hi = 65535; break;
                default:     hi = UINT32_MAX; break;
            }
            return cat("(", IntCT(is), ")GS_RANGE(", iv, ", ", IntStr(lo), ", ", IntStr(hi),
                       ")");
        }
        assert(tt->kind == TY_FLT);
        if (tt->fltstorage == FS_F32) {
            if (!ac->unchecked && st->kind == TY_FLT && st->fltstorage != FS_F32)
                return cat("GS_F2F32(", x, ")");
            return cat("(float)(", x, ")");
        }
        if (!ac->unchecked && st->kind == TY_INT) return cat("GS_I2F(", x, ")");
        return cat("(double)(", x, ")");
    }

    string GenDotX(Dot *d) {
        if (d->variantconst) {
            auto et = d->exprtype;
            auto ei = d->einst;
            auto vi = VarIdx(ei->en, d->variantconst);
            if (et->kind == TY_ENUM && !et->enu->varmode) {
                auto t = T();
                L(CT(et), " ", t, ";");
                L(t, ".tag = ", TagConst(ei, vi), ";");
                return t;
            }
            Fail(d->line, "variant constant in a non-fixed context reached GenX");
        }
        if (d->member >= 0) {   // .len / .cap property.
            auto lv = GenLoc(d->obj);
            if (lv.t->kind == TY_REF) DerefLoc(lv, d->line);
            if (lv.t->kind == TY_ARRAY && d->member == B_CAP) {
                auto &a = *lv.t->arr;
                assert(a.akind == A_LIMITED);
                if (ArrSize(lv.t->arr) >= 0) return cat(ArrSize(lv.t->arr));
                return cat("(int64_t)*(uint32_t *)(", lv.s, ")");
            }
            auto v = ArrayView(lv, d->line);
            return cat("(", v.len, ")");
        }
        auto lv = GenLoc(d);
        return LoadLoc(lv, d->exprtype, d->line);
    }

    string GenSlice(SliceExpr *se) {
        auto lv = GenLoc(se->obj);
        if (lv.t->kind == TY_REF) DerefLoc(lv, se->line);
        auto v = ArrayView(lv, se->line);
        auto lenv = T();
        L("int64_t ", lenv, " = ", v.len, ";");
        auto bound = [&](Node *b, bool fromend, const char *dflt) -> string {
            if (!b) return dflt;
            auto x = GenX(b);
            if (fromend) return cat("(", lenv, " - (", x, "))");
            return x;
        };
        auto lo = bound(se->lo, se->lo_from_end, "0");
        auto hi = bound(se->hi, se->hi_from_end, lenv.c_str());
        auto lov = T(), hiv = T();
        L("int64_t ", lov, " = ", lo, ", ", hiv, " = ", hi, ";");
        L("if (", lov, " < 0 || ", hiv, " < ", lov, " || ", hiv, " > ", lenv,
          ") gs_abort(\"slice bounds out of range\", ", WhereStr(se->line), ");");
        auto t = T();
        // Adapted contexts (slice constructing an array) leave an array
        // exprtype on the node; the slice value's own type comes from the
        // source element.
        auto et = se->exprtype->kind == TY_SLICE ? se->exprtype : MakeSliceT(v.elem, se->line);
        if (IsFix(v.elem)) {
            auto dp = v.typedelems ? v.elems
                                   : cat("(", CT(v.elem), " *)(", v.elems, ")");
            L(CT(et), " ", t, " = { ", dp, " + ", lov, ", ", hiv, " - ", lov, " };");
        } else {
            // Whole-array slices only (TC): elements are sequential bytes.
            L(CT(et), " ", t, " = { ", v.elems, ", ", lenv, " };");
        }
        return t;
    }

    // ------------------------------------------------------------------
    // Construction (§4.2/§4.3): writes a value front-to-back at a stack's
    // top, bumping it. All branches of value-producing control constructs
    // construct to the same destination; calls pass the stack down.

    void Bump(const string &stk, const string &n) { L(stk, "->top += ", n, ";"); }

    void EmitValStore(const string &stk, TypeExpr *t, const string &x) {
        auto sz = FixedSize(t);
        if (sz == 0) { L("(void)(", x, ");"); return; }
        L("*(", CT(t), " *)", stk, "->top = ", x, ";");
        Bump(stk, cat(sz));
    }

    void EmitLenStore(const string &stk, IntStorage ls, const string &n) {
        if (ls == IS_VARINT) {
            Bump(stk, cat("gs_uleb_write(", stk, "->top, (uint64_t)(", n, "))"));
        } else {
            L("*(", IntCT(ls), " *)", stk, "->top = (", IntCT(ls), ")(", n, ");");
            Bump(stk, cat(IntSize(ls)));
        }
    }

    // Writes a plain-reference value into the relative-reference slot at
    // `fa` (§3.9): self-relative signed offset, range-checked. Fixed widths
    // only; the varint form exists only in the stack-top variant below.
    void EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln) {
        auto w = (IntStorage)rt->ref->lenstorage;
        assert(w != IS_VARINT);
        auto addr = IsFatPointee(rt->ref->sub) ? cat(rv, ".addr") : cat("(uint8_t *)(", rv, ")");
        auto off = T();
        L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - (", fa, ")) : 0;");
        auto bits = IntSize(w) * 8;
        if (bits < 64)
            L("if (", off, " < -(1LL << ", bits - 1, ") || ", off, " >= (1LL << ",
              bits - 1, ")) gs_abort(\"relative reference offset overflow\", ",
              WhereStr(ln), ");");
        L("*(", RelCT(w), " *)(", fa, ") = (", RelCT(w), ")", off, ";");
    }

    void EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln) {
        auto w = (IntStorage)rt->ref->lenstorage;
        auto fa = T();
        L("uint8_t *", fa, " = ", stk, "->top;");
        if (w == IS_VARINT) {
            auto addr = IsFatPointee(rt->ref->sub) ? cat(rv, ".addr")
                                                   : cat("(uint8_t *)(", rv, ")");
            auto off = T();
            L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - ", fa, ") : 0;");
            Bump(stk, cat("gs_zig_write(", fa, ", ", off, ")"));
            (void)ln;
        } else {
            EmitRelStoreAt(fa, rt, rv, ln);
            Bump(stk, cat(IntSize(w)));
        }
    }

    // Does a fixed type contain relative references at any depth? Literals of
    // such types must construct in their final location, not via a temp.
    bool HasRelRef(TypeExpr *t) {
        switch (t->kind) {
            case TY_REF: return t->ref->lenstorage >= 0;
            case TY_STRUCT: {
                auto si = SI(t);
                for (size_t i = 0; i < si->st->fields.size(); i++)
                    if (!si->st->fields[i].ispad && HasRelRef(si->ftypes[i])) return true;
                return false;
            }
            case TY_ENUM: {
                if (t->enu->varmode) return false;
                auto ei = EIOf(t);
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++)
                    if (HasRelRef(VariantType(t, (int)vi))) return true;
                return false;
            }
            case TY_VARIANT: {
                auto ei = EIVar(t);
                auto vi = VarIdx(ei->en, t->var->variant);
                for (size_t i = 0; i < ei->en->variants[vi].fields.size(); i++)
                    if (!ei->en->variants[vi].fields[i].ispad &&
                        HasRelRef(ei->vftypes[vi][i])) return true;
                return false;
            }
            case TY_ARRAY:
                return (t->arr->akind == A_FIXED || t->arr->akind == A_LIMITED) &&
                       HasRelRef(t->arr->sub);
            default: return false;
        }
    }

    // Copies `n` elements of type `elem` from `src` (typed or byte pointer)
    // to the stack top.
    void EmitCopyElems(const string &stk, TypeExpr *elem, const string &src, const string &n) {
        if (IsFix(elem)) {
            auto esz = FixedSize(elem);
            L("memcpy(", stk, "->top, ", src, ", (size_t)((", n, ") * ", esz, "));");
            Bump(stk, cat("(", n, ") * ", esz));
        } else {
            auto p = T(), iv = T();
            L("const uint8_t *", p, " = (const uint8_t *)(", src, ");");
            L("for (int64_t ", iv, " = 0; ", iv, " < (", n, "); ", iv, "++) {");
            ind++;
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(elem, p), ";");
            L("memcpy(", stk, "->top, ", p, ", (size_t)", sz, ");");
            Bump(stk, sz);
            L(p, " += ", sz, ";");
            ind--;
            L("}");
        }
    }

    // Element count + elements pointer of an array/slice-valued source node,
    // for construction and append. Understands string literals, slices, and
    // all array kinds (through references too).
    struct SrcElems {
        string elems, n;
    };

    SrcElems GenSrcElems(Node *n) {
        SrcElems r;
        if (auto s = Is<StrLit>(n)) {
            r.elems = StrRaw(s->val);
            r.n = cat(s->val.size());
            return r;
        }
        auto t = n->exprtype;
        if (t->kind == TY_SLICE) {
            auto x = GenPure(n);
            r.elems = cat(x, ".data");
            r.n = cat(x, ".len");
            return r;
        }
        assert(t->kind == TY_ARRAY);
        auto lv = GenLoc(n);
        if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
        if (lv.t->kind == TY_SLICE) {
            r.elems = cat(lv.s, ".data");
            r.n = cat(lv.s, ".len");
            return r;
        }
        auto v = ArrayView(lv, n->line);
        auto nn = T();
        L("int64_t ", nn, " = ", v.len, ";");
        r.elems = v.elems;
        r.n = nn;
        return r;
    }

    void GenConstruct(Node *n, const string &stk, TypeExpr *want = nullptr) {
        auto et = n->exprtype;
        if (want && NeedsDeref(n->exprtype, want)) {
            // A spliced reference in a decayed slot: copy the pointee.
            auto sub = n->exprtype->ref->sub;
            if (IsFix(sub)) {
                EmitValStore(stk, want, GenXD(n, want));
            } else {
                auto x = GenX(n);
                auto src = IsFatPointee(sub) ? cat(x, ".addr") : x;
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(sub, src), ";");
                L("memcpy(", stk, "->top, ", src, ", (size_t)", sz, ");");
                Bump(stk, sz);
            }
            return;
        }
        if (IsCtl(n)) { GenAny(n, Dst { 2, stk, want }); return; }
        if (auto c = Is<Call>(n)) {
            auto rets = EmitCall(c, Dst { 2, stk });
            // A reference-returning call decayed to a bytes value: the callee
            // did not construct at the destination; copy the pointee.
            auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
            if (rt && rt->kind == TY_REF && !rets.empty()) {
                auto src = IsFatPointee(rt->ref->sub) ? cat(rets[0], ".addr") : rets[0];
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(et, cat("(uint8_t *)(", src, ")")), ";");
                L("memcpy(", stk, "->top, ", src, ", (size_t)", sz, ");");
                Bump(stk, sz);
            }
            return;
        }
        if (!IsBytesT(et)) {
            // Fixed values normally construct as C values; ones containing
            // relative references must be built at their final address.
            if ((Is<StructLit>(n) || Is<ArrayLit>(n)) && HasRelRef(et)) {
                FixedLitAtStk(n, stk);
                return;
            }
            EmitValStore(stk, et, GenX(n));
            return;
        }
        if (Is<NullLit>(n)) {
            // The zero value of an optional field.
            assert(et->kind == TY_REF);
            if (et->ref->lenstorage == IS_VARINT) {
                L("*", stk, "->top = 0;");
                Bump(stk, "1");
            } else {
                L("memset(", stk, "->top, 0, ", FixedSize(et), ");");
                Bump(stk, cat(FixedSize(et)));
            }
            return;
        }
        if (auto s = Is<StrLit>(n)) {
            assert(et->kind == TY_ARRAY);
            EmitLenStore(stk, ArrLenStore(et), cat(s->val.size()));
            if (!s->val.empty()) {
                L("memcpy(", stk, "->top, ", StrRaw(s->val), ", ", s->val.size(), ");");
                Bump(stk, cat(s->val.size()));
            }
            return;
        }
        if (auto al = Is<ArrayLit>(n)) { GenArrayLit(al, stk); return; }
        if (auto sl = Is<StructLit>(n)) { GenStructLit(sl, stk); return; }
        if (auto d = Is<Dot>(n); d && d->variantconst) {
            // A payload-less variant constant in variable mode: just the tag.
            assert(et->kind == TY_ENUM && et->enu->varmode);
            auto ei = EIOf(et);
            EmitValStoreTag(stk, TagStore(ei->en),
                            TagConst(ei, VarIdx(ei->en, d->variantconst)));
            return;
        }
        if (auto se = Is<SliceExpr>(n); se && et->kind == TY_ARRAY) {
            // A slice expression constructing an array copies the range.
            Loc slv;
            slv.val = true;
            slv.s = GenSlice(se);
            slv.t = MakeSliceT(et->arr->sub, n->line);
            GenArrayFromLoc(slv, et, stk, n->line);
            return;
        }
        // Remaining nodes denote existing values: resolve the location, then
        // either copy identical layouts wholesale or adapt (slice/array kind
        // changes, ADT mode changes) element/field-wise.
        auto lv = GenLoc(n);
        if (lv.t->kind == TY_REF && et->kind != TY_REF) DerefLoc(lv, n->line);
        if (TEq(lv.t, et)) {
            assert(!lv.val);
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(et, lv.s), ";");
            L("memcpy(", stk, "->top, ", lv.s, ", (size_t)", sz, ");");
            Bump(stk, sz);
            return;
        }
        if (et->kind == TY_ARRAY) { GenArrayFromLoc(lv, et, stk, n->line); return; }
        if (et->kind == TY_ENUM && et->enu->varmode) {
            GenVarEnumFromLoc(lv, et, stk, n->line);
            return;
        }
        Fail(n->line, cat("unsupported construction adaptation to ", Mangle(et)));
    }

    void GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln) {
        auto v = ArrayView(lv, ln);
        auto nn = T();
        L("int64_t ", nn, " = ", v.len, ";");
        switch (et->arr->akind) {
            case A_VAR:     EmitLenStore(stk, LenStore(et->arr), nn); break;
            case A_LIMITED:   // Runtime capacity: chosen as the initial length (v1).
                L("*(uint32_t *)", stk, "->top = (uint32_t)", nn, ";");
                L("*(uint32_t *)(", stk, "->top + 4) = (uint32_t)", nn, ";");
                Bump(stk, "8");
                break;
            default:        EmitLenStore(stk, IS_I64, nn); break;
        }
        EmitCopyElems(stk, et->arr->sub, v.elems, nn);
    }

    void GenVarEnumFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln) {
        auto ei = EIOf(et);
        auto ts = TagStore(ei->en);
        if (lv.t->kind == TY_VARIANT) {
            auto vi = VarIdx(ei->en, lv.t->var->variant);
            EmitValStoreTag(stk, ts, TagConst(ei, vi));
            if (!lv.val) {
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(lv.t, lv.s), ";");
                L("memcpy(", stk, "->top, ", lv.s, ", (size_t)", sz, ");");
                Bump(stk, sz);
            } else {
                EmitValStore(stk, lv.t, lv.s);
            }
            return;
        }
        assert(lv.t->kind == TY_ENUM && !lv.t->enu->varmode);
        auto sv = T();
        L(CT(lv.t), " ", sv, " = ", lv.s, ";");
        EmitValStoreTag(stk, ts, cat("(int64_t)", sv, ".tag"));
        L("switch (", sv, ".tag) {");
        for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
            auto psz = VariantLayout(ei, (int)vi).size;
            L("case ", TagConst(ei, (int)vi), ":");
            ind++;
            if (psz) {
                L("memcpy(", stk, "->top, &", sv, ".u.v_",
                  Sanitize(ei->en->variants[vi].name), ", ", psz, ");");
                Bump(stk, cat(psz));
            }
            L("break;");
            ind--;
        }
        L("}");
        (void)ln;
    }

    IntStorage ArrLenStore(TypeExpr *arrt) {
        switch (arrt->arr->akind) {
            case A_VAR: return LenStore(arrt->arr);
            default:    return IS_I64;   // Resizable inline length.
        }
    }

    // A fixed struct/array literal built directly at the stack top, so its
    // relative references measure offsets from their real addresses. Layout
    // gaps (pads, ADT payload padding) are zero-filled to keep sizes exact.
    void FixedLitAtStk(Node *n, const string &stk) {
        auto et = n->exprtype;
        auto Gap = [&](int64_t bytes) {
            if (bytes <= 0) return;
            L("memset(", stk, "->top, 0, ", bytes, ");");
            Bump(stk, cat(bytes));
        };
        auto EmitF = [&](Node *init, TypeExpr *ft) {
            if (!init) {   // Omitted optional: null.
                Gap(FixedSize(ft));
                return;
            }
            if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
                EmitRelStore(stk, ft, GenX(init), n->line);
                return;
            }
            if ((Is<StructLit>(init) || Is<ArrayLit>(init)) && HasRelRef(ft)) {
                FixedLitAtStk(init, stk);
                return;
            }
            EmitValStore(stk, ft, GenX(init));
        };
        if (auto al = Is<ArrayLit>(n)) {
            auto elem = et->arr->sub;
            if (et->arr->akind == A_LIMITED) {
                EmitLenStore(stk, LenStore(et->arr),
                             cat(al->fillval ? ((IntLit *)al->fillcount)->val
                                             : (int64_t)al->elems.size()));
            }
            if (al->fillval) {
                auto fc = Is<IntLit>(al->fillcount);
                auto iv = T();
                L("for (int64_t ", iv, " = 0; ", iv, " < ", fc->val, "; ", iv, "++) {");
                ind++;
                EmitF(al->fillval, elem);
                ind--;
                L("}");
            } else {
                for (auto e : al->elems) EmitF(e, elem);
            }
            if (et->arr->akind == A_LIMITED) {
                auto filled = al->fillval ? ((IntLit *)al->fillcount)->val
                                          : (int64_t)al->elems.size();
                Gap((ArrSize(et->arr) - filled) * FixedSize(elem));
            }
            return;
        }
        auto sl = Is<StructLit>(n);
        assert(sl);
        auto emitfields = [&](const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                              const vector<Node *> &defaults, const Layout &lo, int64_t total) {
            int64_t cur = 0;
            for (size_t i = 0; i < fields.size(); i++) {
                if (fields[i].ispad) continue;
                Gap(lo.offs[i] - cur);
                cur = lo.offs[i];
                Node *init = nullptr;
                for (size_t k = 0; k < sl->fieldindices.size(); k++)
                    if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
                if (!init && i < defaults.size() && defaults[i]) init = defaults[i];
                EmitF(init, ftypes[i]);
                cur += FixedSize(ftypes[i]);
            }
            Gap(total - cur);
        };
        if (et->kind == TY_STRUCT) {
            auto si = SI(et);
            auto &lo = StructLayout(si);
            emitfields(si->st->fields, si->ftypes, si->defaults, lo, lo.size);
            return;
        }
        if (et->kind == TY_VARIANT) {
            auto ei = EIVar(et);
            auto vi = VarIdx(ei->en, et->var->variant);
            auto &lo = VariantLayout(ei, vi);
            emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi], lo,
                       lo.size);
            return;
        }
        assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
        auto ei = EIOf(et);
        auto vi = VarIdx(ei->en, sl->variant);
        EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
        auto &lo = VariantLayout(ei, vi);
        emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi], lo,
                   lo.size);
        Gap(FixedSize(et) - TagSize(ei->en) - lo.size);
    }

    void EmitValStoreTag(const string &stk, IntStorage ts, const string &x) {
        L("*(", IntCT(ts), " *)", stk, "->top = (", IntCT(ts), ")(", x, ");");
        Bump(stk, cat(IntSize(ts)));
    }

    void GenArrayLit(ArrayLit *al, const string &stk) {
        auto et = al->exprtype;
        assert(et->kind == TY_ARRAY);
        auto elem = et->arr->sub;
        int64_t count = al->fillval ? -1 : (int64_t)al->elems.size();
        string cn;
        if (al->fillval) {
            // The fill count is a constant expression (§4.2).
            auto fc = Is<IntLit>(al->fillcount);
            assert(fc);
            count = fc->val;
        }
        cn = cat(count);
        switch (et->arr->akind) {
            case A_VAR:     EmitLenStore(stk, LenStore(et->arr), cn); break;
            case A_LIMITED:
                L("*(uint32_t *)", stk, "->top = ", count, ";");
                L("*(uint32_t *)(", stk, "->top + 4) = ", count, ";");
                Bump(stk, "8");
                break;
            default:        EmitLenStore(stk, IS_I64, cn); break;
        }
        if (al->fillval) {
            if (IsBytesT(elem)) {
                auto iv = T();
                L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
                ind++;
                GenConstruct(al->fillval, stk);
                ind--;
                L("}");
            } else {
                auto fv = GenPure(al->fillval);
                auto iv = T();
                L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
                ind++;
                EmitValStore(stk, elem, fv);
                ind--;
                L("}");
            }
            return;
        }
        for (auto e : al->elems) GenConstruct(e, stk);
    }

    // Struct/variant literal into a bytes destination: fields in layout
    // order, defaults (or a zero optional) for omitted ones. Named inits out
    // of declaration order still construct in layout order (a note against
    // §2's left-to-right rule; flagged for the spec).
    void GenStructLit(StructLit *sl, const string &stk) {
        auto et = sl->exprtype;
        if (et->kind == TY_ENUM) {
            assert(et->enu->varmode && sl->variant);
            auto ei = EIOf(et);
            auto vi = VarIdx(ei->en, sl->variant);
            EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
            GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi],
                          ei->vdefaults[vi], stk);
            return;
        }
        if (et->kind == TY_VARIANT) {
            auto ei = EIVar(et);
            auto vi = VarIdx(ei->en, et->var->variant);
            GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi],
                          ei->vdefaults[vi], stk);
            return;
        }
        assert(et->kind == TY_STRUCT);
        auto si = SI(et);
        GenFieldInits(sl, si->st->fields, si->ftypes, si->defaults, stk);
    }

    void GenFieldInits(StructLit *sl, const vector<Field> &fields,
                       const vector<TypeExpr *> &ftypes, const vector<Node *> &defaults,
                       const string &stk) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) {
                auto n = fields[i].padsize > 0 ? fields[i].padsize : 0;
                if (n) { L("memset(", stk, "->top, 0, ", n, ");"); Bump(stk, cat(n)); }
                continue;
            }
            Node *init = nullptr;
            for (size_t k = 0; k < sl->fieldindices.size(); k++)
                if (sl->fieldindices[k] == (int)i) init = sl->inits[k].val;
            auto ft = ftypes[i];
            if (!init && i < defaults.size()) init = defaults[i];
            if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
                // Relative-reference slot: store from the plain reference.
                if (!init) {
                    if (ft->ref->lenstorage == IS_VARINT) { L("*", stk, "->top = 0;"); Bump(stk, "1"); }
                    else { L("memset(", stk, "->top, 0, ", IntSize((IntStorage)ft->ref->lenstorage), ");");
                           Bump(stk, cat(IntSize((IntStorage)ft->ref->lenstorage))); }
                    continue;
                }
                auto widened = ast.NewType(TY_REF, ft->line);
                widened->ref = ast.NewDetail<TypeRef>();
                widened->ref->sub = ft->ref->sub;
                widened->ref->optional = ft->ref->optional;
                auto rv = GenX(init);
                (void)widened;
                EmitRelStore(stk, ft, rv, sl->line);
                continue;
            }
            if (!init) {
                // Omitted optional: null (checked by TC that a default exists otherwise).
                assert(ft->kind == TY_REF && ft->ref->optional);
                L("memset(", stk, "->top, 0, ", FixedSize(ft), ");");
                Bump(stk, cat(FixedSize(ft)));
                continue;
            }
            if (ft->kind == TY_INT && ft->intstorage == IS_VARINT) {
                auto x = GenXD(init, ast.inttypes[IS_INT]);
                Bump(stk, cat("gs_zig_write(", stk, "->top, ", x, ")"));
                continue;
            }
            GenConstruct(init, stk, ft);
        }
    }

    // ------------------------------------------------------------------
    // Statements and control flow. GenAny routes a node's value to a Dst;
    // control constructs recurse so every branch reaches the same
    // destination (§4.3). Scopes mirror C braces, so watermark base
    // variables are always in C scope exactly where exits may restore them.

    bool termjump = false;   // The last emitted statement left via goto/return.

    void GenAny(Node *n, Dst d) {
        if (auto b = Is<Block>(n)) {
            PushSc(SC_PLAIN);
            L("{");
            ind++;
            for (auto st : b->stmts) GenStmt(st);
            if (b->tail && !IsVoidT(b->tail->exprtype) && d.k) GenAny(b->tail, d);
            else if (b->tail) GenAny(b->tail, Dst {});
            PopSc();
            ind--;
            L("}");
            return;
        }
        if (auto x = Is<IfExpr>(n)) {
            auto c = GenTruth(x->cond);
            L("if (", c, ") {");
            ind++;
            PushSc(SC_PLAIN);
            GenBlockInner(x->thenb, d);
            PopSc();
            ind--;
            if (x->elseb) {
                L("} else {");
                ind++;
                PushSc(SC_PLAIN);
                if (auto b = Is<Block>(x->elseb)) GenBlockInner(b, d);
                else GenAny(x->elseb, d);
                PopSc();
                ind--;
            }
            L("}");
            termjump = false;
            return;
        }
        if (auto m = Is<MatchExpr>(n)) { GenMatch(m, d); return; }
        if (auto eb = Is<EarlyBlock>(n)) {
            PushSc(SC_BLOCK);
            auto si = (int)cscopes.size() - 1;
            cscopes[si].brklbl = Lbl();
            cscopes[si].dstk = d.k;
            cscopes[si].dsts = d.s;
            L("{");
            ind++;
            for (auto st : eb->body->stmts) GenStmt(st);
            if (eb->body->tail && !IsVoidT(eb->body->tail->exprtype) && d.k)
                GenAny(eb->body->tail, d);
            else if (eb->body->tail) GenAny(eb->body->tail, Dst {});
            auto brk = cscopes.back().usedbrk;
            auto lbl = cscopes.back().brklbl;
            PopSc();
            ind--;
            L("}");
            if (brk) L(lbl, ":;");
            termjump = false;
            return;
        }
        if (auto lp = Is<LoopExpr>(n)) {
            GenLoopBody(nullptr, lp->body, d, n);
            return;
        }
        if (auto ib = Is<InlineBlock>(n)) {
            PushSc(SC_IB);
            auto si = (int)cscopes.size() - 1;
            cscopes[si].ibsf = ib->sf;
            cscopes[si].brklbl = Lbl();
            cscopes[si].dstk = d.k;
            cscopes[si].dsts = d.s;
            GenAny(ib->body, d);
            auto brk = cscopes.back().usedbrk;
            auto lbl = cscopes.back().brklbl;
            PopSc();
            if (brk) L(lbl, ":;");
            termjump = false;
            return;
        }
        if (auto c = Is<Call>(n)) {
            auto rets = EmitCall(c, d);
            // Fixed-value results wire into the destination here; bytes
            // results and channel-passed returns were handled in place.
            if (!rets.empty() && !IsVoidT(c->exprtype) && !IsBytesT(c->exprtype)) {
                auto r0 = CallVal0(c, rets[0]);
                if (d.k == 1 && r0 != d.s) L(d.s, " = ", r0, ";");
                else if (d.k == 2) EmitValStore(d.s, c->exprtype, r0);
            }
            return;
        }
        // Leaf value into the destination.
        if (d.k == 2) { GenConstruct(n, d.s, d.t); return; }
        if (d.k == 1) { L(d.s, " = ", GenXD(n, d.t), ";"); return; }
        if (IsVoidT(n->exprtype)) { GenStmt2(n); return; }
        L("(void)(", GenVal(n), ");");
    }

    // A Block's contents without emitting the braces/scope (the caller did).
    void GenBlockInner(Block *b, Dst d) {
        for (auto st : b->stmts) GenStmt(st);
        if (b->tail && !IsVoidT(b->tail->exprtype) && d.k) GenAny(b->tail, d);
        else if (b->tail) GenAny(b->tail, Dst {});
    }

    void GenStmt(Node *n) {
        PushSc(SC_STMT);
        termjump = false;
        GenStmt2(n);
        if (termjump) cscopes.back().saves.clear();   // Nothing runs after a jump.
        PopSc();
        termjump = false;
    }

    void GenStmt2(Node *n) {
        if (auto vd = Is<VarDecl>(n)) { GenVarDecl(vd); return; }
        if (auto a = Is<Assign>(n)) { GenAssign(a); return; }
        if (auto x = Is<IncDec>(n)) { GenIncDec(x); return; }
        if (Is<FnDecl>(n)) return;   // Nested declarations are separate specs.
        if (auto w = Is<While>(n)) { GenWhile(w); return; }
        if (auto f = Is<ForLoop>(n)) { GenFor(f); return; }
        if (auto g = Is<Guard>(n)) { GenGuard(g); return; }
        if (auto r = Is<Return>(n)) { GenReturn(r); return; }
        if (auto b = Is<Break>(n)) { GenBreak(b); return; }
        if (Is<Continue>(n)) { GenContinue(n); return; }
        GenAny(n, Dst {});
    }

    // ------------------------------------------------------------------
    // Loops. Shape: for (<init>; ; <incr>) { [cond exit] body cnt:; restores }
    // Goose break/continue always leave via gotos with explicit watermark
    // restores; C break/continue are never emitted for them, so nesting
    // inside generated switches stays safe.

    void GenLoopBody(const function<void()> &condexit, Block *bodyb, Dst d, Node *node,
                     const string &forhead = "", const function<void()> &increment = {}) {
        (void)node;
        PushSc(SC_LOOP);
        auto si = (int)cscopes.size() - 1;
        cscopes[si].brklbl = Lbl();
        cscopes[si].cntlbl = Lbl();
        cscopes[si].dstk = d.k;
        cscopes[si].dsts = d.s;
        L(forhead.empty() ? "for (;;) {" : forhead);
        ind++;
        if (condexit) condexit();
        for (auto st : bodyb->stmts) GenStmt(st);
        if (bodyb->tail) GenStmt(bodyb->tail);
        auto &sc = cscopes.back();
        if (sc.usedcnt) L(sc.cntlbl, ":;");
        EmitRestores(sc);
        if (increment) increment();
        ind--;
        L("}");
        auto usedbrk = sc.usedbrk;
        auto brklbl = sc.brklbl;
        cscopes.back().saves.clear();   // Restores already emitted in-loop.
        PopSc();
        if (usedbrk) L(brklbl, ":;");
        termjump = false;
    }

    void GenWhile(While *w) {
        Dst d;
        GenLoopBody([&]() {
            auto c = GenTruth(w->cond);
            auto si = (int)cscopes.size() - 1;
            // The loop scope has no allocations yet, so a plain goto exits
            // cleanly; the condition's own temps were allocated in this
            // scope's compile-time list and are restored... none yet either.
            L("if (!(", c, ")) goto ", cscopes[si].brklbl, ";");
            cscopes[si].usedbrk = true;
        }, w->body, d, w);
    }

    void GenFor(ForLoop *f) {
        auto d = Dst {};
        auto iv = f->vdef ? LocalName(f->vdef) : T();
        auto ix = f->idxdef ? LocalName(f->idxdef) : "";
        if (f->iterkind == IK_RANGE) {
            auto r = Is<RangeExpr>(f->iter);
            auto lo = GenX(r->lo);
            auto lov = T();
            L("int64_t ", lov, " = ", lo, ";");
            auto hi = GenX(r->hi);
            auto hiv = T();
            L("int64_t ", hiv, " = ", hi, ";");
            if (!ix.empty()) L("int64_t ", ix, " = 0;");
            GenLoopBody({}, f->body, d, f,
                        cat("for (int64_t ", iv, " = ", lov, "; ", iv, " < ", hiv, "; ", iv,
                            "++", ix.empty() ? "" : cat(", ", ix, "++"), ") {"));
            return;
        }
        if (f->iterkind == IK_COUNT) {
            auto n = GenX(f->iter);
            auto nv = T();
            L("int64_t ", nv, " = ", n, ";");
            if (!ix.empty()) L("int64_t ", ix, " = 0;");
            GenLoopBody({}, f->body, d, f,
                        cat("for (int64_t ", iv, " = 0; ", iv, " < ", nv, "; ", iv, "++",
                            ix.empty() ? "" : cat(", ", ix, "++"), ") {"));
            return;
        }
        // Arrays and slices. The length re-reads each iteration (growth during
        // iteration is legal, §5.2); element access goes through the view.
        auto lv = GenLoc(f->iter);
        if (lv.t->kind == TY_REF) DerefLoc(lv, f->line);
        auto v = ArrayView(lv, f->line);
        auto gi = ix.empty() ? T() : ix;
        auto et = f->vdef->type;
        if (!IsFix(v.elem)) {
            // Sequential walk, &-binding only; the cursor advances in the
            // increment clause so `continue` behaves.
            auto p = T();
            L("uint8_t *", p, " = (uint8_t *)(", v.elems, ");");
            GenLoopBody([&]() {
                if (f->byref) L("uint8_t *", iv, " = ", p, ";");
            }, f->body, d, f,
                cat("for (int64_t ", gi, " = 0; ", gi, " < (", v.len, "); ", gi, "++, ", p,
                    " += ", SizeX(v.elem, p), ") {"));
            return;
        }
        auto esz = FixedSize(v.elem);
        GenLoopBody([&]() {
            string elem = v.typedelems
                              ? cat(v.elems, "[", gi, "]")
                              : cat("(*(", CT(v.elem), " *)((", v.elems, ") + ", gi, " * ",
                                    esz, "))");
            if (f->byref) L(CT(et), " ", iv, " = &", elem, ";");
            else L(CT(et), " ", iv, " = ", elem, ";");
        }, f->body, d, f,
            cat("for (int64_t ", gi, " = 0; ", gi, " < (", v.len, "); ", gi, "++) {"));
    }

    void GenGuard(Guard *g) {
        auto c = GenTruth(g->cond);
        L("if (!(", c, ")) {");
        ind++;
        PushSc(SC_PLAIN);
        if (g->elseb) {
            GenBlockInner(g->elseb, Dst {});
        } else if (g->implicitexit == 1) {
            GenBreakPath(nullptr, g->line);
        } else {
            GenNormalReturn({}, g->line);
        }
        cscopes.back().saves.clear();   // The block diverged.
        PopSc();
        ind--;
        L("}");
        termjump = false;
    }

    void GenBreak(Break *b) { GenBreakPath(b->val, b->line); }

    void GenBreakPath(Node *val, Line ln) {
        auto si = -1;
        for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
            if (cscopes[i].kind == SC_LOOP || cscopes[i].kind == SC_BLOCK) { si = i; break; }
            if (cscopes[i].kind == SC_FN) break;
        }
        assert(si >= 0);
        if (val) {
            Dst d { cscopes[si].dstk, cscopes[si].dsts };
            if (d.k) GenAny(val, d);
            else GenAny(val, Dst {});
        }
        EmitExitRestores(si);
        cscopes[si].usedbrk = true;
        L("goto ", cscopes[si].brklbl, ";");
        termjump = true;
        (void)ln;
    }

    void GenContinue(Node *n) {
        auto si = -1;
        for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
            if (cscopes[i].kind == SC_LOOP) { si = i; break; }
            if (cscopes[i].kind == SC_FN) break;
        }
        assert(si >= 0);
        EmitExitRestores(si + 1);
        cscopes[si].usedcnt = true;
        L("goto ", cscopes[si].cntlbl, ";");
        termjump = true;
        (void)n;
    }

    // ------------------------------------------------------------------
    // match (§8.1).

    void GenMatch(MatchExpr *m, Dst d) {
        auto st = m->scrutinee->exprtype;
        TypeExpr *enumtype = nullptr;
        auto isref = false;
        if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
            enumtype = st->ref->sub;
            isref = true;
        } else if (st->kind == TY_ENUM) {
            enumtype = st;
        }
        if (!enumtype) {   // Integer match: an if-chain in arm order.
            auto x = GenPure(m->scrutinee);
            auto first = true;
            for (auto &arm : m->arms) {
                if (arm.pat.kind == P_WILDCARD) {
                    L(first ? "{" : "} else {");
                } else if (arm.hi == arm.lo + 1) {
                    L(first ? "" : "} else ", "if (", x, " == ", IntStr(arm.lo), ") {");
                } else {
                    L(first ? "" : "} else ", "if (", x, " >= ", IntStr(arm.lo), " && ", x,
                      " < ", IntStr(arm.hi), ") {");
                }
                first = false;
                ind++;
                PushSc(SC_PLAIN);
                GenAny(arm.body, d);
                PopSc();
                ind--;
            }
            L("}");
            termjump = false;
            return;
        }
        auto varmode = enumtype->enu->varmode;
        auto ei = EIOf(enumtype);
        auto ts = TagSize(ei->en);
        string p, sv, tag;
        if (varmode || isref) {
            if (isref) {
                auto x = GenPure(m->scrutinee);
                p = varmode ? x : cat("((uint8_t *)", x, ")");
            } else {
                p = GenPtr(m->scrutinee);
            }
            tag = varmode ? cat("*(", IntCT(TagStore(ei->en)), " *)", p)
                          : cat("((", CT(enumtype), " *)", p, ")->tag");
        } else {
            sv = GenPure(m->scrutinee);
            tag = cat(sv, ".tag");
        }
        L("switch (", tag, ") {");
        auto haswild = false;
        for (auto &arm : m->arms) {
            if (arm.pat.kind == P_WILDCARD) {
                haswild = true;
                L("default: {");
            } else {
                auto vi = VarIdx(ei->en, arm.variant);
                L("case ", TagConst(ei, vi), ": {");
            }
            ind++;
            PushSc(SC_PLAIN);
            if (arm.binder) {
                auto vi = VarIdx(ei->en, arm.variant);
                auto vt = VariantType(enumtype, vi);
                auto bn = LocalName(arm.binder);
                string payload = varmode
                    ? cat("(", p, " + ", ts, ")")
                    : (isref ? cat("((uint8_t *)&((", CT(enumtype), " *)", p, ")->u.v_",
                                   Sanitize(arm.variant->name), ")")
                             : "");
                if (arm.pat.byref) {
                    // Variable-mode payloads only (§8.1).
                    if (IsBytesT(vt)) L("uint8_t *", bn, " = ", payload, ";");
                    else L(CT(vt), " *", bn, " = (", CT(vt), " *)(", payload, ");");
                } else if (IsBytesT(vt)) {
                    // By-value copy of a variable payload onto its own stack.
                    auto stk = AllocStk(true);
                    L("uint8_t *", bn, " = ", stk, "->top;");
                    SaveBase(true, stk, bn);
                    vstk[arm.binder] = stk;
                    auto sz = T();
                    L("int64_t ", sz, " = ", SizeX(vt, payload), ";");
                    L("memcpy(", stk, "->top, ", payload, ", (size_t)", sz, ");");
                    Bump(stk, sz);
                } else if (arm.variant->fields.empty()) {
                    L(CT(vt), " ", bn, " = {0};");
                } else if (!payload.empty()) {
                    L(CT(vt), " ", bn, " = *(", CT(vt), " *)(", payload, ");");
                } else {
                    L(CT(vt), " ", bn, " = ", sv, ".u.v_", Sanitize(arm.variant->name), ";");
                }
            }
            GenAny(arm.body, d);
            PopSc();
            ind--;
            L("} break;");
        }
        if (!haswild)
            L("default: gs_abort(\"corrupt ADT tag\", ", WhereStr(m->line), ");");
        L("}");
        termjump = false;
    }

    // ------------------------------------------------------------------
    // Declarations and assignment.

    void GenVarDecl(VarDecl *vd) {
        // Multi-name binding from one call: wire the call's channels straight
        // into the locals.
        if (vd->names.size() > 1 && vd->inits.size() == 1) {
            auto c = Is<Call>(vd->inits[0]);
            assert(c);
            vector<Dst> dsts;
            for (size_t i = 0; i < vd->defs.size(); i++) {
                auto d = vd->defs[i];
                auto rt = c->rettypes[i];
                auto name = LocalName(d);
                if (IsBytesT(rt)) {
                    auto stk = AllocStk(true);
                    L("uint8_t *", name, " = ", stk, "->top;");
                    SaveBase(true, stk, name);
                    vstk[d] = stk;
                    dsts.push_back(Dst { 2, stk });
                } else {
                    L(CT(d->type), " ", name, ";");
                    dsts.push_back(Dst { 1, name });
                }
            }
            auto rets = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
            for (size_t i = 0; i < dsts.size() && i < rets.size(); i++)
                if (dsts[i].k == 1 && !rets[i].empty() && rets[i] != dsts[i].s)
                    L(dsts[i].s, " = ", rets[i], ";");
            return;
        }
        for (size_t i = 0; i < vd->defs.size(); i++)
            BindLocal(vd->defs[i], i < vd->inits.size() ? vd->inits[i] : nullptr);
    }

    void BindLocal(VarDef *d, Node *init) {
        auto name = LocalName(d);
        auto t = d->type;
        if (IsBytesT(t)) {
            assert(init);
            string stk;
            if (nrvovars.count(d)) {
                stk = cat("gs_dst", nrvodst[d]);
            } else {
                stk = AllocStk(true);
            }
            L("uint8_t *", name, " = ", stk, "->top;");
            if (!nrvovars.count(d)) SaveBase(true, stk, name);
            vstk[d] = stk;
            if (d->reusable) {
                // Companion freelist: a grow-shrink int64 stack of free slots.
                auto flstk = AllocStk(true);
                auto fln = Unique2(cat(name, "_fl"));
                L("uint8_t *", fln, " = ", flstk, "->top;");
                SaveBase(true, flstk, fln);
                L("*(int64_t *)", fln, " = 0;");
                L(flstk, "->top += 8;");
                vpool[d] = { fln, flstk };
            }
            GenConstruct(init, stk, t);
            return;
        }
        if (!init) {
            L(VarCT(d), " ", name, ";");
            return;
        }
        if (PrefVar(d)) {
            L("gs_pref ", name, " = ", GenPrefVal(init), ";");
            return;
        }
        if (IsCtl(init) || Is<Call>(init)) {
            L(CT(t), " ", name, ";");
            GenAny(init, Dst { 1, name, t });
            return;
        }
        L(CT(t), " ", name, " = ", GenXD(init, t), ";");
    }

    // A gs_pref value from a &pool expression or another pool reference.
    string GenPrefVal(Node *n) {
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
            auto lv = GenLoc(u->child);
            if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
            if (lv.fl.empty()) Fail(n->line, "reusable pool reference lost its freelist");
            auto t = T();
            L("gs_pref ", t, " = { ", lv.s, ", ", lv.stk, ", ", lv.fl, ", ", lv.flstk,
              " };");
            return t;
        }
        if (auto id = Is<Ident>(n); id && id->vdef && PrefVar(id->vdef)) {
            auto lv = VarLoc(id->vdef);
            return lv.s;
        }
        Fail(n->line, "cannot form a reusable pool reference from this expression");
    }

    string Unique2(const string &base) {
        auto name = base;
        for (auto n = 2; fnused.count(name) || used.count(name); n++) name = cat(base, "_", n);
        fnused.insert(name);
        return name;
    }

    void GenIncDec(IncDec *x) {
        auto lv = GenLoc(x->lval);
        if (lv.t->kind == TY_REF) DerefLoc(lv, x->line);
        assert(lv.val);
        auto op = x->op == T_INC ? "GS_ADD(" : "GS_SUB(";
        L(lv.s, " = ", op, lv.s, ", 1);");
    }

    void GenAssign(Assign *a) {
        auto lv = GenLoc(a->lval);
        if (a->op == T_DOTASSIGN) { GenRebind(a, lv); return; }
        if (a->pointee) DerefLoc(lv, a->line);
        // Compound operators: full-width int/flt locations only (TC).
        if (a->op != T_ASSIGN) {
            assert(lv.val);
            auto r = GenX(a->rhs);
            switch (a->op) {
                case T_PLUSEQ:
                    if (lv.t->kind == TY_FLT) L(lv.s, " = ", lv.s, " + (", r, ");");
                    else L(lv.s, " = GS_ADD(", lv.s, ", ", r, ");");
                    break;
                case T_MINUSEQ:
                    if (lv.t->kind == TY_FLT) L(lv.s, " = ", lv.s, " - (", r, ");");
                    else L(lv.s, " = GS_SUB(", lv.s, ", ", r, ");");
                    break;
                case T_MULEQ:
                    if (lv.t->kind == TY_FLT) L(lv.s, " = ", lv.s, " * (", r, ");");
                    else L(lv.s, " = GS_MUL(", lv.s, ", ", r, ");");
                    break;
                case T_DIVEQ:
                    if (lv.t->kind == TY_FLT) L(lv.s, " = ", lv.s, " / (", r, ");");
                    else L(lv.s, " = gs_idiv(", lv.s, ", ", r, ", ", WhereStr(a->line), ");");
                    break;
                case T_MODEQ:
                    if (lv.t->kind == TY_FLT)
                        L(lv.s, " = ", lv.t->fltstorage == FS_F32 ? "fmodf(" : "fmod(", lv.s,
                          ", ", r, ");");
                    else L(lv.s, " = gs_imod(", lv.s, ", ", r, ", ", WhereStr(a->line), ");");
                    break;
                case T_ANDEQ: L(lv.s, " = ", lv.s, " & (", r, ");"); break;
                case T_OREQ:  L(lv.s, " = ", lv.s, " | (", r, ");"); break;
                case T_XOREQ: L(lv.s, " = ", lv.s, " ^ (", r, ");"); break;
                default: assert(false);
            }
            return;
        }
        // Plain assignment, by the target's representation (§4.4).
        auto t = lv.t;
        if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
            // Relative-reference slot: encode from the plain reference value.
            GenRelAssign(lv, a->rhs, a->line);
            return;
        }
        if (IsResz(t)) {
            // Whole-resizable assignment: clear (top back to the value start),
            // then construct the new contents in place.
            assert(!lv.val && !lv.stk.empty());
            L(lv.stk, "->top = ", lv.s, ";");
            GenConstruct(a->rhs, lv.stk, lv.t);
            return;
        }
        if (!lv.val) {
            // Bytes-class fixed-capacity array [..]: copy contents within cap.
            assert(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED);
            string srcstk;
            auto src = GenPtr(a->rhs, &srcstk);
            auto nn = T();
            L("int64_t ", nn, " = (int64_t)*(uint32_t *)(", src, " + 4);");
            L("if (", nn, " > (int64_t)*(uint32_t *)(", lv.s, ")",
              ") gs_abort(\"limited array capacity exceeded\", ", WhereStr(a->line), ");");
            L("*(uint32_t *)((", lv.s, ") + 4) = (uint32_t)", nn, ";");
            L("memcpy((", lv.s, ") + 8, ", src, " + 8, (size_t)(", nn, " * ",
              FixedSize(t->arr->sub), "));");
            return;
        }
        GenAny(a->rhs, Dst { 1, lv.s, lv.t });
    }

    void GenRelAssign(Loc lv, Node *rhs, Line ln) {
        auto rv = GenX(rhs);
        auto w = (IntStorage)lv.t->ref->lenstorage;
        if (w == IS_VARINT) {
            // Rebinding a varint-width relative reference cannot change its
            // encoded length (the container layout is frozen, §3.6).
            auto fa = BytesAddrOf(lv);
            auto fav = T();
            L("uint8_t *", fav, " = ", fa, ";");
            auto addr = IsFatPointee(lv.t->ref->sub) ? cat(rv, ".addr")
                                                     : cat("(uint8_t *)(", rv, ")");
            auto off = T(), buf = T();
            L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - ", fav, ") : 0;");
            L("uint8_t ", buf, "[10];");
            L("if (gs_zig_write(", buf, ", ", off, ") != gs_uleb_size(", fav,
              ")) gs_abort(\"varint relative reference rebind changes size\", ",
              WhereStr(ln), ");");
            L("memcpy(", fav, ", ", buf, ", (size_t)gs_uleb_size(", buf, "));");
            return;
        }
        EmitRelStoreAt(BytesAddrOf(lv), lv.t, rv, ln);
    }

    void GenRebind(Assign *a, Loc lv) {
        if (lv.t->kind == TY_REF && lv.t->ref->lenstorage >= 0) {
            GenRelAssign(lv, a->rhs, a->line);
            return;
        }
        assert(lv.val);
        if (Is<NullLit>(a->rhs)) {
            if (IsFatPointee(lv.t->ref->sub)) L("memset(&", lv.s, ", 0, sizeof(", lv.s, "));");
            else L(lv.s, " = NULL;");
            return;
        }
        L(lv.s, " = ", GenX(a->rhs), ";");
    }

    // ------------------------------------------------------------------
    // Returns: normal, forwarding a multi-value call, exiting an inlined
    // body, and long-distance (§7.9).

    unordered_map<const VarDef *, int> nrvodst;

    void GenReturn(Return *r) {
        // Exiting an inlined body?
        for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
            if (cscopes[i].kind == SC_IB && cscopes[i].ibsf == r->target) {
                if (!r->vals.empty()) {
                    Dst d { cscopes[i].dstk, cscopes[i].dsts };
                    GenAny(r->vals[0], d.k ? d : Dst {});
                    for (size_t j = 1; j < r->vals.size(); j++) GenAny(r->vals[j], Dst {});
                }
                EmitExitRestores(i);
                cscopes[i].usedbrk = true;
                L("goto ", cscopes[i].brklbl, ";");
                termjump = true;
                return;
            }
            if (cscopes[i].kind == SC_FN) break;
        }
        if (curspec && r->target == curspec->sf) {
            GenNormalReturn(r->vals, r->line);
            return;
        }
        GenFromReturn(r);
    }

    void GenNormalReturn(const vector<Node *> &vals, Line ln) {
        auto sp = curspec;
        assert(sp);
        auto &si = *curinfo;
        string retv;
        // A single call forwards all its return values (§7.1).
        if (vals.size() == 1 && sp->rets.size() > 1) {
            if (auto c = Is<Call>(vals[0]); c && c->rettypes.size() == sp->rets.size()) {
                vector<Dst> dsts;
                vector<string> tmps(sp->rets.size());
                for (size_t i = 0; i < sp->rets.size(); i++) {
                    if (IsBytesT(sp->rets[i])) {
                        dsts.push_back(Dst { 2, cat("gs_dst", i) });
                    } else {
                        tmps[i] = T();
                        L(CT(sp->rets[i]), " ", tmps[i], ";");
                        dsts.push_back(Dst { 1, tmps[i] });
                    }
                }
                auto rets = EmitCall(c, dsts[0], &dsts);
                for (size_t i = 0; i < sp->rets.size(); i++) {
                    if (IsBytesT(sp->rets[i])) continue;
                    auto v = i < rets.size() && !rets[i].empty() ? rets[i] : tmps[i];
                    if ((int)i == si.cret) retv = v;
                    else L("*gs_r", i, " = ", v, ";");
                }
                Epilogue(retv);
                return;
            }
        }
        assert(vals.size() == sp->rets.size());
        for (size_t i = 0; i < vals.size(); i++) {
            auto rt = sp->rets[i];
            if (IsBytesT(rt)) {
                auto id = Is<Ident>(vals[i]);
                if (id && id->vdef && nrvovars.count(id->vdef)) continue;   // In place already.
                GenConstruct(vals[i], cat("gs_dst", i), rt);
            } else if ((int)i == si.cret) {
                retv = T();
                L(CT(rt), " ", retv, ";");
                GenAny(vals[i], Dst { 1, retv, rt });
            } else {
                auto tv = T();
                L(CT(rt), " ", tv, ";");
                GenAny(vals[i], Dst { 1, tv, rt });
                L("*gs_r", i, " = ", tv, ";");
            }
        }
        Epilogue(retv);
        (void)ln;
    }

    void Epilogue(const string &retv) {
        EmitExitRestores(0);
        for (auto &s : fdstsaves) L(s);
        if (curinfo && curinfo->hasrf) L("*gs_rf = 0;");
        L(retv.empty() ? "return;" : cat("return ", retv, ";"));
        termjump = true;
    }

    // The dummy C return value used on propagate paths.
    void PropagateReturn(const string &rfval) {
        EmitExitRestores(0);
        for (auto &s : fdstsaves) L(s);
        L("*gs_rf = ", rfval, ";");
        if (curinfo->cret >= 0) {
            auto d = T();
            L(CT(curspec->rets[curinfo->cret]), " ", d, " = {0};");
            L("return ", d, ";");
        } else {
            L("return;");
        }
        termjump = true;
    }

    // Long-distance return site: values into the target's channels, then
    // propagate the discriminant.
    void GenFromReturn(Return *r) {
        auto t = r->target;
        auto tid = fromids[t];
        EnsureFromChannels(t);
        auto &rets = FromRets(t);
        assert(r->vals.size() == rets.size() ||
               (r->vals.size() == 1 && Is<Call>(r->vals[0])));
        if (r->vals.size() == rets.size()) {
            for (size_t i = 0; i < rets.size(); i++) {
                if (IsBytesT(rets[i]))
                    GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i]);
                else GenAny(r->vals[i], Dst { 1, cat("gs_lret_", tid, "_", i), rets[i] });
            }
        } else {
            // Forward one call's values into the channels.
            auto c = Is<Call>(r->vals[0]);
            vector<Dst> dsts;
            for (size_t i = 0; i < rets.size(); i++)
                dsts.push_back(IsBytesT(rets[i]) ? Dst { 2, cat("gs_fdst_", tid, "_", i) }
                                                 : Dst { 1, cat("gs_lret_", tid, "_", i) });
            auto cr = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
            for (size_t i = 0; i < dsts.size() && i < cr.size(); i++)
                if (dsts[i].k == 1 && !cr[i].empty() && cr[i] != dsts[i].s)
                    L(dsts[i].s, " = ", cr[i], ";");
        }
        assert(curinfo && curinfo->hasrf);
        PropagateReturn(cat(tid));
    }

    // Channel types per target: the rets of its first live spec.
    unordered_map<SFunction *, vector<TypeExpr *> *> fromrets;

    vector<TypeExpr *> &FromRets(SFunction *t) {
        auto it = fromrets.find(t);
        if (it != fromrets.end()) return *it->second;
        for (auto sp : t->specs)
            if (sp->live) { fromrets[t] = &sp->rets; return sp->rets; }
        // A target none of whose specs are live: any spec's types will do.
        assert(!t->specs.empty());
        fromrets[t] = &t->specs[0]->rets;
        return t->specs[0]->rets;
    }

    void EnsureFromChannels(SFunction *t) {
        if (fromemitted.count(t)) return;
        fromemitted.insert(t);
        auto tid = fromids[t];
        auto &rets = FromRets(t);
        for (size_t i = 0; i < rets.size(); i++) {
            if (IsBytesT(rets[i]))
                Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
            else
                Append(data, "static GS_TLS ", CT(rets[i]), " gs_lret_", tid, "_", i, ";\n");
        }
    }

    // ------------------------------------------------------------------
    // Calls. Returns one entry per return value: a C expression for fixed
    // values, the value's base pointer for bytes-class ones. d0 is the
    // preferred destination for the first return (in-place construction);
    // alldst supplies destinations for every return (multi-value receives).

    // The first return value adjusted for reference decay: a call that
    // returns a reference received in a value context loads the pointee.
    string CallVal0(Call *c, const string &r0) {
        auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
        auto et = c->exprtype;
        if (rt && rt->kind == TY_REF && rt->ref->lenstorage < 0 && et &&
            et->kind != TY_REF && et->kind != TY_VOID) {
            if (IsFatPointee(rt->ref->sub) || IsBytesT(rt->ref->sub)) return r0;
            return cat("(*", r0, ")");
        }
        return r0;
    }

    vector<string> EmitCall(Call *c, Dst d0, vector<Dst> *alldst = nullptr) {
        if (c->builtin >= 0) return EmitBuiltin(c, d0);
        if (c->fvbody) return EmitFvCall(c, d0);
        if (!c->dispatch.empty()) return EmitDispatch(c, d0, alldst);
        assert(c->spec);
        return EmitSpecCall(c, c->spec, d0, alldst);
    }

    vector<Node *> CallArgNodes(Call *c, size_t nparams) {
        vector<Node *> an;
        if (auto dd = Is<Dot>(c->callee)) an.push_back(dd->obj);
        for (auto a : c->args) an.push_back(a);
        an.resize(nparams);   // Function-value arguments are compile-time only.
        return an;
    }

    // One argument by the parameter's calling convention; appends to args.
    void EmitArg(FnSpec *sp, size_t i, Node *node, vector<string> &args) {
        auto pt = sp->argtypes[i];
        if (IsPoolParam(sp, i)) {
            args.push_back(GenPrefVal(node));
            return;
        }
        if (IsBytesT(pt)) {
            // By-value nonfixed argument: construct into a fresh slot (§7.2).
            auto stk = AllocStk(false);
            auto base = T();
            L("uint8_t *", base, " = ", stk, "->top;");
            SaveBase(false, stk, base);
            GenConstruct(node, stk, pt);
            args.push_back(base);
            if (IsResz(pt)) args.push_back(stk);
            return;
        }
        args.push_back(GenXD(node, pt));
    }

    // A free variable of the callee, from the caller's frame (or passed on).
    void EmitFvArg(const VarDef *fv, vector<string> &args) {
        auto name = VName(fv);
        auto ours = curspec && fv->ownerspec == curspec;
        if (fv->reusable) {
            // A pool local of ours: assemble; a passed-through one: forward.
            if (ours) {
                auto &p = vpool[fv];
                auto t = T();
                L("gs_pref ", t, " = { ", name, ", ", vstk[fv], ", ", p.first, ", ",
                  p.second, " };");
                args.push_back(t);
            } else {
                args.push_back(name);
            }
            return;
        }
        if (IsBytesT(fv->type)) {
            args.push_back(name);
            if (IsResz(fv->type)) args.push_back(ours ? vstk[fv] : cat(name, "_stk"));
            return;
        }
        args.push_back(fvptr.count(fv) ? name : cat("&", name));
    }

    vector<string> EmitSpecCall(Call *c, FnSpec *sp, Dst d0, vector<Dst> *alldst) {
        assert(sinfo.count(sp));
        auto &ki = sinfo[sp];
        auto an = CallArgNodes(c, sp->params.size());
        vector<string> args;
        for (size_t i = 0; i < an.size(); i++) EmitArg(sp, i, an[i], args);
        for (auto fv : ki.freevars) EmitFvArg(fv, args);
        vector<string> retex(sp->rets.size());
        for (size_t i = 0; i < sp->rets.size(); i++) {
            auto rt = sp->rets[i];
            Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                                  : (i == 0 ? d0 : Dst {});
            if (IsBytesT(rt)) {
                string stk;
                if (dd.k == 2) {
                    stk = dd.s;
                } else {
                    stk = AllocStk(false);
                    auto sb = T();
                    L("uint8_t *", sb, " = ", stk, "->top;");
                    SaveBase(false, stk, sb);
                }
                auto base = T();
                L("uint8_t *", base, " = ", stk, "->top;");
                retex[i] = base;
                args.push_back(stk);
            } else if ((int)i != ki.cret) {
                if (dd.k == 1) {
                    retex[i] = dd.s;
                    args.push_back(cat("&", dd.s));
                } else {
                    auto tv = T();
                    L(CT(rt), " ", tv, ";");
                    retex[i] = tv;
                    args.push_back(cat("&", tv));
                }
            }
        }
        if (ki.needssp) args.push_back(SpTop());
        string rfv;
        if (ki.hasrf) {
            rfv = T();
            L("int32_t ", rfv, " = 0;");
            args.push_back(cat("&", rfv));
        }
        string argstr;
        for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
        if (ki.cret >= 0) {
            auto r0 = T();
            L(CT(sp->rets[ki.cret]), " ", r0, " = ", ki.cname, "(", argstr, ");");
            retex[ki.cret] = r0;
        } else {
            L(ki.cname, "(", argstr, ");");
        }
        if (ki.hasrf) EmitRfCheck(rfv, sp);
        // A fixed first return requested onto a stack: store it (mixed cases
        // are handled above via cret; nothing more to do here).
        return retex;
    }

    void EmitRfCheck(const string &rfv, FnSpec *callee) {
        L("if (", rfv, ") {");
        ind++;
        auto caught = curspec && callee->needs.count(curspec->sf);
        if (caught) {
            auto tid = fromids[curspec->sf];
            EnsureFromChannels(curspec->sf);
            L("if (", rfv, " == ", tid, ") {");
            ind++;
            string retv;
            for (size_t i = 0; i < curspec->rets.size(); i++) {
                if (IsBytesT(curspec->rets[i])) continue;   // Already at our destination.
                if ((int)i == curinfo->cret) retv = cat("gs_lret_", tid, "_", i);
                else L("*gs_r", i, " = gs_lret_", tid, "_", i, ";");
            }
            Epilogue(retv);
            ind--;
            L("} else {");
            ind++;
            if (curinfo && curinfo->hasrf) PropagateReturn(rfv);
            else L("gs_abort(\"unexpected long-distance return\", \"runtime\");");
            ind--;
            L("}");
        } else if (curinfo && curinfo->hasrf) {
            PropagateReturn(rfv);
        } else {
            L("gs_abort(\"unexpected long-distance return\", \"runtime\");");
        }
        ind--;
        L("}");
        termjump = false;
    }

    // Calling a function value: the checked body instance is spliced inline
    // (§7.6); its parameters are ordinary locals of this function.
    vector<string> EmitFvCall(Call *c, Dst d0) {
        auto et = c->fvbody->exprtype;
        auto wantsval = !IsVoidT(et);
        string rv;
        Dst d = d0;
        if (wantsval && !IsBytesT(et) && d0.k != 1) {
            rv = T();
            L(CT(et), " ", rv, ";");
            d = Dst { 1, rv };
        } else if (wantsval && IsBytesT(et) && d0.k != 2) {
            auto stk = AllocStk(false);
            rv = T();
            L("uint8_t *", rv, " = ", stk, "->top;");
            SaveBase(false, stk, rv);
            d = Dst { 2, stk };
        } else if (wantsval && d0.k == 1) {
            rv = d0.s;
        }
        PushSc(SC_PLAIN);
        L("{");
        ind++;
        for (size_t i = 0; i < c->fvparams.size(); i++)
            BindLocal(c->fvparams[i], i < c->args.size() ? c->args[i] : nullptr);
        for (auto st : c->fvbody->stmts) GenStmt(st);
        if (c->fvbody->tail) {
            if (wantsval) GenAny(c->fvbody->tail, d);
            else GenAny(c->fvbody->tail, Dst {});
        }
        PopSc();
        ind--;
        L("}");
        termjump = false;
        if (!wantsval) return {};
        return { rv };
    }

    // Case-function tag dispatch (§8.2): switch on the tag, call the matching
    // specialization with the payload, sharing all other argument slots and
    // return channels across the arms.
    vector<string> EmitDispatch(Call *c, Dst d0, vector<Dst> *alldst) {
        auto sp0 = c->dispatch[0];
        auto an = CallArgNodes(c, sp0->params.size());
        auto pos = c->dispatcharg;
        // The scrutinee.
        auto sn = an[pos];
        auto st = sn->exprtype;
        TypeExpr *enumtype = nullptr;
        auto isref = false;
        if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
            enumtype = st->ref->sub;
            isref = true;
        } else {
            assert(st->kind == TY_ENUM);
            enumtype = st;
        }
        auto varmode = enumtype->enu->varmode;
        auto ei = EIOf(enumtype);
        auto ts = TagSize(ei->en);
        string p, sv, tag;
        if (varmode || isref) {
            if (isref) {
                auto x = GenPure(sn);
                p = varmode ? x : cat("((uint8_t *)", x, ")");
            } else {
                p = GenPtr(sn);
            }
            tag = varmode ? cat("*(", IntCT(TagStore(ei->en)), " *)", p)
                          : cat("((", CT(enumtype), " *)", p, ")->tag");
        } else {
            sv = GenPure(sn);
            tag = cat(sv, ".tag");
        }
        // Non-dispatch arguments evaluate once, before the switch.
        vector<string> shared(an.size());
        vector<string> sharedstk(an.size());
        for (size_t i = 0; i < an.size(); i++) {
            if ((int)i == pos) continue;
            auto pt = sp0->argtypes[i];
            if (IsPoolParam(sp0, i)) {
                shared[i] = GenPrefVal(an[i]);
            } else if (IsBytesT(pt)) {
                auto stk = AllocStk(false);
                auto base = T();
                L("uint8_t *", base, " = ", stk, "->top;");
                SaveBase(false, stk, base);
                GenConstruct(an[i], stk);
                shared[i] = base;
                sharedstk[i] = stk;
            } else {
                shared[i] = GenPure(an[i]);
            }
        }
        // Shared return channels.
        auto &ri = sinfo[sp0];
        vector<string> retex(sp0->rets.size());
        vector<string> dststk(sp0->rets.size());
        for (size_t i = 0; i < sp0->rets.size(); i++) {
            auto rt = sp0->rets[i];
            Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                                  : (i == 0 ? d0 : Dst {});
            if (IsBytesT(rt)) {
                string stk;
                if (dd.k == 2) stk = dd.s;
                else {
                    stk = AllocStk(false);
                    auto sb = T();
                    L("uint8_t *", sb, " = ", stk, "->top;");
                    SaveBase(false, stk, sb);
                }
                auto base = T();
                L("uint8_t *", base, " = ", stk, "->top;");
                retex[i] = base;
                dststk[i] = stk;
            } else {
                auto tv = T();
                L(CT(rt), " ", tv, ";");
                retex[i] = tv;
            }
        }
        L("switch (", tag, ") {");
        for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
            auto sp = c->dispatch[vi];
            auto &ki = sinfo[sp];
            L("case ", TagConst(ei, (int)vi), ": {");
            ind++;
            auto vt = VariantType(enumtype, (int)vi);
            auto byref = sp->argtypes[pos]->kind == TY_REF;
            string varg;
            string payload = varmode ? cat("(", p, " + ", ts, ")")
                           : isref ? cat("((uint8_t *)&((", CT(enumtype), " *)", p, ")->u.v_",
                                         Sanitize(ei->en->variants[vi].name), ")")
                                   : "";
            if (byref) {
                if (IsBytesT(vt)) varg = payload;
                else {
                    auto tv = T();
                    L(CT(vt), " *", tv, " = (", CT(vt), " *)(", payload, ");");
                    varg = tv;
                }
            } else if (IsBytesT(vt)) {
                auto stk = AllocStk(false);
                auto base = T();
                L("uint8_t *", base, " = ", stk, "->top;");
                SaveBase(false, stk, base);
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(vt, payload), ";");
                L("memcpy(", stk, "->top, ", payload, ", (size_t)", sz, ");");
                Bump(stk, sz);
                varg = base;
            } else if (ei->en->variants[vi].fields.empty()) {
                auto tv = T();
                L(CT(vt), " ", tv, " = {0};");
                varg = tv;
            } else if (!payload.empty()) {
                auto tv = T();
                L(CT(vt), " ", tv, " = *(", CT(vt), " *)(", payload, ");");
                varg = tv;
            } else {
                auto tv = T();
                L(CT(vt), " ", tv, " = ", sv, ".u.v_", Sanitize(ei->en->variants[vi].name),
                  ";");
                varg = tv;
            }
            // Assemble the arm's call.
            vector<string> args;
            for (size_t i = 0; i < an.size(); i++) {
                if ((int)i == pos) {
                    args.push_back(varg);
                    if (IsBytesT(sp->argtypes[i]) && IsResz(sp->argtypes[i]))
                        Fail(c->line, "resizable by-value dispatch payloads are unsupported");
                } else {
                    args.push_back(shared[i]);
                    if (IsBytesT(sp->argtypes[i]) && IsResz(sp->argtypes[i]))
                        args.push_back(sharedstk[i]);
                }
            }
            for (auto fv : ki.freevars) EmitFvArg(fv, args);
            for (size_t i = 0; i < sp->rets.size(); i++) {
                if (IsBytesT(sp->rets[i])) args.push_back(dststk[i]);
                else if ((int)i != ki.cret) args.push_back(cat("&", retex[i]));
            }
            if (ki.needssp) args.push_back(SpTop());
            string rfv;
            if (ki.hasrf) {
                rfv = T();
                L("int32_t ", rfv, " = 0;");
                args.push_back(cat("&", rfv));
            }
            string argstr;
            for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
            if (ki.cret >= 0) L(retex[ki.cret], " = ", ki.cname, "(", argstr, ");");
            else L(ki.cname, "(", argstr, ");");
            if (ki.hasrf) EmitRfCheck(rfv, sp);
            ind--;
            L("} break;");
        }
        L("default: gs_abort(\"corrupt ADT tag\", ", WhereStr(c->line), ");");
        L("}");
        (void)ri;
        return retex;
    }

    // ------------------------------------------------------------------
    // Builtins (§3.3, §5.4, §11.2, §12). Emitted inline; only I/O, division,
    // varints, and thread/queue machinery call into the runtime.

    unordered_map<string, string> queues;   // Element type mangle -> queue global.

    string QueueFor(TypeExpr *t) {
        usesthreads = true;
        auto m = Mangle(t);
        auto it = queues.find(m);
        if (it != queues.end()) return it->second;
        auto name = Unique(cat("gs_q_", m));
        Append(data, "static gs_queue ", name, " = GS_QUEUE_INIT;\n");
        return queues[m] = name;
    }

    // The receiver of a member operation, dereferenced, with its stack.
    Loc RecvLoc(Node *n) {
        auto lv = GenLoc(n);
        if (lv.t->kind == TY_REF) DerefLoc(lv, n->line);
        return lv;
    }

    TypeExpr *MakeSliceT(TypeExpr *elem, Line l) {
        auto t = ast.NewType(TY_SLICE, l);
        t->sub = elem;
        return t;
    }

    // The C type a length-field write casts to, per receiver representation.
    string LenCast(const Loc &lv) {
        auto ak = lv.t->arr->akind;
        if (ak == A_LIMITED) return lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t";
        if (ak == A_VAR) return IntCT(LenStore(lv.t->arr));
        return "int64_t";
    }

    vector<string> EmitBuiltin(Call *c, Dst d0) {
        vector<Node *> an;
        if (auto dd = Is<Dot>(c->callee)) an.push_back(dd->obj);
        for (auto a : c->args) an.push_back(a);
        auto ln = c->line;
        switch ((BuiltinKind)c->builtin) {
            case B_PRINT: {
                auto t = an[0]->exprtype;
                if (t->kind == TY_INT) { L("gs_print_int(", GenX(an[0]), ");"); return {}; }
                if (t->kind == TY_FLT) {
                    L("gs_print_flt((double)(", GenX(an[0]), "));");
                    return {};
                }
                if (t->kind == TY_BOOL) { L("gs_print_bool(", GenX(an[0]), ");"); return {}; }
                if (t->kind == TY_SLICE) {
                    auto x = GenPure(an[0]);
                    L("gs_print_bytes(", x, ".data, ", x, ".len);");
                    return {};
                }
                assert(t->kind == TY_ARRAY);
                auto lv = RecvLoc(an[0]);
                auto v = ArrayView(lv, ln);
                L("gs_print_bytes(", v.typedelems ? v.elems : cat("(uint8_t *)(", v.elems, ")"),
                  ", ", v.len, ");");
                return {};
            }
            case B_ASSERT: {
                auto x = GenTruth(an[0]);
                L("if (!(", x, ")) gs_abort(\"assert failed\", ", WhereStr(ln), ");");
                return {};
            }
            case B_UNSIGNED_DIV:
                return { cat("gs_udiv(", GenX(an[0]), ", ", GenX(an[1]), ", ", WhereStr(ln),
                             ")") };
            case B_UNSIGNED_MOD:
                return { cat("gs_umod(", GenX(an[0]), ", ", GenX(an[1]), ", ", WhereStr(ln),
                             ")") };
            case B_UNSIGNED_SHR:
                return { cat("(int64_t)((uint64_t)(", GenX(an[0]), ") >> ((", GenX(an[1]),
                             ") & 63))") };
            case B_UNSIGNED_LESS:
                return { cat("(uint8_t)((uint64_t)(", GenX(an[0]), ") < (uint64_t)(",
                             GenX(an[1]), "))") };
            case B_HARDWARE_THREADS: return { "gs_hardware_threads()" };
            case B_THREAD_WAIT: {
                usesthreads = true;
                L("gs_thread_wait(", GenX(an[0]), ", ", WhereStr(ln), ");");
                return {};
            }
            case B_THREAD_SPAWN: return EmitThreadSpawn(c, an);
            case B_QPUT: {
                auto t = an[0]->exprtype;
                auto q = QueueFor(t);
                if (IsBytesT(t)) {
                    auto p = GenPtr(an[0]);
                    L("gs_qput(&", q, ", ", p, ", ", SizeX(t, p), ");");
                } else {
                    auto tv = T();
                    L(CT(t), " ", tv, " = ", GenX(an[0]), ";");
                    L("gs_qput(&", q, ", &", tv, ", ", FixedSize(t), ");");
                }
                return {};
            }
            case B_QGET: case B_QPOLL: {
                auto t = c->rettypes[0];
                auto q = QueueFor(t);
                auto nn = T();
                auto poll = c->builtin == B_QPOLL;
                L("gs_qnode *", nn, poll ? " = gs_qpoll(&" : " = gs_qget(&", q, ");");
                string got;
                if (poll) {
                    got = T();
                    L("uint8_t ", got, " = ", nn, " != NULL;");
                }
                if (IsBytesT(t)) {
                    string stk = d0.k == 2 ? d0.s : "";
                    string base = T();
                    if (stk.empty()) {
                        stk = AllocStk(false);
                        L("uint8_t *", base, " = ", stk, "->top;");
                        SaveBase(false, stk, base);
                    } else {
                        L("uint8_t *", base, " = ", stk, "->top;");
                    }
                    if (poll) L("if (", nn, ") {");
                    else L("{");
                    ind++;
                    L("memcpy(", stk, "->top, ", nn, " + 1, (size_t)", nn, "->size);");
                    Bump(stk, cat(nn, "->size"));
                    L("free(", nn, ");");
                    ind--;
                    if (poll) {
                        // A missed poll still yields a valid (zero) value.
                        L("} else {");
                        ind++;
                        L("memset(", stk, "->top, 0, ", ZeroSize(t), ");");
                        Bump(stk, cat(ZeroSize(t)));
                        ind--;
                    }
                    L("}");
                    return poll ? vector<string> { base, got } : vector<string> { base };
                }
                auto tv = T();
                L(CT(t), " ", tv, ";");
                if (poll) {
                    L("memset(&", tv, ", 0, sizeof(", tv, "));");
                    L("if (", nn, ") { memcpy(&", tv, ", ", nn, " + 1, sizeof(", tv,
                      ")); free(", nn, "); }");
                } else {
                    L("memcpy(&", tv, ", ", nn, " + 1, sizeof(", tv, "));");
                    L("free(", nn, ");");
                }
                return poll ? vector<string> { tv, got } : vector<string> { tv };
            }
            case B_PUSH: return EmitPush(c, an, ln);
            case B_APPEND: EmitAppend(c, an, ln); return {};
            case B_POP: {
                auto lv = RecvLoc(an[0]);
                auto v = ArrayView(lv, ln);
                auto elem = v.elem;
                auto esz = FixedSize(elem);
                auto nl = T();
                L("int64_t ", nl, " = ", v.len, " - 1;");
                L(v.lenlv, " = (", LenCast(lv), ")", nl, ";");
                auto tv = T();
                if (lv.t->arr->akind == A_GROWSHRINK) {
                    // The array tops its stack: the element region ends at top.
                    L(lv.stk, "->top -= ", esz, ";");
                    L(CT(elem), " ", tv, " = *(", CT(elem), " *)", lv.stk, "->top;");
                } else if (v.typedelems) {
                    L(CT(elem), " ", tv, " = ", v.elems, "[", nl, "];");
                } else {
                    L(CT(elem), " ", tv, " = *(", CT(elem), " *)((", v.elems, ") + ", nl,
                      " * ", esz, ");");
                }
                return { tv };
            }
            case B_RESIZE: {
                auto lv = RecvLoc(an[0]);
                auto v = ArrayView(lv, ln);
                auto elem = v.elem;
                auto esz = FixedSize(elem);
                auto ak = lv.t->arr->akind;
                auto nn = GenPure(an[1]);
                string fv;
                if (an.size() > 2) fv = GenPure(an[2]);
                auto ol = T();
                L("int64_t ", ol, " = ", v.len, ";");
                L("if (", nn, " < ", ol, ") {");
                ind++;
                if (ak == A_GROW) L("gs_abort(\"grow-only arrays cannot shrink\", ",
                                    WhereStr(ln), ");");
                else {
                    L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
                    if (ak == A_GROWSHRINK)
                        L(lv.stk, "->top = (uint8_t *)(", v.elems, ") + ", nn, " * ", esz, ";");
                }
                ind--;
                L("} else if (", nn, " > ", ol, ") {");
                ind++;
                if (fv.empty()) {
                    L("gs_abort(\"resize growth requires a fill value\", ", WhereStr(ln), ");");
                } else {
                    if (ak == A_LIMITED) {
                        auto capx = lv.val ? cat(ArrSize(lv.t->arr))
                                           : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
                        L("if (", nn, " > ", capx,
                          ") gs_abort(\"limited array capacity exceeded\", ", WhereStr(ln),
                          ");");
                    }
                    auto iv = T();
                    L("for (int64_t ", iv, " = ", ol, "; ", iv, " < ", nn, "; ", iv, "++) {");
                    ind++;
                    if (ak == A_LIMITED) {
                        if (v.typedelems) L(v.elems, "[", iv, "] = ", fv, ";");
                        else L("*(", CT(elem), " *)((", v.elems, ") + ", iv, " * ", esz,
                               ") = ", fv, ";");
                    } else {
                        L("*(", CT(elem), " *)", lv.stk, "->top = ", fv, ";");
                        L(lv.stk, "->top += ", esz, ";");
                    }
                    ind--;
                    L("}");
                    L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
                }
                ind--;
                L("}");
                if (!fv.empty() && an.size() > 2) { /* fill evaluated once above */ }
                return {};
            }
            case B_CLEAR: {
                auto lv = RecvLoc(an[0]);
                auto v = ArrayView(lv, ln);
                if (lv.t->arr->akind == A_GROWSHRINK)
                    L(lv.stk, "->top = (uint8_t *)(", v.elems, ");");
                L(v.lenlv, " = 0;");
                return {};
            }
            case B_ALLOC_INDEX: case B_ALLOC_REF: return EmitAlloc(c, an, ln, d0);
            case B_FREE: {
                auto lv = RecvLoc(an[0]);
                assert(!lv.fl.empty());
                auto x = GenX(an[1]);
                L("*(int64_t *)", lv.flstk, "->top = ", x, ";");
                L(lv.flstk, "->top += 8;");
                L("(*(int64_t *)", lv.fl, ")++;");
                return {};
            }
            default:
                Fail(ln, cat("builtin not implemented: ", builtindefs[c->builtin].name));
        }
    }

    vector<string> EmitPush(Call *c, vector<Node *> &an, Line ln) {
        auto lv = RecvLoc(an[0]);
        auto v = ArrayView(lv, ln);
        auto elem = v.elem;
        auto ak = lv.t->arr->akind;
        string ref;
        if (ak == A_LIMITED) {
            auto esz = FixedSize(elem);
            auto capx = lv.val ? cat(ArrSize(lv.t->arr)) : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
            auto nl = T();
            L("int64_t ", nl, " = ", v.len, ";");
            L("if (", nl, " >= ", capx, ") gs_abort(\"limited array capacity exceeded\", ",
              WhereStr(ln), ");");
            auto ev = T();
            L(CT(elem), " ", ev, " = ", GenXD(an[1], elem), ";");
            auto e = T();
            if (v.typedelems) L(CT(elem), " *", e, " = &", v.elems, "[", nl, "];");
            else L(CT(elem), " *", e, " = (", CT(elem), " *)((", v.elems, ") + ", nl, " * ",
                   esz, ");");
            L("*", e, " = ", ev, ";");
            L(v.lenlv, " = (", lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t", ")(", nl,
              " + 1);");
            ref = e;
        } else {
            assert(!lv.stk.empty());
            auto e = T();
            if (IsBytesT(elem)) {
                L("uint8_t *", e, " = ", lv.stk, "->top;");
            } else {
                L(CT(elem), " *", e, " = (", CT(elem), " *)", lv.stk, "->top;");
            }
            GenConstruct(an[1], lv.stk, elem);
            L("(*(int64_t *)(", lv.s, ")", ")++;");
            ref = e;
        }
        // push returns a reference on grow-only and limited arrays (§3.3).
        // An un-annotated receiver decayed the reference to an element copy.
        if (c->exprtype && c->exprtype->kind != TY_REF && c->exprtype->kind != TY_VOID &&
            !IsBytesT(elem))
            return { cat("(*", ref, ")") };
        return { ref };
    }

    void EmitAppend(Call *c, vector<Node *> &an, Line ln) {
        (void)c;
        auto lv = RecvLoc(an[0]);
        auto v = ArrayView(lv, ln);
        auto elem = v.elem;
        auto ak = lv.t->arr->akind;
        auto src = an[1];
        // append(f()): construct the callee's value at our top, then slide its
        // length metadata out so the elements join contiguously (§7.3).
        if (auto call = Is<Call>(src); call && IsBytesT(src->exprtype) && ak != A_LIMITED) {
            auto srct = src->exprtype;
            assert(srct->kind == TY_ARRAY);
            auto tv = T();
            L("uint8_t *", tv, " = ", lv.stk, "->top;");
            EmitCall(call, Dst { 2, lv.stk });
            auto ls = ArrLenStore(srct);
            string metasz, count;
            if (ls == IS_VARINT) {
                metasz = cat("gs_uleb_size(", tv, ")");
                count = cat("gs_uleb_read(", tv, ")");
            } else {
                metasz = cat(IntSize(ls));
                count = cat("(int64_t)*(", IntCT(ls), " *)", tv);
            }
            auto nn = T(), ms = T();
            L("int64_t ", nn, " = ", count, ";");
            L("int64_t ", ms, " = ", metasz, ";");
            L("memmove(", tv, ", ", tv, " + ", ms, ", (size_t)(", lv.stk, "->top - ", tv,
              " - ", ms, "));");
            L(lv.stk, "->top -= ", ms, ";");
            L("(*(int64_t *)(", lv.s, ")", ") += ", nn, ";");
            return;
        }
        auto se = GenSrcElems(src);
        auto nn = T();
        L("int64_t ", nn, " = ", se.n, ";");
        if (ak == A_LIMITED) {
            auto esz = FixedSize(elem);
            auto capx = lv.val ? cat(ArrSize(lv.t->arr)) : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
            auto ol = T();
            L("int64_t ", ol, " = ", v.len, ";");
            L("if (", ol, " + ", nn, " > ", capx,
              ") gs_abort(\"limited array capacity exceeded\", ", WhereStr(ln), ");");
            L("memcpy(", v.typedelems ? cat(v.elems, " + ", ol)
                                      : cat("(", v.elems, ") + ", ol, " * ", esz),
              ", ", se.elems, ", (size_t)(", nn, " * ", esz, "));");
            L(v.lenlv, " = (", lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t", ")(", ol,
              " + ", nn, ");");
            return;
        }
        assert(!lv.stk.empty());
        EmitCopyElems(lv.stk, elem, se.elems, nn);
        L("(*(int64_t *)(", lv.s, ")", ") += ", nn, ";");
    }

    vector<string> EmitAlloc(Call *c, vector<Node *> &an, Line ln, Dst d0) {
        (void)d0;
        auto lv = RecvLoc(an[0]);
        assert(!lv.fl.empty() && !lv.stk.empty());
        auto v = ArrayView(lv, ln);
        auto elem = v.elem;
        auto esz = FixedSize(elem);
        auto ev = GenPure(an[1]);
        auto iv = T();
        L("int64_t ", iv, ";");
        L("if (*(int64_t *)", lv.fl, " > 0) {");
        ind++;
        L("(*(int64_t *)", lv.fl, ")--;");
        L(lv.flstk, "->top -= 8;");
        L(iv, " = *(int64_t *)", lv.flstk, "->top;");
        ind--;
        L("} else {");
        ind++;
        L(iv, " = *(int64_t *)(", lv.s, ")", ";");
        L("(*(int64_t *)(", lv.s, ")", ")++;");
        L(lv.stk, "->top += ", esz, ";");
        ind--;
        L("}");
        auto e = T();
        L(CT(elem), " *", e, " = (", CT(elem), " *)((", v.elems, ") + ", iv, " * ", esz,
          ");");
        L("*", e, " = ", ev, ";");
        if (c->builtin == B_ALLOC_INDEX) return { iv };
        if (c->exprtype && c->exprtype->kind != TY_REF) return { cat("(*", e, ")") };
        return { e };
    }

    // thread_spawn(worker, args...): pack the flat arguments contiguously on
    // a scratch stack, hand them to the runtime, unpack in a per-worker thunk.
    unordered_map<FnSpec *, string> thunks;

    vector<string> EmitThreadSpawn(Call *c, vector<Node *> &an) {
        usesthreads = true;
        auto sp = c->spec;
        auto thunk = EnsureThreadThunk(sp);
        auto stk = AllocStk(false);
        auto base = T();
        L("uint8_t *", base, " = ", stk, "->top;");
        SaveBase(false, stk, base);
        for (size_t i = 0; i < sp->argtypes.size(); i++) GenConstruct(an[1 + i], stk);
        return { cat("gs_thread_spawn(", thunk, ", ", base, ", ", stk, "->top - ", base,
                     ")") };
    }

    string EnsureThreadThunk(FnSpec *sp) {
        auto it = thunks.find(sp);
        if (it != thunks.end()) return it->second;
        auto name = Unique(cat("gs_tmain_", Sanitize(sp->sf->name)));
        thunks[sp] = name;
        Append(protos, "static void ", name, "(uint8_t *p);\n");
        auto &ki = sinfo[sp];
        string b;
        Append(b, "static void ", name, "(uint8_t *p) {\n");
        vector<string> args;
        for (size_t i = 0; i < sp->argtypes.size(); i++) {
            auto pt = sp->argtypes[i];
            if (IsBytesT(pt)) {
                if (IsResz(pt))
                    Fail(sp->sf->line, "resizable by-value thread arguments are unsupported");
                Append(b, "    uint8_t *a", i, " = p;\n");
                // Advance past the value; a size fn may be emitted on demand.
                Append(b, "    p += ", SizeX(pt, cat("a", i)).c_str(), ";\n");
                args.push_back(cat("a", i));
            } else {
                Append(b, "    ", CT(pt), " a", i, " = *(", CT(pt), " *)p; p += ",
                       FixedSize(pt), ";\n");
                args.push_back(cat("a", i));
            }
        }
        assert(ki.freevars.empty() && !ki.hasrf && sp->rets.empty());
        if (ki.needssp) args.push_back("0");
        string argstr;
        for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
        Append(b, "    ", ki.cname, "(", argstr, ");\n}\n\n");
        code += b;
        return name;
    }

    // ------------------------------------------------------------------
    // Function bodies.

    // Guaranteed NRVO (§7.3): a top-level local every return hands back in
    // one nonfixed return position is allocated at that destination.
    void DetectNrvo(FnSpec *sp) {
        nrvovars.clear();
        nrvodst.clear();
        if (sp->rets.empty()) return;
        set<const VarDef *> toplocals;
        for (auto st : sp->body->stmts)
            if (auto vd = Is<VarDecl>(st))
                for (auto d : vd->defs) toplocals.insert(d);
        for (size_t j = 0; j < sp->rets.size(); j++) {
            if (!IsBytesT(sp->rets[j])) continue;
            const VarDef *cand = nullptr;
            auto ok = true;
            auto consider = [&](Node *val) {
                auto id = Is<Ident>(val);
                if (!id || !id->vdef || !toplocals.count(id->vdef)) { ok = false; return; }
                if (cand && cand != id->vdef) { ok = false; return; }
                cand = id->vdef;
            };
            function<void(Node *)> walk = [&](Node *n) {
                if (!n || !ok) return;
                if (auto r = Is<Return>(n)) {
                    if (r->target == sp->sf) {
                        if (r->vals.size() != sp->rets.size()) ok = false;
                        else consider(r->vals[j]);
                    }
                }
                if (auto cc = Is<Call>(n)) walk(cc->fvbody);
                n->Children([&](Node *ch) { walk(ch); });
            };
            walk(sp->body);
            if (sp->body->tail && sp->rets.size() == 1 &&
                !IsVoidT(sp->body->tail->exprtype)) consider(sp->body->tail);
            if (ok && cand) {
                nrvovars.insert(cand);
                nrvodst[cand] = (int)j;
            }
        }
    }

    void ResetFnState() {
        vnames.clear();
        vstk.clear();
        vpool.clear();
        fvptr.clear();
        fnused.clear();
        nrvovars.clear();
        nrvodst.clear();
        fdstsaves.clear();
        cscopes.clear();
        body.clear();
        tmpn = 0;
        stknext = 0;
        stkmax = 0;
        ind = 1;
        termjump = false;
    }

    void EmitSpec(FnSpec *sp) {
        curspec = sp;
        curinfo = &sinfo[sp];
        ResetFnState();
        cursp = curinfo->needssp;
        spexpr = cursp ? "gs_sp" : "0";
        PushSc(SC_FN);
        auto params = SigParams(sp, true);
        DetectNrvo(sp);
        if (fromids.count(sp->sf)) {
            // This is a long-distance return target with nonfixed returns:
            // record our destinations for in-flight values (§7.9).
            EnsureFromChannels(sp->sf);
            auto tid = fromids[sp->sf];
            for (size_t i = 0; i < sp->rets.size(); i++) {
                if (!IsBytesT(sp->rets[i])) continue;
                auto sav = T();
                L("gs_stack *", sav, " = gs_fdst_", tid, "_", i, ";");
                L("gs_fdst_", tid, "_", i, " = gs_dst", i, ";");
                fdstsaves.push_back(cat("gs_fdst_", tid, "_", i, " = ", sav, ";"));
            }
        }
        for (auto st : sp->body->stmts) GenStmt(st);
        if (auto tail = sp->body->tail) {
            auto asvalue = !sp->rets.empty();
            if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) asvalue = false;
            if (Is<Guard>(tail)) asvalue = false;
            if (asvalue && !IsVoidT(tail->exprtype)) {
                vector<Node *> vals = { tail };
                GenNormalReturn(vals, tail->line);
            } else {
                GenStmt(tail);
            }
        }
        if (!termjump) {
            if (curinfo->cret >= 0) {
                L("gs_abort(\"function fell off the end\", ", WhereStr(sp->sf->line), ");");
                auto d = T();
                L(CT(sp->rets[curinfo->cret]), " ", d, " = {0};");
                L("return ", d, ";");
            } else {
                Epilogue("");
            }
        }
        cscopes.clear();
        Append(code, "static ", SigRet(sp), " ", curinfo->cname, "(", params, ") {\n");
        if (stkmax > 0)
            Append(code, "    GS_ENSURE(", spexpr, " + ", stkmax, ");\n");
        code += body;
        code += "}\n\n";
        curspec = nullptr;
        curinfo = nullptr;
    }

    // ------------------------------------------------------------------
    // Globals (§11.1): C globals plus dedicated data stacks for nonfixed
    // ones; initializers run in declaration order before main.

    void EmitGlobalDecls() {
        for (auto g : ast.globals) {
            for (auto d : g->defs) {
                auto name = Unique(Sanitize(d->name));
                gnames[d] = name;
                if (IsBytesT(d->type)) {
                    auto stk = Unique(cat("gs_gstk_", name));
                    Append(data, "static uint8_t *", name, ";\nstatic gs_stack ", stk,
                           ";\n");
                    gstks[d] = cat("(&", stk, ")");
                    if (d->reusable) {
                        auto fln = Unique(cat(name, "_fl"));
                        auto flstk = Unique(cat("gs_gstk_", fln));
                        Append(data, "static uint8_t *", fln, ";\nstatic gs_stack ", flstk,
                               ";\n");
                        gpools[d] = { fln, cat("(&", flstk, ")") };
                    }
                } else {
                    Append(data, "static ", VarCT(d), " ", name, ";\n");
                }
            }
        }
    }

    void EmitGlobalInit() {
        curspec = nullptr;
        curinfo = nullptr;
        ResetFnState();
        cursp = false;
        spexpr = "0";
        PushSc(SC_FN);
        for (auto g : ast.globals) {
            if (g->inits.empty()) continue;
            PushSc(SC_STMT);
            if (g->defs.size() > 1 && g->inits.size() == 1) {
                auto c = Is<Call>(g->inits[0]);
                assert(c);
                vector<Dst> dsts;
                for (size_t i = 0; i < g->defs.size(); i++) {
                    auto d = g->defs[i];
                    if (IsBytesT(d->type)) {
                        InitGlobalStack(d);
                        dsts.push_back(Dst { 2, gstks[d] });
                    } else {
                        dsts.push_back(Dst { 1, gnames[d] });
                    }
                }
                auto rets = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
                for (size_t i = 0; i < dsts.size() && i < rets.size(); i++)
                    if (dsts[i].k == 1 && !rets[i].empty() && rets[i] != dsts[i].s)
                        L(dsts[i].s, " = ", rets[i], ";");
            } else {
                for (size_t i = 0; i < g->defs.size(); i++) {
                    auto d = g->defs[i];
                    if (IsBytesT(d->type)) {
                        InitGlobalStack(d);
                        GenConstruct(g->inits[i], gstks[d]);
                    } else if (PrefVar(d)) {
                        L(gnames[d], " = ", GenPrefVal(g->inits[i]), ";");
                    } else {
                        GenAny(g->inits[i], Dst { 1, gnames[d] });
                    }
                }
            }
            if (termjump) cscopes.back().saves.clear();
            PopSc();
            termjump = false;
        }
        EmitExitRestores(0);
        cscopes.clear();
        Append(code, "static void gs_init_globals(void) {\n");
        if (stkmax > 0) Append(code, "    GS_ENSURE(", stkmax, ");\n");
        code += body;
        code += "}\n\n";
    }

    void InitGlobalStack(VarDef *d) {
        auto stk = gstks[d];
        L("gs_stack_init(", stk, ");");
        L(gnames[d], " = ", stk, "->top;");
        if (d->reusable) {
            auto &p = gpools[d];
            L("gs_stack_init(", p.second, ");");
            L(p.first, " = ", p.second, "->top;");
            L("*(int64_t *)", p.first, " = 0;");
            L(p.second, "->top += 8;");
        }
    }

    void EmitMain() {
        FnSpec *mainspec = nullptr;
        auto mit = ast.functionmap.find("main");
        if (mit != ast.functionmap.end() && !mit->second[0]->specs.empty())
            mainspec = mit->second[0]->specs[0];
        Append(code, "int main(void) {\n    gs_rt_init();\n    gs_init_globals();\n");
        if (mainspec && sinfo.count(mainspec)) {
            auto &mi = sinfo[mainspec];
            Append(code, "    ", mi.cname, "(", mi.needssp ? "0" : "", ");\n");
        }
        Append(code, "    return 0;\n}\n");
    }

    // ------------------------------------------------------------------
    // Driver.

    string result;   // Everything after the runtime paste.

    CodeGen(Ast &_ast) : ast(_ast) {
        CollectSpecs();
        EmitGlobalDecls();
        // Prototypes for every live specialization, then their bodies.
        for (auto sp : livespecs)
            Append(protos, "static ", SigRet(sp), " ", sinfo[sp].cname, "(",
                   SigParams(sp, false), ");\n");
        for (auto sp : livespecs) EmitSpec(sp);
        EmitGlobalInit();
        EmitMain();
        if (usesthreads) predefs = "#define GS_NEED_THREADS 1\n";
        Append(result, "\n/* ---- types ---- */\n#pragma pack(push, 1)\n", tdecls, pdata,
               "#pragma pack(pop)\n\n/* ---- data ---- */\n", data,
               "\n/* ---- prototypes ---- */\n", protos, "\n/* ---- code ---- */\n", code);
    }
};

}  // namespace goose
