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
// * References to resizable-class values are fat (gs_rref: header + stack);
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

// `lenlv` is set for k == 2 destinations of resizable class: elements go
// to the stack, and the construction assigns the element count to it.
struct Dst {
    int k = 0;
    string s;
    TypeExpr *t = nullptr;
    string lenlv;
};

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

    // Abort locations: the file path becomes one static string per source
    // file in the generated code; call sites pass it plus the line number.
    map<int, string> filerefs;

    string LocArgs(Line l) {
        auto it = filerefs.find(l.fileidx);
        if (it == filerefs.end()) {
            auto f = l.fileidx >= 0 && l.fileidx < (int)ast.sources.size()
                         ? ast.sources[l.fileidx].first : string("?");
            for (auto &c : f) if (c == '\\') c = '/';
            auto name = Unique(cat("gs_file", filerefs.size()));
            Append(data, "static const char ", name, "[] = ", CStr(f), ";\n");
            it = filerefs.emplace(l.fileidx, name).first;
        }
        return cat(it->second, ", ", l.line);
    }

    // ------------------------------------------------------------------
    // Type utilities on concrete (post-typecheck) types. Sizes of fixed and
    // limited arrays were evaluated during checking; assert rather than
    // re-evaluate.

    int64_t ArrSize(TypeArray *a) {
        assert(a->size >= 0 || !a->sizeexpr);
        return a->size;
    }

    bool TEq(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (a->kind != b->kind) return false;
        switch (a->kind) {
            case TY_INT:  return a->intstorage == b->intstorage;
            case TY_FLT:  return a->fltstorage == b->fltstorage;
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

    static int64_t IntSize(IntStorage s) { return IntBits(s) / 8; }

    static const char *IntCT(IntStorage s) {
        switch (s) {
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

    // The value range of an integer type (u64's maximum reads as -1).
    static pair<int64_t, int64_t> IntRange(IntStorage s) {
        switch (s) {
            case IS_I8:  return { -128, 127 };
            case IS_I16: return { -32768, 32767 };
            case IS_I32: return { INT32_MIN, INT32_MAX };
            case IS_U8:  return { 0, 255 };
            case IS_U16: return { 0, 65535 };
            case IS_U32: return { 0, (int64_t)UINT32_MAX };
            case IS_U64: return { 0, -1 };
            default:     return { INT64_MIN, INT64_MAX };
        }
    }

    // The runtime arithmetic helper suffix per integer type (runtime.h).
    static const char *IntSfx(IntStorage s) {
        switch (s) {
            case IS_I8:  return "i8";
            case IS_I16: return "i16";
            case IS_I32: return "i32";
            case IS_U8:  return "u8";
            case IS_U16: return "u16";
            case IS_U32: return "u32";
            case IS_U64: return "u64";
            default:     return "i64";
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
                return t->intstorage == IS_VARINT ? "vi" : IntSfx(t->intstorage);
            case TY_FLT: return t->fltstorage == FS_F32 ? "f32" : "f64";
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
        // A resizable value is a frame header (C.2): the data pointer (element
        // region for arrays, struct start for resizable-tailed structs) plus
        // the tail element count. A reference to one points at the header.
        tdecls +=
            "typedef struct { uint8_t *base; int64_t len; } gs_rhdr;\n"
            "typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;\n"
            "typedef struct { gs_rhdr *hdr; gs_stack *stk; gs_rhdr *fl; gs_stack *flstk; } "
            "gs_pref;\n\n";
        used.insert("gs_rhdr");
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
                    default:
                        // Resizable values never nest inside bytes values;
                        // their length lives in a frame header (C.2).
                        assert(false);
                        return;
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
        assert(!IsResz(t));   // Resizable zero values are empty headers.
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
            return cat("(", a, ".hdr == ", b, ".hdr)");
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
    string SigParams(FnSpec *sp, bool decls, bool er = false) {
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
                EmitCoreTypes();
                add(cat("gs_pref ", pn));
            } else if (IsResz(pt)) {
                // By-value resizable: the header by value plus its stack (C.3).
                EmitCoreTypes();
                add(cat("gs_rhdr ", pn));
                add(cat("gs_stack *", pn, "_stk"));
                if (decls) vstk[vd] = cat(pn, "_stk");
            } else if (IsBytesT(pt)) {
                add(cat("uint8_t *", pn));
            } else {
                add(cat(CT(pt), " ", pn));
            }
        }
        auto fvn = 0;
        for (auto fv : si.freevars) {
            auto fn = decls ? LocalName(const_cast<VarDef *>(fv)) : cat("fv", fvn++);
            auto ft = fv->type;
            if (fv->reusable) {
                EmitCoreTypes();
                add(cat("gs_pref ", fn));
            } else if (IsResz(ft)) {
                EmitCoreTypes();
                add(cat("gs_rhdr *", fn));
                add(cat("gs_stack *", fn, "_stk"));
                if (decls) {
                    vstk[fv] = cat(fn, "_stk");
                    fvptr.insert(fv);
                }
            } else if (IsBytesT(ft)) {
                add(cat("uint8_t *", fn));
            } else {
                add(cat(VarCT(fv), " *", fn));
                if (decls) fvptr.insert(fv);
            }
        }
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (IsResz(sp->rets[i]) || (er && i == 0)) {
                // Elements go to the destination stack; the count comes back
                // through the length out-parameter (C.3's element-run form —
                // always for resizables, on demand for variable arrays).
                add(cat("gs_stack *gs_dst", i));
                add(cat("int64_t *gs_rl", i));
            } else if (IsBytesT(sp->rets[i])) {
                add(cat("gs_stack *gs_dst", i));
            } else if ((int)i != si.cret) {
                add(cat(CT(sp->rets[i]), " *gs_r", i));
            }
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
    bool emiter = false;                 // Emitting a spec's element-run twin.
    unordered_map<FnSpec *, string> ernames;   // "" = ineligible.
    vector<FnSpec *> erqueue;
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
        Dst dst;                              // Break/IB value destination.
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

    // ------------------------------------------------------------------
    // Data-stack top caching. A bump pointer read and written through
    // gs_stks[i].top is memory the C backend must reload after every byte
    // store, because a `uint8_t *` store may alias it; a run of pushes then
    // costs a load, an add and a store each instead of register arithmetic.
    // So each stack the function owns keeps its top in a local, and memory is
    // synchronized only where something else can observe it: across calls
    // (which may be handed the stack) and at every function exit.
    //
    // Soundness rests on one spelling per cached stack. Own stacks qualify:
    // a callee's indices start above the caller's in-use watermark (§10.3), so
    // `GS(gs_sp + k)` names a stack no caller expression can also name, and
    // global stacks live outside the indexed block entirely. A reference to a
    // resizable, though, carries its stack inside the reference value, giving
    // a second spelling for a stack that may be one of ours -- so a function
    // holding one keeps the plain memory form throughout (CanCacheTops).
    unordered_map<string, string> topcache;   // Stack expr -> its local.
    vector<string> toporder;                  // Discovery order, for stable output.
    bool cachetops = false;

    static constexpr const char *FLUSHMARK = "@@gsflush@@";
    static constexpr const char *RELOADMARK = "@@gsreload@@";

    set<string> gstkexprs;   // Every global's dedicated stack expression.

    // The function's own indexed stacks, plus the globals' dedicated ones --
    // both have a single spelling here (see the note above). A caller's stack,
    // arriving as a parameter, does not: it may be the very global stack this
    // body also names directly, so those keep the memory form.
    bool CacheableStk(const string &stk) {
        return cachetops && (stk.compare(0, 3, "GS(") == 0 || gstkexprs.count(stk));
    }

    // The lvalue for a stack's top: the cached local where the stack qualifies.
    string Top(const string &stk) {
        if (!CacheableStk(stk)) return cat(stk, "->top");
        auto it = topcache.find(stk);
        if (it != topcache.end()) return it->second;
        auto v = T();
        toporder.push_back(stk);
        return topcache[stk] = v;
    }

    // Sync points are marked rather than written, because a stack first used
    // after a call still needs that call's reload: the expansion happens once
    // the whole body is emitted and every cached stack is known.
    void MarkFlush()  { if (cachetops) L(FLUSHMARK); }
    void MarkReload() { if (cachetops) L(RELOADMARK); }

    // Replaces each marker line with the sync for every stack this function
    // caches, keeping the marker's own indentation.
    // A body that cached nothing still has its markers to remove.
    string ExpandTopMarkers(const string &b) {
        if (!cachetops) return b;
        string out;
        for (size_t i = 0; i < b.size();) {
            auto eol = b.find('\n', i);
            if (eol == string::npos) eol = b.size();
            auto line = string_view(b).substr(i, eol - i);
            auto ind0 = line.find_first_not_of(' ');
            auto rest = ind0 == string_view::npos ? string_view() : line.substr(ind0);
            if (rest == FLUSHMARK || rest == RELOADMARK) {
                auto store = rest == FLUSHMARK;
                for (auto &stk : toporder) {
                    out.append(ind0, ' ');
                    if (store) Append(out, stk, "->top = ", topcache[stk], ";\n");
                    else Append(out, topcache[stk], " = ", stk, "->top;\n");
                }
            } else {
                out.append(line);
                out += '\n';
            }
            i = eol + 1;
        }
        return out;
    }

    void PushSc(int kind) {
        CScope s;
        s.kind = kind;
        s.stkbase = stknext;
        cscopes.push_back(s);
    }

    void EmitRestores(const CScope &s) {
        for (auto i = (int)s.saves.size() - 1; i >= 0; i--)
            L(Top(s.saves[i].first), " = ", s.saves[i].second, ";");
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

    // For resizable-class locations, `s` is the data pointer (elements for
    // arrays, struct start for resizable-tailed structs) and `lenlv` the
    // int64 length lvalue of the owning header; `fl` is a gs_rhdr lvalue for
    // a reusable pool's freelist.
    struct Loc {
        string s;
        TypeExpr *t = nullptr;
        bool val = false;
        bool ispref = false;   // s is a gs_pref-typed lvalue (pool reference).
        string stk, lenlv, fl, flstk;
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

    // Whether a value of type `have` needs a pointee load to serve as `want`.
    bool NeedsDeref(TypeExpr *have, TypeExpr *want) {
        return have && want && have->kind == TY_REF && !have->ref->optional &&
               have->ref->lenstorage < 0 && want->kind != TY_REF && want->kind != TY_VOID;
    }

    Loc BytesLoc(const string &ptr, TypeExpr *t, const Loc &from) {
        Loc l;
        l.t = t;
        l.stk = from.stk;
        l.lenlv = from.lenlv;
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
            nl.s = cat(rv, ".hdr->base");
            nl.lenlv = cat(rv, ".hdr->len");
            nl.stk = cat(rv, ".stk");
            // A pool reference (gs_pref) has the same leading members plus the
            // freelist; keep the companions reachable for pool operations.
            nl.fl = lv.ispref ? cat("(*", rv, ".fl)") : lv.fl;
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
            // A reusable pool: locals/globals have gs_rhdr companions; a
            // captured pool arrives as a gs_pref.
            auto pit = vpool.find(vd);
            auto git = gpools.find(vd);
            if (pit != vpool.end() || git != gpools.end()) {
                auto &p = pit != vpool.end() ? pit->second : git->second;
                auto hdr = fvptr.count(vd) ? cat("(*", name, ")") : name;
                l.val = false;
                l.s = cat(hdr, ".base");
                l.lenlv = cat(hdr, ".len");
                l.stk = VStkOf(vd);
                l.fl = p.first;
                l.flstk = p.second;
            } else {
                l.val = false;
                l.s = cat(name, ".hdr->base");
                l.lenlv = cat(name, ".hdr->len");
                l.stk = cat(name, ".stk");
                l.fl = cat("(*", name, ".fl)");
                l.flstk = cat(name, ".flstk");
            }
            return l;
        }
        if (IsResz(vd->type)) {
            // A gs_rhdr in the frame (or a pointer to one, when captured).
            auto hdr = fvptr.count(vd) ? cat("(*", name, ")") : name;
            l.val = false;
            l.s = cat(hdr, ".base");
            l.lenlv = cat(hdr, ".len");
            l.stk = VStkOf(vd);
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
            l.fl = cat("(*", l.s, ".fl)");
            l.flstk = cat(l.s, ".flstk");
        }
        return l;
    }

    // The gs_rhdr lvalue of a resizable variable (frame header form, C.2).
    string HdrLv(VarDef *vd) {
        auto name = VName(vd);
        return fvptr.count(vd) ? cat("(*", name, ")") : name;
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
            default:   // Grow / grow-shrink: elements at base, length in the header.
                assert(!lv.val && !lv.lenlv.empty());
                v.elems = cat("(", lv.s, ")");
                v.len = cat("(", lv.lenlv, ")");
                v.lenlv = lv.lenlv;
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

    // Indexed element location with bounds check (elided when the BCE pass
    // proved it redundant, or for provably in-range constant indices into
    // fixed arrays).
    Loc IndexLoc(Loc lv, Node *idxnode, Line ln, bool nobc) {
        if (lv.t->kind == TY_REF) DerefLoc(lv, ln);
        auto v = ArrayView(lv, ln);
        auto idx = GenPure(idxnode);
        string ix;
        auto il = Is<IntLit>(idxnode);
        auto statlen = lv.t->kind == TY_ARRAY && lv.t->arr->akind == A_FIXED
                           ? ArrSize(lv.t->arr) : -1;
        if (nobc || (il && statlen >= 0 && il->val >= 0 && il->val < statlen)) ix = idx;
        else ix = cat("GS_IDX((int64_t)(", idx, "), ", v.len, ", ", LocArgs(ln), ")");
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
            return IndexLoc(lv, ix->idx, ix->line, ix->nobc);
        }
        // Any other expression: an addressed temporary (TC's LValueBase).
        Loc l;
        l.t = n->exprtype;
        if (IsResz(n->exprtype)) return GenRzTmp(n);
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

    // A resizable-valued temporary (a call or control result): elements on a
    // fresh statement-scoped stack, header in a temp.
    Loc GenRzTmp(Node *n) {
        EmitCoreTypes();
        auto stk = AllocStk(false);
        auto h = T();
        L("gs_rhdr ", h, " = { ", Top(stk), ", 0 };");
        SaveBase(false, stk, cat(h, ".base"));
        GenAny(n, Dst { 2, stk, n->exprtype, cat(h, ".len") });
        Loc l;
        l.t = n->exprtype;
        l.s = cat(h, ".base");
        l.lenlv = cat(h, ".len");
        l.stk = stk;
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
    // already encodes operand unification and reference decay.

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
            // Offset 0 is null only for the optional spelling (§3.9); a
            // non-optional slot holds a reference, so the address is the sum.
            if (r.optional) L("uint8_t *", p, " = ", ov, " ? ", faddr, " + ", ov, " : NULL;");
            else L("uint8_t *", p, " = ", faddr, " + ", ov, ";");
            // Relative references never point at resizables: pool elements
            // are at most variable-class (S3.3).
            assert(!IsFatPointee(r.sub));
            if (IsBytesT(r.sub)) return p;
            return cat("((", CT(r.sub), " *)", p, ")");
        }
        if (et && et->kind == TY_SLICE && lv.t->kind == TY_ARRAY) {
            // Whole-array argument to a slice parameter (§3.10), any loc form
            // (fixed value or bytes/resizable pointer).
            auto v = ArrayView(lv, ln);
            auto t = T();
            auto dp = v.typedelems && IsBytesT(et->sub)
                          ? cat("(uint8_t *)(", v.elems, ")") : string(v.elems);
            if (!v.typedelems && !IsBytesT(et->sub))
                dp = cat("(", CT(et->sub), " *)(", v.elems, ")");
            L(CT(et), " ", t, " = { ", dp, ", ", v.len, " };");
            return t;
        }
        if (!lv.val) {
            assert(IsBytesT(lv.t));
            if (lv.t->kind == TY_INT)   // varint field read: decode to i64.
                return cat("gs_zig_read(", lv.s, ")");
            if (et && IsFix(et) && !TEq(lv.t, et)) return AdaptToFixed(lv, et, ln);
            return lv.s;   // Bytes value: the pointer is the currency.
        }
        if (et && IsFix(et) && lv.val && lv.t->kind == TY_SLICE && et->kind == TY_ARRAY)
            return AdaptToFixed(lv, et, ln);
        if (lv.ispref && et && et->kind == TY_REF) {
            // A pool reference read as a plain reference drops the freelist.
            auto t = T();
            L("gs_rref ", t, " = { ", lv.s, ".hdr, ", lv.s, ".stk };");
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
          ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
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
        // A reference to a resizable value points at its frame header (C.2),
        // so it can only be taken of a whole variable.
        if (auto id = Is<Ident>(child); id && id->vdef && IsResz(id->vdef->type)) {
            auto stk = VStkOf(id->vdef);
            assert(!stk.empty());
            auto t = T();
            L("gs_rref ", t, " = { &", HdrLv(id->vdef), ", ", stk, " };");
            return t;
        }
        auto lv = GenLoc(child);
        if (lv.t->kind == TY_REF) {
            if (lv.t->ref->lenstorage >= 0) return LoadLoc(lv, nullptr, ln);
            return lv.s;
        }
        if (IsFatPointee(lv.t))
            Fail(ln, "a reference to a nested resizable value is unsupported; "
                     "reference the owning variable");
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
        if (t->kind == TY_REF && IsFatPointee(t->ref->sub)) return cat("(", x, ".hdr != 0)");
        return x;
    }

    // The three per-node passes dispatch virtually (ast.h); the bodies live
    // together at the end of this file, delegating into the machinery here.
    string GenX(Node *n) { return n->CgX(*this); }
    void GenAny(Node *n, Dst d) { n->CgAny(*this, d); }
    void GenStmt2(Node *n) { n->CgStmt(*this); }

    // A control construct used as a fixed-class value: route it into a temp.
    string CtlValX(Node *n) {
        auto t = T();
        L(CT(n->exprtype), " ", t, ";");
        GenAny(n, Dst { 1, t });
        return t;
    }

    // A non-control node's value routed to a destination.
    void LeafAny(Node *n, const Dst &d) {
        if (d.k == 2) { GenConstruct(n, d.s, d.t, d.lenlv); return; }
        if (d.k == 1) { L(d.s, " = ", GenXD(n, d.t), ";"); return; }
        if (IsVoidT(n->exprtype)) { Fail(n->line, "internal: valueless leaf"); }
        L("(void)(", GenVal(n), ");");
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
    // Resizable values carry a header and go through GenLoc/GenRzTmp instead.
    string GenPtr(Node *n, string *stkout = nullptr) {
        assert(!IsResz(n->exprtype));
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
        L("uint8_t *", base, " = ", Top(stk), ";");
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
    // Binary operators. Operand exprtypes are already decayed and unified.

    // Operand exprtype as an operator sees it: a spliced reference reads as
    // its pointee.
    TypeExpr *OperandT(TypeExpr *t) {
        if (t && t->kind == TY_REF && !t->ref->optional && t->ref->lenstorage < 0)
            return t->ref->sub;
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
        return GenRangeEq(st->sub, cat(l, ".data"), cat(l, ".len"), cat(r, ".data"),
                          cat(r, ".len"));
    }

    // Structural equality of two element ranges (§4.5): length then elements.
    string GenRangeEq(TypeExpr *elem, const string &ae, const string &an, const string &be,
                      const string &bn) {
        auto t = T();
        L("uint8_t ", t, " = ", an, " == ", bn, ";");
        L("if (", t, ") {");
        ind++;
        if (ScalarEq(elem) && GapFree(elem)) {
            L(t, " = memcmp(", ae, ", ", be, ", (size_t)((", an, ") * ", FixedSize(elem),
              ")) == 0;");
        } else if (IsFix(elem)) {
            auto pa = T(), pb = T(), iv = T();
            L("const ", CT(elem), " *", pa, " = (const ", CT(elem), " *)(", ae, ");");
            L("const ", CT(elem), " *", pb, " = (const ", CT(elem), " *)(", be, ");");
            L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < (", an, "); ", iv, "++)");
            L("    ", t, " = ", EqX(elem, cat(pa, "[", iv, "]"), cat(pb, "[", iv, "]")),
              ";");
        } else {
            // Variable elements: parallel cursor walk.
            auto pa = T(), pb = T(), iv = T();
            L("const uint8_t *", pa, " = (const uint8_t *)(", ae, "), *", pb,
              " = (const uint8_t *)(", be, ");");
            L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < (", an, "); ", iv, "++) {");
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

    // Elementwise arithmetic (§6.1), expanded member by member with direct
    // loads and stores — no whole-struct temporaries, which backends fail to
    // scalarize. Member i of the result depends only on member i of each
    // operand, so writing members straight into `dst` is exact even when it
    // aliases an operand (the p.vel = p.vel + g shape).
    void GenElemwiseInto(Binary *b, const string &l, const string &r, const string &dst) {
        function<void(TypeExpr *, const string &)> rec = [&](TypeExpr *tt, const string &path) {
            switch (tt->kind) {
                case TY_INT: case TY_FLT: {
                    auto a = cat("(", l, ")", path), c = cat("(", r, ")", path);
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
                    } else {
                        auto sfx = IntSfx(tt->intstorage);
                        switch (b->op) {
                            case T_PLUS:  x = cat("gs_add_", sfx, "(", a, ", ", c, ")"); break;
                            case T_MINUS: x = cat("gs_sub_", sfx, "(", a, ", ", c, ")"); break;
                            case T_MUL:   x = cat("gs_mul_", sfx, "(", a, ", ", c, ")"); break;
                            case T_DIV:   x = cat("gs_div_", sfx, "(", a, ", ", c, ", ",
                                                  LocArgs(b->line), ")"); break;
                            default:      x = cat("gs_mod_", sfx, "(", a, ", ", c, ", ",
                                                  LocArgs(b->line), ")"); break;
                        }
                    }
                    L(dst, path, " = ", x, ";");
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
        rec(b->exprtype, "");
    }

    // Left-to-right evaluation (§2): when the right operand needs statements,
    // the left's value must be snapshotted before they run.
    void ElemwiseOperands(Binary *b, string &l, string &r) {
        if (HasStmts(b->right)) {
            l = GenVal(b->left);
            auto lt = T();
            L(CT(b->exprtype), " ", lt, " = ", l, ";");
            l = lt;
            r = GenVal(b->right);
        } else {
            l = GenVal(b->left);
            r = GenVal(b->right);
        }
    }

    string GenElemwise(Binary *b, const string &l, const string &r) {
        auto tv = T();
        L(CT(b->exprtype), " ", tv, ";");
        GenElemwiseInto(b, l, r, tv);
        return tv;
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
        if (!se->nobc)
            L("if (", lov, " < 0 || ", hiv, " < ", lov, " || ", hiv, " > ", lenv,
              ") gs_abort(GS_E_SLICE, ", LocArgs(se->line), ");");
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

    void Bump(const string &stk, const string &n) { L(Top(stk), " += ", n, ";"); }

    void EmitValStore(const string &stk, TypeExpr *t, const string &x) {
        auto sz = FixedSize(t);
        if (sz == 0) { L("(void)(", x, ");"); return; }
        L("*(", CT(t), " *)", Top(stk), " = ", x, ";");
        Bump(stk, cat(sz));
    }

    void EmitLenStore(const string &stk, IntStorage ls, const string &n) {
        if (ls == IS_VARINT) {
            Bump(stk, cat("gs_uleb_write(", Top(stk), ", (uint64_t)(", n, "))"));
        } else {
            L("*(", IntCT(ls), " *)", Top(stk), " = (", IntCT(ls), ")(", n, ");");
            Bump(stk, cat(IntSize(ls)));
        }
    }

    // Writes a plain-reference value into the relative-reference slot at
    // `fa` (§3.9): self-relative signed offset, range-checked. Fixed widths
    // only; the varint form exists only in the stack-top variant below.
    void EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln) {
        auto w = (IntStorage)rt->ref->lenstorage;
        assert(w != IS_VARINT);
        assert(!IsFatPointee(rt->ref->sub));
        auto addr = cat("(uint8_t *)(", rv, ")");
        auto off = T();
        if (rt->ref->optional)
            L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - (", fa, ")) : 0;");
        else
            L("int64_t ", off, " = (int64_t)(", addr, " - (", fa, "));");
        auto bits = IntSize(w) * 8;
        if (bits < 64)
            L("if (", off, " < -(1LL << ", bits - 1, ") || ", off, " >= (1LL << ",
              bits - 1, ")) gs_abort(GS_E_RELOFF, ",
              LocArgs(ln), ");");
        L("*(", RelCT(w), " *)(", fa, ") = (", RelCT(w), ")", off, ";");
    }

    void EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln) {
        auto w = (IntStorage)rt->ref->lenstorage;
        auto fa = T();
        L("uint8_t *", fa, " = ", Top(stk), ";");
        if (w == IS_VARINT) {
            assert(!IsFatPointee(rt->ref->sub));
            auto addr = cat("(uint8_t *)(", rv, ")");
            auto off = T();
            if (rt->ref->optional)
                L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - ", fa, ") : 0;");
            else
                L("int64_t ", off, " = (int64_t)(", addr, " - ", fa, ");");
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
            L("memcpy(", Top(stk), ", ", src, ", (size_t)((", n, ") * ", esz, "));");
            Bump(stk, cat("(", n, ") * ", esz));
        } else {
            auto p = T(), iv = T();
            L("const uint8_t *", p, " = (const uint8_t *)(", src, ");");
            L("for (int64_t ", iv, " = 0; ", iv, " < (", n, "); ", iv, "++) {");
            ind++;
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(elem, p), ";");
            L("memcpy(", Top(stk), ", ", p, ", (size_t)", sz, ");");
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

    // Constructs n's value at stk's top. For resizable-class values, lenlv
    // names the receiving header's length lvalue: elements are written and
    // the count assigned there (§7.3's metadata-outside-the-data form).
    void GenConstruct(Node *n, const string &stk, TypeExpr *want = nullptr,
                      const string &lenlv = "") {
        auto et = n->exprtype;
        if (want && NeedsDeref(n->exprtype, want)) {
            // A spliced reference in a decayed slot: copy the pointee.
            auto sub = n->exprtype->ref->sub;
            if (IsFix(sub)) {
                EmitValStore(stk, want, GenXD(n, want));
            } else if (IsFatPointee(sub)) {
                auto x = GenX(n);
                Loc lv;
                lv.t = sub;
                lv.s = cat(x, ".hdr->base");
                lv.lenlv = cat(x, ".hdr->len");
                EmitRzCopy(lv, sub, stk, lenlv, n->line);
            } else {
                auto x = GenX(n);
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(sub, x), ";");
                L("memcpy(", Top(stk), ", ", x, ", (size_t)", sz, ");");
                Bump(stk, sz);
            }
            return;
        }
        if (IsCtl(n)) { GenAny(n, Dst { 2, stk, want, lenlv }); return; }
        if (auto c = Is<Call>(n)) {
            auto rets = EmitCall(c, Dst { 2, stk, want, lenlv });
            // A reference-returning call decayed to a value here: the callee
            // did not construct at the destination; copy the pointee.
            auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
            if (rt && rt->kind == TY_REF && !rets.empty() && et->kind != TY_REF) {
                if (IsFatPointee(rt->ref->sub)) {
                    Loc lv;
                    lv.t = rt->ref->sub;
                    lv.s = cat(rets[0], ".hdr->base");
                    lv.lenlv = cat(rets[0], ".hdr->len");
                    EmitRzCopy(lv, et, stk, lenlv, n->line);
                } else if (IsBytesT(rt->ref->sub)) {
                    auto sz = T();
                    L("int64_t ", sz, " = ", SizeX(et, rets[0]), ";");
                    L("memcpy(", Top(stk), ", ", rets[0], ", (size_t)", sz, ");");
                    Bump(stk, sz);
                }
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
                L("*", Top(stk), " = 0;");
                Bump(stk, "1");
            } else {
                L("memset(", Top(stk), ", 0, ", FixedSize(et), ");");
                Bump(stk, cat(FixedSize(et)));
            }
            return;
        }
        if (auto s2 = Is<StrLit>(n); s2 && (IsResz(et) || !lenlv.empty())) {
            // A string literal building a resizable string: raw elements,
            // count into the receiving header.
            assert(!lenlv.empty());
            L(lenlv, " = ", s2->val.size(), ";");
            if (!s2->val.empty()) {
                L("memcpy(", Top(stk), ", ", StrRaw(s2->val), ", ", s2->val.size(), ");");
                Bump(stk, cat(s2->val.size()));
            }
            return;
        }
        if (auto s = Is<StrLit>(n)) {
            assert(et->kind == TY_ARRAY && et->arr->akind == A_VAR && lenlv.empty());
            EmitLenStore(stk, LenStore(et->arr), cat(s->val.size()));
            if (!s->val.empty()) {
                L("memcpy(", Top(stk), ", ", StrRaw(s->val), ", ", s->val.size(), ");");
                Bump(stk, cat(s->val.size()));
            }
            return;
        }
        if (auto al = Is<ArrayLit>(n)) { GenArrayLit(al, stk, lenlv); return; }
        if (auto sl = Is<StructLit>(n)) { GenStructLit(sl, stk, lenlv); return; }
        if (auto d = Is<Dot>(n); d && d->variantconst) {
            // A payload-less variant constant in variable mode: just the tag.
            assert(et->kind == TY_ENUM && et->enu->varmode);
            auto ei = EIOf(et);
            EmitValStoreTag(stk, TagStore(ei->en),
                            TagConst(ei, VarIdx(ei->en, d->variantconst)));
            if (!lenlv.empty()) L(lenlv, " = 0;");
            return;
        }
        if (auto se = Is<SliceExpr>(n); se && et->kind == TY_ARRAY) {
            // A slice expression constructing an array copies the range.
            Loc slv;
            slv.val = true;
            slv.s = GenSlice(se);
            slv.t = MakeSliceT(et->arr->sub, n->line);
            GenArrayFromLoc(slv, et, stk, n->line, lenlv);
            return;
        }
        // Remaining nodes denote existing values: resolve the location, then
        // either copy identical layouts wholesale or adapt (slice/array kind
        // changes, ADT mode changes) element/field-wise.
        auto lv = GenLoc(n);
        if (lv.t->kind == TY_REF && et->kind != TY_REF) DerefLoc(lv, n->line);
        if (IsResz(et)) {
            if (lv.t->kind == TY_ARRAY || TEq(lv.t, et)) {
                if (TEq(lv.t, et) && et->kind != TY_ARRAY) {
                    EmitRzCopy(lv, et, stk, lenlv, n->line);
                } else {
                    GenArrayFromLoc(lv, et, stk, n->line, lenlv);
                }
                return;
            }
            Fail(n->line, cat("unsupported resizable construction from ", Mangle(lv.t)));
        }
        if (!lenlv.empty() && et->kind == TY_ARRAY) {
            // Element-run destination: elements only, count into lenlv.
            GenArrayFromLoc(lv, et, stk, n->line, lenlv);
            return;
        }
        if (TEq(lv.t, et)) {
            assert(!lv.val);
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(et, lv.s), ";");
            L("memcpy(", Top(stk), ", ", lv.s, ", (size_t)", sz, ");");
            Bump(stk, sz);
            return;
        }
        if (et->kind == TY_ARRAY) { GenArrayFromLoc(lv, et, stk, n->line, lenlv); return; }
        if (et->kind == TY_ENUM && et->enu->varmode) {
            GenVarEnumFromLoc(lv, et, stk, n->line);
            return;
        }
        Fail(n->line, cat("unsupported construction adaptation to ", Mangle(et)));
    }

    void GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln,
                         const string &lenlv = "") {
        auto v = ArrayView(lv, ln);
        auto nn = T();
        L("int64_t ", nn, " = ", v.len, ";");
        switch (et->arr->akind) {
            case A_VAR:
                if (!lenlv.empty()) L(lenlv, " = ", nn, ";");
                else EmitLenStore(stk, LenStore(et->arr), nn);
                break;
            case A_LIMITED:   // Runtime capacity: chosen as the initial length (v1).
                L("*(uint32_t *)", Top(stk), " = (uint32_t)", nn, ";");
                L("*(uint32_t *)(", Top(stk), " + 4) = (uint32_t)", nn, ";");
                Bump(stk, "8");
                break;
            default:
                assert(!lenlv.empty());
                L(lenlv, " = ", nn, ";");
                break;
        }
        EmitCopyElems(stk, et->arr->sub, v.elems, nn);
    }

    // Copies an existing resizable value (source location) to the stack top:
    // the static prefix of any resizable-tailed structs, then the tail's
    // elements; the count goes to the receiving header.
    void EmitRzCopy(Loc lv, TypeExpr *et, const string &stk, const string &lenlv, Line ln) {
        assert(!lenlv.empty());
        int64_t prefix = 0;
        TypeExpr *elem = nullptr;
        if (!RzShape(et, prefix, elem))
            Fail(ln, "copying this resizable value is unsupported (variable-size prefix)");
        auto len = T();
        L("int64_t ", len, " = ", lv.lenlv, ";");
        if (prefix) {
            L("memcpy(", Top(stk), ", ", lv.s, ", ", prefix, ");");
            Bump(stk, cat(prefix));
        }
        EmitCopyElems(stk, elem, cat("(", lv.s, ") + ", prefix), len);
        L(lenlv, " = ", len, ";");
    }

    // The static byte size before a resizable value's tail elements, plus the
    // tail's element type. False when the prefix is not statically sized.
    bool RzShape(TypeExpr *t, int64_t &prefix, TypeExpr *&elem) {
        if (t->kind == TY_ARRAY) {
            elem = t->arr->sub;
            return true;
        }
        if (t->kind == TY_STRUCT) {
            auto si = SI(t);
            auto last = -1;
            for (auto i = 0; i < (int)si->st->fields.size(); i++)
                if (!si->st->fields[i].ispad) last = i;
            assert(last >= 0);
            vector<Field> pre(si->st->fields.begin(), si->st->fields.begin() + last);
            vector<TypeExpr *> pret(si->ftypes.begin(), si->ftypes.begin() + last);
            for (auto ft : pret) if (ft && !IsFix(ft)) return false;
            auto lo = LayoutFields(pre, pret);
            int64_t sub = 0;
            if (!RzShape(si->ftypes[last], sub, elem)) return false;
            prefix += lo.size + sub;
            return true;
        }
        return false;   // Resizable variable-mode ADTs: unsupported copies.
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
                L("memcpy(", Top(stk), ", ", lv.s, ", (size_t)", sz, ");");
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
                L("memcpy(", Top(stk), ", &", sv, ".u.v_",
                  Sanitize(ei->en->variants[vi].name), ", ", psz, ");");
                Bump(stk, cat(psz));
            }
            L("break;");
            ind--;
        }
        L("}");
        (void)ln;
    }

    // A fixed struct/array literal built directly at the stack top, so its
    // relative references measure offsets from their real addresses. Layout
    // gaps (pads, ADT payload padding) are zero-filled to keep sizes exact.
    void FixedLitAtStk(Node *n, const string &stk) {
        auto et = n->exprtype;
        auto Gap = [&](int64_t bytes) {
            if (bytes <= 0) return;
            L("memset(", Top(stk), ", 0, ", bytes, ");");
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
        L("*(", IntCT(ts), " *)", Top(stk), " = (", IntCT(ts), ")(", x, ");");
        Bump(stk, cat(IntSize(ts)));
    }

    void GenArrayLit(ArrayLit *al, const string &stk, const string &lenlv = "") {
        auto et = al->exprtype;
        assert(et->kind == TY_ARRAY);
        auto elem = et->arr->sub;
        if (al->capexpr) {
            // [..cap]: capacity + zero length; the slots stay uninitialized
            // and the runtime commits the skipped range on first touch (C.4).
            assert(et->arr->akind == A_LIMITED);
            auto cv = GenX(al->capexpr);
            auto t = T();
            L("int64_t ", t, " = ", cv, ";");
            L("if (", t, " < 0 || ", t, " > (int64_t)UINT32_MAX) "
              "gs_abort(GS_E_CAPRANGE, ", LocArgs(al->line), ");");
            L("*(uint32_t *)", Top(stk), " = (uint32_t)", t, ";");
            L("*(uint32_t *)(", Top(stk), " + 4) = 0;");
            Bump(stk, cat("8 + ", t, " * ", FixedSize(elem)));
            return;
        }
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
            case A_VAR:
                if (!lenlv.empty()) L(lenlv, " = ", cn, ";");
                else EmitLenStore(stk, LenStore(et->arr), cn);
                break;
            case A_LIMITED:
                L("*(uint32_t *)", Top(stk), " = ", count, ";");
                L("*(uint32_t *)(", Top(stk), " + 4) = ", count, ";");
                Bump(stk, "8");
                break;
            default:
                assert(!lenlv.empty());
                L(lenlv, " = ", cn, ";");
                break;
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
    void GenStructLit(StructLit *sl, const string &stk, const string &lenlv = "") {
        auto et = sl->exprtype;
        if (et->kind == TY_ENUM) {
            assert(et->enu->varmode && sl->variant);
            auto ei = EIOf(et);
            auto vi = VarIdx(ei->en, sl->variant);
            EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
            // A resizable-class ADT: variants without a resizable tail leave
            // the receiving header length zero.
            if (!lenlv.empty() && IsFix(VariantType(et, vi))) L(lenlv, " = 0;");
            GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi],
                          ei->vdefaults[vi], stk, lenlv);
            return;
        }
        if (et->kind == TY_VARIANT) {
            auto ei = EIVar(et);
            auto vi = VarIdx(ei->en, et->var->variant);
            GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi],
                          ei->vdefaults[vi], stk, lenlv);
            return;
        }
        assert(et->kind == TY_STRUCT);
        auto si = SI(et);
        GenFieldInits(sl, si->st->fields, si->ftypes, si->defaults, stk, lenlv);
    }

    void GenFieldInits(StructLit *sl, const vector<Field> &fields,
                       const vector<TypeExpr *> &ftypes, const vector<Node *> &defaults,
                       const string &stk, const string &lenlv = "") {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) {
                auto n = fields[i].padsize > 0 ? fields[i].padsize : 0;
                if (n) { L("memset(", Top(stk), ", 0, ", n, ");"); Bump(stk, cat(n)); }
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
                    if (ft->ref->lenstorage == IS_VARINT) { L("*", Top(stk), " = 0;"); Bump(stk, "1"); }
                    else { L("memset(", Top(stk), ", 0, ", IntSize((IntStorage)ft->ref->lenstorage), ");");
                           Bump(stk, cat(IntSize((IntStorage)ft->ref->lenstorage))); }
                    continue;
                }
                auto rv = GenX(init);
                EmitRelStore(stk, ft, rv, sl->line);
                continue;
            }
            if (!init) {
                // Omitted optional: null (checked by TC that a default exists otherwise).
                assert(ft->kind == TY_REF && ft->ref->optional);
                L("memset(", Top(stk), ", 0, ", FixedSize(ft), ");");
                Bump(stk, cat(FixedSize(ft)));
                continue;
            }
            if (ft->kind == TY_INT && ft->intstorage == IS_VARINT) {
                auto x = GenXD(init, ast.inttypes[IS_I64]);
                Bump(stk, cat("gs_zig_write(", Top(stk), ", ", x, ")"));
                continue;
            }
            // The resizable tail (if any) receives the enclosing header's
            // length; every other field constructs plainly.
            GenConstruct(init, stk, ft, IsResz(ft) ? lenlv : "");
        }
    }

    // ------------------------------------------------------------------
    // Statements and control flow. GenAny routes a node's value to a Dst;
    // control constructs recurse so every branch reaches the same
    // destination (§4.3). Scopes mirror C braces, so watermark base
    // variables are always in C scope exactly where exits may restore them.

    bool termjump = false;   // The last emitted statement left via goto/return.

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
        cscopes[si].dst = d;
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

    void GenBreakPath(Node *val, Line ln) {
        auto si = -1;
        for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
            if (cscopes[i].kind == SC_LOOP || cscopes[i].kind == SC_BLOCK) { si = i; break; }
            if (cscopes[i].kind == SC_FN) break;
        }
        assert(si >= 0);
        if (val) GenAny(val, cscopes[si].dst);
        EmitExitRestores(si);
        cscopes[si].usedbrk = true;
        L("goto ", cscopes[si].brklbl, ";");
        termjump = true;
        (void)ln;
    }

    // ------------------------------------------------------------------
    // Declarations and assignment.

    void BindLocal(VarDef *d, Node *init) {
        auto name = LocalName(d);
        auto t = d->type;
        if (IsResz(t)) {
            assert(init);
            EmitCoreTypes();
            string stk;
            if (nrvovars.count(d)) {
                stk = cat("gs_dst", nrvodst[d]);
            } else {
                stk = AllocStk(true);
            }
            L("gs_rhdr ", name, " = { ", Top(stk), ", 0 };");
            if (!nrvovars.count(d)) SaveBase(true, stk, cat(name, ".base"));
            vstk[d] = stk;
            if (d->reusable) {
                // Companion freelist: free slot indices on their own stack.
                auto flstk = AllocStk(true);
                auto fln = Unique2(cat(name, "_fl"));
                L("gs_rhdr ", fln, " = { ", Top(flstk), ", 0 };");
                SaveBase(true, flstk, cat(fln, ".base"));
                vpool[d] = { fln, flstk };
            }
            GenConstruct(init, stk, t, cat(name, ".len"));
            return;
        }
        if (IsBytesT(t)) {
            assert(init);
            auto stk = AllocStk(true);
            L("uint8_t *", name, " = ", Top(stk), ";");
            SaveBase(true, stk, name);
            vstk[d] = stk;
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
            if (auto id = Is<Ident>(u->child); id && id->vdef && id->vdef->reusable) {
                auto vd = id->vdef;
                auto pit = vpool.find(vd);
                auto git = gpools.find(vd);
                assert(pit != vpool.end() || git != gpools.end());
                auto &pl = pit != vpool.end() ? pit->second : git->second;
                auto t = T();
                L("gs_pref ", t, " = { &", HdrLv(vd), ", ", VStkOf(vd), ", &", pl.first,
                  ", ", pl.second, " };");
                return t;
            }
            auto lv = GenLoc(u->child);
            if (lv.t->kind != TY_REF || !lv.ispref)
                Fail(n->line, "cannot form a reusable pool reference here");
            return lv.s;
        }
        if (auto id = Is<Ident>(n); id && id->vdef && PrefVar(id->vdef)) {
            return fvptr.count(id->vdef) ? cat("(*", VName(id->vdef), ")")
                                         : VName(id->vdef);
        }
        Fail(n->line, "cannot form a reusable pool reference from this expression");
    }

    string Unique2(const string &base) {
        auto name = base;
        for (auto n = 2; fnused.count(name) || used.count(name); n++) name = cat(base, "_", n);
        fnused.insert(name);
        return name;
    }

    void GenRelAssign(Loc lv, Node *rhs, Line ln) {
        auto rv = GenX(rhs);
        // Varint-width relative references are construction-only (typechecked).
        assert(lv.t->ref->lenstorage != IS_VARINT);
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
                    if (IsResz(sp->rets[i])) {
                        dsts.push_back(Dst { 2, cat("gs_dst", i), sp->rets[i],
                                             cat("(*gs_rl", i, ")") });
                    } else if (IsBytesT(sp->rets[i])) {
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
            if (IsResz(rt) || (emiter && i == 0)) {
                auto id = Is<Ident>(vals[i]);
                if (id && id->vdef && nrvovars.count(id->vdef)) {
                    // Built at the destination; only the count travels.
                    L("*gs_rl", i, " = ", HdrLv(id->vdef), ".len;");
                    continue;
                }
                GenConstruct(vals[i], cat("gs_dst", i), rt, cat("(*gs_rl", i, ")"));
            } else if (IsBytesT(rt)) {
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
        MarkFlush();   // The caller and every later callee read stack tops from memory.
        for (auto &s : fdstsaves) L(s);
        if (curinfo && curinfo->hasrf) L("*gs_rf = 0;");
        L(retv.empty() ? "return;" : cat("return ", retv, ";"));
        termjump = true;
    }

    // The dummy C return value used on propagate paths.
    void PropagateReturn(const string &rfval) {
        EmitExitRestores(0);
        MarkFlush();
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
                if (IsResz(rets[i]))
                    GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i],
                                 cat("gs_lret_", tid, "_", i));
                else if (IsBytesT(rets[i]))
                    GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i]);
                else GenAny(r->vals[i], Dst { 1, cat("gs_lret_", tid, "_", i), rets[i] });
            }
        } else {
            // Forward one call's values into the channels.
            auto c = Is<Call>(r->vals[0]);
            vector<Dst> dsts;
            for (size_t i = 0; i < rets.size(); i++) {
                if (IsResz(rets[i]))
                    dsts.push_back(Dst { 2, cat("gs_fdst_", tid, "_", i), rets[i],
                                         cat("gs_lret_", tid, "_", i) });
                else if (IsBytesT(rets[i]))
                    dsts.push_back(Dst { 2, cat("gs_fdst_", tid, "_", i) });
                else
                    dsts.push_back(Dst { 1, cat("gs_lret_", tid, "_", i) });
            }
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
            if (IsResz(rets[i])) {
                Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
                Append(data, "static GS_TLS int64_t gs_lret_", tid, "_", i, ";\n");
            } else if (IsBytesT(rets[i])) {
                Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
            } else {
                Append(data, "static GS_TLS ", CT(rets[i]), " gs_lret_", tid, "_", i, ";\n");
            }
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
        // An element-run destination for a variable-array result: builtins
        // and tag dispatch have no element-run form, so take the value form
        // and slide its length prefix out. Specialization calls route to an
        // element-run twin (or the same fallback) inside EmitSpecCall, and a
        // function value's tail construction honors the destination as-is.
        if (d0.k == 2 && !d0.lenlv.empty() && d0.t && d0.t->kind == TY_ARRAY &&
            d0.t->arr->akind == A_VAR && (c->builtin >= 0 || !c->dispatch.empty())) {
            auto base = T();
            L("uint8_t *", base, " = ", Top(d0.s), ";");
            auto rets = c->builtin >= 0 ? EmitBuiltin(c, Dst { 2, d0.s })
                                        : EmitDispatch(c, Dst { 2, d0.s }, alldst);
            auto ls = LenStore(d0.t->arr);
            string metasz, count;
            if (ls == IS_VARINT) {
                metasz = cat("gs_uleb_size(", base, ")");
                count = cat("gs_uleb_read(", base, ")");
            } else {
                metasz = cat(IntSize(ls));
                count = cat("(int64_t)*(", IntCT(ls), " *)", base);
            }
            auto ms = T();
            L("int64_t ", ms, " = ", metasz, ";");
            L(d0.lenlv, " = ", count, ";");
            L("memmove(", base, ", ", base, " + ", ms, ", (size_t)(", Top(d0.s), " - ",
              base, " - ", ms, "));");
            L(Top(d0.s), " -= ", ms, ";");
            return rets;
        }
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
        if (IsResz(pt)) {
            // By-value resizable argument: construct into a fresh slot and
            // pass the header by value with its stack (§7.2, C.3).
            EmitCoreTypes();
            auto stk = AllocStk(false);
            auto h = T();
            L("gs_rhdr ", h, " = { ", Top(stk), ", 0 };");
            SaveBase(false, stk, cat(h, ".base"));
            GenConstruct(node, stk, pt, cat(h, ".len"));
            args.push_back(h);
            args.push_back(stk);
            return;
        }
        if (IsBytesT(pt)) {
            // By-value nonfixed argument: construct into a fresh slot (§7.2).
            auto stk = AllocStk(false);
            auto base = T();
            L("uint8_t *", base, " = ", Top(stk), ";");
            SaveBase(false, stk, base);
            GenConstruct(node, stk, pt);
            args.push_back(base);
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
                L("gs_pref ", t, " = { &", name, ", ", vstk[fv], ", &", p.first, ", ",
                  p.second, " };");
                args.push_back(t);
            } else {
                args.push_back(name);
            }
            return;
        }
        if (IsResz(fv->type)) {
            args.push_back(fvptr.count(fv) ? name : cat("&", name));
            args.push_back(ours ? vstk[fv] : cat(name, "_stk"));
            return;
        }
        if (IsBytesT(fv->type)) {
            args.push_back(name);
            return;
        }
        args.push_back(fvptr.count(fv) ? name : cat("&", name));
    }

    vector<string> EmitSpecCall(Call *c, FnSpec *sp, Dst d0, vector<Dst> *alldst) {
        assert(sinfo.count(sp));
        auto &ki = sinfo[sp];
        auto an = CallArgNodes(c, sp->params.size());
        string ername, slidebase;
        Dst slide;
        vector<string> args;
        for (size_t i = 0; i < an.size(); i++) EmitArg(sp, i, an[i], args);
        for (auto fv : ki.freevars) EmitFvArg(fv, args);
        vector<string> retex(sp->rets.size());
        for (size_t i = 0; i < sp->rets.size(); i++) {
            auto rt = sp->rets[i];
            Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                                  : (i == 0 ? d0 : Dst {});
            if (IsResz(rt)) {
                // Elements land at the destination's top; the count returns
                // through the length channel (into the receiver's header, or
                // a temporary one).
                EmitCoreTypes();
                string stk, lenlv;
                if (dd.k == 2 && !dd.lenlv.empty()) {
                    stk = dd.s;
                    lenlv = dd.lenlv;
                    retex[i] = "";
                } else {
                    stk = AllocStk(false);
                    auto h = T();
                    L("gs_rhdr ", h, " = { ", Top(stk), ", 0 };");
                    SaveBase(false, stk, cat(h, ".base"));
                    lenlv = cat(h, ".len");
                    retex[i] = h;
                }
                args.push_back(stk);
                args.push_back(cat("&", lenlv));
            } else if (i == 0 && dd.k == 2 && !dd.lenlv.empty() && rt->kind == TY_ARRAY &&
                       rt->arr->akind == A_VAR) {
                // An element-run receiver of a variable-array result: use the
                // callee's element-run twin when it can have one; otherwise
                // take the value form and slide its length prefix out.
                auto er = EnsureEr(sp);
                if (!er.empty()) {
                    ername = er;
                    args.push_back(dd.s);
                    args.push_back(cat("&", dd.lenlv));
                    retex[i] = "";
                } else {
                    slide = dd;
                    slidebase = T();
                    L("uint8_t *", slidebase, " = ", Top(dd.s), ";");
                    args.push_back(dd.s);
                    retex[i] = "";
                }
            } else if (IsBytesT(rt)) {
                string stk;
                if (dd.k == 2) {
                    stk = dd.s;
                } else {
                    stk = AllocStk(false);
                    auto sb = T();
                    L("uint8_t *", sb, " = ", Top(stk), ";");
                    SaveBase(false, stk, sb);
                }
                auto base = T();
                L("uint8_t *", base, " = ", Top(stk), ";");
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
        auto callee = ername.empty() ? ki.cname : ername;
        // The callee reads and bumps whatever stacks it was handed.
        MarkFlush();
        if (ki.cret >= 0) {
            auto r0 = T();
            L(CT(sp->rets[ki.cret]), " ", r0, " = ", callee, "(", argstr, ");");
            retex[ki.cret] = r0;
        } else {
            L(callee, "(", argstr, ");");
        }
        MarkReload();
        if (!slidebase.empty()) {
            // Value form arrived as [len][elems]; slide the prefix out and
            // report the count (the one caller-side copy of this fallback).
            auto srct = sp->rets[0];
            auto ls = LenStore(srct->arr);
            string metasz, count;
            if (ls == IS_VARINT) {
                metasz = cat("gs_uleb_size(", slidebase, ")");
                count = cat("gs_uleb_read(", slidebase, ")");
            } else {
                metasz = cat(IntSize(ls));
                count = cat("(int64_t)*(", IntCT(ls), " *)", slidebase);
            }
            auto ms = T();
            L("int64_t ", ms, " = ", metasz, ";");
            L(slide.lenlv, " = ", count, ";");
            L("memmove(", slidebase, ", ", slidebase, " + ", ms, ", (size_t)(",
              Top(slide.s), " - ", slidebase, " - ", ms, "));");
            L(Top(slide.s), " -= ", ms, ";");
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
                if (IsResz(curspec->rets[i])) {
                    // Elements are at our destination; forward the count.
                    L("*gs_rl", i, " = gs_lret_", tid, "_", i, ";");
                    continue;
                }
                if (IsBytesT(curspec->rets[i])) continue;   // Already at our destination.
                if ((int)i == curinfo->cret) retv = cat("gs_lret_", tid, "_", i);
                else L("*gs_r", i, " = gs_lret_", tid, "_", i, ";");
            }
            Epilogue(retv);
            ind--;
            L("} else {");
            ind++;
            if (curinfo && curinfo->hasrf) PropagateReturn(rfv);
            else L("gs_panic(\"unexpected long-distance return\");");
            ind--;
            L("}");
        } else if (curinfo && curinfo->hasrf) {
            PropagateReturn(rfv);
        } else {
            L("gs_panic(\"unexpected long-distance return\");");
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
            L("uint8_t *", rv, " = ", Top(stk), ";");
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
                L("uint8_t *", base, " = ", Top(stk), ";");
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
                    L("uint8_t *", sb, " = ", Top(stk), ";");
                    SaveBase(false, stk, sb);
                }
                auto base = T();
                L("uint8_t *", base, " = ", Top(stk), ";");
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
                L("uint8_t *", base, " = ", Top(stk), ";");
                SaveBase(false, stk, base);
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(vt, payload), ";");
                L("memcpy(", Top(stk), ", ", payload, ", (size_t)", sz, ");");
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
            MarkFlush();
            if (ki.cret >= 0) L(retex[ki.cret], " = ", ki.cname, "(", argstr, ");");
            else L(ki.cname, "(", argstr, ");");
            MarkReload();
            if (ki.hasrf) EmitRfCheck(rfv, sp);
            ind--;
            L("} break;");
        }
        L("default: GS_UNREACHABLE(", LocArgs(c->line), ");");
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
                if (t->kind == TY_INT) {
                    if (t->intstorage == IS_U64)
                        L("gs_print_uint(", GenX(an[0]), ");");
                    else
                        L("gs_print_int((int64_t)(", GenX(an[0]), "));");
                    return {};
                }
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
                L("if (!(", x, ")) gs_abort(GS_E_ASSERT, ", LocArgs(ln), ");");
                return {};
            }
            case B_HARDWARE_THREADS: return { "gs_hardware_threads()" };
            case B_THREAD_WAIT: {
                usesthreads = true;
                L("gs_thread_wait(", GenX(an[0]), ", ", LocArgs(ln), ");");
                return {};
            }
            case B_THREAD_SPAWN: return EmitThreadSpawn(c, an);
            case B_QPUT: {
                auto t = an[0]->exprtype;
                auto q = QueueFor(t);
                if (IsResz(t)) {
                    // Serialize as [int64 len][elements] on a scratch stack.
                    auto src = GenLoc(an[0]);
                    if (src.t->kind == TY_REF) DerefLoc(src, ln);
                    auto stk = AllocStk(false);
                    auto base = T();
                    L("uint8_t *", base, " = ", Top(stk), ";");
                    SaveBase(false, stk, base);
                    auto lenv = T();
                    L("int64_t ", lenv, ";");
                    EmitValStore(stk, ast.inttypes[IS_I64], "0");
                    EmitRzCopy(src, t, stk, lenv, ln);
                    L("*(int64_t *)", base, " = ", lenv, ";");
                    L("gs_qput(&", q, ", ", base, ", ", Top(stk), " - ", base, ");");
                } else if (IsBytesT(t)) {
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
                if (IsResz(t)) {
                    // The queued image is [int64 len][elements]: elements go
                    // to the destination, the count to the receiving header.
                    EmitCoreTypes();
                    string stk = d0.k == 2 && !d0.lenlv.empty() ? d0.s : "";
                    string lenlv = d0.k == 2 ? d0.lenlv : "";
                    string hv;
                    if (stk.empty()) {
                        stk = AllocStk(false);
                        hv = T();
                        L("gs_rhdr ", hv, " = { ", Top(stk), ", 0 };");
                        SaveBase(false, stk, cat(hv, ".base"));
                        lenlv = cat(hv, ".len");
                    }
                    if (poll) {
                        L(lenlv, " = 0;");
                        L("if (", nn, ") {");
                    } else {
                        L("{");
                    }
                    ind++;
                    L(lenlv, " = *(int64_t *)(", nn, " + 1);");
                    L("memcpy(", Top(stk), ", (uint8_t *)(", nn, " + 1) + 8, (size_t)(",
                      nn, "->size - 8));");
                    Bump(stk, cat("(", nn, "->size - 8)"));
                    L("free(", nn, ");");
                    ind--;
                    L("}");
                    return poll ? vector<string> { hv, got } : vector<string> { hv };
                }
                if (IsBytesT(t)) {
                    string stk = d0.k == 2 ? d0.s : "";
                    string base = T();
                    if (stk.empty()) {
                        stk = AllocStk(false);
                        L("uint8_t *", base, " = ", Top(stk), ";");
                        SaveBase(false, stk, base);
                    } else {
                        L("uint8_t *", base, " = ", Top(stk), ";");
                    }
                    if (poll) L("if (", nn, ") {");
                    else L("{");
                    ind++;
                    L("memcpy(", Top(stk), ", ", nn, " + 1, (size_t)", nn, "->size);");
                    Bump(stk, cat(nn, "->size"));
                    L("free(", nn, ");");
                    ind--;
                    if (poll) {
                        // A missed poll still yields a valid (zero) value.
                        L("} else {");
                        ind++;
                        L("memset(", Top(stk), ", 0, ", ZeroSize(t), ");");
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
                // On empty, the stored length would wrap (limited arrays) or
                // the stack top drop below the elements (grow-shrink).
                L("if (", nl, " < 0) gs_abort(GS_E_POP, ", LocArgs(ln), ");");
                L(v.lenlv, " = (", LenCast(lv), ")", nl, ";");
                auto tv = T();
                if (lv.t->arr->akind == A_GROWSHRINK) {
                    // The array tops its stack: the element region ends at top.
                    L(Top(lv.stk), " -= ", esz, ";");
                    L(CT(elem), " ", tv, " = *(", CT(elem), " *)", Top(lv.stk), ";");
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
                // A negative target length shrinks past empty: the same
                // corruption as a pop on an empty array. An unsigned `n` above
                // INT64_MAX casts negative and is rejected here as well.
                L("if ((int64_t)(", nn, ") < 0) gs_abort(GS_E_RESIZENEG, ", LocArgs(ln),
                  ");");
                L("if (", nn, " < ", ol, ") {");
                ind++;
                L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
                if (ak == A_GROWSHRINK)
                    L(Top(lv.stk), " = (uint8_t *)(", v.elems, ") + ", nn, " * ", esz, ";");
                ind--;
                L("} else if (", nn, " > ", ol, ") {");
                ind++;
                if (fv.empty()) {
                    L("gs_abort(GS_E_RESIZEFILL, ", LocArgs(ln), ");");
                } else {
                    if (ak == A_LIMITED) {
                        auto capx = lv.val ? cat(ArrSize(lv.t->arr))
                                           : cat("(int64_t)*(uint32_t *)(", lv.s, ")");
                        L("if (", nn, " > ", capx,
                          ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln),
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
                        L("*(", CT(elem), " *)", Top(lv.stk), " = ", fv, ";");
                        L(Top(lv.stk), " += ", esz, ";");
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
                    L(Top(lv.stk), " = (uint8_t *)(", v.elems, ");");
                L(v.lenlv, " = 0;");
                return {};
            }
            case B_ALLOC_INDEX: case B_ALLOC_REF: return EmitAlloc(c, an, ln, d0);
            case B_FREE: {
                auto lv = RecvLoc(an[0]);
                assert(!lv.fl.empty());
                auto x = GenX(an[1]);
                L("*(int64_t *)", Top(lv.flstk), " = ", x, ";");
                L(Top(lv.flstk), " += 8;");
                L(lv.fl, ".len++;");
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
            L("if (", nl, " >= ", capx, ") gs_abort(GS_E_CAPACITY, ",
              LocArgs(ln), ");");
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
                L("uint8_t *", e, " = ", Top(lv.stk), ";");
            } else {
                L(CT(elem), " *", e, " = (", CT(elem), " *)", Top(lv.stk), ";");
            }
            GenConstruct(an[1], lv.stk, elem);
            L(v.lenlv, "++;");
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
        // append(f()) where f returns a resizable: the callee emits raw
        // elements at our top and hands back the count -- contiguous by
        // construction (§7.3).
        if (auto call = Is<Call>(src); call && IsResz(src->exprtype) && ak != A_LIMITED) {
            auto nn = T();
            L("int64_t ", nn, " = 0;");
            EmitCall(call, Dst { 2, lv.stk, src->exprtype, nn });
            L(v.lenlv, " += ", nn, ";");
            return;
        }
        // append(f()) where f returns a variable array: request the
        // element-run form (C.3) -- raw elements at our top plus a count.
        // Callees that cannot supply it fall back to a value-form call with
        // its length prefix slid out (inside EmitSpecCall).
        if (auto call = Is<Call>(src); call && IsBytesT(src->exprtype) && ak != A_LIMITED) {
            assert(src->exprtype->kind == TY_ARRAY);
            auto nn = T();
            L("int64_t ", nn, " = 0;");
            EmitCall(call, Dst { 2, lv.stk, src->exprtype, nn });
            L(v.lenlv, " += ", nn, ";");
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
              ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
            L("memcpy(", v.typedelems ? cat(v.elems, " + ", ol)
                                      : cat("(", v.elems, ") + ", ol, " * ", esz),
              ", ", se.elems, ", (size_t)(", nn, " * ", esz, "));");
            L(v.lenlv, " = (", lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t", ")(", ol,
              " + ", nn, ");");
            return;
        }
        assert(!lv.stk.empty());
        EmitCopyElems(lv.stk, elem, se.elems, nn);
        L(v.lenlv, " += ", nn, ";");
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
        L("if (", lv.fl, ".len > 0) {");
        ind++;
        L(lv.fl, ".len--;");
        L(Top(lv.flstk), " -= 8;");
        L(iv, " = *(int64_t *)", Top(lv.flstk), ";");
        ind--;
        L("} else {");
        ind++;
        L(iv, " = ", lv.lenlv, ";");
        L(lv.lenlv, "++;");
        L(Top(lv.stk), " += ", esz, ";");
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
        L("uint8_t *", base, " = ", Top(stk), ";");
        SaveBase(false, stk, base);
        for (size_t i = 0; i < sp->argtypes.size(); i++) GenConstruct(an[1 + i], stk);
        return { cat("gs_thread_spawn(", thunk, ", ", base, ", ", Top(stk), " - ", base,
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

    // A reference to a resizable carries its stack inside the reference value,
    // so a function holding one has a second spelling for a stack it may also
    // name directly; that function keeps the plain memory form throughout.
    bool CanCacheTops(FnSpec *sp) {
        auto ok = true;
        auto check = [&](const VarDef *v) {
            // A pool reference likewise carries its stack (and its freelist's).
            if (v && (PrefVar(v) || (v->type && v->type->kind == TY_REF &&
                                     IsResz(v->type->ref->sub))))
                ok = false;
        };
        for (auto p : sp->params) check(p);
        for (auto fv : sinfo[sp].freevars) check(fv);
        function<void(Node *)> walk = [&](Node *n) {
            if (!n || !ok) return;
            if (auto id = Is<Ident>(n)) check(id->vdef);
            if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) check(d);
            if (auto fl = Is<ForLoop>(n)) check(fl->vdef);
            if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) check(arm.binder);
            if (auto c = Is<Call>(n)) { walk(c->fvbody); for (auto p : c->fvparams) check(p); }
            n->Children([&](Node *ch) { walk(ch); });
        };
        walk(sp->body);
        return ok;
    }

    void ResetFnState() {
        topcache.clear();
        toporder.clear();
        cachetops = false;
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

    // The element-run twin of a spec (C.3): same body compiled to emit raw
    // elements at the destination plus a count, instead of a [len][elems]
    // value. Returns its C name, or "" when the spec cannot have one.
    string EnsureEr(FnSpec *sp) {
        auto it = ernames.find(sp);
        if (it != ernames.end()) return it->second;
        auto rt = sp->rets.size() == 1 ? sp->rets[0] : nullptr;
        auto ok = rt && rt->kind == TY_ARRAY && rt->arr->akind == A_VAR && sp->body &&
                  !fromids.count(sp->sf);
        if (!ok) return ernames[sp] = "";
        auto name = Unique(cat(sinfo[sp].cname, "_er"));
        ernames[sp] = name;
        Append(protos, "static ", SigRet(sp), " ", name, "(", SigParams(sp, false, true),
               ");\n");
        erqueue.push_back(sp);
        return name;
    }

    void EmitSpec(FnSpec *sp, bool er = false) {
        curspec = sp;
        curinfo = &sinfo[sp];
        emiter = er;
        ResetFnState();
        cursp = curinfo->needssp;
        spexpr = cursp ? "gs_sp" : "0";
        cachetops = CanCacheTops(sp);
        PushSc(SC_FN);
        auto params = SigParams(sp, true, er);
        // In the element-run form named results are not built at the
        // destination: the callee-side copy at return is the specified cost
        // of operating on the whole value first (§7.3).
        if (!er) DetectNrvo(sp);
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
                L("GS_UNREACHABLE(", LocArgs(sp->sf->line), ");");
                auto d = T();
                L(CT(sp->rets[curinfo->cret]), " ", d, " = {0};");
                L("return ", d, ";");
            } else {
                Epilogue("");
            }
        }
        cscopes.clear();
        Append(code, "static ", SigRet(sp), " ", er ? ernames[sp] : curinfo->cname, "(",
               params, ") {\n");
        if (stkmax > 0)
            Append(code, "    GS_ENSURE(", spexpr, " + ", stkmax, ");\n");
        // Cached tops load once the stacks are known to exist, and the sync
        // markers expand now that every stack this body caches is known.
        for (auto &stk : toporder)
            Append(code, "    uint8_t *", topcache[stk], " = ", stk, "->top;\n");
        code += ExpandTopMarkers(body);
        code += "}\n\n";
        curspec = nullptr;
        curinfo = nullptr;
        emiter = false;
    }

    // ------------------------------------------------------------------
    // Globals (§11.1): C globals plus dedicated data stacks for nonfixed
    // ones; initializers run in declaration order before main.

    // Globals whose declaration carries a C initializer, so gs_init_globals
    // has nothing left to do for them (§11.1).
    set<const VarDef *> gstatic;

    // Spelling out more elements than this would trade startup work for source
    // size; such a value keeps its runtime initialization.
    static constexpr int64_t MAXSTATICELEMS = 256;

    // A C initializer for a compile-time constant of fixed, flat type. Fails
    // for everything with a runtime component -- references, slices, ADTs,
    // relative references -- leaving the value to gs_init_globals.
    bool StaticInitX(Node *n, TypeExpr *t, string &out) {
        if (!n || !t || !IsFix(t) || HasRelRef(t)) return false;
        switch (t->kind) {
            case TY_INT: {
                auto i = Is<IntLit>(n);
                if (!i || t->intstorage == IS_VARINT) return false;
                out = i->val < 0 && t->intstorage == IS_U64 ? cat((uint64_t)i->val, "ULL")
                                                            : IntStr(i->val);
                return true;
            }
            case TY_FLT: {
                auto f = Is<FltLit>(n);
                if (!f) return false;
                out = FltStr(f->val, t->fltstorage == FS_F32);
                return true;
            }
            case TY_BOOL: {
                auto b = Is<BoolLit>(n);
                if (!b) return false;
                out = b->val ? "1" : "0";
                return true;
            }
            case TY_STRUCT: {
                auto sl = Is<StructLit>(n);
                auto si = SI(t);
                if (!sl || sl->sinst != si) return false;
                string s = "{ ";
                auto any = false;
                for (size_t fi = 0; fi < si->st->fields.size(); fi++) {
                    if (si->st->fields[fi].ispad) continue;   // Pads zero-fill.
                    Node *v = nullptr;
                    for (size_t k = 0; k < sl->fieldindices.size(); k++)
                        if (sl->fieldindices[k] == (int)fi) { v = sl->inits[k].val; break; }
                    if (!v && fi < si->defaults.size()) v = si->defaults[fi];
                    string fx;
                    if (!StaticInitX(v, si->ftypes[fi], fx)) return false;
                    Append(s, any ? ", " : "", ".", Sanitize(si->st->fields[fi].name),
                           " = ", fx);
                    any = true;
                }
                out = any ? s + " }" : "{ 0 }";
                return true;
            }
            case TY_ARRAY: {
                auto &a = *t->arr;
                if (a.akind != A_FIXED) return false;
                auto k = ArrSize(t->arr);
                if (k < 1 || k > MAXSTATICELEMS) return false;
                string s = "{ { ";
                if (auto str = Is<StrLit>(n)) {
                    if (!IsU8T(a.sub) || (int64_t)str->val.size() != k) return false;
                    for (int64_t e = 0; e < k; e++)
                        Append(s, e ? ", " : "", (int)(uint8_t)str->val[(size_t)e]);
                } else if (auto al = Is<ArrayLit>(n)) {
                    string ex;
                    if (al->fillval) {
                        if (!StaticInitX(al->fillval, a.sub, ex)) return false;
                        for (int64_t e = 0; e < k; e++) Append(s, e ? ", " : "", ex);
                    } else {
                        if ((int64_t)al->elems.size() != k) return false;
                        for (int64_t e = 0; e < k; e++) {
                            if (!StaticInitX(al->elems[(size_t)e], a.sub, ex)) return false;
                            Append(s, e ? ", " : "", ex);
                        }
                    }
                } else {
                    return false;
                }
                out = s + " } }";
                return true;
            }
            default: return false;
        }
    }

    void EmitGlobalDecls() {
        for (auto g : ast.globals) {
            // The multi-value call form has one initializer for several defs;
            // only the one-init-per-def shape can carry a static initializer.
            auto perdef = g->inits.size() == g->defs.size();
            for (size_t di = 0; di < g->defs.size(); di++) {
                auto d = g->defs[di];
                auto name = Unique(Sanitize(d->name));
                gnames[d] = name;
                // Registered as they are created; see CacheableStk.
                auto reg = [&](const string &s) { gstkexprs.insert(s); };
                if (IsResz(d->type)) {
                    EmitCoreTypes();
                    auto stk = Unique(cat("gs_gstk_", name));
                    Append(data, "static gs_rhdr ", name, ";\nstatic gs_stack ", stk,
                           ";\n");
                    gstks[d] = cat("(&", stk, ")");
                    reg(gstks[d]);
                    if (d->reusable) {
                        auto fln = Unique(cat(name, "_fl"));
                        auto flstk = Unique(cat("gs_gstk_", fln));
                        Append(data, "static gs_rhdr ", fln, ";\nstatic gs_stack ", flstk,
                               ";\n");
                        gpools[d] = { fln, cat("(&", flstk, ")") };
                        reg(gpools[d].second);
                    }
                } else if (IsBytesT(d->type)) {
                    auto stk = Unique(cat("gs_gstk_", name));
                    Append(data, "static uint8_t *", name, ";\nstatic gs_stack ", stk,
                           ";\n");
                    gstks[d] = cat("(&", stk, ")");
                    reg(gstks[d]);
                } else {
                    string init;
                    if (perdef && !d->isvar && !PrefVar(d) &&
                        StaticInitX(g->inits[di], d->type, init)) {
                        Append(data, "static ", VarCT(d), " ", name, " = ", init, ";\n");
                        gstatic.insert(d);
                    } else {
                        Append(data, "static ", VarCT(d), " ", name, ";\n");
                    }
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
                    if (IsResz(d->type)) {
                        InitGlobalStack(d);
                        dsts.push_back(Dst { 2, gstks[d], d->type,
                                             cat(gnames[d], ".len") });
                    } else if (IsBytesT(d->type)) {
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
                    if (gstatic.count(d)) continue;   // Initialized at its declaration.
                    if (IsResz(d->type)) {
                        InitGlobalStack(d);
                        GenConstruct(g->inits[i], gstks[d], d->type,
                                     cat(gnames[d], ".len"));
                    } else if (IsBytesT(d->type)) {
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
        if (IsResz(d->type)) {
            L(gnames[d], ".base = ", Top(stk), ";");
            L(gnames[d], ".len = 0;");
        } else {
            L(gnames[d], " = ", Top(stk), ";");
        }
        if (d->reusable) {
            auto &p = gpools[d];
            L("gs_stack_init(", p.second, ");");
            L(p.first, ".base = ", Top(p.second), ";");
            L(p.first, ".len = 0;");
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
        // Element-run twins requested by call sites (may request more).
        for (size_t i = 0; i < erqueue.size(); i++) EmitSpec(erqueue[i], true);
        EmitGlobalInit();
        EmitMain();
        if (usesthreads) predefs = "#define GS_NEED_THREADS 1\n";
        Append(result, "\n/* ---- types ---- */\n#pragma pack(push, 1)\n", tdecls, pdata,
               "#pragma pack(pop)\n\n/* ---- data ---- */\n", data,
               "\n/* ---- prototypes ---- */\n", protos, "\n/* ---- code ---- */\n", code);
    }
};

// ---------------------------------------------------------------------------
// The per-node codegen implementations (the CgX / CgAny / CgStmt virtuals):
// the pass reads top to bottom here; the shared machinery lives in the
// CodeGen methods above. CgX yields a C expression for fixed-class values,
// CgAny routes any value to a destination (all branches of a control
// construct reach the same one, §4.3), CgStmt emits statement position.

// ---- CgX ------------------------------------------------------------------

inline string IntLit::CgX(CodeGen &) {
    // A u64-typed value above i64.max has negative bits; emit it unsigned.
    if (val < 0 && exprtype && exprtype->kind == TY_INT && exprtype->intstorage == IS_U64)
        return cat((uint64_t)val, "ULL");
    return CodeGen::IntStr(val);
}

inline string FltLit::CgX(CodeGen &) {
    return CodeGen::FltStr(val, exprtype && exprtype->kind == TY_FLT &&
                                exprtype->fltstorage == FS_F32);
}

inline string BoolLit::CgX(CodeGen &) { return val ? "1" : "0"; }

inline string NullLit::CgX(CodeGen &cg) {
    if (exprtype && exprtype->kind == TY_REF && cg.IsFatPointee(exprtype->ref->sub)) {
        auto t = cg.T();
        cg.L("gs_rref ", t, " = { 0, 0 };");
        return t;
    }
    return "NULL";
}

inline string StrLit::CgX(CodeGen &cg) {
    auto et = exprtype;
    if (et->kind == TY_SLICE) {
        auto t = cg.T();
        cg.L(cg.CT(et), " ", t, " = { ", cg.StrRaw(val), ", ", val.size(), " };");
        return t;
    }
    // A fixed u8[k] array value.
    assert(et->kind == TY_ARRAY && et->arr->akind == A_FIXED);
    auto t = cg.T();
    cg.L(cg.CT(et), " ", t, ";");
    if (!val.empty())
        cg.L("memcpy(", t, ".e, ", cg.StrRaw(val), ", ", val.size(), ");");
    return t;
}

inline string Ident::CgX(CodeGen &cg) {
    assert(vdef);   // Named-function values never reach runtime.
    return cg.LoadLoc(cg.VarLoc(vdef), exprtype, line);
}

inline string Unary::CgX(CodeGen &cg) {
    if (op == T_BITAND) return cg.GenRefVal(child, line);
    auto x = cg.GenX(child);
    switch (op) {
        case T_MINUS:
            if (child->exprtype->kind == TY_FLT) return cat("(-", x, ")");
            return cat("gs_neg_", cg.IntSfx(child->exprtype->intstorage), "(", x, ")");
        case T_NOT: {
            auto ct = child->exprtype;
            if (ct->kind == TY_REF && cg.IsFatPointee(ct->ref->sub))
                return cat("(", x, ".hdr == 0)");
            return cat("(uint8_t)(!", x, ")");
        }
        case T_BITNOT:
            // The cast undoes C's promotion to int for the narrow types.
            return cat("(", cg.CT(exprtype), ")(~(", x, "))");
        default: assert(false); return x;
    }
}

inline string Binary::CgX(CodeGen &cg) {
    if (op == T_ANDAND || op == T_OROR) {
        // Short-circuit with left-to-right statement emission: the right
        // operand's statements may only run when the left allows.
        auto l = cg.GenTruth(left);
        auto t = cg.T();
        cg.L("uint8_t ", t, " = (uint8_t)(", l, op == T_ANDAND ? " != 0);" : " != 0);");
        cg.L("if (", op == T_ANDAND ? t : cat("!", t), ") {");
        cg.ind++;
        auto r = cg.GenTruth(right);
        cg.L(t, " = (uint8_t)(", r, " != 0);");
        cg.ind--;
        cg.L("}");
        return t;
    }
    auto lt = cg.OperandT(left->exprtype), rt = cg.OperandT(right->exprtype);
    // Null tests (§3.8).
    if (op == T_EQ || op == T_NEQ) {
        auto lnull = Is<NullLit>(left) != nullptr, rnull = Is<NullLit>(right) != nullptr;
        if (lnull || rnull) {
            if (lnull && rnull) return op == T_EQ ? "1" : "0";
            auto other = lnull ? right : left;
            // A narrowed optional variable's exprtype may already be the
            // decayed pointee; the null test reads the reference itself.
            string x;
            TypeExpr *ot = other->exprtype;
            if (auto id = Is<Ident>(other); id && id->vdef && cg.IsOpt(id->vdef->type)) {
                auto lv = cg.VarLoc(id->vdef);
                x = lv.s;
                ot = id->vdef->type;
            } else {
                x = cg.GenX(other);
            }
            auto addr = ot->kind == TY_REF && cg.IsFatPointee(ot->ref->sub)
                            ? cat(x, ".addr") : x;
            return cat("(", addr, op == T_EQ ? " == NULL)" : " != NULL)");
        }
    }
    // Resizable operands compare as element ranges through their views.
    if ((op == T_EQ || op == T_NEQ) && cg.IsResz(cg.OperandT(left->exprtype))) {
        if (left->exprtype->kind != right->exprtype->kind ||
            cg.OperandT(left->exprtype)->kind != TY_ARRAY)
            cg.Fail(line, "comparing resizable structs is unsupported");
        auto la = cg.GenLoc(left);
        if (la.t->kind == TY_REF) cg.DerefLoc(la, line);
        auto ra = cg.GenLoc(right);
        if (ra.t->kind == TY_REF) cg.DerefLoc(ra, line);
        auto lvw = cg.ArrayView(la, line), rvw = cg.ArrayView(ra, line);
        auto eq = cg.GenRangeEq(lvw.elem, lvw.elems, lvw.len, rvw.elems, rvw.len);
        return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
    }
    // Order of evaluation is left-to-right (§2): if the right operand
    // needs statements, the left must land in a temp first.
    auto leftfirst = cg.HasStmts(right);
    string l, r;
    if (leftfirst) {
        l = cg.GenPureVal(left);
        r = cg.GenVal(right);
    } else {
        l = cg.GenVal(left);
        r = cg.GenVal(right);
    }
    auto isint = lt->kind == TY_INT && lt->intstorage != IS_VARINT;
    auto isflt = lt->kind == TY_FLT;
    auto f32 = exprtype && exprtype->kind == TY_FLT &&
               exprtype->fltstorage == FS_F32;
    // Operands were unified by the typechecker; casting to the common C type
    // realizes any implicit widening (and truncates nothing).
    auto ct = isint || isflt ? cg.CT(lt) : "";
    auto lc = isint || isflt ? cat("(", ct, ")(", l, ")") : l;
    auto rc = isint || isflt ? cat("(", ct, ")(", r, ")") : r;
    switch (op) {
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            if (isflt) {
                if (op == T_MOD) return cat(f32 ? "fmodf(" : "fmod(", lc, ", ", rc, ")");
                const char *o = op == T_PLUS ? " + " : op == T_MINUS ? " - "
                                : op == T_MUL ? " * " : " / ";
                return cat("(", lc, o, rc, ")");
            }
            if (isint) {
                auto sfx = cg.IntSfx(lt->intstorage);
                switch (op) {
                    case T_PLUS:  return cat("gs_add_", sfx, "(", l, ", ", r, ")");
                    case T_MINUS: return cat("gs_sub_", sfx, "(", l, ", ", r, ")");
                    case T_MUL:   return cat("gs_mul_", sfx, "(", l, ", ", r, ")");
                    case T_DIV:   return cat("gs_div_", sfx, "(", l, ", ", r, ", ",
                                             cg.LocArgs(line), ")");
                    default:      return cat("gs_mod_", sfx, "(", l, ", ", r, ", ",
                                             cg.LocArgs(line), ")");
                }
            }
            // Elementwise math on identical struct/fixed-array types (§6.1).
            return cg.GenElemwise(this, l, r);
        }
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ: {
            const char *o = op == T_LT ? " < " : op == T_GT ? " > "
                            : op == T_LTEQ ? " <= " : " >= ";
            return cat("(uint8_t)(", lc, o, rc, ")");
        }
        case T_EQ: case T_NEQ: {
            if (isint || isflt) {
                auto eq = cat("(", lc, " == ", rc, ")");
                return op == T_EQ ? cat("(uint8_t)", eq) : cat("(uint8_t)(!", eq, ")");
            }
            auto eq = cg.GenEquality(lt, rt, left, right, l, r);
            return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
        }
        case T_BITAND: return cat("(", ct, ")(", lc, " & ", rc, ")");
        case T_BITOR:  return cat("(", ct, ")(", lc, " | ", rc, ")");
        case T_XOR:    return cat("(", ct, ")(", lc, " ^ ", rc, ")");
        case T_SHL:    return cat("gs_shl_", cg.IntSfx(lt->intstorage), "(", l,
                                  ", (int64_t)(", r, "))");
        case T_SHR:    return cat("gs_shr_", cg.IntSfx(lt->intstorage), "(", l,
                                  ", (int64_t)(", r, "))");
        default: assert(false); return l;
    }
}

inline string Dot::CgX(CodeGen &cg) {
    if (variantconst) {
        auto et = exprtype;
        auto ei = einst;
        auto vi = cg.VarIdx(ei->en, variantconst);
        if (et->kind == TY_ENUM && !et->enu->varmode) {
            auto t = cg.T();
            cg.L(cg.CT(et), " ", t, ";");
            cg.L(t, ".tag = ", cg.TagConst(ei, vi), ";");
            return t;
        }
        cg.Fail(line, "variant constant in a non-fixed context reached GenX");
    }
    if (member >= 0) {   // .len / .cap property.
        auto lv = cg.GenLoc(obj);
        if (lv.t->kind == TY_REF) cg.DerefLoc(lv, line);
        if (lv.t->kind == TY_ARRAY && member == B_CAP) {
            auto &a = *lv.t->arr;
            assert(a.akind == A_LIMITED);
            if (cg.ArrSize(lv.t->arr) >= 0) return cat(cg.ArrSize(lv.t->arr));
            return cat("(int64_t)*(uint32_t *)(", lv.s, ")");
        }
        auto v = cg.ArrayView(lv, line);
        return cat("(", v.len, ")");
    }
    auto lv = cg.GenLoc(this);
    return cg.LoadLoc(lv, exprtype, line);
}

inline string Index::CgX(CodeGen &cg) {
    auto lv = cg.GenLoc(this);
    return cg.LoadLoc(lv, exprtype, line);
}

inline string SliceExpr::CgX(CodeGen &cg) { return cg.GenSlice(this); }
// `as` range-checks in debug builds (GS_RANGE and friends are identity
// casts unless the C is compiled with -DGS_DEBUG=1); `as!` always wraps
// or truncates (§6.3).
inline string AsCast::CgX(CodeGen &cg) {
    auto x = cg.GenX(child);
    auto st = child->exprtype;
    auto tt = exprtype;
    // u64 is the one source whose values exceed the int64 range the checks
    // compute in; it gets unsigned-compare variants.
    auto su64 = st->kind == TY_INT && st->intstorage == IS_U64;
    if (tt->kind == TY_INT) {
        auto is = tt->intstorage;
        auto tct = cg.IntCT(is);
        if (st->kind == TY_FLT) {
            if (unchecked) return cat("(", tct, ")gs_f2iwrap(", x, ")");
            if (is == IS_U64) return cat("(uint64_t)GS_F2U(", x, ")");
            // GS_F2I checks the exact i64 value; GS_RANGE narrows further.
            if (cg.IntSize(is) == 8) return cat("(", tct, ")GS_F2I(", x, ")");
            auto [lo, hi] = cg.IntRange(is);
            return cat("(", tct, ")GS_RANGE(GS_F2I(", x, "), ", cg.IntStr(lo), ", ",
                       cg.IntStr(hi), ")");
        }
        if (unchecked || cg.TEq(st, tt)) return cat("(", tct, ")(", x, ")");
        if (su64) {
            // From u64: value-preserving iff it does not exceed the target's
            // maximum (all targets' maxima fit an unsigned compare).
            if (is == IS_U64) return cat("(", x, ")");
            auto [lo, hi] = cg.IntRange(is);
            (void)lo;
            return cat("(", tct, ")GS_RANGE_U(", x, ", ", (uint64_t)hi, "ULL)");
        }
        if (is == IS_U64)   // Source ≤ i64.max: only negatives are out of range.
            return cat("(uint64_t)GS_RANGE((int64_t)(", x, "), 0, INT64_MAX)");
        if (cg.IntSize(is) == 8) return cat("(", tct, ")(", x, ")");
        auto [lo, hi] = cg.IntRange(is);
        return cat("(", tct, ")GS_RANGE((int64_t)(", x, "), ", cg.IntStr(lo), ", ",
                   cg.IntStr(hi), ")");
    }
    assert(tt->kind == TY_FLT);
    if (tt->fltstorage == FS_F32) {
        if (!unchecked && st->kind == TY_FLT && st->fltstorage != FS_F32)
            return cat("GS_F2F32(", x, ")");
        if (!unchecked && st->kind == TY_INT) {
            if (IntBits(st->intstorage) <= 16) return cat("(float)(", x, ")");  // Exact.
            if (su64) return cat("GS_U2F32(", x, ")");
            return cat("GS_I2F32((int64_t)(", x, "))");
        }
        return cat("(float)(", x, ")");
    }
    if (!unchecked && st->kind == TY_INT) {
        if (IntBits(st->intstorage) <= 32) return cat("(double)(", x, ")");  // Exact.
        if (su64) return cat("GS_U2F(", x, ")");
        return cat("GS_I2F(", x, ")");
    }
    return cat("(double)(", x, ")");
}

inline string Call::CgX(CodeGen &cg) {
    auto rets = cg.EmitCall(this, Dst {});
    assert(!rets.empty());
    return cg.CallVal0(this, rets[0]);
}

// Fixed struct/variant/ADT literal as a C value. Relative-reference
// fields store against the member's own address (only meaningful when the
// value stays put; the typechecker's root rules keep this sane).
inline string StructLit::CgX(CodeGen &cg) {
    auto et = exprtype;
    auto tv = cg.T();
    cg.L(cg.CT(et), " ", tv, ";");
    auto fieldset = [&](const string &base, const vector<Field> &fields,
                        const vector<TypeExpr *> &ftypes, const vector<Node *> &defaults) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            Node *init = nullptr;
            for (size_t k = 0; k < fieldindices.size(); k++)
                if (fieldindices[k] == (int)i) init = inits[k].val;
            if (!init && i < defaults.size() && defaults[i]) init = defaults[i];
            auto ft = ftypes[i];
            auto path = cat(base, ".", cg.Sanitize(fields[i].name));
            if (!init) {   // Omitted optional: null.
                assert(ft->kind == TY_REF);
                cg.L("memset(&", path, ", 0, sizeof(", path, "));");
                continue;
            }
            if (ft->kind == TY_REF && ft->ref->lenstorage >= 0) {
                cg.EmitRelStoreAt(cat("(uint8_t *)&", path), ft, cg.GenX(init), line);
                continue;
            }
            cg.GenAny(init, Dst { 1, path });
        }
    };
    if (et->kind == TY_STRUCT) {
        auto si = cg.SI(et);
        fieldset(tv, si->st->fields, si->ftypes, si->defaults);
        return tv;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = cg.EIVar(et);
        auto vi = cg.VarIdx(ei->en, et->var->variant);
        if (ei->en->variants[vi].fields.empty()) cg.L("memset(&", tv, ", 0, sizeof(", tv, "));");
        fieldset(tv, ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi]);
        return tv;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && variant);
    auto ei = cg.EIOf(et);
    auto vi = cg.VarIdx(ei->en, variant);
    cg.L(tv, ".tag = ", cg.TagConst(ei, vi), ";");
    if (!ei->en->variants[vi].fields.empty())
        fieldset(cat(tv, ".u.v_", cg.Sanitize(ei->en->variants[vi].name)),
                 ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi]);
    return tv;
}

inline string ArrayLit::CgX(CodeGen &cg) { return cg.GenFixedArrayLit(this); }

inline string Block::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string IfExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string MatchExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string EarlyBlock::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string LoopExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string InlineBlock::CgX(CodeGen &cg) { return cg.CtlValX(this); }

// Statements and compile-time-only nodes have no value expression.
inline string While::CgX(CodeGen &cg) { cg.Fail(line, "internal: While as value"); }
inline string ForLoop::CgX(CodeGen &cg) { cg.Fail(line, "internal: ForLoop as value"); }
inline string Guard::CgX(CodeGen &cg) { cg.Fail(line, "internal: Guard as value"); }
inline string Return::CgX(CodeGen &cg) { cg.Fail(line, "internal: Return as value"); }
inline string Break::CgX(CodeGen &cg) { cg.Fail(line, "internal: Break as value"); }
inline string Continue::CgX(CodeGen &cg) { cg.Fail(line, "internal: Continue as value"); }
inline string RangeExpr::CgX(CodeGen &cg) { cg.Fail(line, "internal: RangeExpr as value"); }
inline string FunVal::CgX(CodeGen &cg) { cg.Fail(line, "internal: FunVal as value"); }
inline string VarDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: VarDecl as value"); }
inline string Assign::CgX(CodeGen &cg) { cg.Fail(line, "internal: Assign as value"); }
inline string IncDec::CgX(CodeGen &cg) { cg.Fail(line, "internal: IncDec as value"); }
inline string FnDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: FnDecl as value"); }
inline string StructDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }
inline string EnumDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }
inline string AliasDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }

// ---- CgAny ----------------------------------------------------------------

inline void Block::CgAny(CodeGen &cg, const Dst &d) {
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.L("{");
    cg.ind++;
    cg.GenBlockInner(this, d);
    cg.PopSc();
    cg.ind--;
    cg.L("}");
}

inline void IfExpr::CgAny(CodeGen &cg, const Dst &d) {
    auto c = cg.GenTruth(cond);
    cg.L("if (", c, ") {");
    cg.ind++;
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.GenBlockInner(thenb, d);
    cg.PopSc();
    cg.ind--;
    if (elseb) {
        cg.L("} else {");
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (auto b = Is<Block>(elseb)) cg.GenBlockInner(b, d);
        else cg.GenAny(elseb, d);
        cg.PopSc();
        cg.ind--;
    }
    cg.L("}");
    cg.termjump = false;
}

inline void MatchExpr::CgAny(CodeGen &cg, const Dst &d) {
    auto st = scrutinee->exprtype;
    TypeExpr *enumtype = nullptr;
    auto isref = false;
    if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
        enumtype = st->ref->sub;
        isref = true;
    } else if (st->kind == TY_ENUM) {
        enumtype = st;
    }
    if (!enumtype) {   // Integer match: an if-chain in arm order.
        auto x = cg.GenPure(scrutinee);
        // Compare at the scrutinee's type (unsigned scrutinees compare
        // unsigned; pattern constants carry that type's value bits).
        auto sct = cg.CT(scrutinee->exprtype);
        auto k = [&](int64_t v) { return cat("(", sct, ")", cg.IntStr(v)); };
        auto xs = cat("(", sct, ")(", x, ")");
        auto first = true;
        for (auto &arm : arms) {
            if (arm.pat.kind == P_WILDCARD) {
                cg.L(first ? "{" : "} else {");
            } else if (arm.hi == arm.lo + 1) {
                cg.L(first ? "" : "} else ", "if (", xs, " == ", k(arm.lo), ") {");
            } else {
                cg.L(first ? "" : "} else ", "if (", xs, " >= ", k(arm.lo), " && ", xs,
                  " < ", k(arm.hi), ") {");
            }
            first = false;
            cg.ind++;
            cg.PushSc(CodeGen::SC_PLAIN);
            cg.GenAny(arm.body, d);
            cg.PopSc();
            cg.ind--;
        }
        cg.L("}");
        cg.termjump = false;
        return;
    }
    auto varmode = enumtype->enu->varmode;
    auto ei = cg.EIOf(enumtype);
    auto ts = cg.TagSize(ei->en);
    string p, sv, tag;
    if (varmode || isref) {
        if (isref) {
            auto x = cg.GenPure(scrutinee);
            p = varmode ? x : cat("((uint8_t *)", x, ")");
        } else {
            p = cg.GenPtr(scrutinee);
        }
        tag = varmode ? cat("*(", cg.IntCT(cg.TagStore(ei->en)), " *)", p)
                      : cat("((", cg.CT(enumtype), " *)", p, ")->tag");
    } else {
        sv = cg.GenPure(scrutinee);
        tag = cat(sv, ".tag");
    }
    cg.L("switch (", tag, ") {");
    auto haswild = false;
    for (auto &arm : arms) {
        if (arm.pat.kind == P_WILDCARD) {
            haswild = true;
            cg.L("default: {");
        } else {
            auto vi = cg.VarIdx(ei->en, arm.variant);
            cg.L("case ", cg.TagConst(ei, vi), ": {");
        }
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (arm.binder) {
            auto vi = cg.VarIdx(ei->en, arm.variant);
            auto vt = cg.VariantType(enumtype, vi);
            auto bn = cg.LocalName(arm.binder);
            string payload = varmode
                ? cat("(", p, " + ", ts, ")")
                : (isref ? cat("((uint8_t *)&((", cg.CT(enumtype), " *)", p, ")->u.v_",
                               cg.Sanitize(arm.variant->name), ")")
                         : "");
            if (arm.pat.byref) {
                // Variable-mode payloads only (§8.1).
                if (cg.IsBytesT(vt)) cg.L("uint8_t *", bn, " = ", payload, ";");
                else cg.L(cg.CT(vt), " *", bn, " = (", cg.CT(vt), " *)(", payload, ");");
            } else if (cg.IsBytesT(vt)) {
                // By-value copy of a variable payload onto its own stack.
                auto stk = cg.AllocStk(true);
                cg.L("uint8_t *", bn, " = ", cg.Top(stk), ";");
                cg.SaveBase(true, stk, bn);
                cg.vstk[arm.binder] = stk;
                auto sz = cg.T();
                cg.L("int64_t ", sz, " = ", cg.SizeX(vt, payload), ";");
                cg.L("memcpy(", cg.Top(stk), ", ", payload, ", (size_t)", sz, ");");
                cg.Bump(stk, sz);
            } else if (arm.variant->fields.empty()) {
                cg.L(cg.CT(vt), " ", bn, " = {0};");
            } else if (!payload.empty()) {
                cg.L(cg.CT(vt), " ", bn, " = *(", cg.CT(vt), " *)(", payload, ");");
            } else {
                cg.L(cg.CT(vt), " ", bn, " = ", sv, ".u.v_", cg.Sanitize(arm.variant->name), ";");
            }
        }
        cg.GenAny(arm.body, d);
        cg.PopSc();
        cg.ind--;
        cg.L("} break;");
    }
    if (!haswild)
        cg.L("default: GS_UNREACHABLE(", cg.LocArgs(line), ");");
    cg.L("}");
    cg.termjump = false;
}

inline void EarlyBlock::CgAny(CodeGen &cg, const Dst &d) {
    cg.PushSc(CodeGen::SC_BLOCK);
    auto si = (int)cg.cscopes.size() - 1;
    cg.cscopes[si].brklbl = cg.Lbl();
    cg.cscopes[si].dst = d;
    cg.L("{");
    cg.ind++;
    cg.GenBlockInner(body, d);
    auto brk = cg.cscopes.back().usedbrk;
    auto lbl = cg.cscopes.back().brklbl;
    cg.PopSc();
    cg.ind--;
    cg.L("}");
    if (brk) cg.L(lbl, ":;");
    cg.termjump = false;
}

inline void LoopExpr::CgAny(CodeGen &cg, const Dst &d) {
    cg.GenLoopBody({}, body, d, this);
}

inline void InlineBlock::CgAny(CodeGen &cg, const Dst &d) {
    cg.PushSc(CodeGen::SC_IB);
    auto si = (int)cg.cscopes.size() - 1;
    cg.cscopes[si].ibsf = sf;
    cg.cscopes[si].brklbl = cg.Lbl();
    cg.cscopes[si].dst = d;
    cg.GenAny(body, d);
    auto brk = cg.cscopes.back().usedbrk;
    auto lbl = cg.cscopes.back().brklbl;
    cg.PopSc();
    if (brk) cg.L(lbl, ":;");
    cg.termjump = false;
}

inline void Call::CgAny(CodeGen &cg, const Dst &d) {
    auto rets = cg.EmitCall(this, d);
    // Fixed-value results wire into the destination here; bytes results and
    // channel-passed returns were handled in place.
    if (!rets.empty() && !cg.IsVoidT(exprtype) && !cg.IsBytesT(exprtype)) {
        auto r0 = cg.CallVal0(this, rets[0]);
        if (d.k == 1 && r0 != d.s) cg.L(d.s, " = ", r0, ";");
        else if (d.k == 2) cg.EmitValStore(d.s, exprtype, r0);
    }
}

inline void IntLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void FltLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void BoolLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void StrLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void NullLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Ident::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void ArrayLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void StructLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Unary::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Binary::CgAny(CodeGen &cg, const Dst &d) {
    // An elementwise result (struct/fixed-array typed, §6.1) writes its
    // members straight into a plain destination — including one that aliases
    // an operand (see GenElemwiseInto).
    if (d.k == 1 && exprtype &&
        (exprtype->kind == TY_STRUCT || exprtype->kind == TY_ARRAY)) {
        string l, r;
        cg.ElemwiseOperands(this, l, r);
        cg.GenElemwiseInto(this, l, r, d.s);
        return;
    }
    cg.LeafAny(this, d);
}
inline void Dot::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Index::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void SliceExpr::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void AsCast::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void RangeExpr::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }

// Statement nodes reached with a discard destination just emit themselves.
inline void While::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void ForLoop::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Guard::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Return::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Break::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Continue::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void FunVal::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: FunVal emitted"); }
inline void VarDecl::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Assign::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void IncDec::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void FnDecl::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void StructDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }
inline void EnumDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }
inline void AliasDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }

// ---- CgStmt ---------------------------------------------------------------

inline void VarDecl::CgStmt(CodeGen &cg) {
    // Multi-name binding from one call: wire the call's channels straight
    // into the locals.
    if (names.size() > 1 && inits.size() == 1) {
        auto c = Is<Call>(inits[0]);
        assert(c);
        vector<Dst> dsts;
        for (size_t i = 0; i < defs.size(); i++) {
            auto d = defs[i];
            auto rt = c->rettypes[i];
            auto name = cg.LocalName(d);
            if (cg.IsResz(rt)) {
                cg.EmitCoreTypes();
                auto stk = cg.AllocStk(true);
                cg.L("gs_rhdr ", name, " = { ", cg.Top(stk), ", 0 };");
                cg.SaveBase(true, stk, cat(name, ".base"));
                cg.vstk[d] = stk;
                dsts.push_back(Dst { 2, stk, d->type, cat(name, ".len") });
            } else if (cg.IsBytesT(rt)) {
                auto stk = cg.AllocStk(true);
                cg.L("uint8_t *", name, " = ", cg.Top(stk), ";");
                cg.SaveBase(true, stk, name);
                cg.vstk[d] = stk;
                dsts.push_back(Dst { 2, stk });
            } else {
                cg.L(cg.CT(d->type), " ", name, ";");
                dsts.push_back(Dst { 1, name });
            }
        }
        auto rets = cg.EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
        for (size_t i = 0; i < dsts.size() && i < rets.size(); i++)
            if (dsts[i].k == 1 && !rets[i].empty() && rets[i] != dsts[i].s)
                cg.L(dsts[i].s, " = ", rets[i], ";");
        return;
    }
    for (size_t i = 0; i < defs.size(); i++)
        cg.BindLocal(defs[i], i < inits.size() ? inits[i] : nullptr);
}

inline void Assign::CgStmt(CodeGen &cg) {
    auto lv = cg.GenLoc(lval);
    if (op == T_DOTASSIGN) { cg.GenRebind(this, lv); return; }
    if (pointee) cg.DerefLoc(lv, line);
    // Compound operators: full-width int/flt locations only (TC).
    if (op != T_ASSIGN) {
        assert(lv.val);
        auto r = cg.GenX(rhs);
        auto sfx = lv.t->kind == TY_INT ? cg.IntSfx(lv.t->intstorage) : "";
        switch (op) {
            case T_PLUSEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " + (", r, ");");
                else cg.L(lv.s, " = gs_add_", sfx, "(", lv.s, ", ", r, ");");
                break;
            case T_MINUSEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " - (", r, ");");
                else cg.L(lv.s, " = gs_sub_", sfx, "(", lv.s, ", ", r, ");");
                break;
            case T_MULEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " * (", r, ");");
                else cg.L(lv.s, " = gs_mul_", sfx, "(", lv.s, ", ", r, ");");
                break;
            case T_DIVEQ:
                if (lv.t->kind == TY_FLT) cg.L(lv.s, " = ", lv.s, " / (", r, ");");
                else cg.L(lv.s, " = gs_div_", sfx, "(", lv.s, ", ", r, ", ",
                          cg.LocArgs(line), ");");
                break;
            case T_MODEQ:
                if (lv.t->kind == TY_FLT)
                    cg.L(lv.s, " = ", lv.t->fltstorage == FS_F32 ? "fmodf(" : "fmod(", lv.s,
                      ", ", r, ");");
                else cg.L(lv.s, " = gs_mod_", sfx, "(", lv.s, ", ", r, ", ",
                          cg.LocArgs(line), ");");
                break;
            case T_ANDEQ: cg.L(lv.s, " = (", cg.CT(lv.t), ")(", lv.s, " & (", r, "));"); break;
            case T_OREQ:  cg.L(lv.s, " = (", cg.CT(lv.t), ")(", lv.s, " | (", r, "));"); break;
            case T_XOREQ: cg.L(lv.s, " = (", cg.CT(lv.t), ")(", lv.s, " ^ (", r, "));"); break;
            default: assert(false);
        }
        return;
    }
    // Plain assignment, by the target's representation (§4.4).
    auto t = lv.t;
    if (t->kind == TY_REF && t->ref->lenstorage >= 0) {
        // Relative-reference slot: encode from the plain reference value.
        cg.GenRelAssign(lv, rhs, line);
        return;
    }
    if (cg.IsResz(t)) {
        // Whole-resizable assignment: clear (top back to the value start),
        // then construct the new contents in place (§4.4).
        assert(!lv.val && !lv.stk.empty() && !lv.lenlv.empty());
        cg.L(cg.Top(lv.stk), " = ", lv.s, ";");
        cg.GenConstruct(rhs, lv.stk, lv.t, lv.lenlv);
        return;
    }
    if (!lv.val) {
        // Bytes-class fixed-capacity array [..]: copy contents within cap.
        assert(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED);
        string srcstk;
        auto src = cg.GenPtr(rhs, &srcstk);
        auto nn = cg.T();
        cg.L("int64_t ", nn, " = (int64_t)*(uint32_t *)(", src, " + 4);");
        cg.L("if (", nn, " > (int64_t)*(uint32_t *)(", lv.s, ")",
          ") gs_abort(GS_E_CAPACITY, ", cg.LocArgs(line), ");");
        cg.L("*(uint32_t *)((", lv.s, ") + 4) = (uint32_t)", nn, ";");
        cg.L("memcpy((", lv.s, ") + 8, ", src, " + 8, (size_t)(", nn, " * ",
          cg.FixedSize(t->arr->sub), "));");
        return;
    }
    cg.GenAny(rhs, Dst { 1, lv.s, lv.t });
}

inline void IncDec::CgStmt(CodeGen &cg) {
    auto lv = cg.GenLoc(lval);
    if (lv.t->kind == TY_REF) cg.DerefLoc(lv, line);
    assert(lv.val);
    auto o = op == T_INC ? "gs_add_" : "gs_sub_";
    cg.L(lv.s, " = ", o, cg.IntSfx(lv.t->intstorage), "(", lv.s, ", 1);");
}

inline void FnDecl::CgStmt(CodeGen &) {}   // Nested declarations are separate specs.
inline void While::CgStmt(CodeGen &cg) {
    Dst d;
    cg.GenLoopBody([&]() {
        auto c = cg.GenTruth(cond);
        auto si = (int)cg.cscopes.size() - 1;
        // The loop scope has no allocations yet, so a plain goto exits
        // cleanly; the condition's own temps were allocated in this
        // scope's compile-time list and are restored... none yet either.
        cg.L("if (!(", c, ")) goto ", cg.cscopes[si].brklbl, ";");
        cg.cscopes[si].usedbrk = true;
    }, body, d, this);
}

inline void ForLoop::CgStmt(CodeGen &cg) {
    auto d = Dst {};
    auto iv = vdef ? cg.LocalName(vdef) : cg.T();
    auto ix = idxdef ? cg.LocalName(idxdef) : "";
    if (iterkind == IK_RANGE) {
        // The loop variable runs at the range's own type: narrow counters
        // stay narrow through the body (§6.5).
        auto ict = cg.CT(vdef->type);
        auto r = Is<RangeExpr>(iter);
        auto lo = cg.GenX(r->lo);
        auto lov = cg.T();
        cg.L(ict, " ", lov, " = ", lo, ";");
        auto hi = cg.GenX(r->hi);
        auto hiv = cg.T();
        cg.L(ict, " ", hiv, " = ", hi, ";");
        if (!ix.empty()) cg.L("int64_t ", ix, " = 0;");
        cg.GenLoopBody({}, body, d, this,
                    cat("for (", ict, " ", iv, " = ", lov, "; ", iv, " < ", hiv, "; ", iv,
                        "++", ix.empty() ? "" : cat(", ", ix, "++"), ") {"));
        return;
    }
    if (iterkind == IK_COUNT) {
        auto ict = cg.CT(vdef->type);
        auto n = cg.GenX(iter);
        auto nv = cg.T();
        cg.L(ict, " ", nv, " = ", n, ";");
        if (!ix.empty()) cg.L("int64_t ", ix, " = 0;");
        cg.GenLoopBody({}, body, d, this,
                    cat("for (", ict, " ", iv, " = 0; ", iv, " < ", nv, "; ", iv, "++",
                        ix.empty() ? "" : cat(", ", ix, "++"), ") {"));
        return;
    }
    // Arrays and slices. The length re-reads each iteration (growth during
    // iteration is legal, §5.2); element access goes through the view.
    auto lv = cg.GenLoc(iter);
    if (lv.t->kind == TY_REF) cg.DerefLoc(lv, line);
    auto v = cg.ArrayView(lv, line);
    // Where BCE proved the body cannot resize it, both halves of the view are
    // loop-invariant. Only a length that is an actual memory load is worth
    // spelling as a local: that is the one the C backend cannot hoist for
    // itself, since an element store in the body might alias it. A resizable
    // owned here keeps its length in a frame header (C.2) -- an ordinary local
    // the backend already knows nothing aliases, and hoisting it by hand only
    // lengthens a live range.
    auto memlen = v.len.find('*') != string::npos || v.len.find("->") != string::npos;
    if (fixedlen && memlen) {
        auto nv = cg.T();
        cg.L("int64_t ", nv, " = ", v.len, ";");
        auto bv = cg.T();
        if (v.typedelems) cg.L(cg.CT(v.elem), " *", bv, " = ", v.elems, ";");
        else cg.L("uint8_t *", bv, " = (uint8_t *)(", v.elems, ");");
        v.len = nv;
        v.elems = bv;
    }
    auto gi = ix.empty() ? cg.T() : ix;
    auto et = vdef->type;
    if (!cg.IsFix(v.elem)) {
        // Sequential walk, &-binding only; the cursor advances in the
        // increment clause so `continue` behaves.
        auto p = cg.T();
        cg.L("uint8_t *", p, " = (uint8_t *)(", v.elems, ");");
        cg.GenLoopBody([&]() {
            if (byref) cg.L("uint8_t *", iv, " = ", p, ";");
        }, body, d, this,
            cat("for (int64_t ", gi, " = 0; ", gi, " < (", v.len, "); ", gi, "++, ", p,
                " += ", cg.SizeX(v.elem, p), ") {"));
        return;
    }
    auto esz = cg.FixedSize(v.elem);
    cg.GenLoopBody([&]() {
        string elem = v.typedelems
                          ? cat(v.elems, "[", gi, "]")
                          : cat("(*(", cg.CT(v.elem), " *)((", v.elems, ") + ", gi, " * ",
                                esz, "))");
        if (byref) cg.L(cg.CT(et), " ", iv, " = &", elem, ";");
        else cg.L(cg.CT(et), " ", iv, " = ", elem, ";");
    }, body, d, this,
        cat("for (int64_t ", gi, " = 0; ", gi, " < (", v.len, "); ", gi, "++) {"));
}

inline void Guard::CgStmt(CodeGen &cg) {
    auto c = cg.GenTruth(cond);
    cg.L("if (!(", c, ")) {");
    cg.ind++;
    cg.PushSc(CodeGen::SC_PLAIN);
    if (elseb) {
        cg.GenBlockInner(elseb, Dst {});
    } else if (implicitexit == 1) {
        cg.GenBreakPath(nullptr, line);
    } else {
        cg.GenNormalReturn({}, line);
    }
    cg.cscopes.back().saves.clear();   // The block diverged.
    cg.PopSc();
    cg.ind--;
    cg.L("}");
    cg.termjump = false;
}

inline void Return::CgStmt(CodeGen &cg) {
    // Exiting an inlined body?
    for (auto i = (int)cg.cscopes.size() - 1; i >= 0; i--) {
        if (cg.cscopes[i].kind == CodeGen::SC_IB && cg.cscopes[i].ibsf == target) {
            if (!vals.empty()) {
                cg.GenAny(vals[0], cg.cscopes[i].dst);
                for (size_t j = 1; j < vals.size(); j++) cg.GenAny(vals[j], Dst {});
            }
            cg.EmitExitRestores(i);
            cg.cscopes[i].usedbrk = true;
            cg.L("goto ", cg.cscopes[i].brklbl, ";");
            cg.termjump = true;
            return;
        }
        if (cg.cscopes[i].kind == CodeGen::SC_FN) break;
    }
    if (cg.curspec && target == cg.curspec->sf) {
        cg.GenNormalReturn(vals, line);
        return;
    }
    cg.GenFromReturn(this);
}

inline void Break::CgStmt(CodeGen &cg) { cg.GenBreakPath(val, line); }
inline void Continue::CgStmt(CodeGen &cg) {
    auto si = -1;
    for (auto i = (int)cg.cscopes.size() - 1; i >= 0; i--) {
        if (cg.cscopes[i].kind == CodeGen::SC_LOOP) { si = i; break; }
        if (cg.cscopes[i].kind == CodeGen::SC_FN) break;
    }
    assert(si >= 0);
    cg.EmitExitRestores(si + 1);
    cg.cscopes[si].usedcnt = true;
    cg.L("goto ", cg.cscopes[si].cntlbl, ";");
    cg.termjump = true;
}

// Everything else in statement position evaluates for effect.
inline void IntLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void FltLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void BoolLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void StrLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void NullLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Ident::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void ArrayLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void StructLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Unary::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Binary::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Dot::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Index::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void SliceExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void AsCast::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void RangeExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Call::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Block::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void IfExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void MatchExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void EarlyBlock::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void LoopExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void InlineBlock::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void FunVal::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: FunVal statement"); }
inline void StructDecl::CgStmt(CodeGen &) {}
inline void EnumDecl::CgStmt(CodeGen &) {}
inline void AliasDecl::CgStmt(CodeGen &) {}

}  // namespace goose
