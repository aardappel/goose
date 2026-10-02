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
// value carries every place it may point (Roots, ast.h): one alternative
// per root, a VarDef (null = static data) with a bit saying whether that
// root owns the pointee or merely outlives it, and the provenance bits
// (writable §9.5, reusable §5.4). A value that may be any of several -- an
// if's branches, a variable's bindings, a call's returns -- has all of
// their alternatives, and every rule asks each of them. Roots are compared
// by scope depth along the current compile-time call path; only rules that
// need the pointee's *identity* -- storing it as a relative reference
// (§3.9), and codegen's proof that two fat reference parameters are
// distinct stacks -- need a single exact alternative, and fall back to the
// conservative path without one. A reference read out of a container is
// re-rooted by ReadBackRoot: the container bounds the lifetime, and the
// candidate variables of the enclosing scope that can hold the pointee by
// value are its alternatives, exact where one is a variable's own storage
// (§9.5). Other deliberate v1 rules (now part of the spec, §9.2/§9.5): a
// read-back reference is writable regardless of its original provenance
// (writability launders through storage -- the language's const-cast
// loophole); a reference variable commits to one root depth for its whole
// life. A function's result is rooted as a branch value is: each call maps
// the roots its returns give to its own arguments.
// Inside a recursive cycle (§7.8) a reference may be stored
// only if it is rooted at a global, at a local of an enclosing function
// outside the cycle, at a pool parameter -- a parameter root class whose
// members are all references to resizable-class values, which no cycle
// function holds across a call into its cycle (VarDef::poolclass,
// JoinCycle), and which a back edge must then pass exactly as the entry call
// did (ValidatePoolArgs) -- or at a threaded parameter: a class of exactly
// rooted references or slices that every call into the cycle passes on as
// it was first given, from storage outside every cycle (ThreadedClass). A
// back edge may pass a threaded class something else, so a store relies on
// it, and that call is then an error at the store (ValidateThreadArgs). A
// cycle's *return* roots, shrinks, growths, stores and rebinds cannot come
// from its bodies' records while a back edge reaches a function before its
// body is checked through, so a cycle is checked in rounds: the first with
// back edges that have no effects and results pointing nowhere yet, each
// later one reading what the round before recorded, until nothing recorded
// changes (CheckSpecBody). Remaining conservatisms marked TODO: long-distance
// returns carry only global/static refs, and references rooted at a caller's
// fixed-size local that a call back into the cycle replaces are still
// pass-down-only inside a cycle.
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

// Whether a `break` in a loop's body gives the loop a value: one no construct
// inside the body is the target of instead (FindBreakScope) -- a nested
// loop, a `block` or a function value. A `for`'s iterated expression runs
// before its loop does.
inline bool BreaksWithValue(Node *body) {
    auto found = false;
    function<void(Node *)> walk = [&](Node *n) {
        if (found) return;
        if (auto b = Is<Break>(n); b && b->val) {
            found = true;
            return;
        }
        if (auto f = Is<ForLoop>(n)) {
            walk(f->iter);
            return;
        }
        if (Is<LoopExpr>(n) || Is<While>(n) || Is<EarlyBlock>(n) || Is<FunVal>(n)) return;
        n->Children(walk);
    };
    walk(body);
    return found;
}

// Whether a trailing expression can never supply a value: an `if` without a
// final `else`, or with a branch that cannot; a `match` with an arm that
// cannot; a `loop` none of whose `break`s carries a value; and a block
// ending in one of those. Callers that decide whether a body's tail is
// its result consult this first, so that such a tail -- inside `block { }`
// or a bare scope or not -- is a statement, checked as one, rather than a
// value the construct has no way to supply. A block ending in a statement
// is not one: its end may be unreachable (`if a { 1 } else { return 2; }`).
inline bool IsValuelessTail(const Node *n) {
    if (!n) return false;
    if (auto fi = Is<IfExpr>(n))
        return !fi->elseb || IsValuelessTail(fi->thenb) || IsValuelessTail(fi->elseb);
    if (auto m = Is<MatchExpr>(n)) {
        for (auto &arm : m->arms)
            if (IsValuelessTail(arm.body)) return true;
        return false;
    }
    if (auto l = Is<LoopExpr>(n)) return !BreaksWithValue(l->body);
    if (auto b = Is<Block>(n)) return IsValuelessTail(b->tail);
    if (auto e = Is<EarlyBlock>(n)) return IsValuelessTail(e->body);
    return false;
}

// A control construct, whose value is what one of its branches gives (§6.4).
inline bool IsBranchConstruct(const Node *n) {
    return Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<Block>(n) || Is<EarlyBlock>(n) ||
           Is<LoopExpr>(n);
}

struct TypeCheck {
    Ast &ast;

    // (Val, the checked value of an expression, lives in ast.h: node Check
    // overrides return it.)

    // What a temporary holds (TempContents): where its contents point, and
    // the container whose store events describe them exactly, if any.
    struct ReadBack {
        Roots roots;
        VarDef *from = nullptr;
    };

    // An assignable/addressable path: Ident, field, or element. Its
    // provenance names the storage's owner, and `writable` whether the whole
    // path admits writes.
    struct LVal : Prov {
        TypeExpr *type = nullptr;    // The location's own type (varints undecoded).
        VarDef *var = nullptr;       // Set when the path is a bare variable name.
        // The location is a `let` binding or field itself, which is not
        // assigned as a whole (§4.4); its contents are another matter.
        bool letbound = false;
        string_view letname;
        VarDef *copyof = nullptr;    // The path starts at a by-value binding (VarDef::copybind).
        bool fromstorage = false;    // Reached by a field or element step, so a
                                     // reference read out of it is a read-back (§9.5).
        bool throughref = false;     // Reached by crossing a reference: a slice
                                     // loaded out of it is the slot's (SlotView).
        // The location itself is a field or an element, not the pointee of
        // a reference, which may be a variable: what is loaded out of it
        // was stored there (Prov::slotread).
        bool isslot = false;
        bool fotail = false;         // A frame object's resizable tail: has its own header (C.2).
        bool isvarint = false;       // varint field: read-only refs, not assignable.
        // A path into a temporary that has not crossed a reference: a
        // reference or slice loaded out of it points where the temporary's
        // holder root says (TempContents), not into the temporary.
        bool intemp = false;
        ReadBack contents;
    };

    // What a nested function's body can name outside its own scopes (§7.5),
    // as its declaration sees it: the variables in scope there, innermost
    // first, each with the index it holds in `vars` while it is in scope;
    // and every function declared in the blocks around the declaration,
    // before or after it, then those the declaring body sees from outside,
    // each with the environment it is declared in. Kept after the declaring
    // scope ends, for a function whose value leaves it.
    struct DeclSite {
        vector<pair<VarDef *, int>> vars;
        vector<pair<SFunction *, FnSpec *>> fns;
        int scope = 0;               // The declaring scope, and its Scope::serial.
        int serial = 0;
    };

    // A function body's exits as its caller's flow sees them (§4.4): the
    // variables outside it that it can name and were unassigned when its
    // check began -- the ones it may assign -- whether each is assigned at
    // every exit reached so far and may be at any of them (NoteExit), and
    // whether it may have been when the check began.
    struct BodyExits {
        vector<VarDef *> vars;
        vector<bool> assigned, maybeassigned, maybeonentry;
        bool reached = false;
    };

    // One level of the compile-time call path.
    struct Frame {
        SFunction *sf = nullptr;     // Null for the global-initializer frame.
        FnSpec *spec = nullptr;      // Owner of locals declared here (null at globals).
        FnSpec *lexspec = nullptr;   // Lexical env: generic bindings, parent of nested fns.
        int lexframe = -1;           // Frame index for free-variable lookup chains.
        // A nested function's declaration site, which its body names things
        // through instead of the lexical parent frame's scopes as they are now.
        DeclSite *decl = nullptr;
        int scopebase = 0;           // First scope index belonging to this frame.
        int varbase = 0;             // First var index belonging to this frame.
        Line callline;               // Call site, for instantiation chain diagnostics.
        bool isfunval = false;
        bool isdefault = false;    // Lexically isolated, but caller values remain live.
        // A parameter default's (§7.1): the function, which parameter, and
        // for a nested function its declaration site, whose names the
        // default may not use (DefaultScopeName).
        SFunction *defaultfn = nullptr;
        int defaultparam = -1;
        DeclSite *defaultsite = nullptr;
        // A field default's (§3.2): the declaration's field, and the literal
        // taking it, which builds a `defaultowner` (LitTypeStr).
        const Field *defaultfield = nullptr;
        const StructLit *defaultlit = nullptr;
        TypeExpr *defaultowner = nullptr;
        // Calls into a recursive cycle this frame has been inside so far, and
        // where it made the latest (JoinCycle).
        int cyclecalls = 0;
        Line cyclecall;
        // A specialization's body being checked (CheckSpecBodyOnce): its exits.
        BodyExits *exits = nullptr;
    };

    enum ScopeKind { SK_PLAIN, SK_FN, SK_LOOP, SK_BLOCK };
    // Snapshot of assigned/narrowed for the variables the code between a
    // save and its restore can name (NamedFrames): those of the current
    // frame, of the frames it is lexically nested in, and of the frames the
    // function values it can call were written in, and the globals'
    // narrowing. The other frames on the call path hold variables no code
    // checked meanwhile can name, so a deep call path does not copy them all
    // at every branch.
    struct VarFlow {
        bool assigned = false;
        bool maybeassigned = false;
        TypeExpr *narrowed = nullptr;
        bool operator==(const VarFlow &) const = default;
    };
    struct FlowEntry {
        int index;                            // Into vars, ascending.
        VarDef *var;                          // The variable occupying that index.
        VarFlow state;
        bool operator==(const FlowEntry &) const = default;
    };
    struct FlowState {
        vector<FlowEntry> locals;
        vector<pair<VarDef *, TypeExpr *>> globals;
        bool reachable = true;
        bool operator==(const FlowState &) const = default;
    };
    struct Scope {
        int kind = SK_PLAIN;
        int serial = 0;              // Unique per scope opened: tells a later one at its index apart.
        int varbase = 0;
        int fnbase = 0;              // Into localfns.
        Node *node = nullptr;        // The loop / `block` construct for SK_LOOP/SK_BLOCK.
        TypeExpr *breaktype = nullptr;
        Val breakvalue;             // Roots and permissions of all valued exits.
        TypeExpr *breakexpected = nullptr;   // The construct's expected value type.
        bool onargpath = false;     // The construct is on argpath, and so are its breaks' values.
        bool onjoinpath = false;    // The same of joinpath.
        bool inreturn = false;      // The construct's value is returned, as are its breaks' values.
        bool hasbreak = false;
        bool valuelessbreak = false;
        // For SK_LOOP/SK_BLOCK: the join of the states at the breaks out of
        // it reached so far, which the construct exits in besides its own
        // exit, if it has one (NoteBreak, JoinBreakFlow).
        FlowState breakflow;
        bool breakflows = false;
        // For SK_LOOP: the join of the states at its back edges so far (the
        // end of the body, every continue), which the next iteration starts
        // in (CheckLoopPasses).
        FlowState backedge;
        bool backedges = false;
    };

    vector<Frame> frames;
    vector<Scope> scopes;
    // The open blocks, outermost first, and the statement each is at: a
    // shrink asks whether a holder is mentioned again after that point
    // (UsedAfter, §5.1).
    struct BlockPos {
        Block *block = nullptr;
        size_t idx = 0;             // The statement being checked; stmts.size() at the tail.
        int scopeidx = 0;           // The block's own scope.
    };
    vector<BlockPos> blockpos;
    // Checks a block's statements, keeping blockpos current; the tail is
    // the caller's to check, inside the same BlockScope.
    struct BlockScope {
        TypeCheck &tc;
        BlockScope(TypeCheck &_tc, Block *b) : tc(_tc) {
            tc.blockpos.push_back({ b, 0, (int)tc.scopes.size() - 1 });
        }
        ~BlockScope() { tc.blockpos.pop_back(); }
    };
    void CheckStmts(Block *b);
    bool MentionsName(Node *n, string_view name, set<SFunction *> &seen);
    bool UsedAfter(VarDef *v);

    // The node being checked and every node it is nested in, innermost last,
    // from the statement being checked outward (CheckStmt, CheckV,
    // CheckLValue), each with how many of its operands have been evaluated
    // so far (`pos`), which is where the operand being checked stands. What
    // a rule at some point of a statement asks about the rest of the
    // statement it derives from this and from the values recorded per node
    // (`nodevals`): the values evaluated before that point and still live --
    // the earlier operands of every node on the path, which their parents
    // consume after the point (HeldOperands, §5.1, §5.2) -- and the parts
    // that run after it (LaterOperands, UsedAfter). Nothing is recorded
    // along the way, so no construct can be left out.
    struct PathEntry {
        Node *node;
        int idx;     // Its index among its parent's operands, or -1.
        int pos;     // Operands evaluated so far.
        // A call resolving its overload: its operands are checked for their
        // types, and the values it keeps are those its phase 2 checks.
        bool discovering = false;
        // The frame checking it, whose variables and lexical parents' its
        // names are (UsedAfter).
        int frame = 0;
    };
    struct Discovering {
        TypeCheck &tc;
        Node *node;
        Discovering(TypeCheck &t, Node *n) : tc(t), node(n) { Set(true); }
        ~Discovering() { Set(false); }
        void Set(bool on) {
            if (!tc.cur.nodepath.empty() && tc.cur.nodepath.back().node == node)
                tc.cur.nodepath.back().discovering = on;
        }
    };
    struct NodeScope {
        TypeCheck &tc;
        Node *node;
        bool pushed;
        NodeScope(TypeCheck &t, Node *n) : tc(t), node(n), pushed(t.Descend(n)) {}
        ~NodeScope() { if (pushed) tc.Ascend(node); }
    };
    bool Descend(Node *n);
    void Ascend(Node *n);
    // The checked value of every expression that points somewhere, by node.
    unordered_map<Node *, Val> nodevals;
    void RecordVal(Node *n, const Val &v) {
        if (!v.None() || v.holderset) nodevals[n] = v;
    }
    // How a parent consumes an operand after its later operands ran: as its
    // value (a reference or slice, a holder's references), as a view of an
    // array's elements too (`==`), as the elements it indexes or slices, as
    // a builtin's receiver, as the location an assignment writes, or as the
    // sequence a `for` walks while its body runs.
    enum HoldKind { HK_NONE, HK_VALUE, HK_VIEW, HK_ELEMS, HK_RECEIVER, HK_LOCATION,
                    HK_SEQUENCE };
    template<typename F> void ForOperands(Node *n, F f);
    int OperandIndex(Node *parent, Node *child);
    // A value evaluated earlier in the statement and still live: a reference
    // or slice, a view of an array's elements, a holder's references, or an
    // assignment's location, which is only the slot the value lands in (what
    // the slot holds now is overwritten, so a shrink does not reach it).
    struct Held {
        Node *node;
        Val v;
        bool location = false;
        // The builtin (print, str, format) rendering `node`, which runs the
        // format overloads of its parts meanwhile.
        const char *render = nullptr;
        // `v` views the elements of `node`, which its parent reads.
        bool elems = false;
        // The `for` whose body runs meanwhile, which walks `node`, its
        // sequence, in place, or, where `reread`, loads the reference or
        // slice `node` is out of storage again on every iteration.
        ForLoop *loop = nullptr;
        bool reread = false;
        // `v` is a reference to what the rendering walks in place
        // (BodyState::renderwalks): not the path to a resizable, which a
        // shrink leaves in place, but its parts as the walk read them.
        bool inplace = false;
    };
    template<typename F> void HoldAs(Node *n, const Val &v, HoldKind kind, Node *parent,
                                     const char *render, F f);
    template<typename F> void HoldForSequence(ForLoop *fl, const Val &v, F f);
    template<typename F> void HeldOperands(F f);
    template<typename F> void LaterOperands(F f);
    template<typename F> void AfterHead(Node *n, Node *next, F f);
    vector<VarDef *> vars;                            // All in-scope variables, all frames.
    vector<pair<int, SFunction *>> localfns;          // Nested fns, with their scope index.
    int scopeserial = 0;
    deque<DeclSite> declsites;
    // The latest declaration site of each nested function, by the
    // environment it is declared in: a call specializes it there.
    map<pair<FnSpec *, SFunction *>, DeclSite *> declsiteof;
    bool reachable = true;
    // Checking what a return, or the body's tail, gives the caller: the
    // function's own locals move. The statements, conditions, scrutinees
    // and loops inside it are no part of that value; a valued block or loop
    // keeps the flag for its breaks' values (Scope::inreturn).
    bool inreturn = false;
    // Sets a flag for a scope; the value in force outside returns on exit,
    // an error's throw included.
    struct FlagScope {
        bool &flag;
        bool saved;
        FlagScope(bool &f, bool v) : flag(f), saved(f) { f = v; }
        ~FlagScope() { flag = saved; }
    };
    // The destination of the value under construction (for reference stores):
    // where the storage the value lands in may be (an lvalue's roots).
    struct Dest {
        Roots roots;
        bool varbind = false;  // A reference/slice variable itself: a binding, not a store.
        // The type of the storage the path to it reached, which the slots it
        // fills lie in (Prov::reached); null where it crossed no reference.
        TypeExpr *reached = nullptr;
        // A destination reached through a reference that points nowhere yet
        // (RefProvOf): it has no roots, unlike no destination at all.
        bool unknown = false;
        // The slot a reference to a slice names, which may be a slice
        // variable's own (StoreIntoSlot).
        bool slot = false;
        Dest() {}
        Dest(const Roots &r, bool vb = false, TypeExpr *re = nullptr)
            : roots(r), varbind(vb), reached(re), unknown(r.None()) {}
        // A variable's own storage.
        Dest(VarDef *vd, bool vb = false) : varbind(vb) { roots.Set(vd, true); }
    };
    Dest curdst;
    // Whether the value being checked lands in a typed slot -- a field, an
    // element, an annotated variable, an assignment target -- whose declared
    // constness a read-only reference or slice must match (§9.5). A call
    // argument or a return value is not one: a parameter's or result's
    // constness is inferred per instantiation.
    bool constslot = false;
    struct SlotScope {
        TypeCheck &tc;
        bool saved;
        SlotScope(TypeCheck &t, bool slot) : tc(t), saved(t.constslot) { tc.constslot = slot; }
        ~SlotScope() { tc.constslot = saved; }
    };
    // The destination in force while a scope runs; the enclosing one returns
    // on exit, an error's throw included.
    struct DestScope {
        TypeCheck &tc;
        Dest saved;
        DestScope(TypeCheck &t, Dest d) : tc(t), saved(t.curdst) { tc.curdst = d; }
        ~DestScope() { tc.curdst = saved; }
    };
    // Sentinel root outlived by everything: what a reference variable not
    // bound yet points at (RefRootOf). A temporary has a root of its own
    // (TempRoot).
    VarDef *temproot = nullptr;
    TypeExpr *fntype = nullptr;  // Shared type of function values.
    TypeExpr *u8slice = nullptr; // A slice of u8.
    TypeExpr *cu8slice = nullptr; // `const u8[:]`: the type of a string literal (§3.7).
    TypeExpr *nulltype = nullptr;  // Placeholder type of a bare null literal.

    // ------------------------------------------------------------------
    // Errors, with the compile-time instantiation chain (§7.7).

    string Where(Line l) {
        if (l.fileidx < 0 || l.fileidx >= (int)ast.sources.size()) return "?";
        return cat(ast.sources[l.fileidx].first, ":", l.line);
    }

    // Whether declared type t names type variable `name` where Subst replaces
    // it, so that the substituted type shows what the variable is bound to.
    static bool NamesGeneric(const TypeExpr *t, string_view name) {
        switch (t->kind) {
            case TY_GENERIC: return t->named->name == name;
            case TY_STRUCT:
                for (auto a : t->struc->args) if (NamesGeneric(a, name)) return true;
                return false;
            case TY_ENUM:
                for (auto a : t->enu->args) if (NamesGeneric(a, name)) return true;
                return false;
            case TY_ARRAY:   return NamesGeneric(t->arr->sub, name);
            case TY_SLICE:   return NamesGeneric(t->sub, name);
            case TY_REF:     return NamesGeneric(t->ref->sub, name);
            case TY_VARIANT: return NamesGeneric(t->var->adt, name);
            default:         return false;
        }
    }

    static constexpr int MAXCHAIN = 20;

    [[noreturn]] void Error(Line l, const string &msg) { ErrorIn(l, msg, InstantiationChain()); }

    // An error at code checked earlier, with the instantiation chain taken
    // there: how a check made once the whole program has been reports one.
    [[noreturn]] void ErrorIn(Line l, const string &msg, const string &chain) {
        for (auto &w : cur.pendingwarnings) if (!w.cast) PrintWarning(w);
        cur.pendingwarnings.clear();
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
        s += chain;
        throw CompileError { s };
    }

    string InstantiationChain() {
        string s;
        // A long chain shows its innermost and outermost instantiations.
        auto chain = 0, nth = 0;
        for (auto &f : frames) chain += f.sf && !f.isfunval;
        for (auto i = (int)frames.size() - 1; i > 0; i--) {
            auto &f = frames[i];
            if (f.defaultfn) {
                Append(s, "\n  in the default of parameter ",
                       f.defaultfn->params[f.defaultparam].name, " of ", f.defaultfn->qname,
                       ", for the call at ", Where(f.callline));
                continue;
            }
            if (f.defaultfield) {
                Append(s, "\n  in the default of field ", f.defaultfield->name, " of ",
                       LitTypeStr(f.defaultlit, f.defaultowner), ", for the construction at ",
                       Where(f.defaultlit->line));
                continue;
            }
            if (!f.sf || f.isfunval) continue;
            nth++;
            if (chain > MAXCHAIN && nth > MAXCHAIN / 2 && nth <= chain - MAXCHAIN / 2) {
                if (nth == MAXCHAIN / 2 + 1)
                    Append(s, "\n  ... ", chain - MAXCHAIN, " more instantiations");
                continue;
            }
            Append(s, "\n  in ", f.sf->isthread ? "thread_fn " : "fn ");
            if (!f.spec) Append(s, f.sf->qname, "()");
            else DumpInstance(s, f.sf, f.spec->argtypes, f.spec->litparams, f.spec->bindings,
                              f.spec->fnvals);
            Append(s, " instantiated from ", Where(f.callline));
        }
        return s;
    }

    // A specialization as a diagnostic names it. The argument types show the
    // bindings of type variables a parameter type names; the others are
    // listed, and so are the bound function values (`size<T = f64>()`,
    // `apply<F = wide>(i64)`), or distinct specializations would print alike.
    void DumpInstance(string &s, SFunction *sf, const vector<TypeExpr *> &argtypes,
                      const vector<int> &litparams,
                      const vector<pair<string_view, TypeExpr *>> &bindings,
                      const vector<pair<string_view, FnValBind>> &fnvals) {
        s += sf->qname;
        auto listed = false;
        auto item = [&](string_view n) {
            Append(s, listed ? ", " : "<", n, " = ");
            listed = true;
        };
        for (auto &g : sf->generics) {
            for (auto &[n, fb] : fnvals) {
                if (n != g.name) continue;
                item(n);
                auto fv = fb.fv;
                if (fb.named) s += fb.named->qname;
                else if (fv) Append(s, "{block at ", Where(fv->line), ":", fv->col, "}");
                else s += "?";
            }
            auto named = false;
            for (auto &p : sf->params) named |= p.type && NamesGeneric(p.type, g.name);
            if (named) continue;
            for (auto &[n, t] : bindings) {
                if (n != g.name) continue;
                item(n);
                DumpShort(s, t);
            }
        }
        if (listed) s += ">";
        s += "(";
        for (size_t j = 0; j < argtypes.size(); j++) {
            if (j) s += ", ";
            for (auto li : litparams) if (li == (int)j) s += "literal ";
            DumpShort(s, argtypes[j]);
        }
        s += ")";
    }

    // A type as a diagnostic shows it: cut short past any useful length.
    void DumpShort(string &s, const TypeExpr *t) {
        auto limit = s.size() + 200;
        t->Dump(s, limit);
        if (s.size() > limit) {
            s.resize(limit);
            s += "...";
        }
    }

    [[noreturn]] void Error(const Node *n, const string &msg) { Error(n->line, msg); }

    // A warning's text, about node `at` as the source has it; or, where
    // `cast` is set, a check's verdict on that explicit cast as the source
    // has it (CastVerdict): redundant, `text` saying why, with the cast it
    // was judged at, which has to stay; or not.
    struct Warning {
        string text;
        const Node *at = nullptr;
        const AsCast *cast = nullptr;
        bool redundant = false;
        const AsCast *dependson = nullptr;
    };
    // A check that phase 2 of a call repeats leaves its warnings to that
    // repetition (BindBranchesByRef).
    bool quiet = false;
    void Warn(const Node *n, const string &msg) {
        if (quiet) return;
        Warning w { cat(Where(n->line), ": warning: ", msg, "\n"), n->Origin() };
        if (WarningsHeld()) {
            cur.pendingwarnings.push_back(std::move(w));
            return;
        }
        PrintWarning(w);
    }
    // Warnings wait while a check may yet be repeated: a loop's pass
    // (CheckLoopPasses), or a construct's first check of its branches
    // (CheckJoin). The repetition's warnings are the ones that stand. A
    // verdict on a cast waits for all of that cast's checks
    // (ReportRedundantCasts).
    bool WarningsHeld() { return !cur.looppasses.empty() || cur.joinprobes > 0; }
    void FlushWarnings() {
        for (auto &w : cur.pendingwarnings) {
            if (w.cast) castverdicts.push_back(std::move(w));
            else PrintWarning(w);
        }
        cur.pendingwarnings.clear();
    }
    // Other checks are repeated with every check's warnings standing, which
    // gives the same ones again: a call's argument, checked for its overload
    // and again against its parameter (ResolveCall), with every call nested
    // in it; a receiver, checked for its call and again by the builtin taking
    // it (CheckPrintable); a cycle's body, in every round (CheckSpecBody).
    // Clones of one node give its warnings as well: a specialization's body,
    // and a default or a function value's body at each check of a call. A
    // warning prints once for the node as the source has it, where a check
    // first gives it.
    set<pair<const Node *, string>> warned;
    void PrintWarning(const Warning &w) {
        if (warned.insert({ w.at, w.text }).second) fputs(w.text.c_str(), stderr);
    }

    string TypeStr(const TypeExpr *t) {
        // No program names the type of a [] nothing gave an element type; it
        // shows as the literal.
        if (IsUntypedEmptyArray(t)) return "[]";
        string s;
        t->Dump(s);
        return s;
    }

    // The type struct or variant literal sl builds, whose value is a `selft`
    // (an enum for a variant in fixed mode), as a diagnostic names it.
    string LitTypeStr(const StructLit *sl, const TypeExpr *selft) {
        auto s = TypeStr(selft);
        if (selft->kind == TY_ENUM) Append(s, ".", sl->variant->name);
        return s;
    }

    // ------------------------------------------------------------------
    // Constant expression evaluation: array sizes, match arm bounds, literal
    // fit. Understands literals, arithmetic, and `let` globals.

    // A size, fill count or match pattern `at` (a `what`), which takes the
    // named constants its expression names at their initializers' values
    // (RelyOnConstant). `named`: the one the expression itself names whose
    // initializer is being evaluated.
    struct ConstUse {
        Node *at;
        const char *what;
        VarDef *named = nullptr;
    };

    bool ConstIntValue(Node *n, Val &v, bool &literal, set<VarDecl *> &visiting,
                       ConstUse *use = nullptr);

    bool ConstInt(Node *n, int64_t &v, ConstUse *use = nullptr) {
        Val value;
        bool literal;
        set<VarDecl *> visiting;
        if (!ConstIntValue(n, value, literal, visiting, use)) return false;
        v = value.ival;   // Keep all 64 bits: u64 match patterns also use this path.
        return true;
    }

    int64_t ConstIntOrError(Node *n, const char *context) {
        int64_t v;
        ConstUse use { n, context };
        if (!ConstInt(n, v, &use))
            Error(n, cat("constant integer expression expected for ", context));
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

    // What a type is that takes no bytes of a layout, though C has no empty
    // arrays or structs and gives it some (C.2): a fixed array, or a limited
    // one of static capacity, with no element slots, or a variant type with
    // no fields. Null for any other type. Never a field or an element (§3.4).
    const char *ZeroSizeKind(TypeExpr *t) {
        if (t->kind == TY_VARIANT && EmptyLayout(t->var->variant->fields))
            return "a variant type with no fields";
        if (t->kind == TY_ARRAY && (t->arr->akind == A_FIXED || t->arr->akind == A_LIMITED) &&
            ArraySize(t->arr) == 0)
            return "a zero-length array";
        return nullptr;
    }
    void NoZeroSizeElement(TypeExpr *t, Line l) {
        if (auto zs = ZeroSizeKind(t))
            Error(l, cat("an element cannot be ", zs, ": ", TypeStr(t), " (§3.4)"));
    }

    // The names in sizes, capacities, fill counts and match patterns, which
    // ConstIntValue takes as globals, checked where they are written.
    void ConstName(Ident *id, const char *what);
    void ConstNames(Node *n, const char *what);
    void ConstNamesIn(TypeExpr *t);
    void SignatureNames(const vector<Param> &params, const vector<TypeExpr *> &rets,
                        const vector<GenericParam> &generics);
    void TypeParamSizes(TypeExpr *t, const vector<GenericParam> &generics);
    int64_t FillCount(Node *n);

    // Calls f(id, what) for each name constant expression n takes, where
    // ConstIntValue would look it up: as an operand of the arithmetic it folds.
    template<typename F> void ConstExprNames(Node *n, const char *what, F f) {
        if (auto id = Is<Ident>(n)) {
            f(id, what);
        } else if (auto u = Is<Unary>(n)) {
            ConstExprNames(u->child, what, f);
        } else if (auto b = Is<Binary>(n)) {
            ConstExprNames(b->left, what, f);
            ConstExprNames(b->right, what, f);
        }
    }

    // The same for the sizes and capacities written in type t. An alias's
    // use stands for the alias's type, written where the alias is declared.
    template<typename F> void SizeNames(TypeExpr *t, F f) {
        if (t->aliasuse) return;
        switch (t->kind) {
            case TY_ARRAY:
                if (t->arr->sizeexpr)
                    ConstExprNames(t->arr->sizeexpr,
                                   t->arr->akind == A_LIMITED ? "array capacity" : "array size", f);
                SizeNames(t->arr->sub, f);
                return;
            case TY_SLICE: SizeNames(t->sub, f); return;
            case TY_REF: SizeNames(t->ref->sub, f); return;
            case TY_STRUCT: for (auto a : t->struc->args) SizeNames(a, f); return;
            case TY_ENUM: for (auto a : t->enu->args) SizeNames(a, f); return;
            case TY_VARIANT: SizeNames(t->var->adt, f); return;
            default: return;
        }
    }

    // ------------------------------------------------------------------
    // Type equality on concrete (post-substitution) types.

    bool TypeEq(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (a->kind != b->kind || a->cq != b->cq) return false;
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

    // Equal but for the constness of the type itself: what a parameter or
    // result accepts, since its constness is inferred per instantiation, and
    // what a copy of a value drops (§9.5). Constness nested deeper is part
    // of the type.
    bool TopConstEq(TypeExpr *a, TypeExpr *b) {
        if (a->cq == b->cq) return TypeEq(a, b);
        TypeExpr ta = *a, tb = *b;
        ta.cq = tb.cq = false;
        return TypeEq(&ta, &tb);
    }

    bool TypeArgsEq(vector<TypeExpr *> &a, vector<TypeExpr *> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) if (!TypeEq(a[i], b[i])) return false;
        return true;
    }

    // Generic bindings bind each name once, in whatever order explicit type
    // arguments and inference produced them.
    bool BindingsEq(vector<pair<string_view, TypeExpr *>> &a,
                    vector<pair<string_view, TypeExpr *>> &b) {
        if (a.size() != b.size()) return false;
        for (auto &[n, t] : a) {
            auto same = false;
            for (auto &[m, u] : b)
                if (m == n) { same = TypeEq(t, u); break; }
            if (!same) return false;
        }
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

    // The innermost type parameter of the name in scope, as an expression
    // names it (§11.1): one bound to a type returns the type, one bound to a
    // function value sets `fn`.
    TypeExpr *LookupTypeParam(string_view name, const FnValBind *&fn) {
        fn = nullptr;
        for (auto sp = frames.back().lexspec; sp; sp = sp->lexparent) {
            for (auto &[n, fv] : sp->fnvals) if (n == name) { fn = &fv; return nullptr; }
            for (auto &[n, t] : sp->bindings) if (n == name) return t;
        }
        return nullptr;
    }

    // Substitutes generic parameter names in t using the current lexical
    // bindings; returns t itself when nothing changed. A `const T` keeps its
    // qualifier whatever T is bound to.
    TypeExpr *Subst(TypeExpr *t) {
        auto n = SubstRaw(t);
        return t->cq && !n->cq ? ast.ConstOf(n) : n;
    }

    TypeExpr *SubstRaw(TypeExpr *t) {
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
                    return ast.EnumOf(b->enu->en, b->enu->args, true, t->line);
                }
                return b;
            }
            case TY_STRUCT: {
                auto args = SubstArgs(t->struc->args);
                if (!args) return t;
                return ast.StructOf(t->struc->st, std::move(*args), t->line);
            }
            case TY_ENUM: {
                auto args = SubstArgs(t->enu->args);
                if (!args) return t;
                return ast.EnumOf(t->enu->en, std::move(*args), t->enu->varmode, t->line);
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
                auto n = ast.SliceOf(sub, t->line);
                n->cq = t->cq;
                return n;
            }
            case TY_REF: {
                auto sub = Subst(t->ref->sub);
                if (sub == t->ref->sub) return t;
                // There are no references to references (§3.8): `T?` of a
                // reference makes that reference nullable, as `X&?` does,
                // and `T&` is the reference itself, as `&` of a location
                // holding one gives the stored one, loaded if relative.
                if (sub->kind == TY_REF && t->ref->lenstorage < 0) {
                    if (!t->ref->optional) return LoadType(sub);
                    if (sub->ref->optional) return sub;
                    auto n = ast.NewType(TY_REF, t->line);
                    n->ref = ast.NewDetail<TypeRef>();
                    *n->ref = *sub->ref;
                    n->ref->optional = true;
                    n->cq = sub->cq;
                    return n;
                }
                auto n = ast.NewType(TY_REF, t->line);
                n->ref = ast.NewDetail<TypeRef>();
                *n->ref = *t->ref;
                n->ref->sub = sub;
                n->cq = t->cq;
                return n;
            }
            case TY_VARIANT: {
                auto adt = Subst(t->var->adt);
                if (adt == t->var->adt) return t;
                if (adt->kind != TY_ENUM)
                    Error(t->line, cat("variant type of non-ADT type ", TypeStr(adt)));
                auto name = t->var->name;
                auto found = adt->enu->en->FindVariant(name);
                if (!found)
                    Error(t->line, cat("enum ", adt->enu->en->name, " has no variant named ", name));
                return ast.VariantOf(adt, name, found, t->line);
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
    void BuildVariant(EnumInst *inst, size_t vi);
    void LimitNestedInsts(TypeExpr *t);
    // The field runs of a nominal type (ast.h FieldRun), instantiating it
    // as needed; empty for every other kind.
    vector<FieldRun> FieldRuns(TypeExpr *t);
    // Whether `f` holds of some field type of t, pads skipped, and the same
    // for each of them; instantiating t as FieldRuns does.
    template<typename F> bool AnyField(TypeExpr *t, F f) { return AnyFieldOf(FieldRuns(t), f); }
    template<typename F> void EachField(TypeExpr *t, F f) { EachFieldOf(FieldRuns(t), f); }
    // A default is checked where it runs, in a frame of its own (§3.2,
    // §7.1): it names what its declaration would at the top level, with
    // `env`'s type bindings, and none of the locals of the code it runs in,
    // whose values stay live and whose effects it shares. A parameter's
    // default gives the call it is part of as `callline`; a field default's
    // frame names none and is part of the construction's frame (JoinCycle).
    struct DefaultScope {
        TypeCheck &tc;
        DefaultScope(TypeCheck &t, FnSpec *env, Line callline);
        ~DefaultScope() { tc.frames.pop_back(); }
    };
    // The frames of the defaults the code checked now runs in, innermost
    // first: each default's, then that of the construction or call it runs
    // for; from the body of a function value or nested function, the frame
    // it was written in; up to the body of a top-level function. Each check
    // of a default checks anew the defaults of the constructions and calls
    // in it, and the functions and blocks written in it, so taking a default
    // of this chain again would be checked without end, as it would run
    // (§3.2, §7.1). Through a top-level function's body it is a recursion,
    // which the cache of specializations and their bound (MAXNESTEDSPECS)
    // keep finite (§7.8).
    template<typename F> void EachDefaultInPlace(F f) {
        for (auto i = (int)frames.size() - 1; i > 0;) {
            auto &fr = frames[i];
            if (fr.isdefault) {
                f(fr);
                i--;
            } else if (fr.lexframe >= 0) i = min(fr.lexframe, i - 1);
            else break;
        }
    }
    Val CheckDefaultInit(Node *&n, TypeExpr *ft, TypeExpr *owner, const StructLit *sl,
                         const Field &field);
    Call *DefaultCall(TypeExpr *t, Line line);
    Node *DefaultValue(TypeExpr *t, Line line);
    SizeClass ClassOf(TypeExpr *t);
    bool IsFlat(TypeExpr *t);
    bool HoldsPlainRef(TypeExpr *t);
    bool HasDefault(TypeExpr *t, string &why);

    // Serialization (docs/design/serialization.md): what to_bytes will write
    // out, and the stricter question of what from_bytes can verify its way
    // back to. Both walk the element type; `why` explains a refusal.
    bool ImageSafe(TypeExpr *t, string &why);
    bool VerifiableElem(TypeExpr *t, TypeExpr *elem, string &why);

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
    static bool IsUntypedEmptyArray(const TypeExpr *t);
    void NoUntypedEmptyArray(const Val &v, Node *at, string_view use);
    TypeExpr *PendingElemFrom(const Val &av, Node *at);
    TypeExpr *PendingElemFromSeq(const Val &av, Node *at);
    void CompletePending(TypeExpr *arrt, TypeExpr *elem, Line l);
    void RequireComplete(TypeExpr *t, Line l);
    void ValidateType(TypeExpr *t, Line l, int pos);

    // A reference or slice only refers to its pointee, so building a struct
    // or enum instance leaves its fields' pointees until the outermost
    // instance being built is done: an instance still being built is then
    // always one the type at hand contains by value (§3.4), never one a
    // reference leads back to, as `enum L { Nil, Cons { next: L? } }` does.
    int typesbuilding = 0;
    // The types whose instances led to the one being built, outermost
    // first: those being built, and for a pointee that waited, the ones that
    // were when it was met (LimitNestedInsts).
    vector<TypeExpr *> typechain;
    struct LaterPointee { TypeExpr *t; Line l; vector<TypeExpr *> chain; };
    vector<LaterPointee> laterpointees;
    void BeginTypeBuild() { typesbuilding++; }
    void EndTypeBuild();
    void ValidatePointee(TypeExpr *t, Line l);

    // ------------------------------------------------------------------
    // Scopes, variables, and flow state (definite assignment + optional
    // narrowing, merged at control-flow joins).

    void PushScope(int kind, Node *node = nullptr);
    void PopScope();

    int CurDepth() { return (int)scopes.size(); }
    static int Depth(const VarDef *v) { return RootDepth(v); }
    // One variable's own storage, as a value rooted there exactly.
    static Roots RootsOf(VarDef *v) {
        Roots r;
        r.Set(v, true);
        return r;
    }

    // The storage of a temporary made here (a call's result, a literal): it
    // lasts until the statement being checked ends, or the block whose tail
    // value this is, so its depth is one past the current scope. It
    // outlives the variables of the scopes that statement opens -- a `for`
    // body over it, say -- and not the ones the statement itself declares
    // (§9.2).
    VarDef *TempRoot() {
        auto t = ast.NewVarDef();
        t->name = "<temporary>";
        t->istemp = true;
        t->depth = CurDepth() + 1;
        return t;
    }
    static bool IsTemp(VarDef *v) { return v && v->istemp; }

    // The root of the reference a variable holds -- one class root for a
    // parameter, else the innermost of its binding's; a null-initialized
    // optional has no commitment yet and reads as the temp sentinel, which no
    // store outlives (conservative).
    VarDef *RefRootOf(VarDef *vd) { return vd->refrootknown ? vd->ref.Root() : temproot; }
    // Every root the reference a variable holds may have (§9.2): none yet
    // where it is unbound in a discovery pass of a loop (RefProvOf), and
    // for a global `var` in a function's body all it can be given
    // (GlobalVarRead).
    Roots RefRootsOf(VarDef *vd) {
        if (BoundAnywhere(vd) && CurRealFrame().spec) return GlobalVarRead(vd);
        if (vd->refrootknown) return vd->ref;
        Roots r;
        if (UnboundIsBottom()) r.SetUnknown();
        else r.Set(temproot, false);
        return r;
    }

    bool ContainsGrowShrink(TypeExpr *t);
    TypeExpr *ResizableArrayIn(TypeExpr *t);
    bool IsGrowShrinkRoot(VarDef *r);
    bool GrowShrinkCanHold(VarDef *r, TypeExpr *of);
    bool BoundReachesGrowShrink(VarDef *r, TypeExpr *of, bool byteview);
    bool IntoGrowShrink(const Prov &v, VarDef *root, TypeExpr *t, bool holder);
    VarDef *GrowShrinkTaint(const Prov &p, TypeExpr *t, bool *reach = nullptr);
    VarDef *StoredIntoGrowShrink(const Val &v, const Roots &roots, TypeExpr *t, bool holder,
                                 bool *reach);
    string NeverStoredError(VarDef *root, bool may, bool reach);
    string ReachedThroughStr(VarDef *root);
    bool CycleStorable(VarDef *r);
    bool CycleStorable(const Roots &r);
    bool MayBeViewed(VarDef *r);
    bool Viewable(TypeExpr *t);
    void GrowShrinkElems(TypeExpr *t, vector<TypeExpr *> &out);
    bool GrowShrinkContains(TypeExpr *t, TypeExpr *of);
    bool RefMayPointInto(VarDef *v, VarDef *root, bool growonly = false,
                         TypeExpr *bound = nullptr);
    bool ClassReadMayPointInto(VarDef *cls, VarDef *root, TypeExpr *bound);
    bool RefMayRetarget(VarDef *v, VarDef *root);
    bool SlotReadable(TypeExpr *t);
    bool HeldRefsMayPointInto(VarDef *v, const Prov &p, TypeExpr *t, VarDef *root,
                              TypeExpr *bound, bool growonly);
    Prov SlotView(const Prov &p, TypeExpr *slice);
    void BindProv(VarDef *vd, const Prov &p);
    void BindRefProvenance(VarDef *vd, const Val &v);
    bool BoundAnywhere(VarDef *vd);
    Prov GlobalVarRead(VarDef *vd);
    Prov RefProvOf(VarDef *vd);
    VarDef *ResetLocal(VarDef *previous);
    VarDef *NewVar(string_view name, TypeExpr *type, Line l, bool isvar,
                   VarDef *previous = nullptr);
    VarDef *LookupVar(string_view name, string_view ns, Node *use = nullptr);
    const char *ScopeNameKind(string_view name);

    // The variables the body frame fi checks can name outside its own
    // scopes, innermost first: a nested function's are those in scope where
    // it is declared, a function value's those of the frame it is written
    // in as they are at the call it is written at, then those that frame
    // names outside its own. f gets each with its index in `vars` while in
    // scope, and the frame whose declaration site lists it (-1 for a frame's
    // current scopes); the walk stops when f returns true, and returns
    // whether it did.
    template <typename F> bool ForOuterVars(int fi, F f) {
        for (;;) {
            auto &fr = frames[fi];
            if (fr.decl) {
                for (auto [v, i] : fr.decl->vars) if (f(v, i, fi)) return true;
                return false;
            }
            auto p = fr.lexframe;
            if (p < 0) return false;
            for (auto i = frames[p + 1].varbase - 1; i >= frames[p].varbase; i--)
                if (f(vars[i], i, -1)) return true;
            fi = p;
        }
    }

    // The same for the nested functions it can call, in the order a name
    // resolves to them, each with the environment it is declared in.
    template <typename F> bool ForOuterFns(int fi, F f) {
        for (;;) {
            auto &fr = frames[fi];
            if (fr.decl) {
                for (auto [sf, env] : fr.decl->fns) if (f(sf, env)) return true;
                return false;
            }
            auto p = fr.lexframe;
            if (p < 0) return false;
            for (auto i = (int)localfns.size() - 1; i >= 0; i--) {
                auto [si, sf] = localfns[i];
                if (si >= frames[p].scopebase && si < frames[p + 1].scopebase &&
                    f(sf, frames[p].lexspec))
                    return true;
            }
            fi = p;
        }
    }

    bool InScope(VarDef *v, int idx) { return idx < (int)vars.size() && vars[idx] == v; }
    bool ScopeEnded(const DeclSite &d);
    void DeclareLocalFn(FnDecl *fd);
    string_view CurNs();
    int FrameOfSpec(FnSpec *sp);
    int LexFrame(FnSpec *env);
    int LexFrame(FnSpec *env, SFunction *sf);
    int LexFrame(const FnValBind &fb);
    int FrameOfScope(int s);
    bool NamesFrame(int fi, int target);
    FnSpec *NamedSpec(FnSpec *env);
    SFunction *LookupLocalFn(string_view name);

    FlowState SaveFlow();
    void RestoreFlow(const FlowState &f);
    // The frames whose variables code in frame fi can name, ascending: it,
    // the ones it is lexically nested in, the global initializers', and
    // those the function values bound in `spec`, or in the specialization
    // of any of those frames, were written in (which calling one reaches).
    vector<int> NamedFrames(int fi, FnSpec *spec);
    // The variables of those frames, in vars order.
    template<typename F> void EachNamedVar(int fi, FnSpec *spec, F f) {
        for (auto k : NamedFrames(fi, spec)) {
            auto end = k + 1 < (int)frames.size() ? frames[k + 1].varbase : (int)vars.size();
            for (auto i = frames[k].varbase; i < end; i++) f(i);
        }
    }
    void MergeFlow(const FlowState &a, const FlowState &b);
    void NarrowCond(Node *cond, bool sense);

    void KillNarrow(VarDef *vd) { vd->narrowed = nullptr; }
    // The join of two states over the variables in scope, without changing
    // the live checker state: a fact holds iff it
    // holds in every reachable one, and a variable may be assigned iff it
    // may be in any.
    FlowState JoinFlow(const FlowState &a, const FlowState &b);
    bool SameFlow(const FlowState &a, const FlowState &b);

    void ApplyCalleeRebinds(FnSpec *spec);
    // What a body reads of the variables outside its activation
    // (FnSpec::envreads): noted where the body first names one, and where a
    // callee it calls read one, and compared at every later call; and where
    // its check leaves them (FnSpec::envexits), which a call reusing it
    // leaves them as too.
    static EnvRead EnvReadOf(VarDef *vd);
    static bool EnvIs(const VarDef *vd, const EnvRead &r);
    void NoteEnvRead(VarDef *vd);
    void NoteCalleeEnvReads(FnSpec *callee);
    bool EnvUnchanged(const FnSpec *spec);
    void ShareCycleEnvReads(FnSpec *head);
    void RecordEnvExits(FnSpec *spec);
    void ReplayEnvExits(FnSpec *spec);
    // A body's caller finds the variables outside it assigned as all of the
    // body's exits agree, and maybe assigned where any may be (BodyExits):
    // its returns, the tail, a reachable end, and an exit of it that a
    // callee takes -- a function value's `return`, a `return from`. A
    // callee records such an exit (FnSpec::outerexits), which a call reusing
    // it takes again.
    void NoteExit(int tf, const OuterExit *added = nullptr);
    void ReplayOuterExits(FnSpec *spec);

    // A loop body is checked as many times as it takes for what it feeds
    // back to its head to settle (CheckLoopPasses): the roots its rebinds
    // give the variables declared outside it, the stores into their
    // contents (NoteFact), the narrowings and assignments its back edges
    // drop, and the assignments they may have made, which a `let` assigned
    // in the loop meets on the next pass (§4.4). Every pass but a settled
    // one is discovery: a reference variable read before any binding points
    // nowhere yet (RefProvOf), and every rule passes such a value by, as the
    // shrink scans pass the variable by; the pass after the last that
    // changed anything reads it as outside a loop would, and its errors
    // stand. A rule that errs where a value cannot point somewhere needs
    // every place the value may point, which a later pass can still add, so
    // it leaves its verdict to that pass (VerdictDeferred).
    struct LoopPass {
        int scopeidx = 0;          // The loop's scope.
        size_t firstbase = 0;      // storeevents.size() when the loop's first pass began.
        size_t eventbase = 0;      // The same for this pass.
        bool settled = false;
        bool changed = false;      // A fact fed back to this loop's head changed.
        bool sawunbound = false;   // A variable was read before any binding.
        bool deferred = false;     // A rule waits for the settled pass.
    };
    bool InDiscovery() {
        for (auto &lp : cur.looppasses) if (!lp.settled) return true;
        return false;
    }
    // A variable read before any binding: nowhere yet, in a discovery pass.
    bool UnboundIsBottom() {
        if (!InDiscovery()) return false;
        cur.looppasses.back().sawunbound = true;
        return true;
    }
    // A rule that errs where a value cannot point somewhere, in a discovery
    // pass: every loop still discovering runs a settled pass, which judges it.
    bool VerdictDeferred() {
        if (!InDiscovery()) return false;
        for (auto &lp : cur.looppasses) if (!lp.settled) lp.deferred = true;
        return true;
    }
    // A fact about vd that every loop it is declared outside of feeds back
    // to its head: the loops of the bodies this one was called from as much
    // as its own, since a nested function or a function value changes the
    // variables of the frames around it where its caller's loop reaches it.
    // A recursive cycle's round feeds it back to the next round the same
    // way, where vd lies outside the cycle's activations (CycleRound).
    // `floor`: only the loops and rounds from that scope on, those of the
    // activation a class root names (ActivationFloor).
    void NoteFact(const VarDef *vd, int floor = 0) {
        auto mark = [&](BodyState &b) {
            for (auto &lp : b.looppasses)
                if (Depth(vd) <= lp.scopeidx && lp.scopeidx >= floor) lp.changed = true;
        };
        mark(cur);
        for (auto b : outerbodies) mark(*b);
        for (auto &r : cyclerounds)
            if (Depth(vd) <= r.scopebase && r.scopebase >= floor) r.changed = true;
    }
    // Where the activation whose parameter's class `root` is begins: a store
    // of a value rooted there, into a variable outside that activation, is a
    // fact for its own loops and rounds; its callers learn it at the call,
    // which maps the class onto the arguments (ApplyCalleeStores). 0 for any
    // other root.
    int ActivationFloor(VarDef *root) {
        if (!IsClassRoot(root)) return 0;
        for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
            auto &f = frames[fi];
            if (f.isfunval || !f.spec) continue;
            auto &crs = f.spec->classroots;
            if (find(crs.begin(), crs.end(), root) != crs.end()) return f.scopebase;
        }
        return 0;
    }
    // A body being checked (CheckSpecBody), innermost last: the scopes
    // outside its activation, and whether a fact about a variable declared
    // in them changed during its latest round. Such a variable is the same
    // one at every level of a recursion, so a round that gave it a root may
    // have read it at the next level without that root.
    struct CycleRound {
        int scopebase = 0;
        bool changed = false;
    };
    vector<CycleRound> cyclerounds;
    // The store events a holder still carries: all of them, but for one
    // declared inside a loop only its current pass's, an earlier pass's
    // being a previous iteration's, whose value died with it (a copy made
    // then still leads to them, HolderMayPointInto's src).
    size_t LiveEventBase(const VarDef *holder) {
        for (auto i = cur.looppasses.size(); i-- > 0;)
            if (Depth(holder) > cur.looppasses[i].scopeidx) return cur.looppasses[i].eventbase;
        return 0;
    }
    // Whether the store event at index i was recorded by an earlier pass of
    // an enclosing loop: the next iteration reaches it.
    bool CarriedEvent(size_t i) {
        for (auto &lp : cur.looppasses) if (lp.firstbase <= i && i < lp.eventbase) return true;
        return false;
    }

    // ------------------------------------------------------------------
    // Small type constructors and views.

    TypeExpr *NarrowedRef(TypeExpr *t, Line l);
    TypeExpr *GrowU8Array(Line l);

    TypeExpr *LoadType(TypeExpr *t);
    TypeExpr *ValueType(TypeExpr *t);
    static bool ImplicitInt(IntStorage from, IntStorage to);
    TypeExpr *DerefType(TypeExpr *t);
    Val CheckIntAny(Node *n);
    bool HasRelRefT(TypeExpr *t, bool inpool = false);
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
    // What a parameter's references lead to is the caller's storage, known
    // here only by the parameter's root, which is then a bound as well.

    bool CanContain(TypeExpr *t, TypeExpr *of);
    void ReachedThroughRefs(TypeExpr *t, vector<TypeExpr *> &out);
    bool ReachesThroughRefs(TypeExpr *t, TypeExpr *of);
    void BoundReach(VarDef *r, vector<TypeExpr *> &out);
    TypeExpr *PointeeOf(TypeExpr *t);
    void VisibleVars(const function<void(VarDef *)> &f);
    void ShrinkScanVars(const function<void(VarDef *)> &f);
    bool StaticCanContain(TypeExpr *of);
    // `slots`: the pointee is the slot a reference names, which may be a
    // slice variable's own.
    Roots RootCandidates(TypeExpr *of, int d, bool globalsonly, bool writable,
                         bool slots = false);

    bool TempContents(const Val &v, ReadBack &contents);
    // `inplace`: the read is made where it is checked, which a loop's passes
    // check again, so what a holder of the activation's holds so far is what
    // it holds there (ClassCopyReadBack).
    Roots ReadBackRoot(TypeExpr *rt, const Roots &container, bool byteview = false,
                       const ReadBack *contents = nullptr, bool slotread = false,
                       bool inplace = false);
    bool ClassCopyReadBack(VarDef *h, Roots &out);
    string ReadBackWhy(const Roots &r);
    bool RootedAtReceiver(const Val &rv, const Val &av);
    void CheckRootedAtReceiver(Call *c, const char *op, const Val &rv, const Val &av,
                               const char *what, const char *sec);

    // ------------------------------------------------------------------
    // Lvalue paths: names, fields, elements, optionally through references.

    LVal CheckLValue(Node *n);
    LVal LValueBase(Node *n, bool cmpview = false);
    void NoTemporaryLiteral(Node *n, TypeExpr *t);
    void DerefLValue(LVal &lv, Node *at);
    void RequireAssigned(VarDef *vd, Node *at);
    void SliceProvenance(LVal &lv, Node *at);
    void ReadBackLVal(LVal &lv);
    bool SlotLoadWritable(TypeExpr *t, bool path);
    Val ContainerRead(LVal lv);
    void ResolveMemberLValue(LVal &lv, Dot *d);
    // Where a holder value's references point: what was derived for it, else
    // for a reference or slice its own roots, and for a by-value holder what
    // they bound (a temporary's outlive nothing).
    static Roots ContentsOf(const Val &v) {
        if (v.holderset) return v.contents;
        return !v.type || IsRefOrSlice(v.type) ? v.AsRoots() : Bounds(v);
    }
    // Where something that `at`'s roots only bound points: what a holder
    // lying there holds, everything stored there outliving the storage, or
    // what a callee read out of storage its argument only bounds. How that
    // storage was reached says nothing of where this came from, so no
    // alternative keeps a container it was read out of (RootAlt::from,
    // classread).
    static Roots Bounds(const Roots &at) {
        Roots r = at;
        r.Weaken();
        for (auto &a : r.alts) a.from = nullptr;
        return r;
    }
    // The container whose stores say what a holder lying where `at` points
    // holds (Val::holderfrom): the one it lies in exactly, and none where it
    // may lie in any of several, or anywhere a root only bounds.
    static VarDef *HolderSource(const Roots &at) {
        return at.Exact() && !IsTemp(at.alts[0].root) ? at.alts[0].root : nullptr;
    }
    // A holder that lies in the storage a parameter's class stands for alone
    // (Val::holderfrom) holds views that storage holds: its contents there
    // are marked so (RootAlt::classread), which a merge, a literal and a
    // call's result keep where every value they join at that class has it
    // (Roots::Add, RetAltVal).
    void MarkClassCopy(Val &v) {
        if (!IsClassRoot(v.holderfrom)) return;
        for (auto &a : v.contents.alts)
            if (a.root == v.holderfrom) a.classread = true;
    }
    // A store of alternative a into holder x, which the store record says
    // came out of the storage of parameter class src alone or not
    // (StoreEvent::classread): x's contents grow by it, which a loop around
    // the store feeds back (NoteFact), a class of an activation's to that
    // activation's own loops and rounds (ActivationFloor). They keep the
    // mark where what x got at the class is one of the views its storage
    // holds (RootAlt::classread): a view read out of there, a holder holding
    // such views, or a copy of a holder that lay there alone. A later store
    // of anything else at the class takes the mark away, which the loop
    // feeds back as it does a new root (Roots::Add).
    void AddContents(VarDef *x, const RootAlt &a, bool classread, const VarDef *src) {
        auto held = a.classread || (classread && src == a.root);
        if (x->contents.Add({ a.root, a.exact, a.from, a.slotread, held }))
            NoteFact(x, ActivationFloor(a.root));
    }
    // A container a store record may name as the source of what it stores
    // (StoreEvent::src), one whose stores say what it holds. A temporary was
    // filled by whatever made it, and a local reference or slice variable
    // holds what its binding says, which no store records: for those the
    // stored value's own roots bound it. A global one's bindings are on
    // record (NoteGlobalBinding), and the judgements take those or its type.
    static VarDef *StoreSource(VarDef *src) {
        if (!src || IsTemp(src)) return nullptr;
        if (src->type && IsRefOrSlice(src->type) && !src->isglobal) return nullptr;
        return src;
    }
    bool ShrinkMayFree(VarDef *root, TypeExpr *bound, bool growonly, TypeExpr *of,
                       bool byteview);
    bool ShrinkMayMove(VarDef *root, TypeExpr *bound, TypeExpr *t);
    void CheckHeldShrinks(Node *at, const string &op, VarDef *root,
                          const string &what, bool growonly, TypeExpr *bound = nullptr);

    // ------------------------------------------------------------------
    // Values: the per-node dispatch plus the implicit-conversion rules.

    // The raw per-node check: virtual dispatch; the value may still denote a
    // reference. Consumers go through CheckValue/CheckArg/Operand, which
    // apply reference transparency.
    Val CheckV(Node *n, TypeExpr *expected) {
        NodeScope ns(*this, n);
        auto v = n->Check(*this, expected);
        if (v.type == fntype && !Is<Ident>(n) && !Is<FunVal>(n))
            Error(n, "a function value must be a function name or block literal (§7.6); "
                     "evaluate runtime expressions separately");
        RecordVal(n, v);
        return v;
    }

    Val DecayRef(Val v);
    bool KeepsRef(const Val &v, TypeExpr *dt);
    Node *AutoRef(Node *n, Val &v, bool writes = true);
    void SlotRoots(Val &v);
    void NoteWritableRef(VarDef *d, Node *at);
    VarDef *RelyOnNonneg(const Val &v, Node *at);
    void RelyOnConstant(ConstUse &use, VarDef *d);
    void RelyOnNamed(const Val &v, Node *at);
    TypeExpr *StorageType(const Val &v);
    bool BindsRef(const Val &v, TypeExpr *dt);
    bool IsNonFixedLValue(const Val &v);
    bool IsNonFixedRef(const Val &v);
    bool Referenceable(Node *n, const Val &v);
    bool FieldsInFrame(Dot *d);
    [[noreturn]] void NoResizableRef(Node *at);
    bool UserRefOf(Node *n);
    bool IsOwnLocal(Node *n);
    bool ImplicitCopy(const Val &v, Node *n, TypeExpr *dt);
    [[noreturn]] void ImplicitCopyError(Node *n);
    void RequireCopyable(const Val &v, Node *n, TypeExpr *dt) {
        if (ImplicitCopy(v, n, dt)) ImplicitCopyError(n);
    }
    void CheckBranchCopy(const Val &v, Node *n, TypeExpr *dt, Val &out);
    void RefCopyWarning(Node *n);
    // The redundant `&x` branches an argument's copy noted (Val::refcopies),
    // where its parameter takes the copy rather than binding them.
    void RefCopyWarnings(const Val &v) {
        for (auto n : v.refcopies) RefCopyWarning(n);
    }
    // The value path of a call's argument checked before its parameter's
    // type is known: the argument, then each branch a control construct on
    // it checks next. A reference parameter binds such a construct's
    // branches by reference, so the copy CheckBranchCopy would report, and
    // the redundant `&` of a branch the copy loads, are left to the
    // argument's check against its parameter.
    Node *argpath = nullptr;
    struct PathScope {
        TypeCheck &tc;
        Node *saved;
        PathScope(TypeCheck &t, Node *n) : tc(t), saved(t.argpath) { tc.argpath = n; }
        ~PathScope() { tc.argpath = saved; }
    };
    // The value path of a control construct whose branches no destination
    // type adapts, checked a first time to learn whether they join as a
    // slice (CheckJoin): the construct, then each branch it checks next, a
    // construct there included, whose value the first one settles. The copy
    // CheckBranchCopy would report waits for that as well.
    Node *joinpath = nullptr;
    struct JoinPathScope {
        TypeCheck &tc;
        Node *saved;
        JoinPathScope(TypeCheck &t, Node *n) : tc(t), saved(t.joinpath) { tc.joinpath = n; }
        ~JoinPathScope() { tc.joinpath = saved; }
    };
    // Whether a construct with destination type `dest` makes its value a
    // copy of the branch taken (CheckBranchCopy): where it has none.
    static bool CopiesBranch(TypeExpr *dest) { return !dest || dest->kind == TY_VOID; }
    void BindBranchesByRef(vector<Node *> &argnodes, vector<Val> &argvals,
                           const vector<TypeExpr *> &paramtypes, SFunction *sf, int skip = -1,
                           size_t end = SIZE_MAX);
    void BindBranchByRef(Node *&n, Val &v, TypeExpr *pt, TypeExpr *declared);
    Val CheckInferredResult(Node *&n, FnSpec *tspec);
    // A parameter that takes the value -- a slice, or a fixed-class value
    // that a non-fixed one constructs by copy (an array of another kind
    // into a static-capacity limited array, §4.2) -- takes the argument
    // itself: the reference the argument loop made of a non-fixed lvalue
    // (§4.1) is undone, so the adaptation is the plain one.
    void UnrefForValueParam(Node *&a, TypeExpr *pt) {
        if (pt->kind != TY_SLICE && (pt->kind == TY_REF || ClassOf(pt) != SC_FIXED)) return;
        if (auto u = Is<Unary>(a); u && u->synth) a = u->child;
    }
    Val CheckValue(Node *&n, TypeExpr *expected, bool callsite = false, bool branchcopy = false,
                   bool inferred = false);

    Val CheckArg(Node *&n, TypeExpr *expected) {
        SlotScope ss(*this, false);
        return CheckValue(n, expected, true);
    }

    Val CheckValueAt(Node *&n, TypeExpr *expected, Dest d, bool callsite = false);
    Val Operand(Node *n);

    string fitfail;  // A specific reason from the last failing FitsAt, if any.

    void MustFit(Val &v, Node *n, TypeExpr *dt);
    void CheckArrayCount(Node *n, TypeExpr *t, int64_t count) {
        if (!t || t->kind != TY_ARRAY) return;
        auto a = t->arr;
        auto ls = a->akind == A_VAR ? (a->lenstorage < 0 ? IS_U32 : (IntStorage)a->lenstorage)
                  : a->akind == A_LIMITED && !a->sizeexpr ? IS_U32 : IS_U64;
        if (ls != IS_VARINT && IntBits(ls) < 64 &&
            (uint64_t)count >= (1ull << IntBits(ls)))
            Error(n, cat("array length ", count, " exceeds storage range of ", TypeStr(t)));
    }
    bool FitsAt(Val &v, TypeExpr *dt);
    static string ConstStr(const Val &v);
    Val CheckCond(Node *n);
    TypeExpr *UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue);
    Val VoidVal();
    // The branch-merging checkers below give a construct that diverges on
    // every path the null "bottom" type, which unifies with any sibling
    // branch. That meaning ends at the construct's node: to everything that
    // consumes an expression's value it is a value-less expression, like a
    // call to abort, and reads as void.
    Val VoidIfBottom(Val v) {
        if (!v.type) v.type = ast.voidtype;
        return v;
    }
    Val CheckRefOf(Unary *x);
    void FoldInt(TType op, Val &l, Val &r, Val &out, Node *at);
    TypeExpr *UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt, TypeExpr *rt,
                           bool cmp = false);
    bool unifytrial = false;   // UnifyNumeric gives no type rather than an error (NumericBinary).
    void RetypeOperands(Node *&left, Node *&right, Val &lv, Val &rv, TypeExpr *ct);
    void RetypeVal(Val &v, TypeExpr *ct);
    Val NumericBinary(Binary *b, Val lv, Val rv, TypeExpr *&ct, bool trial);
    Val NumericUnary(Unary *u, const Val &v, bool trial);
    // A float whose type comes from float literals alone (§6.3): a constant,
    // a literal parameter (§7.7), or a Val::litfloat.
    static bool LitFloat(const Val &v) {
        return v.type && v.type->kind == TY_FLT && (v.ck == CK_FLT || v.unsized || v.litfloat);
    }
    // The integer constants a value may be, lo to hi: a constant's value, or
    // a construct's constants (Val::litint). A u64 constant above i64.max is
    // left to the rules for constants alone.
    static bool IntConsts(const Val &v, int64_t &lo, int64_t &hi) {
        if (v.ck == CK_INT && !v.uns) { lo = hi = v.ival; return true; }
        if (v.litint) { lo = v.litlo; hi = v.lithi; return true; }
        return false;
    }
    static bool ConstsFit(const Val &v, IntStorage s) {
        int64_t lo, hi;
        return IntConsts(v, lo, hi) && FitsIntStorage(lo, false, s) && FitsIntStorage(hi, false, s);
    }
    // The constants a value is computed from, lo to hi: IntConsts', or a
    // Val::flexint's.
    static bool FlexConsts(const Val &v, int64_t &lo, int64_t &hi) {
        if (v.flexint) { lo = v.litlo; hi = v.lithi; return true; }
        return IntConsts(v, lo, hi);
    }
    static bool FlexFits(const Val &v, IntStorage s) {
        return FitsIntStorage(v.litlo, false, s) && FitsIntStorage(v.lithi, false, s);
    }
    string ConstsNoFit(const Val &v, TypeExpr *t) {
        if (v.flexint && v.litlo == v.lithi)
            return cat("constant ", v.litlo, " does not fit ", TypeStr(t));
        if (v.flexint)
            return cat("the constants ", v.litlo, " to ", v.lithi, " it is computed from do not "
                       "all fit ", TypeStr(t));
        if (!v.litint) return cat("constant ", ConstStr(v), " does not fit ", TypeStr(t));
        return cat("the branches' constants ", v.litlo, " to ", v.lithi, " do not all fit ",
                   TypeStr(t));
    }
    static void IntToFloat(Val &v, TypeExpr *ft);
    void ToFloat(Node *&n, TypeExpr *from, TypeExpr *ft);
    void RetypeFlex(Node *&n, TypeExpr *t);
    void RetypeFlexInt(Node *n, TypeExpr *t);
    void RetypeBranch(Node *&n, TypeExpr *t);
    void RetypeBranches(Node *x, TypeExpr *t);

    // Redundant casts (§6.3): an explicit `as` whose deletion would leave
    // the program meaning the same is a warning. The checker follows each
    // cast up the expression while the value without it (CastAlt::alt) could
    // still come to the same, and each check of the cast gives a verdict
    // where its consumer tells (CastVerdict).
    struct CastAlt {
        AsCast *cast = nullptr;
        Val operand;          // The cast's operand, a value (DecayRef).
        Val raw;              // The operand as checked, which an argument passes.
        Val alt;              // The value of the node followed, without the cast.
        // Followed past the cast's consumer (FollowCast): the node is one
        // that consumed the operand's value, directly or not.
        bool pending = false;
        // A float of literals and integers the deletion leaves (litfloat)
        // where the node's value is a plain float computes at whatever type
        // it settles at: the type that has to be, the one the cast gave.
        TypeExpr *settle = nullptr;
        TypeExpr *reach = nullptr;   // The type the operand reaches, for the warning.
    };
    unordered_map<Node *, CastAlt> castalts;
    // Nodes whose value a cast the checker follows would change: the cast
    // itself, unless its operand's value is the same, and every node a
    // deletion's difference was followed through. A cast's verdict never
    // rests on another such value beside it, which that one's deletion
    // could change as well.
    unordered_set<Node *> casttyped;
    // The direct arguments of a builtin call no function of its name
    // matched: without a cast, one might (CheckNamedCall).
    unordered_set<Node *> builtinfallback;
    vector<Warning> castverdicts;   // The verdicts of checks that stand.
    void ForgetCastAlt(Node *n) {
        castalts.erase(n);
        casttyped.erase(n);
    }
    bool InGenericCode();
    bool Converts(const Val &v, TypeExpr *t);
    static bool SameNum(TypeExpr *a, TypeExpr *b) {
        if (!a || !b || a->kind != b->kind) return false;
        if (a->kind == TY_INT) return a->intstorage == b->intstorage;
        return a->kind == TY_FLT && a->fltstorage == b->fltstorage;
    }
    bool SameNumVal(const Val &a, const Val &b);
    bool SameReach(const CastAlt &a, TypeExpr *at, bool typed);
    bool Reaches(const CastAlt &a, TypeExpr *at, bool typed);
    void NoteCast(AsCast *x, const Val &raw, const Val &cv, const Val &v);
    void FollowCast(Node *n, const CastAlt &a, const Val &tv, const Val &v, TypeExpr *at);
    void JudgeBinaryCasts(Binary *b, Node *l, Node *r, const Val &lv, const Val &rv, const Val &v,
                          TypeExpr *ct);
    void JudgeUnaryCast(Unary *u, Node *c, const Val &r);
    void JudgeCastAt(Node *n, TypeExpr *dt, const Val &v);
    string CastReason(const CastAlt &a, TypeExpr *reach, bool bycast = false);
    void CastVerdict(const CastAlt &a, const string &reason, const AsCast *dependson = nullptr);
    void ReportRedundantCasts();
    bool ElementwiseOK(TypeExpr *t);
    Val CheckVariantConst(Dot *d, SEnum *en);
    Val MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at, bool wantvalue);
    Val JoinBranches(const Val &a, bool areach, const Val &b, bool breach, Node *at,
                     bool wantvalue, bool onjoin);
    template<typename F> Val CheckJoin(Node *x, TypeExpr *expected, F check);
    // A join of arrays of different types with no slice among them
    // (JoinBranches), which stands only where a parameter declared a slice
    // takes it.
    void NoArrayJoin(const Val &v) {
        if (v.joinslice && !v.joinhasslice)
            Error(v.joinat, cat("branches have mismatched types: ", TypeStr(v.joina), " vs ",
                                TypeStr(v.joinb)));
    }
    // An argument a slice parameter views whole rather than taking as its
    // first check left it: a control construct's array value, a copy of its
    // branch, or its branches' arrays of different types (§6.4), and a []
    // nothing typed, which the parameter makes a temporary array (§4.2).
    static bool ViewedWhole(Node *arg, const Val &v, TypeExpr *pt) {
        return pt->kind == TY_SLICE &&
               (v.emptyarr ||
                ((v.type->kind == TY_ARRAY || v.joinslice) && IsBranchConstruct(arg)));
    }
    void CheckBranchRoot(const Val &v, int depth, Node *at, const char *construct);
    Val TempCopy(Val v);
    Node *WholeSlice(Node *n);
    Val CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue);
    Val CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue, int scopekind,
                      Node *scopenode = nullptr);
    Val CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue);
    Val CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue);
    void CheckLoopBody(Block *body);
    Scope CheckLoopPasses(Node *x, FlowState &head, const function<void()> &pass);
    Val CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue);
    void CheckWhile(While *x);
    void CheckFor(ForLoop *x);
    int RealFrameIndex();
    Frame &CurRealFrame() { return frames[RealFrameIndex()]; }
    int FindBreakScope(bool forcontinue);
    void CheckBreak(Break *b);
    // A loop or block exits in the join of the states at its breaks
    // (NoteBreak) and at its own exit, where it has one (JoinBreakFlow).
    void NoteBreak(int si);
    void JoinBreakFlow(const Scope &sc, bool ownexit);
    void CheckContinue(Node *n);

    // ------------------------------------------------------------------
    // Calls: builtin members, builtins, UFCS, overload resolution with
    // generic inference (§7.1, §7.7), and case-function tag dispatch (§8.2).

    // Return values of the most recent call, for `let a, b = f();`.
    vector<Val> lastcallrets;

    struct MatchInfo {
        // 0 = exact, 1 = generic binding, 2 = coercions, INTTOFLOAT = an
        // integer converted to a float (§6.3), the last resort (ResolveCall).
        static constexpr int INTTOFLOAT = 3;
        SFunction *sf = nullptr;
        int tier = 0;
        vector<pair<string_view, TypeExpr *>> bindings;
        vector<pair<string_view, FnValBind>> fnvals;
        vector<TypeExpr *> paramtypes;  // Concrete, one per declared parameter.
        vector<int> litparams;          // Literal arguments to type variables (§7.7).
        FnSpec *env = nullptr;          // Lexical parent for nested functions.
        // The parameters the call's arguments bind, in order; the rest take
        // their defaults (§7.1).
        size_t nwritten = 0;
    };

    // Literal parameters (§7.7). A literal at a call site is checked against
    // every type the parameter adapted to, in the callee and in anything it
    // passed the literal on to, once the whole program is checked and those
    // records are complete.
    struct LitCheck {
        FnSpec *spec = nullptr;
        int param = 0;
        Val lit;
        Node *at = nullptr;
    };
    vector<LitCheck> litchecks;
    bool litrecord = true;   // Off while overloads are tried: only the chosen one records.
    void RecordLitAdapt(const Val &v, TypeExpr *t, Line at);
    void NoteLitArgs(FnSpec *spec, vector<Val> &argvals, Node *at);
    void VerifyLiterals();

    Val CheckCall(Call *c);
    Val CheckNamedCall(Call *c, Ident *id);
    SFunction *LookupLocalFnEnv(string_view name, FnSpec *&env);
    Val CheckUfcsCall(Call *c, Dot *d);
    Val ResolveCall(Call *c, vector<SFunction *> &cands, FnSpec *env, string_view name, Val *preval,
                    Node *&prenode, bool *nomatch = nullptr);
    void MatchCandidates(Call *c, vector<SFunction *> &cands, FnSpec *env, vector<Val> &argvals,
                         vector<MatchInfo> &tied, vector<MatchInfo> &converting, string *failures,
                         string_view name);
    void JudgeCallCasts(Call *c, vector<SFunction *> &cands, FnSpec *env, vector<Node *> &argnodes,
                        vector<Val> &argvals, MatchInfo &best, string_view name, bool builtin);
    // `defaults`: the call may leave out parameters that have a default
    // (§7.1), which ResolveCall then passes. Tag dispatch and rendering
    // hooks give every argument.
    bool TryMatch(SFunction *sf, Call *c, vector<Val> &argvals, MatchInfo &mi, string &why,
                  bool defaults = false);
    FnSpec *ParamDefaultEnv(const MatchInfo &mi);
    void AddParamDefaults(Call *c, MatchInfo &best, vector<Node *> &argnodes,
                          vector<Val> &argvals, bool receiver, FnSpec *env);
    template<typename F> void InParamDefault(Call *c, const MatchInfo &mi, FnSpec *env,
                                             size_t param, F f);
    void DefaultScopeName(string_view name, Node *at, bool fnonly);
    void RefArg(Node *&a, Val &v);
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

    // How many specializations of one function may be in progress on one
    // compile-time call path (§7.8), and instantiations of one type on one
    // chain of instantiations (§3.2), the way C++ bounds template
    // instantiation depth: a recursion whose types never repeat would
    // otherwise instantiate without end.
    static constexpr int MAXNESTEDSPECS = 16;
    FnSpec *GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode);
    void LoadSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes);
    void RefSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes, Line at);
    void NoteHeld(Val &av, TypeExpr *slice);
    bool HasView(Val &av, TypeExpr *pt, int reach, SFunction *sf);
    // The class root of parameter p's view in spec's body (FnSpec::views),
    // or null where it has none or the slot holds static data.
    static VarDef *ViewClassOf(const FnSpec *spec, size_t p) {
        if (p >= spec->views.size() || spec->views[p].cls <= 0 ||
            spec->views[p].cls >= (int)spec->classroots.size())
            return nullptr;
        return spec->classroots[spec->views[p].cls];
    }
    void ValidateCycle(FnSpec *spec, Node *callnode);
    static FnSpec *CycleHead(FnSpec *s);
    void JoinCycle(FnSpec *spec, Node *callnode);
    void NoInferredRefResult(FnSpec *spec, Line at);
    // A store the cycle store rule refuses (§7.8), or admits only while the
    // threaded classes it relies on stay threaded, made by a function that is
    // neither recursive nor yet known to be in a cycle. A function calling
    // back into one is in it from its first statement, but becomes known to
    // be only at that call (JoinCycle), where its first such store the rule
    // refuses is an error, and the others rely on their classes from then
    // on. One whose check ends outside any cycle drops its own.
    string_view FrameFnName(int fi);
    static VarDef *UltimateRoot(VarDef *v);
    void ValidatePoolArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode);
    // A reference or slice parameter class its creating call rooted exactly,
    // which every call back into the recursive cycle passes on as that call
    // did: in every activation it points where that argument did, so where
    // that is outside the cycle, a reference rooted at it may be stored
    // inside the cycle as one rooted at a pool may (§7.8). Nothing makes a
    // back edge pass it on, so it is restricted only once a store relies on
    // it: a call back into the cycle passing something else is then an error
    // at that store; one checked before any store leaves it pass-down-only.
    struct ThreadedClass {
        bool broken = false;
        string why;                  // What broke it, as the diagnostics say.
        // The classes created from its parameters' values, or given them at a
        // back edge: each points where it does only while it stays threaded.
        vector<VarDef *> heirs;
    };
    unordered_map<VarDef *, ThreadedClass> threadedclasses;
    void ValidateThreadArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode);
    void NoteThreadedClass(VarDef *cls);
    bool ThreadedChain(VarDef *r);
    bool ThreadStorable(VarDef *r);
    void Unthread(VarDef *cls, const string &why);
    string CycleStoreError(VarDef *root, const string &more = {});
    void ValidateNeeds(FnSpec *spec, Node *callnode);
    void AddNeed(FnSpec *s, FnSpec *t);
    // The record of a callee a call reads: the callee's own once it is
    // checked, the round before's while it is in progress (a back edge), and
    // none in a cycle's first round.
    static const FnRecord *RecordOf(const FnSpec *spec) {
        return spec->inprogress ? spec->prev.get() : &spec->record;
    }
    void CheckSpecBodyOnce(FnSpec *spec, vector<Val> *argvals, Line callline);
    bool SameRecord(const FnRecord &a, const FnRecord &b, const FnSpec *spec);
    bool ExternValueOk(TypeExpr *t, string &why);
    bool ExternParamOk(TypeExpr *t, string &why);
    void CheckExternSpec(FnSpec *spec);
    int ClassDepth(VarDef *r);
    vector<VarDef *> ExternalOptionals(const MatchInfo &mi);
    int EnvReach(const MatchInfo &mi);
    void CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline);
    void RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at);
    Val RetAltVal(FnSpec *spec, const RootAlt &alt, vector<Val> &argvals, TypeExpr *t, Node *at);
    Val CallResult(Call *c, FnSpec *spec, vector<Val> &argvals);
    void CheckReturn(Return *r);
    FnSpec *EnsureThreadSpec(SFunction *sf, Line l);

    // ------------------------------------------------------------------
    // Struct and variant literals (§4.2). The per-node entry is
    // StructLit::Check in typecheck_nodes.h.

    // Where one literal's initialized fields and elements point.
    struct LitDeep {
        Roots roots;
        bool byteview = false;
    };
    LitDeep CheckInits(StructLit *sl, vector<Field> &fields, vector<TypeExpr *> &ftypes,
                       string_view what, TypeExpr *selft);
    void CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft);

    // ------------------------------------------------------------------
    // Statements.

    void CheckStmt(Node *n);
    void CheckStmtExpr(Node *n);
    void CheckVarDecl(VarDecl *vd, bool global);
    void CheckBindingRoot(VarDef *d, const Val &v, Node *at);
    void CheckCycleInit(VarDef *d, int fi, int calls);
    void AssignableClassCheck(TypeExpr *t, Node *at);
    void CheckAssign(Assign *a);
    Val CheckAssignedValue(Assign *a, TypeExpr *target, TypeExpr *arr, const Roots &built,
                           Dest dest);
    void CheckRebind(Assign *a, LVal &lv);
    bool PointeeWritable(LVal &lv, Node *at);
    void PointeeAssign(Assign *a, LVal &lv, const LVal &at);
    // `via`: how a store through a reference reached slice variable vd
    // (" through a reference"), where it did not assign vd itself.
    bool CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv, const string &via = {});
    // A slice stored into the slot of type `slice` a reference to a slice
    // names: into storage r owns, or where `bound`, storage r's references
    // lead to.
    bool StoreIntoSlot(Node *at, VarDef *r, bool bound, TypeExpr *slice, const Val &v,
                       const string &via);
    bool RebindSliceVar(Node *at, VarDef *sv, const Val &v, const string &via);
    // The slice variable storage r is, whose own slot a reference to a slice
    // may name; null for any other storage.
    static VarDef *SliceVarOf(VarDef *r) {
        return r && r->type && r->type->kind == TY_SLICE ? r : nullptr;
    }
    void CompoundAssign(Assign *a, TypeExpr *st, bool writable);
    void NoLetAssign(Node *at, const LVal &lv);
    void NoLetReassign(Node *at, const VarDef *vd, const char *verb, const char *done);
    void NoCopyWrite(Node *at, const LVal &lv);
    bool WholeWritable(const LVal &lv);
    void CheckIncDec(IncDec *x);

    // ------------------------------------------------------------------
    // Builtins (§3.7, §9.3, §11.2) and array members (§3.3, §5.4).

    Val CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv);
    void CheckPrintable(Call *c, const char *what, vector<Node *> &args, size_t i,
                        const Val *out = nullptr);
    // The types on the path from an argument to the part being checked
    // (CheckRenderable), and the ones met again on it: types that reach
    // themselves through references.
    struct RenderSeen {
        vector<TypeExpr *> path, recurring;
    };
    TypeExpr *OverloadedPart(Call *c, TypeExpr *t, vector<TypeExpr *> &seen);
    void CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at, RenderSeen &seen,
                         Val value, const Val &out);
    FnSpec *UserFormat(Call *c, TypeExpr *t, const Val &value, const Val &out, Node *arg);
    FnSpec *UserFormatIn(Call *c, TypeExpr *t, string_view ns, const Val &value, const Val &out,
                         Node *arg);
    StrLit *ConstStrLit(Node *n);
    const string *EmbedShader(Call *c, vector<Node *> &args);
    void CheckGrowShrink(Node *at, const char *op, Node *recv, const Val &rv);
    // `what` names the array in the diagnostics; `bound` is its type where vd
    // only bounds it (ShrinkTarget).
    void GrowOnlyShrinkAt(Node *c, const string &op, VarDef *vd, const string &what,
                          TypeExpr *bound = nullptr);

    // Every store of a reference, slice or holder value into a container
    // (ast.h StoreEvent), program-wide: a function value's body stores into
    // its lexical function's containers while being checked in another frame.
    vector<StoreEvent> storeevents;
    Node *fitnode = nullptr;         // The node MustFit is fitting, for RecordStore.
    // The deepest root among the references a literal under construction
    // holds: its holder root (§9.2). Each literal owns its accumulator, so
    // checking a nested literal cannot replace its enclosing literal's facts.
    void NoteLitElem(LitDeep &deep, Node *at, const Val &v, TypeExpr *t);
    void HolderFromLit(Val &v, const LitDeep &deep);
    void AddStoreEvent(const StoreEvent &e);
    void NoteSlotStore(const StoreEvent &e);
    // How NeverStoredError words a grow-shrink array `gs` a value pointing at
    // `roots` may point into: as the array itself where it is the value's one
    // root, else as one it may point into.
    static bool MayPointWording(const Roots &roots, VarDef *gs) {
        return !(roots.alts.size() == 1 && roots.alts[0].root == gs);
    }
    // A store of what points at `roots` (a reference's, or a holder's
    // contents) into container: one event per root, the container's
    // contents updated (§9.2).
    void RecordStore(VarDef *container, const Roots &roots, bool byteview, TypeExpr *pointee,
                     VarDef *src = nullptr, TypeExpr *reached = nullptr, bool bound = false,
                     bool slot = false, bool sliceref = false);
    // Whether a holder may hold a reference into arr: `hit` takes the index
    // of the event that says so, and `via` the type of the slot a reference
    // stored there refers to where it leads into arr only through what that
    // slot holds.
    bool HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype, size_t from,
                            Line *where, size_t *hit = nullptr, TypeExpr **via = nullptr);
    bool HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype, size_t from,
                            Line *where, set<VarDef *> &seen, size_t *hit,
                            TypeExpr **via = nullptr);
    TypeExpr *StoredSlotMayPointInto(VarDef *holder, TypeExpr *pointee, bool sliceref, VarDef *r,
                                     bool exact, VarDef *arr, TypeExpr *arrtype, Line *where,
                                     set<VarDef *> &seen);
    void ApplyCalleeStores(FnSpec *spec, vector<Val> &argvals, Node *at);
    void NoteHolderBinding(VarDef *d, const Val &v, Node *at);

    // Whether a global may hold a reference into a global array is known
    // only once every function has been checked, since a store into it may
    // come from one checked after the shrink (CheckGlobalShrinks). What that
    // judgement follows: every store as it was made -- `storeevents` has a
    // callee's events mapped in place for its first call -- and every
    // binding of a global reference or slice, and what each parameter's
    // class stood for at every call.
    vector<StoreEvent> madestores;
    struct ClassUse {
        Roots roots;               // Where the argument may point (ClassArgRoots).
        VarDef *src = nullptr;     // The container a holder argument is a copy of.
        bool byteview = false;
    };
    map<VarDef *, vector<ClassUse>> classuses;
    void NoteClassUses(FnSpec *spec, const vector<Val> &argvals);
    void NoteClassUse(VarDef *cr, const ClassUse &u);
    void NoteGlobalBinding(VarDef *gd, const Roots &roots, bool byteview, TypeExpr *pointee,
                           Line at);
    // A shrink of a global array, judged against every other global once
    // checking is over (CheckGlobalShrinks), with the instantiation chain
    // its error reports.
    struct GlobalShrink {
        VarDef *arr = nullptr;
        TypeExpr *bound = nullptr;
        Line at;
        string prefix;
        string chain;
    };
    vector<GlobalShrink> globalshrinks;
    void NoteGlobalShrink(Node *at, const string &prefix, VarDef *vd, TypeExpr *bound);
    void CheckGlobalShrinks();
    // Whether what a store record leads to may point into the array `arr`
    // of type `arrtype`: its stores followed through the containers they
    // copied and the calls that passed the parameter classes they name.
    struct GlobalReach {
        TypeCheck &tc;
        VarDef *arr;
        TypeExpr *arrtype;
        const map<VarDef *, vector<size_t>> &bycontainer;
        set<VarDef *> held;
        set<pair<VarDef *, bool>> rooted;
        bool Fits(TypeExpr *pointee, bool byteview);
        bool Root(VarDef *r, bool exact, VarDef *from, TypeExpr *pointee, bool byteview);
        bool Class(VarDef *cr, bool exact, TypeExpr *pointee, bool byteview);
        bool Holds(VarDef *x, Line *where = nullptr);
        bool Event(const StoreEvent &e);
        bool Covered(VarDef *src, VarDef *root, bool exact);
    };
    bool GrowOnlyTail(TypeExpr *t);
    bool IsGrowOnlyRootVar(VarDef *r);
    void RefPointees(TypeExpr *t, vector<TypeExpr *> &out);
    void RefSlots(TypeExpr *t, vector<pair<TypeExpr *, bool>> &out);
    VarDef *HolderRootOf(const Val &v);
    Roots ClassArgRoots(TypeExpr *pt, const Val &v);

    string ExprStr(Node *n) {
        string s;
        dumpwritten = true;
        n->Dump(s, 0);
        dumpwritten = false;
        return s;
    }

    void CheckShrinkHolders(Node *at, const string &op, VarDef *root, const string &what,
                            TypeExpr *bound = nullptr);
    // Records an event on root for callers: globals and captured owners
    // remain external roots (`external` gets the specialization they are
    // external to), while a specialization's parameter roots map at calls
    // (`param` gets it and the parameter's index).
    template <typename P, typename X> void NoteRootEvent(VarDef *root, P param, X external);
    void NoteShrink(VarDef *root, TypeExpr *bound = nullptr,
                    ShrinkBalance balance = SB_UNBALANCED);
    void ShrinkGrowShrink(Node *at, const string &op, VarDef *root, const string &what,
                          TypeExpr *bound = nullptr, ShrinkBalance balance = SB_UNBALANCED);
    bool ResizesToMark(Node *recv, Node *len);
    bool SamePath(Node *a, Node *b);
    // An array a shrink may free (§5.1, §5.2): the one in root's own storage,
    // or, where root cannot hold one, one its storage leads to through the
    // references it holds (`bound`: root only bounds that array's lifetime).
    struct ShrinkTarget {
        VarDef *root = nullptr;
        bool bound = false;
    };
    vector<ShrinkTarget> ShrinkTargets(const Roots &roots, TypeExpr *arr, bool slots = false);
    string TargetStr(const ShrinkTarget &t);
    void ShrinkThrough(Node *at, const string &verb, const string &recv, const Roots &roots,
                       TypeExpr *arr, ShrinkBalance balance = SB_UNBALANCED);
    void ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name);
    // A shrink's scan sees the views of the activation only, and takes a
    // parameter's class for an array of its own: whether an argument was a
    // view of the shrunk array, or the shrunk array one the activation's
    // views point into, is its callers' to judge, from the pairs a
    // specialization records (FnRecord::liveshrinks).
    bool IsClassRoot(VarDef *v) {
        return v && !v->type && !v->isglobal && !IsTemp(v);
    }
    bool CallersJudge(VarDef *r, VarDef *root);
    void NoteLiveViews(Node *at, const string &prefix, VarDef *root, const string &what,
                       bool growonly, TypeExpr *bound);
    // A view still used after a shrink (NoteLiveViews), or one a callee's
    // pair leads to at a call (MapLiveShrinks): where it may point, and at
    // what.
    struct LiveView {
        Prov p;
        TypeExpr *pointee = nullptr;
        bool reached = false;    // What the value leads to, not the value.
        bool contents = false;   // What p's storage holds (LiveShrink::contents).
        bool inplace = false;    // A walk's (LiveShrink::inplace).
    };
    // The temporaries a call is handed, with where what each holds points
    // (TempContents).
    using TempHolds = vector<pair<VarDef *, Roots>>;
    void RecordedViews(VarDef *h, size_t from, TypeExpr *ht, vector<LiveView> &out,
                       set<VarDef *> &seen);
    void StoredViews(VarDef *r, bool exact, TypeExpr *pointee, bool sliceref, bool byteview,
                     TypeExpr *ht, vector<LiveView> &out, set<VarDef *> &seen);
    void HeldViews(const Prov &p, TypeExpr *held, bool isvar, vector<LiveView> &out,
                   const TempHolds *temps = nullptr);
    template<typename F> void EachHolderRoot(VarDef *holder, size_t from, F f);
    int NoteLiveShrink(LiveShrink ls, FnSpec *current);
    void ApplyCalleeLiveShrinks(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name);
    // A call, with its arguments' roots, whose callee's pairs are mapped
    // onto them (MapLiveShrinks).
    struct CallSite {
        Node *at = nullptr;
        FnSpec *caller = nullptr;
        FnSpec *callee = nullptr;   // Stable parameter/class mapping.
        const FnRecord *record = nullptr;   // This call's round (RecordOf).
        vector<Roots> args;   // What each parameter's class stands for here.
        string name;
        vector<Roots> views;  // And each parameter's view, the slice its slot held.
        TempHolds temps;      // What the temporaries among the arguments hold.
    };
    bool MapLiveShrinks(const CallSite &site);
    // A growth of the array rooted at root -- a push, an append, a pool
    // allocation, format, resize, a whole assignment -- by this body or by
    // a callee, or a shrink of it. A value built in place at a root's top or
    // slot is under construction while its expression runs (§1.3(4)): it is
    // checked against the growths and shrinks logged meanwhile
    // (CheckGrowsSince), and callers learn a body's growths as they learn
    // its shrinks.
    struct GrowEvent {
        Node *at = nullptr;
        VarDef *root = nullptr;
        bool exact = false;   // root is the array itself, not a bound on its lifetime.
        string what;
        bool shrink = false;
    };

    // The state of the body being checked, which a body checked inside it
    // (a call's, CheckSpecBodyOnce) starts afresh and hands back when it
    // ends, an error's throw included (BodyScope): the caller's statement
    // is its own, and the call site replays the body's effects against it
    // from the summary. A function value's body is checked in its call's
    // statement and keeps it (CheckFunValCall).
    struct BodyState {
        // The node being checked and every node it is nested in (PathEntry).
        vector<PathEntry> nodepath;
        // The argument print, str or format is rendering, and as which
        // builtin: its views stay live while its parts' format overloads run,
        // and so does where it lies (`renderwhere`) unless an overload takes
        // it whole.
        Node *renderarg = nullptr;
        Node *renderwhere = nullptr;
        const char *rendering = nullptr;
        // The parts of it the rendering is walking in place, outermost
        // first, each as where it lies: a variable-mode ADT, whose tag it
        // reads once, and an array of a size not fixed, whose count it reads
        // once (RenderLoc), before rendering the parts they cover. A shrink
        // of the storage one lies in may free the elements, or, as a whole
        // assignment rebuilds every variable-size part of a resizable value,
        // replace the variant or the count, so each stays held until its
        // walk ends.
        vector<Val> renderwalks;
        // The passes of the loops open around the point being checked,
        // outermost first (LoopPass).
        vector<LoopPass> looppasses;
        // A pass's warnings are kept back until the loop's last pass, whose
        // warnings are the ones that stand (CheckLoopPasses), and so are
        // those of a construct's first check of its branches (CheckJoin).
        vector<Warning> pendingwarnings;
        int joinprobes = 0;
        // Every growth and shrink so far (GrowEvent): a value built in place
        // is checked against those logged while its expression ran
        // (CheckGrowsSince).
        vector<GrowEvent> growlog;
    };
    BodyState cur;
    // The states of the bodies being checked around this one, outermost
    // first: the callers' on the compile-time call path.
    vector<BodyState *> outerbodies;
    // A fresh body state for the extent of a scope; the enclosing one
    // returns when it ends.
    struct BodyScope {
        TypeCheck &tc;
        BodyState saved;
        BodyScope(TypeCheck &t) : tc(t) {
            std::swap(tc.cur, saved);
            tc.outerbodies.push_back(&saved);
        }
        ~BodyScope() {
            tc.outerbodies.pop_back();
            std::swap(tc.cur, saved);
        }
    };
    void NoteGrow(Node *at, const Roots &roots, const string &what);
    void NoteShrinkEvent(Node *at, VarDef *root, TypeExpr *bound, const string &what);
    void CheckGrowsSince(size_t base, const Roots &built, const string &what);
    void ApplyCalleeGrows(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name);
    // Whether an element pushed into, or allocated in, an array of `elem`
    // is built in place at its slot: a variable-size one, or a fixed one
    // holding relative references of either form, which codegen writes
    // where the element stays (an `in pool` offset measures from the pool,
    // but is stored field by field all the same).
    bool BuiltInPlace(TypeExpr *elem);
    // The array an array literal appended to an array of `elem` is: the run
    // of elements it adds (§4.2).
    TypeExpr *AppendedRun(TypeExpr *elem, ArrayLit *al);
    // The elements any other source `av` adds to receiver `rv`, which are
    // copies of its own.
    void AppendedCopies(Node *an, const Val &av, TypeExpr *elem, const Val &rv);
    // Whether two roots may name one array: no, yes, or only the call
    // sites can tell (two parameter classes of one activation, settled by
    // ResolveGrowConflicts once every call site has been seen).
    enum Alias { AL_NO, AL_YES, AL_DEFER };
    Alias MayAliasRoots(VarDef *a, bool aexact, VarDef *b, bool bexact);
    Alias MayAliasRoots(VarDef *a, bool aexact, VarDef *b, bool bexact, FnSpec *current);
    // A growth of, or a use of (CheckBuiltUses), a root the call sites have
    // to tell apart from the one under construction.
    struct GrowConflict {
        Node *at = nullptr;
        FnSpec *spec = nullptr;
        VarDef *grown = nullptr;
        VarDef *built = nullptr;
        string msg;
    };
    vector<GrowConflict> growconflicts;
    // The right-hand side of a whole assignment runs while the array's new
    // contents are built over its old ones (§4.4), so nothing it runs may
    // use the array: name it, or a reference to it, directly or in a
    // function it calls.
    void CheckBuiltUses(Node *rhs, Node *lval, const Roots &built, TypeExpr *arr);
    Alias ReachesBuilt(VarDef *v, VarDef *built, bool exact, TypeExpr *arr, VarDef *&root);
    template<typename F, typename G> void EachUse(Node *n, F named, G called);
    bool FieldsApart(Node *use, Node *lval);
    bool NamedOutside(FnSpec *spec, vector<VarDef *> &out);
    // Its results for callees whose bodies were all checked, which no
    // later check changes.
    map<FnSpec *, vector<VarDef *>> namedoutside;
    void ElemArg(Node *&n, TypeExpr *elem, Val &rv);

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
        temproot->istemp = true;
        temproot->depth = INT32_MAX;
        fntype = ast.NewType(TY_FN, Line {});
        fntype->fn = ast.NewDetail<TypeFn>();
        u8slice = ast.SliceOf(ast.inttypes[IS_U8], Line {});
        cu8slice = ast.SliceOf(ast.inttypes[IS_U8], Line {});
        cu8slice->cq = true;
        nulltype = ast.RefTo(ast.voidtype, Line {}, true);
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
        for (auto st : ast.structs)
            if (st->generics.empty()) GetStructInst(ast.StructOf(st, {}, st->line));
        for (auto en : ast.enums)
            if (en->generics.empty()) GetEnumInst(ast.EnumOf(en, {}, true, en->line));
        // And the sizes in every function's signature, for its parameters' and
        // type parameters' names, and in every generic type's fields.
        for (auto sf : ast.functions) SignatureNames(sf->params, sf->rets, sf->generics);
        for (auto st : ast.structs)
            for (auto &fld : st->fields)
                if (fld.type) TypeParamSizes(fld.type, st->generics);
        for (auto en : ast.enums)
            for (auto &v : en->variants)
                for (auto &fld : v.fields)
                    if (fld.type) TypeParamSizes(fld.type, en->generics);
        for (auto g : ast.globals) {
            CheckVarDecl(g, true);
            for (auto d : g->defs) d->assigned = d->maybeassigned = true;
        }
        // Concrete ones only; a pointee still spelled with a type parameter
        // gets here again once a specialization substitutes it.
        for (auto t : ast.alltypes)
            if (t->kind == TY_REF && t->ref->pool && !HasGenerics(t->ref->sub)) ValidatePool(t);
        auto mainsf = ast.MainFunction();
        if (!mainsf) throw CompileError { "program needs exactly one global fn main()" };
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
        // standalone here with permissive assumptions (every reference, slice
        // or holder parameter rooted at a global of its own, writable,
        // reusable where it could be).
        // Errors these produce are real; a lack of errors is weaker than for
        // reached code, since no call-site facts were available.
        for (auto sf : ast.functions) CheckUnreached(sf);
        CheckGlobalShrinks();
        SettleParamRootExactness();
        ResolveGrowConflicts();
        VerifyLiterals();
        ReportRedundantCasts();
    }

    // Two parameter classes of one activation are one array where some
    // call site passes the same array to both, which a class that is
    // concrete and exact at every call site rules out (SettleParamRootExactness).
    void ResolveGrowConflicts() {
        for (auto &gc : growconflicts) {
            auto distinct = [&](VarDef *cr) {
                auto found = false;
                for (size_t i = 0; gc.spec && i < gc.spec->params.size() &&
                                   i < gc.spec->roots.size(); i++) {
                    if (gc.spec->params[i]->ref.Root() != cr) continue;
                    if (!gc.spec->roots[i].concrete || !gc.spec->roots[i].exact) return false;
                    found = true;
                }
                return found;
            };
            if (!distinct(gc.grown) || !distinct(gc.built)) Error(gc.at, gc.msg);
        }
    }

    // Checking is over, so every call site of every specialization has been
    // seen. A root class named one array inside its body whatever the call
    // site (GetOrCreateSpec keeps an inexactly rooted argument out of every
    // other argument's class), but *which* array only a call site knows, and
    // the later passes ask that question instead: whether two classes are two
    // arrays. Record the answer where they read it.
    void SettleParamRootExactness() {
        for (auto spec : ast.fnspecs) {
            for (size_t i = 0; i < spec->params.size() && i < spec->argtypes.size(); i++) {
                auto t = spec->argtypes[i];
                if (!IsRefOrSlice(t)) continue;
                if (!spec->roots[i].exact) spec->params[i]->ref.Weaken();
            }
        }
        // A class passed on is concrete only while the parameter it stands for
        // is both concrete and exact: two classes of a parameter that was
        // inexactly rooted at some call may be one array there. Calls that
        // pass classes around a cycle keep what the cycle's entry calls
        // establish, so this removes facts until none changes.
        for (auto changed = true; changed;) {
            changed = false;
            for (auto spec : ast.fnspecs)
                for (auto &ra : spec->roots)
                    for (auto [p, j] : ra.via)
                        if (ra.concrete && (j >= (int)p->roots.size() || !p->roots[j].concrete ||
                                            !p->roots[j].exact)) {
                            ra.concrete = false;
                            changed = true;
                        }
        }
    }

    // A function nothing calls is checked as if the global initializers
    // called it with a global of its own for each reference, slice or holder
    // parameter, made up here: a class to itself at the globals' depth
    // (RootArg's defaults), concrete and exact, standing for that global
    // (VarDef::classfrom). So the body may shrink or grow the array apart
    // from the others' and store its references wherever a global's may go,
    // inside a recursive cycle too.
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
        vector<Val> args(sf->params.size());
        auto classes = 0;
        for (size_t i = 0; i < sf->params.size(); i++) {
            auto &p = sf->params[i];
            auto t = Subst(p.type);
            ValidateType(t, sf->line, VT_PARAM);
            spec->argtypes.push_back(t);
            RootArg ra;
            auto holder = !IsRefOrSlice(t) && HoldsPlainRef(t);
            if (IsRefOrSlice(t) || holder) {
                ra.cls = ++classes;
                ra.concrete = true;
                auto g = ast.NewVarDef();
                g->name = p.name;
                g->isglobal = true;
                args[i].Set(g, true);
            }
            if (IsRefOrSlice(t)) {
                ra.writable = true;
                if (CarriesPool(t) && ClassOf(t->ref->sub->arr->sub) == SC_FIXED)
                    ra.reusable = RU_SLOTS | RU_SLICES;
            } else if (holder) {
                // What it holds points into its class's array, as a literal's
                // references into one global would.
                ra.heldexact = !sf->isrec;
            }
            spec->roots.push_back(ra);
            RootArg noview;
            noview.cls = -1;
            spec->views.push_back(noview);
        }
        sf->specs.push_back(spec);
        CheckSpecBody(spec, &args, sf->line);
    }

    // Walks a thread program's call graph: a worker's program has its own
    // copy of every global it uses, taken at spawn (§11.2), which a value
    // holding references cannot be.
    void CheckThreadGlobals(FnSpec *entry) {
        set<FnSpec *> seen;
        function<void(FnSpec *)> rec = [&](FnSpec *sp) {
            if (!sp || !sp->body || !seen.insert(sp).second) return;
            function<void(Node *)> walk = [&](Node *n) {
                if (!n) return;
                if (auto id = Is<Ident>(n)) {
                    auto g = id->vdef;
                    if (g && g->isglobal && g->type && !IsFlat(g->type))
                        Error(n, cat("thread programs may access only flat globals (§11.2): ",
                                     id->name, " holds references (reached from thread_fn ",
                                     entry->sf->name, ")"));
                }
                if (auto c = Is<Call>(n)) {
                    // The native layers keep one state for the process, and
                    // SDL's windowing is main-thread only.
                    if (c->spec && c->spec->sf->isextern) {
                        auto &cname = c->spec->sf->cname;
                        auto module = cname.rfind("gs_gfx_", 0) == 0    ? "gfx"
                                      : cname.rfind("gs_phys_", 0) == 0 ? "physics"
                                      : cname.rfind("gs_ui_", 0) == 0   ? "ui"
                                                                        : nullptr;
                        if (module)
                            Error(n, cat(module, " runs on the main thread only: ",
                                         c->spec->sf->qname, " is reached from thread_fn ",
                                         entry->sf->name));
                    }
                    rec(c->spec);
                    for (auto d : c->dispatch) rec(d);
                    for (auto &fs : c->fmtspecs) rec(fs.second);
                }
                RunChildren(n, walk);
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
