// Goose compiler — codegen's types (definitions of CodeGen members,
// codegen.h): type utilities and packed layouts (C.2), C naming, mangled
// type identities with on-demand typedefs, the per-type size walkers of
// bytes values, default values (§4.2) and structural equality (§4.5).
#pragma once

namespace goose {

// ------------------------------------------------------------------
// Type utilities on concrete (post-typecheck) types. Sizes of fixed and
// limited arrays were evaluated during checking; assert rather than
// re-evaluate.

inline bool CodeGen::TEq(TypeExpr *a, TypeExpr *b) {
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
inline StructInst *CodeGen::SI(TypeExpr *t) {
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

inline EnumInst *CodeGen::EIOf(TypeExpr *t) {
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

inline EnumInst *CodeGen::EIVar(TypeExpr *t) {   // For TY_VARIANT.
    assert(t->kind == TY_VARIANT);
    return EIOf(t->var->adt);
}

inline int CodeGen::VarIdx(SEnum *en, SVariant *v) {
    for (size_t i = 0; i < en->variants.size(); i++)
        if (&en->variants[i] == v) return (int)i;
    assert(false);
    return 0;
}

inline SizeClass CodeGen::Cls(TypeExpr *t) {
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

inline int CodeGen::TailIdx(StructInst *si) {
    auto last = -1;
    for (auto i = 0; i < (int)si->st->fields.size(); i++)
        if (!si->st->fields[i].ispad) last = i;
    return last;
}

// The innermost tail's gs_rhdr lvalue within frame object `obj`.
inline string CodeGen::FoTailHdr(TypeExpr *t, const string &obj) {
    auto si = SI(t);
    auto ti = TailIdx(si);
    auto s = cat(obj, ".", Sanitize(si->st->fields[ti].name));
    auto ft = si->ftypes[ti];
    return IsFrameObj(ft) ? FoTailHdr(ft, s) : s;
}

inline TypeExpr *CodeGen::FoTailArr(TypeExpr *t) {
    auto si = SI(t);
    auto ft = si->ftypes[TailIdx(si)];
    return IsFrameObj(ft) ? FoTailArr(ft) : ft;
}

// The bytes of a frame object before its innermost tail header, which is
// the C struct's last member: the fixed fields at every nesting level.
inline string CodeGen::FoPrefixSize(TypeExpr *t) {
    return cat("(sizeof(", CT(t), ") - sizeof(gs_rhdr))");
}

inline bool CodeGen::IsFatRef(TypeExpr *t) {
    return t->kind == TY_REF && t->ref->lenstorage < 0 && IsResz(t->ref->sub);
}

// The stored length-field type of an array (A_VAR: declared or u32
// default; A_LIMITED with static capacity: smallest fitting type).
inline IntStorage CodeGen::LenStore(TypeArray *a) {
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

inline const char *CodeGen::IntCT(IntStorage s) {
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
inline pair<int64_t, int64_t> CodeGen::IntRange(IntStorage s) {
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
inline const char *CodeGen::IntSfx(IntStorage s) {
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

// The C type behind a relative reference's unsigned width spelling:
// signed for a self-relative offset, which runs both ways from its own
// field, unsigned for an `in pool` one, which is a distance from the
// pool's base (§3.9).
inline const char *CodeGen::RelCT(IntStorage s, bool uns) {
    switch (IntSize(s)) {
        case 1: return uns ? "uint8_t" : "int8_t";
        case 2: return uns ? "uint16_t" : "int16_t";
        case 4: return uns ? "uint32_t" : "int32_t";
        default: return uns ? "uint64_t" : "int64_t";
    }
}

inline const char *CodeGen::RelCT(TypeExpr *rt) {
    return RelCT((IntStorage)rt->ref->lenstorage, rt->ref->pool != nullptr);
}

// StructInst / (EnumInst, variant).

// The alignment `pad` (bare form) gives the next field: its scalar width.
inline int64_t CodeGen::PadAlign(TypeExpr *t) {
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

inline CodeGen::Layout CodeGen::LayoutFields(const vector<Field> &fields,
                                             const vector<TypeExpr *> &ftypes) {
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

inline const CodeGen::Layout &CodeGen::StructLayout(StructInst *si) {
    auto key = pair<const void *, int>(si, -1);
    auto it = layouts.find(key);
    if (it != layouts.end()) return it->second;
    return layouts[key] = LayoutFields(si->st->fields, si->ftypes);
}

inline const CodeGen::Layout &CodeGen::VariantLayout(EnumInst *ei, int vi) {
    auto key = pair<const void *, int>(ei, vi);
    auto it = layouts.find(key);
    if (it != layouts.end()) return it->second;
    return layouts[key] = LayoutFields(ei->en->variants[vi].fields, ei->vftypes[vi]);
}

inline int64_t CodeGen::FixedSize(TypeExpr *t) {
    switch (t->kind) {
        case TY_INT:  assert(t->intstorage != IS_VARINT); return IntSize(t->intstorage);
        case TY_FLT:  return t->fltstorage == FS_F32 ? 4 : 8;
        case TY_BOOL: return 1;
        case TY_REF:
            if (t->ref->lenstorage >= 0) {
                assert(t->ref->lenstorage != IS_VARINT);
                return IntSize((IntStorage)t->ref->lenstorage);
            }
            return IsResz(t->ref->sub) ? 16 : 8;
        case TY_SLICE: return 16;
        case TY_STRUCT: {
            // An empty struct takes the one byte C gives it.
            auto sz = StructLayout(SI(t)).size;
            return sz ? sz : 1;
        }
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

inline bool CodeGen::CReserved(const string &s) {
    static const set<string> words = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
        "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
        "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
        "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "main",
        "bool", "true", "false", "NULL", "memcpy", "memmove", "memset", "printf", "free",
        "malloc", "abort", "exit",
        // libc and libm names the runtime's headers bring into scope.
        "abs", "labs", "llabs", "div", "ldiv", "atoi", "atol", "atof", "strtol", "strtoul",
        "strtod", "rand", "srand", "system", "getenv", "qsort", "bsearch", "calloc",
        "realloc", "clock", "time", "difftime", "mktime", "sleep", "exp", "exp2", "log",
        "log2", "log10", "log1p", "expm1", "pow", "sqrt", "cbrt", "hypot", "sin", "cos",
        "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "floor", "ceil",
        "round", "trunc", "fabs", "fmod", "modf", "frexp", "ldexp", "fmin", "fmax", "nan",
        "remove", "rename", "tmpfile", "fopen", "fclose", "fread", "fwrite", "fgetc",
        "fputc", "fgets", "fputs", "fflush", "fseek", "ftell", "getc", "putc", "getchar",
        "putchar", "puts", "gets", "scanf", "sscanf", "sprintf", "snprintf", "perror",
        "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat", "strchr", "strrchr",
        "strstr", "strtok", "strerror", "memcmp", "memchr", "tolower", "toupper",
        "isalpha", "isdigit", "isspace", "isupper", "islower", "isalnum", "raise",
        "signal", "assert", "errno", "stdin", "stdout", "stderr", "alloca", "index",
        "read", "write", "open", "close",
        // windows.h macros.
        "min", "max",
    };
    return words.count(s) != 0;
}

// A user name shaped like a generated temporary or label (t12, L3).
inline bool CodeGen::TempLike(const string &s) {
    if (s.size() < 2 || (s[0] != 't' && s[0] != 'L')) return false;
    for (size_t i = 1; i < s.size(); i++) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

inline string CodeGen::Sanitize(string_view name) {
    string s(name);
    if (s.empty()) s = "_";
    if (CReserved(s) || TempLike(s) || s.compare(0, 3, "gs_") == 0 ||
        s.compare(0, 3, "GS_") == 0)
        s += "_";
    return s;
}

inline string CodeGen::Unique(string base) {
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

inline string CodeGen::Mangle(TypeExpr *t) {
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

inline void CodeGen::EmitCoreTypes() {
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
inline string CodeGen::CT(TypeExpr *t) {
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
            if (IsResz(r.sub)) return "gs_rref";
            if (IsBytesT(r.sub)) return "uint8_t *";
            // A pointer needs only the pointee's name, so a node type can
            // hold references to itself and the body can come later.
            return cat(StructLike(r.sub) ? NameCT(r.sub) : CT(r.sub), " *");
        }
        default: break;
    }
    auto name = NameCT(t);
    auto m = Mangle(t);
    if (cdefined.count(m)) return name;
    cdefined.insert(m);   // Before the body: recursion terminates via refs.
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
            Append(d, "struct ", name, " {\n");
            auto any = false;
            for (auto &f : si->st->fields) any |= !f.ispad;
            if (any) EmitCFields(d, si->st->fields, si->ftypes);
            else Append(d, "    uint8_t gs_empty;\n");
            Append(d, "};\n");
            break;
        }
        case TY_VARIANT: {
            auto ei = EIVar(t);
            auto vi = VarIdx(ei->en, t->var->variant);
            auto &v = ei->en->variants[vi];
            Append(d, "struct ", name, " {\n");
            if (v.fields.empty()) Append(d, "    uint8_t gs_empty;\n");
            else EmitCFields(d, v.fields, ei->vftypes[vi]);
            Append(d, "};\n");
            break;
        }
        case TY_ENUM: {
            assert(!t->enu->varmode);
            auto ei = EIOf(t);
            EnsureTagEnum(ei);
            Append(d, "struct ", name, " {\n    ", IntCT(TagStore(ei->en)), " tag;\n");
            string members;
            for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                if (ei->en->variants[vi].fields.empty()) continue;
                auto vt = VariantType(t, (int)vi);
                Append(members, "        ", CT(vt), " v_",
                       Sanitize(ei->en->variants[vi].name), ";\n");
            }
            if (!members.empty()) Append(d, "    union {\n", members, "    } u;\n");
            Append(d, "};\n");
            break;
        }
        default: assert(false);
    }
    tdecls += d;
    tdecls += "\n";
    return name;
}

// Fixed-class kinds that C declares as a named struct: they get a
// forward typedef the moment they are named (NameCT), so a reference to
// one needs only the name and the body follows when the type is first
// needed by value.
inline bool CodeGen::StructLike(TypeExpr *t) {
    return t->kind == TY_STRUCT || t->kind == TY_VARIANT ||
           (t->kind == TY_ENUM && !t->enu->varmode);
}

// The C name of a fixed-class type, assigned (and, for struct-like kinds,
// forward-declared) on first sight; CT emits the body.
inline string CodeGen::NameCT(TypeExpr *t) {
    auto m = Mangle(t);
    auto it = ctypes.find(m);
    if (it != ctypes.end()) return it->second;
    EmitCoreTypes();
    auto name = Unique(m);
    ctypes[m] = name;
    if (StructLike(t)) Append(tdecls, "typedef struct ", name, " ", name, ";\n");
    return name;
}

// Mangled names whose C body has been emitted.

inline void CodeGen::EmitCFields(string &d, const vector<Field> &fields,
                                 const vector<TypeExpr *> &ftypes) {
    // A frame object's tail is not part of the packed layout: it is the
    // tail's own header (or nested frame object) after the fixed fields.
    auto n = fields.size();
    while (n > 0 && ftypes[n - 1] && IsResz(ftypes[n - 1])) n--;
    vector<Field> pre(fields.begin(), fields.begin() + n);
    vector<TypeExpr *> pret(ftypes.begin(), ftypes.begin() + n);
    auto lo = LayoutFields(pre, pret);
    auto npad = 0;
    for (size_t i = 0; i < n; i++) {
        auto &f = fields[i];
        if (f.ispad) {
            auto next = i + 1 < n ? lo.offs[i + 1] : lo.size;
            auto sz = next - lo.offs[i];
            if (sz > 0) Append(d, "    uint8_t gs_pad", npad++, "[", sz, "];\n");
            continue;
        }
        Append(d, "    ", CT(ftypes[i]), " ", Sanitize(f.name), ";\n");
    }
    for (auto i = n; i < fields.size(); i++) {
        EmitCoreTypes();
        Append(d, "    ", IsFrameObj(ftypes[i]) ? CT(ftypes[i]) : string("gs_rhdr"), " ",
               Sanitize(fields[i].name), ";\n");
    }
}

inline void CodeGen::EnsureTagEnum(EnumInst *ei) {
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
}

inline string CodeGen::TagConst(EnumInst *ei, int vi) {
    EnsureTagEnum(ei);
    return cat(tagprefix[ei], "_", Sanitize(ei->en->variants[vi].name), "_k");
}

inline TypeExpr *CodeGen::VariantType(TypeExpr *enumtype, int vi) {
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

// A C expression for the size of the value of type t at ptr; emits the
// walker function on demand for dynamic types.
inline string CodeGen::SizeX(TypeExpr *t, const string &ptr) {
    if (IsFix(t)) return cat(FixedSize(t));
    return cat(SizeFn(t), "(", ptr, ")");
}

inline string CodeGen::SizeFn(TypeExpr *t) {
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
inline void CodeGen::EmitSizeWalk(string &b, TypeExpr *t, const string &q) {
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
                        Append(b, "    { int64_t n = GS_ULEB_READ(", q, "); ", q,
                               " += GS_ULEB_SIZE(", q, ");\n");
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

inline void CodeGen::EmitSizeElems(string &b, TypeExpr *elem, const string &q) {
    if (IsFix(elem)) {
        Append(b, "    ", q, " += n * ", FixedSize(elem), ";\n");
    } else {
        Append(b, "    for (int64_t i = 0; i < n; i++) ", q, " += ", SizeFn(elem), "(",
               q, ");\n");
    }
}

// The size of the all-zeroes minimal value of a type (arrays empty, ADTs
// at variant 0): what a missed qpoll writes so the value stays valid.
inline int64_t CodeGen::ZeroSize(TypeExpr *t) {
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
// default<T>() (§4.2): all-zero bytes are the default of every fixed type
// -- numbers, false, null, empty slices and limited arrays, variant 0 --
// except where a field declares its own default, which is written over
// the zeroes afterwards.

// Does any field at any depth of a fixed type declare a default value?
inline bool CodeGen::HasFieldDefaults(TypeExpr *t) {
    auto any = [&](const vector<Field> &fs, const vector<TypeExpr *> &fts) {
        for (size_t i = 0; i < fs.size(); i++) {
            if (fs[i].ispad) continue;
            if (fs[i].defaultval || HasFieldDefaults(fts[i])) return true;
        }
        return false;
    };
    switch (t->kind) {
        case TY_STRUCT: { auto si = SI(t); return any(si->st->fields, si->ftypes); }
        case TY_ENUM: {
            if (t->enu->varmode) return false;
            auto ei = EIOf(t);
            return any(ei->en->variants[0].fields, ei->vftypes[0]);
        }
        case TY_VARIANT: {
            auto ei = EIVar(t);
            auto vi = VarIdx(ei->en, t->var->variant);
            return any(ei->en->variants[vi].fields, ei->vftypes[vi]);
        }
        case TY_ARRAY: return t->arr->akind == A_FIXED && HasFieldDefaults(t->arr->sub);
        default: return false;
    }
}

// Writes the default value of fixed type t into the C lvalue lv.
inline void CodeGen::EmitDefaultInto(const string &lv, TypeExpr *t) {
    L("memset(&", lv, ", 0, sizeof(", lv, "));");
    EmitDefaultFields(lv, t);
}

// The declared field defaults of t, over an already zeroed lv.
inline void CodeGen::EmitDefaultFields(const string &lv, TypeExpr *t) {
    if (!HasFieldDefaults(t)) return;
    auto fields = [&](const string &base, const vector<Field> &fs,
                      const vector<TypeExpr *> &fts, const vector<Node *> &defaults) {
        for (size_t i = 0; i < fs.size(); i++) {
            if (fs[i].ispad) continue;
            auto path = cat(base, ".", Sanitize(fs[i].name));
            if (i < defaults.size() && defaults[i]) GenAny(defaults[i], Dst { DK_LVALUE, path, fts[i] });
            else EmitDefaultFields(path, fts[i]);
        }
    };
    switch (t->kind) {
        case TY_STRUCT: {
            auto si = SI(t);
            fields(lv, si->st->fields, si->ftypes, si->defaults);
            return;
        }
        case TY_ENUM: {
            auto ei = EIOf(t);
            if (ei->en->variants[0].fields.empty()) return;
            fields(cat(lv, ".u.v_", Sanitize(ei->en->variants[0].name)),
                   ei->en->variants[0].fields, ei->vftypes[0], ei->vdefaults[0]);
            return;
        }
        case TY_VARIANT: {
            auto ei = EIVar(t);
            auto vi = VarIdx(ei->en, t->var->variant);
            fields(lv, ei->en->variants[vi].fields, ei->vftypes[vi], ei->vdefaults[vi]);
            return;
        }
        case TY_ARRAY: {
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", ArrSize(t->arr), "; ", iv, "++) {");
            ind++;
            EmitDefaultFields(cat(lv, ".e[", iv, "]"), t->arr->sub);
            ind--;
            L("}");
            return;
        }
        default: return;
    }
}

// ------------------------------------------------------------------
// Structural equality (§4.5): gs_eq_<mangle>. Fixed values pass by value,
// bytes values as pointers. Gap-free fixed types shortcut to memcmp.

inline bool CodeGen::GapFree(TypeExpr *t) {
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

inline bool CodeGen::ScalarEq(TypeExpr *t) {
    return t->kind == TY_INT || t->kind == TY_FLT || t->kind == TY_BOOL ||
           (t->kind == TY_REF && t->ref->lenstorage < 0 && !IsFatRef(t));
}

// Equality of two values as a C expression; a/b are values (fixed) or
// byte pointers (bytes-class). May emit an eq function.
inline string CodeGen::EqX(TypeExpr *t, const string &a, const string &b) {
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

inline string CodeGen::EqFn(TypeExpr *t) {
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

inline void CodeGen::EmitEqFixed(string &bo, TypeExpr *t) {
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

inline void CodeGen::EmitEqBytes(string &bo, TypeExpr *t) {
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
inline void CodeGen::EmitEqWalk(string &bo, TypeExpr *t, const string &pa, const string &pb,
                                int depth) {
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
                        Append(bo, I, "int64_t ", out, " = GS_ULEB_READ(", p, "); ", p,
                               " += GS_ULEB_SIZE(", p, ");\n");
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

}  // namespace goose
