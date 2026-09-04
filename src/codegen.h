// Goose compiler — C code generation. Consumes the optimized specialization
// bodies (FnSpec::body) plus globals and emits one self-contained C file (the
// runtime files from src/runtime/ are prepended by the driver).
//
// Representation (Appendix C; §10.3's hidden-stack-argument strategy):
// * Fixed-size types become packed C types (#pragma pack(1)): scalars, packed
//   structs, fixed/limited arrays wrapped in structs, fixed-mode ADTs as
//   tag + union, references as pointers, slices as { data, len }.
// * Variable values ("bytes" values) are self-describing byte images on a
//   data stack, held as a uint8_t* to the value start. A resizable value is
//   a header in the owning frame (gs_rhdr: element base + count, C.2) -- or a
//   frame object, the C struct of a resizable-tailed struct's fixed fields
//   with the tail's header last -- plus the elements on a data stack: growth
//   bumps the stack top and the header's count; the base pointer never moves.
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
// * `return ... from` (§7.9) signals through one thread-local discriminant,
//   gs_rf, plus per-target thread-local channels for the in-flight fixed
//   values (nonfixed ones land directly on the target's destination stack,
//   recorded thread-locally at target entry). gs_rf is zero except between a
//   `return ... from` and the catch in its target frame, so only those two
//   points write it: every other exit of a propagating function leaves it
//   alone, and a call on a propagation path costs one load and a
//   never-taken branch.
//
// Scope exits restore data-stack watermarks: every nonfixed local's own base
// pointer doubles as the watermark to restore, so exits (fallthrough, break,
// continue, return, propagate) emit `stack->top = base;` runs in reverse
// declaration order.
//
// This file holds the CodeGen class -- its state, the small utilities, and
// the driver -- with its members declared in the order they are defined
// across codegen_types.h, codegen_frames.h, codegen_values.h,
// codegen_construct.h, codegen_stmts.h, codegen_calls.h, codegen_builtins.h,
// codegen_render.h and codegen_emit.h; the per-node CgX / CgAny / CgStmt
// overrides are codegen_nodes.h.
#pragma once

namespace goose {

// Where a value goes: nowhere (evaluated for effect), into the C lvalue `s`,
// or constructed at the top of the data stack `s`. `t` is the wanted type
// where the receiver knows it; `lenlv` is set for stack destinations of
// resizable class: elements go to the stack, and the construction assigns
// the element count (or the whole frame object) to it.
enum DstKind { DK_DISCARD, DK_LVALUE, DK_STACK };
struct Dst {
    DstKind k = DK_DISCARD;
    string s;
    TypeExpr *t = nullptr;
    string lenlv;
};

// Set by the driver before codegen (§7.10): the runtime's extern-support C,
// spliced after the generated types, and the user's --include headers.
inline string gs_runtime_os_text;
inline vector<string> gs_includes;

struct CodeGen {
    Ast &ast;

    // Output sections, concatenated by Run() in this order (after the
    // driver-prepended runtime): predefs go before the runtime paste.
    string predefs;
    string tdecls;      // Packed typedefs.
    string pdata;       // Packed static data (string literals).
    string data;        // Queues, long-distance return channels, globals.
    string protos;
    set<FnSpec *> usedexterns;   // Extern fns called by live code: prototypes.
    string code;        // Function bodies, size/eq helpers, thunks, main.
    bool usesthreads = false;
    // Measurement only, and unsound: emit the whole `return ... from`
    // machinery but none of the post-call discriminant checks, so a program
    // that never takes a long-distance return still prints the right answer
    // and the checks' cost can be priced (--unsafe-no-rf-check).
    bool norfcheck = false;

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

    bool TEq(TypeExpr *a, TypeExpr *b);
    StructInst *SI(TypeExpr *t);
    EnumInst *EIOf(TypeExpr *t);
    EnumInst *EIVar(TypeExpr *t);
    int VarIdx(SEnum *en, SVariant *v);
    SizeClass Cls(TypeExpr *t);

    bool IsFix(TypeExpr *t)  { return Cls(t) == SC_FIXED; }
    bool IsResz(TypeExpr *t) { return Cls(t) == SC_RESIZABLE; }
    bool IsBytesT(TypeExpr *t) { return Cls(t) != SC_FIXED; }
    // A resizable-tailed struct with an all-fixed prefix is a frame object
    // (C.2): a C struct of its fixed fields plus its tail's own gs_rhdr (or
    // nested frame object), held in the owning frame like a fixed value;
    // only the innermost tail's elements occupy a data stack. A gs_rref to
    // one carries the object's address in `hdr`.
    bool IsFrameObj(TypeExpr *t) { return t->kind == TY_STRUCT && SI(t)->frameobj; }
    int TailIdx(StructInst *si);
    string FoTailHdr(TypeExpr *t, const string &obj);
    TypeExpr *FoTailArr(TypeExpr *t);
    string FoPrefixSize(TypeExpr *t);
    bool IsFatRef(TypeExpr *t);
    bool IsOpt(TypeExpr *t) { return t->kind == TY_REF && t->ref->optional; }
    bool IsVoidT(TypeExpr *t) { return !t || t->kind == TY_VOID; }
    bool IsU8T(TypeExpr *t) { return t->kind == TY_INT && t->intstorage == IS_U8; }

    IntStorage LenStore(TypeArray *a);

    static int64_t IntSize(IntStorage s) { return IntBits(s) / 8; }

    static const char *IntCT(IntStorage s);
    static pair<int64_t, int64_t> IntRange(IntStorage s);
    static const char *IntSfx(IntStorage s);
    static const char *RelCT(IntStorage s, bool uns = false);
    static const char *RelCT(TypeExpr *rt);

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

    int64_t PadAlign(TypeExpr *t);
    Layout LayoutFields(const vector<Field> &fields, const vector<TypeExpr *> &ftypes);
    const Layout &StructLayout(StructInst *si);
    const Layout &VariantLayout(EnumInst *ei, int vi);
    int64_t FixedSize(TypeExpr *t);

    // ------------------------------------------------------------------
    // Naming: one global identifier space for types, functions, globals, and
    // static data; per-function spaces for locals seeded from it.

    set<string> used;

    static bool CReserved(const string &s);
    static bool TempLike(const string &s);
    string Sanitize(string_view name);
    string Unique(string base);

    // ------------------------------------------------------------------
    // Mangled type identities and on-demand C type emission. The mangle keys
    // every per-type artifact (typedef, size fn, eq fn); the C name is the
    // uniquified mangle.

    string Mangle(TypeExpr *t);

    unordered_map<string, string> ctypes;   // mangle -> emitted C type name.
    bool corebuiltins = false;

    void EmitCoreTypes();
    string CT(TypeExpr *t);
    static bool StructLike(TypeExpr *t);
    string NameCT(TypeExpr *t);
    set<string> cdefined;    // Mangled names whose C body has been emitted.

    void EmitCFields(string &d, const vector<Field> &fields, const vector<TypeExpr *> &ftypes);

    // Tag constants, one enum per EnumInst: <Mangle>_<Variant>_k.
    set<EnumInst *> tagenums;
    unordered_map<EnumInst *, string> tagprefix;

    void EnsureTagEnum(EnumInst *ei);
    string TagConst(EnumInst *ei, int vi);

    // The TY_VARIANT type for (enum type, variant index); cached per pair.
    map<pair<EnumInst *, int>, TypeExpr *> varianttypes;

    TypeExpr *VariantType(TypeExpr *enumtype, int vi);

    // ------------------------------------------------------------------
    // Runtime size of a bytes-class value: a generated per-type walker,
    // gs_size_<mangle>(p). Static-size subruns collapse into constants.

    set<string> sizefns, eqfns;

    string SizeX(TypeExpr *t, const string &ptr);
    string SizeFn(TypeExpr *t);
    void EmitSizeWalk(string &b, TypeExpr *t, const string &q);
    void EmitSizeElems(string &b, TypeExpr *elem, const string &q);
    int64_t ZeroSize(TypeExpr *t);

    // ------------------------------------------------------------------
    // default<T>() (§4.2): all-zero bytes are the default of every fixed type
    // -- numbers, false, null, empty slices and limited arrays, variant 0 --
    // except where a field declares its own default, which is written over
    // the zeroes afterwards.

    bool HasFieldDefaults(TypeExpr *t);
    void EmitDefaultInto(const string &lv, TypeExpr *t);
    void EmitDefaultFields(const string &lv, TypeExpr *t);

    // ------------------------------------------------------------------
    // Structural equality (§4.5): gs_eq_<mangle>. Fixed values pass by value,
    // bytes values as pointers. Gap-free fixed types shortcut to memcmp.

    bool GapFree(TypeExpr *t);
    bool ScalarEq(TypeExpr *t);
    string EqX(TypeExpr *t, const string &a, const string &b);
    string EqFn(TypeExpr *t);
    void EmitEqFixed(string &bo, TypeExpr *t);
    void EmitEqBytes(string &bo, TypeExpr *t);
    void EmitEqWalk(string &bo, TypeExpr *t, const string &pa, const string &pb, int depth);

    // ------------------------------------------------------------------
    // Per-specialization call interface. Signature shape (C.3 order):
    //   [declared params (resizable by-value ones add a gs_stack*)]
    //   [free-variable references, §7.5]
    //   [out-pointers for fixed returns after the first]
    //   [destination stacks for nonfixed returns]
    //   [int64_t gs_sp].
    // The C return value is the first fixed return, else void; a `return ...
    // from` discriminant travels in the thread-local gs_rf, not the signature.

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

    bool IsPoolParam(FnSpec *sp, size_t i);

    // Which globals with dedicated data stacks a specialization may touch, its
    // callees included. A call can only move a stack it can name: one handed to
    // it as an argument, or a global it (transitively) mentions. Everything
    // else the caller has cached stays cached across the call.
    unordered_map<FnSpec *, set<const VarDef *>> gtouch;

    void ComputeGlobalTouch();
    bool PassesOpaqueStack(FnSpec *sp);
    string SyncReach(FnSpec *callee, const vector<string> &args);
    void CollectSpecs();
    string SigParams(FnSpec *sp, bool decls, bool er = false);
    string SigRet(FnSpec *sp);

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

    // A named result built at its destination (§7.3): the local's elements are
    // written where the value ends up, so only its metadata travels at the
    // return. A destination that wants a length prefix in front of the
    // elements gets those bytes reserved at the declaration and patched at the
    // return, rather than the elements moved out of the way.
    struct NrvoDest {
        string stk;               // Destination stack expression.
        string lenlv;             // Receiving count lvalue; empty when prefix.
        IntStorage ls = IS_U32;   // The prefix's length storage.
        bool prefix = false;      // Reserve prefix bytes and patch the count.
        bool inlined = false;     // Bound by an InlineBlock, not by DetectNrvo.
        bool fo = false;          // A frame object: lenlv receives the whole object.
        string pref, hdr;         // Reserved prefix address, header name (BindLocal).
    };
    unordered_map<const VarDef *, NrvoDest> nrvo;
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
    // So a stack the function owns keeps its top in a local, and memory is
    // synchronized only where something else can observe it: across calls
    // (which may be handed the stack) and at every function exit.
    //
    // The local pays for itself only where the stack actually grows, and
    // elsewhere it is a live pointer holding a register for nothing, so it is
    // confined to the loops that grow the stack: a kernel loop that only reads
    // and writes elements of an array keeps the memory form for it. Growth
    // outside every loop caches the stack over the whole body, which is the
    // extent it is live across anyway.
    //
    // Soundness rests on one spelling per cached stack. Own stacks qualify:
    // a callee's indices start above the caller's in-use watermark (§10.3), so
    // `GS(gs_sp + k)` names a stack no caller expression can also name, and
    // global stacks live outside the indexed block entirely. A reference to a
    // resizable carries its stack inside the reference value (`p.stk`), which
    // is a second spelling for a stack the body may also name directly, so a
    // function holding one gets neither of those two (CanCacheTops).
    //
    // It caches its reference parameters' stacks instead, where RefTopsOk
    // clears it: `p.stk` is then the body's only spelling of that stack, and
    // the checker's root classes say which parameters are distinct stacks
    // (§10.2). That is the form a push through a reference wants -- `*(T *)top
    // = e; top += n` with top in a register -- and the same marks carry it
    // across calls.
    vector<string> toporder;                  // Cacheable stacks, discovery order.
    // Every growth or shrink, as parallel stack index and innermost enclosing
    // loop (-1 for one outside every loop).
    vector<int> growstk, growloop;
    vector<int> loopparent;                   // Loop -> enclosing loop, or -1.
    vector<int> loopstack;                    // Loops open at this point of the emission.
    vector<int> regstk, regloop;              // The regions, as stack and loop.
    vector<string> topfnlocal;                // Stack -> its whole-body local, or "".
    bool cachetops = false;
    bool reftops = false;               // Caching reference parameters' stacks.
    set<string> refstkexprs;            // Their `<param>.stk` / `.flstk` spellings.

    static constexpr const char *FLUSHMARK = "@@gsflush@@";
    static constexpr const char *RELOADMARK = "@@gsreload@@";
    static constexpr const char *LOOPMARK = "@@gsloop@@";
    static constexpr const char *TOPMARK = "@@gstop";

    set<string> gstkexprs;   // Every global's dedicated stack expression.

    bool CacheableStk(const string &stk);
    int TopIdx(const string &stk);
    bool IsRegion(int k, int id);
    string Top(const string &stk);
    string TopW(const string &stk);

    // Sync points are marked rather than written, because a stack first used
    // after a call still needs that call's reload: the expansion happens once
    // the whole body is emitted and every cached stack is known. The mark
    // carries what the call can reach, so expansion can sync just those --
    // "*" means everything, which is what a function exit needs.
    void MarkFlush(const string &reach = "*")  { if (cachetops) L(FLUSHMARK, reach); }
    void MarkReload(const string &reach = "*") { if (cachetops) L(RELOADMARK, reach); }

    int MarkLoopBegin();
    void MarkLoopEnd(int id);
    void PlanTopCaches();
    static bool LineIs(string_view s, const char *pfx);
    static string_view GotoTarget(string_view line);
    static string_view LabelHere(string_view line);
    static string_view NextLine(const string &b, size_t &i, size_t &ind0);
    string ExpandTopMarkers(const string &b);
    void PushSc(int kind);
    void EmitRestores(const CScope &s);
    void PopSc();
    void EmitExitRestores(int to);
    string AllocStk(bool forlocal);
    void SaveBase(bool forlocal, const string &stk, const string &basevar);

    // Globals keep their names for the whole run; locals are named per
    // function (a captured variable gets an independent name as the hidden
    // parameter of each capturer — arguments are positional).
    unordered_map<const VarDef *, string> gnames;
    unordered_map<const VarDef *, string> gstks;
    unordered_map<const VarDef *, pair<string, string>> gpools;

    // The base an `in pool` offset is measured from (§3.9), per function: the
    // pool's element region, which its stack's reservation fixes once and
    // growth never moves. A byte store through a `uint8_t *` may alias the
    // header in C, so reading `pool.base` per access would reload it after
    // every relative store; each function that needs it loads it once into a
    // local instead, emitted at entry by EmitSpec.
    vector<pair<const VarDef *, string>> poolbases;
    set<const VarDef *> poolglobals;   // Globals some `in pool` type names.

    string PoolBase(const VarDef *pool);
    string PoolBaseOr(const VarDef *vd, const string &hdrbase);
    string LocalName(VarDef *vd);
    string VName(const VarDef *vd);

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
        string hdr;            // The resizable's own header/frame-object lvalue, when it has one.
        // A loop-hoisted view of the array behind this reference (see `views`):
        // set on the reference by VarLoc, and, for the length, carried across
        // the deref to the pointee, where ArrayView reads it instead of memory.
        string hbase, hlen;
    };

    bool PrefVar(const VarDef *vd);

    // Optimizer splices can leave a reference-typed tree in a slot whose
    // checked type already decayed; Dst::t says what the receiver wants, and
    // the pointee load then happens at the leaf against it.

    bool NeedsDeref(TypeExpr *have, TypeExpr *want);
    Loc FatRefLoc(const string &x, TypeExpr *sub);
    Loc BytesLoc(const string &ptr, TypeExpr *t, const Loc &from);
    void RelParts(const Loc &lv, string &faddr, string &off);
    string RelOrigin(TypeExpr *rt, const string &faddr);
    void DerefLoc(Loc &lv, Line ln);
    Loc VarLoc(VarDef *vd);
    string HdrLv(VarDef *vd);
    string VarCT(const VarDef *vd);
    string VStkOf(const VarDef *vd);
    string FieldPtr(const string &base, const vector<Field> &fields,
                    const vector<TypeExpr *> &ftypes, int fieldidx);

    // Elements pointer + length expression of an array-typed loc, all kinds.
    struct ArrView {
        string elems;      // Pointer expression (typed for val fixed/limited).
        string len;        // int64 length expression.
        string lenlv;      // Length lvalue for ops that change it (may be typed).
        TypeExpr *elem = nullptr;
        bool typedelems = false;   // elems is CT* (else uint8_t*).
    };

    ArrView ArrayView(const Loc &lv, Line ln);
    ArrView RawArrayView(const Loc &lv, Line ln);

    // ------------------------------------------------------------------
    // Loop-invariant array views. An array reached through a reference keeps
    // both halves of its view behind that reference, and the C backend reloads
    // them at every access, since a byte store through any reference may alias
    // the header they live in. BCE marks per loop which reference variables it
    // can neither resize nor re-bind (`hoistrefs`); for those the view is read
    // into locals once before the loop and every access inside uses them.
    unordered_map<const VarDef *, pair<string, string>> views;   // base, length.

    bool AddView(VarDef *vd, Line ln);

    // Installs the views a loop body may read, for the extent of that body.
    struct ViewScope {
        CodeGen &cg;
        vector<VarDef *> added;
        ViewScope(CodeGen &_cg, const vector<VarDef *> &refs, Line ln) : cg(_cg) {
            for (auto vd : refs) if (cg.AddView(vd, ln)) added.push_back(vd);
        }
        ~ViewScope() { for (auto vd : added) cg.views.erase(vd); }
    };

    string GenPure(Node *n);
    static string IntStr(int64_t v);
    static string FltStr(double v, bool f32);
    Loc IndexLoc(Loc lv, Node *idxnode, Line ln, bool nobc);
    Loc GenLoc(Node *n);
    string BytesTemp(string &stk);
    string RzTemp(TypeExpr *t, string &stk);
    string RzLenLv(TypeExpr *t, const string &h);
    Loc RzTempLoc(TypeExpr *t, const string &h, const string &stk);
    Loc GenRzTmp(Node *n);
    Loc MemberLoc(Loc lv, Dot *d);
    Loc FieldLocAt(Loc lv, int fieldidx);

    // ------------------------------------------------------------------
    // Expression values. GenX produces a C expression for fixed-class values
    // (possibly after emitting statements); GenPtr produces a byte pointer to
    // a bytes-class value. Both follow the node's checked exprtype, which
    // already encodes operand unification and reference decay.

    static bool IsCtl(Node *n);
    string LoadLoc(Loc lv, TypeExpr *et, Line ln);
    string AdaptToFixed(Loc lv, TypeExpr *et, Line ln);
    string BytesAddrOf(const Loc &lv);
    string GenRefVal(Node *child, Line ln);
    string GenXD(Node *n, TypeExpr *want);
    string GenTruth(Node *n);

    // The three per-node passes dispatch virtually (ast.h); the bodies live
    // together at the end of this file, delegating into the machinery here.
    string GenX(Node *n) { return n->CgX(*this); }
    void GenAny(Node *n, Dst d) { n->CgAny(*this, d); }
    void GenStmt2(Node *n) { n->CgStmt(*this); }

    string CtlValX(Node *n);
    void LeafAny(Node *n, const Dst &d);
    string GenFixedArrayLit(ArrayLit *al);
    string GenPtr(Node *n, string *stkout = nullptr);

    // ------------------------------------------------------------------
    // String literals: static byte data. As a slice: { data, len }. As a
    // u8[...] value: a static [lenfield][bytes] image (emitted writable, since
    // §9.5's laundering makes writes through read-back references legal).

    unordered_map<string, string> strdata;   // text -> raw byte array name.
    unordered_map<string, string> strval;                 // mangle+text -> value name.

    string StrRaw(const string &v);
    string GenStrBytes(StrLit *s);

    // ------------------------------------------------------------------
    // Binary operators. Operand exprtypes are already decayed and unified.

    TypeExpr *OperandT(TypeExpr *t);
    string GenVal(Node *n);
    string GenPureVal(Node *n);
    bool HasStmts(Node *n);
    string GenEquality(TypeExpr *lt, const string &l, const string &r);
    string GenSliceEq(TypeExpr *st, const string &l, const string &r);
    string GenRangeEq(TypeExpr *elem, const string &ae, const string &an, const string &be,
                      const string &bn);
    void GenElemwiseInto(Binary *b, const string &l, const string &r, const string &dst);
    void ElemwiseOperands(Binary *b, string &l, string &r);
    string GenElemwise(Binary *b, const string &l, const string &r);
    string GenSlice(SliceExpr *se);

    // ------------------------------------------------------------------
    // Construction (§4.2/§4.3): writes a value front-to-back at a stack's
    // top, bumping it. All branches of value-producing control constructs
    // construct to the same destination; calls pass the stack down.

    void Bump(const string &stk, const string &n) { L(TopW(stk), " += ", n, ";"); }

    void EmitValStore(const string &stk, TypeExpr *t, const string &x);
    void EmitLenStore(const string &stk, IntStorage ls, const string &n);
    void EmitRelRangeCheck(TypeExpr *rt, const string &off, Line ln, bool inroot);
    void EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln, bool inroot);
    void EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln);
    void EmitRelSelfAt(const string &fa, TypeExpr *rt, int64_t fieldoff, Line ln,
                       bool inroot = true);
    void EmitRelSelfStore(const string &stk, TypeExpr *rt, int64_t fieldoff, Line ln);
    bool HasRelRef(TypeExpr *t);

    // Byte span of the largest fixed value that can be a relative
    // reference's root array (§3.9). Roots are variables, so this is the
    // widest offset a store into a root that is *not* on a data stack can
    // produce; stack roots are bounded by GS_STACK_RESERVE instead. Both
    // bounds decide whether EmitRelStoreAt emits its range check.
    int64_t relrootmax = 0;

    void ComputeRelRootMax();
    void EmitCopyElems(const string &stk, TypeExpr *elem, const string &src, const string &n);

    // Element count + elements pointer of an array/slice-valued source node,
    // for construction and append. Understands string literals, slices, and
    // all array kinds (through references too).
    struct SrcElems {
        string elems, n;
    };

    SrcElems GenSrcElems(Node *n);
    void GenConstruct(Node *n, const string &stk, TypeExpr *want = nullptr,
                      const string &lenlv = "");
    void GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln,
                         const string &lenlv = "");
    void EmitRzCopy(Loc lv, TypeExpr *et, const string &stk, const string &lenlv, Line ln);
    bool RzShape(TypeExpr *t, int64_t &prefix, TypeExpr *&elem);
    void GenVarEnumFromLoc(Loc lv, TypeExpr *et, const string &stk);
    void FixedLitAtStk(Node *n, const string &stk);
    void FixedLitAt(Node *n, const string &dst);
    void FixedLitAtLv(Node *n, const string &base, bool inroot);
    void FixedArrayLitAt(ArrayLit *al, const string &base, bool inroot);
    void StructLitAt(StructLit *sl, const string &base, bool inroot);
    void EmitValStoreTag(const string &stk, IntStorage ts, const string &x);
    void GenArrayLit(ArrayLit *al, const string &stk, const string &lenlv = "");
    static bool HasSelfInit(StructLit *sl);
    void GenStructLit(StructLit *sl, const string &stk, const string &lenlv = "");
    void GenFrameObjLit(StructLit *sl, StructInst *si, const string &stk, const string &obj);
    void GenFieldInits(StructLit *sl, const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                       const vector<Node *> &defaults, const string &stk, const string &lenlv = "",
                       const string &selfbase = "");

    // ------------------------------------------------------------------
    // Statements and control flow. GenAny routes a node's value to a Dst;
    // control constructs recurse so every branch reaches the same
    // destination (§4.3). Scopes mirror C braces, so watermark base
    // variables are always in C scope exactly where exits may restore them.

    bool termjump = false;   // The last emitted statement left via goto/return.

    void GenBlockInner(Block *b, Dst d);
    void GenStmt(Node *n);

    // ------------------------------------------------------------------
    // Loops. Shape: for (<init>; ; <incr>) { [cond exit] body cnt:; restores }
    // Goose break/continue always leave via gotos with explicit watermark
    // restores; C break/continue are never emitted for them, so nesting
    // inside generated switches stays safe.

    void GenLoopBody(const function<void()> &condexit, Block *bodyb, Dst d,
                     const string &forhead = "");
    void GenBreakPath(Node *val);

    // ------------------------------------------------------------------
    // Declarations and assignment.

    void BindLocal(VarDef *d, Node *init);
    string GenPrefVal(Node *n);
    string Unique2(const string &base);
    void GenRelAssign(Loc lv, Node *rhs, Line ln);
    void GenRebind(Assign *a, Loc lv);

    // ------------------------------------------------------------------
    // Returns: normal, forwarding a multi-value call, exiting an inlined
    // body, and long-distance (§7.9).

    void GenNormalReturn(const vector<Node *> &vals);

    // The bytes a length prefix of this storage reserves ahead of the
    // elements. A varint takes the one byte that covers counts under 128.
    int64_t PrefixBytes(IntStorage ls) { return ls == IS_VARINT ? 1 : IntSize(ls); }

    void EmitPrefixPatch(const string &pref, IntStorage ls, const string &stk, const string &count,
                         const string &elems);
    void EmitNrvoFinish(const NrvoDest &nd);
    void Epilogue(const string &retv);
    void PropagateReturn(const string &rfval);
    void GenFromReturn(Return *r);

    // Channel types per target: the rets of its first live spec.
    unordered_map<SFunction *, vector<TypeExpr *> *> fromrets;

    vector<TypeExpr *> &FromRets(SFunction *t);
    void EnsureFromChannels(SFunction *t);

    // ------------------------------------------------------------------
    // Calls. Returns one entry per return value: a C expression for fixed
    // values, the value's base pointer for bytes-class ones. d0 is the
    // preferred destination for the first return (in-place construction);
    // alldst supplies destinations for every return (multi-value receives).

    string CallVal0(Call *c, const string &r0);
    void EmitSlidePrefix(const string &base, IntStorage ls, const string &stk, const string &lenlv);
    vector<string> EmitCall(Call *c, Dst d0, vector<Dst> *alldst = nullptr);
    vector<string> EmitExternCall(Call *c, FnSpec *sp);
    string ExternProto(FnSpec *sp);
    vector<Node *> CallArgNodes(Call *c, size_t nparams);
    void EmitArg(FnSpec *sp, size_t i, Node *node, vector<string> &args);
    void EmitFvArg(const VarDef *fv, vector<string> &args);
    vector<string> EmitSpecCall(Call *c, FnSpec *sp, Dst d0, vector<Dst> *alldst);
    void EmitReprefix(FnSpec *sp, const Dst &dd, const string &base, const string &cnt);
    void EmitRfCheck(FnSpec *callee);
    vector<string> EmitFvCall(Call *c, Dst d0);
    vector<string> EmitDispatch(Call *c, Dst d0, vector<Dst> *alldst);

    // ------------------------------------------------------------------
    // Builtins (§3.3, §3.7, §5.4, §9.3, §11.2). Emitted inline; only I/O, division,
    // varints, and thread/queue machinery call into the runtime.

    unordered_map<string, string> queues;   // Element type mangle -> queue global.

    string QueueFor(TypeExpr *t);
    Loc RecvLoc(Node *n);
    TypeExpr *MakeSliceT(TypeExpr *elem, Line l);
    string LenCast(const Loc &lv);
    vector<string> EmitBuiltin(Call *c, Dst d0);

    // ------------------------------------------------------------------
    // Text forms (§3.7): print, str and format share them. A scalar's text
    // comes from a gs_fmt_* runtime helper writing at most GS_FMT_MAX bytes;
    // a u8 array or slice contributes its bytes as they are.

    // One print argument to stdout.
    // ------------------------------------------------------------------
    // Rendering aggregates (§3.7): the text of any value appended to a
    // u8[>..] builder, structurally, or through a user `format` overload
    // recorded on the call for that type.

    TypeExpr *growu8 = nullptr;
    TypeExpr *GrowU8();
    Loc TempBuilder();
    FnSpec *FmtSpecFor(Call *c, TypeExpr *t);
    bool SimpleText(Call *c, TypeExpr *t);
    void RenderLit(Loc &out, const string &text);
    void RenderN(Loc &out, const string &nexpr);
    void RenderLoc(Loc &out, Loc lv, TypeExpr *t, bool nested, Call *c, Line ln);
    void EmitUserFormat(Loc &out, Loc lv, FnSpec *sp, Line ln);
    Loc RenderToTemp(Node *a, Call *c);
    void EmitOutArg(Node *a, Call *c);
    string FmtCall(Node *a, const string &dst);
    void EmitFormatInto(Loc lv, Node *a, Line ln, Call *c);
    vector<string> EmitStr(Call *c, vector<Node *> &an, Dst d0, Line ln);
    vector<string> EmitPush(Call *c, vector<Node *> &an, Line ln);
    void EmitAppend(vector<Node *> &an, Line ln);
    vector<string> EmitAlloc(Call *c, vector<Node *> &an, Line ln);

    // thread_spawn(worker, args...): pack the flat arguments contiguously on
    // a scratch stack, hand them to the runtime, unpack in a per-worker thunk.
    unordered_map<FnSpec *, string> thunks;

    vector<string> EmitThreadSpawn(Call *c, vector<Node *> &an);
    string EnsureThreadThunk(FnSpec *sp);

    // ------------------------------------------------------------------
    // Function bodies.

    void DetectNrvo(FnSpec *sp);
    const VarDef *OpenIbNrvo(InlineBlock *ib, const Dst &d);
    bool CanCacheTops(FnSpec *sp);
    bool RefTopsOk(FnSpec *sp);
    void ResetFnState();
    string EnsureEr(FnSpec *sp);
    void EmitSpec(FnSpec *sp, bool er = false);

    // ------------------------------------------------------------------
    // Globals (§11.1): C globals plus dedicated data stacks for nonfixed
    // ones; initializers run in declaration order before main.

    // Globals whose declaration carries a C initializer, so gs_init_globals
    // has nothing left to do for them (§11.1).
    set<const VarDef *> gstatic;

    // Spelling out more elements than this would trade startup work for source
    // size; such a value keeps its runtime initialization.
    static constexpr int64_t MAXSTATICELEMS = 256;

    bool StaticInitX(Node *n, TypeExpr *t, string &out);
    void EmitGlobalDecls();
    void EmitGlobalInit();
    string GlobalLenLv(VarDef *d);
    void InitGlobalStack(VarDef *d);
    void EmitMain();

    // ------------------------------------------------------------------
    // Driver.

    string result;   // Everything after the runtime paste.

    CodeGen(Ast &_ast, bool _norfcheck = false) : ast(_ast), norfcheck(_norfcheck) {
        for (auto t : ast.alltypes)
            if (t->kind == TY_REF && t->ref->pool) poolglobals.insert(t->ref->pool);
        ComputeRelRootMax();
        CollectSpecs();
        // Zero means "no long-distance return in flight", which is also the
        // state every propagating function's ordinary exit leaves behind.
        if (!fromids.empty()) data += "static GS_TLS int32_t gs_rf;\n";
        EmitGlobalDecls();
        ComputeGlobalTouch();
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
        // Extern fns: the runtime's own C follows the types it is written
        // against, then user headers, then prototypes for whatever neither
        // defines.
        string externs;
        if (!usedexterns.empty() || !gs_runtime_os_text.empty()) {
            EmitCoreTypes();
            CT(MakeSliceT(ast.inttypes[IS_U8], Line {}));
        }
        for (auto sp : usedexterns) {
            if (gs_runtime_os_text.find(cat(" ", sp->sf->cname, "(")) != string::npos) continue;
            externs += ExternProto(sp);
        }
        string includes;
        for (auto &inc : gs_includes) Append(includes, "#include \"", inc, "\"\n");
        Append(result, "\n/* ---- types ---- */\n#pragma pack(push, 1)\n", tdecls, pdata,
               "#pragma pack(pop)\n\n/* ---- data ---- */\n", data,
               "\n/* ---- runtime (extern support) ---- */\n", gs_runtime_os_text,
               "\n/* ---- includes ---- */\n", includes,
               "\n/* ---- extern prototypes ---- */\n", externs,
               "\n/* ---- prototypes ---- */\n", protos, "\n/* ---- code ---- */\n", code);
    }
};

}  // namespace goose
