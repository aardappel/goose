# The Goose compiler: implementation notes

These notes explain how the compiler in `src/` implements `goose_spec.md`.
They cover each pass, with particular attention to reference roots and
provenance, writability, shrinking, recursion, optimization, bounds-check
elimination, and C representations. There are two intended audiences:

* **Compiler developers and independent implementors.** Sections 1 to 8
  describe the data structures, rule enforcement, and conservative assumptions
  in each pass, with function names to help locate the implementation.
* **Advanced users.** Section 9 explains which bounds checks and copies the
  compiler can remove, and which representations generate efficient code.
  It refers to earlier sections for implementation details.

Conventions: `§n` refers to a section of the specification, `C.n` and `E` to
its appendices. File names are under `src/`; function names are given as
they appear there. These notes describe the current source, including the
supported subset in section 10. Section 11 records correctness defects;
their behavior is not a compatibility requirement.

**What an equivalent implementation must preserve.** Read these notes with
`goose_spec.md`: the specification defines the language, and these notes
make its implementation choices and current limits explicit. Equivalence
means the same type and lifetime decisions within that supported subset,
the same evaluation, mutation, results and required runtime failures, and
the specified layouts where bytes or C interfaces expose them. It does not
require the same AST, specialization count, stack numbering, optimization
thresholds, generated C text, or diagnostic wording. Bounds checks may be
removed whenever sound; acceptance must not depend on an optimization
proving an otherwise illegal lifetime safe. Debug arithmetic has the
specific optimization latitude given in spec §6.2.

The source names below are navigation aids, not prescribed algorithms. In
particular, an exact root is a claim about storage identity, while an
inexact root is only an outlives bound. Neither equal depth nor distinct
specialization parameters alone proves that two arrays cannot alias.

---

## 1. Pipeline

`main.cpp` runs the phases in this order; each is one or more headers in a
single translation unit, included in the order the driver lists them.

| Phase | Where | Consumes | Produces |
|---|---|---|---|
| Parse | `lexer.h`, `parser.h`, `ParseProgram` in `main.cpp` | source files, following `import` | the `Ast`: nodes, type expressions, symbols per namespace, globals in initialization order |
| Resolve | `resolve.h` (`ResolveTypeNames`) | type names as written | struct/enum/generic kinds, aliases substituted away |
| Typecheck | `typecheck*.h` (`TypeCheckProgram`) | the `Ast` | one `FnSpec` per specialization with a cloned, annotated body; `StructInst`/`EnumInst`; `VarDef`s; the store record; every diagnostic of §3--§11; the shader blobs `embed_shader` compiles (`gfx.h`, `shaderc.c`) |
| Optimize | `optimize.h`, `optimize_basecase.h`, `optimize_tre.h` (`Optimizer`) | live specializations | bodies rewritten in place (inlined, folded, loops), liveness and use counts |
| BCE | `bce.h` (`BCE::RunAll`) | live specializations | `Index::nobc`, `SliceExpr::nobc`, `ForLoop::fixedlen`, per-loop `hoistrefs` |
| Codegen | `codegen*.h` (`CodeGen`) | live specializations, globals | one C file, with `src/runtime/` prepended |
| JIT | `jit.h` | the C text | the program run in-process through libtcc, when no `-o` was given |

The passes are virtual methods on `Node` (`ast.h`): `Dump`, `Clone`,
`Children`, `Check`, `Cp1`/`Opt`, `BceWalk`/`BceMark`, `CgX`/`CgAny`/`CgStmt`.
The per-node bodies of each pass live together in one file (`dump.h`,
`clone.h`, `typecheck_nodes.h`, the tail of `optimize.h`, the tail of
`bce.h`, `codegen_nodes.h`) so that each pass can be read in one place. Shared
state and helpers belong to the pass object (`TypeCheck`, `Optimizer`, `BCE`, `CodeGen`),
split across the `typecheck_*.h` and `codegen_*.h` files by topic.

**Ownership.** `Ast` owns every node, type expression, type detail, symbol,
`VarDef`, instantiation and specialization in append-only tables; the trees
merely point at each other and nothing has a destructor. Type expressions are
shared templates: a function body is *cloned* per specialization
(`Node::Clone`) and the clone carries that instantiation's annotations
(`exprtype`, resolved symbols, `VarDef`s). The optimizer's inliner copies with
`Cp1`, which preserves annotations and remaps the callee's `VarDef`s to fresh
ones.

**The compile thread.** `Main` runs the phases up to the finished C on a
thread of its own with a 64 MB stack (`RunOnCompilerStack`, `COMPILERSTACK`
in `main.cpp`): the typechecker recurses as deep as the program's
compile-time call path (§3.1 below), and the platforms' main threads get
anything from 1 MB (Windows) to 8 MB (Linux and macOS by default). The stack
is reserved address space, committed only as deep as a compile goes. A JIT
run starts the program back on the main thread, which is where macOS lets a
window be made. The link reserves 64 MB for that thread's stack as well on
Windows and macOS (`CMakeLists.txt`); on Linux it has the stack limit the
process started with (`ulimit -s`), which nothing in the executable sets.

**Driver flags** (`Main` in `main.cpp`):

| Flag | Effect |
|---|---|
| `--tokens`, `--dump`, `--parse` | stop after lexing, after parsing (dump is parse-level: no resolution), after resolution |
| `--check` | stop after typecheck, optimization and BCE; no C is written |
| `-O0`, `-O1` (default), `-O2` | inlining thresholds (§4); folding and propagation run at every level; base-case inlining and tail-recursion elimination need `-O1` or above |
| `--specs` | print every live specialization's optimized body |
| `--no-bce` | skip bounds-check elimination (this also loses the loop-view hoist, §6.10) |
| `--bce-test` | verify `// bce:elide` / `// bce:keep` annotations in the sources |
| `--bce-lines` | print elided/kept counts per source line |
| `--unsafe-no-rf-check` | omit the `return from` discriminant checks after calls: a measurement aid, unsound |
| `-o out.c`, `--jit`, `-D`, `--include`, `--stdlib`, `--` | output file, in-process run, a define written into the generated C, a user header, the stdlib directory, program arguments |
| `--gfx-link msvc\|cc` | print the response file of link inputs a program using `gfx` needs (`gfx.h`, `GfxLinkFile`) |
| `--physics-link msvc\|cc` | the same for `physics` (`physics.h`, `PhysicsLinkFile`) |
| `--compile-shader f [--shader-source msl\|hlsl]` | hidden: what a shader compiles to, without a program around it |

The debug build of the *generated* C is `-DGS_DEBUG=1` on the C compiler (or
`-DGS_DEBUG=1` to `goose`, which writes it into the file): it enables the
overflow, `as` range and ADT tag checks of §9.3. The Goose compiler's own
build configuration is unrelated.

---

## 2. Front end

**Lexer** (`lexer.h`): a hand-written scanner over a 0-terminated buffer; the
token set is an X-macro table. Lexing and parsing handle the syntax described in
§2 and Appendix D: `T&<u8>` is `&` `<` in type context, and `1..2` lexes as a
range because a `.` starts a fraction only when a digit follows. `LexRawString`
removes the closing line's indentation from each content line of a `"""` string
and normalizes `\r\n` to `\n`. Its token and the resulting `StrLit` record
whether it was multiline, allowing `embed_shader` to report errors at the
corresponding source line.

**Parser** (`parser.h`): recursive descent mirroring Appendix D, one
function per construct. Points that matter to later passes:

* Statement termination (§2) is `stmt_level`/`stmt_ended`: a block-ended
  construct on a statement's spine ends the statement, so no operator or
  postfix continues it. `no_struct_lit` implements the Rust rule for
  scrutinee positions.
* `f<` commits to a type argument list only when followed by `(`, `{` or
  `.ident {`; `>>` closing two generic lists is split by
  `ExpectClosingAngle`.
* Namespaces (`docs/design/namespaces.md`): a qualified reference is
  interned as one string (`Ident::name` is `"image::Pixel"` as written), so
  it can never coincide with a local; every reference also records the
  namespace it was written in (`Ident::ns`, `TypeName::ns`, `Dot::ns`,
  `Return::ns`), which is where an unqualified name resolves first. A
  declaration may spell its own namespace, which sets `curns` for the rest
  of it.
* `Call::standalone` is set by `Parser::Standalone` on a call that is a
  whole statement, a declaration's initializer, or an assignment's
  right-hand side: exactly the positions §5.1 allows a grow-only shrink in.
* Only the root file's `main` is registered; an imported file's is dropped
  at the declaration (§11.1).
* Imports are collected per file; `ParseProgram` resolves `import a.b` against
  the root file's directory then the stdlib directories (`StdlibDirs`), and
  `import .a.b` against the importing file, loads each file once, and
  stable-sorts `ast.globals` by a post-order walk of the import graph so an
  imported file's globals initialize first.

Import traversal visits a file's imports in source order; an edge to a file
already being visited contributes no second visit. This also determines
the initializer order in import cycles. Within a file, declaration order
is preserved. Name visibility does not make a global initialized: reading
one before its initializer, including through a called function, fails
definite assignment. The standard-library search order in `StdlibDirs` is
`--stdlib`, `GOOSE_STDLIB`, `stdlib/` beside the executable and up to three
ancestors, then `stdlib/` in the working directory; the first existing
candidate wins, after the root-file-relative candidate.

**Resolution** (`resolve.h`): every parse-time `TY_UNRESOLVED` name is
rewritten in place. A struct or enum name becomes `TY_STRUCT`/`TY_ENUM`; an
alias use is substituted with its target (after a structural cycle check
that treats nominal declarations as boundaries), keeping the use site's line
and adding its `const` qualifier; anything else becomes `TY_GENERIC`, which
the typechecker later validates against the generics in scope -- except a
qualified unknown name, which is an error here since it can only be a
declaration. Variant types (`Shape.Circle`) are resolved last. After this
pass, aliases do not exist as types and "is this an integer" is a kind
comparison.

**Dump** (`dump.h`): `--dump` regenerates source from the parsed tree, and
the test runner checks that dump, reparse and dump again are identical.

---

## 3. The typechecker

### 3.1 Whole-program checking in call-graph order

The typechecker (`TypeCheck`, `typecheck.h`) checks the program from its
roots: the `TypeCheck` constructor creates a `VarDef` for every global up
front (so names resolve in any order), resolves the pools named by relative
reference types, validates every non-generic struct and enum declaration,
checks the global initializers in order, then `main`, then every
`thread_fn`, and finally eligible functions nothing reached (`CheckUnreached`).
That last check covers only top-level functions with fully annotated
parameters, no generics, and no syntactically detected `return from` to an
outside target. Unused generic and nested bodies are checked only when
instantiated. The standalone check gives reference parameters static-root,
writable provenance, and eligible grow-only parameters both reusable-pool
capabilities; it is not evidence that every possible call is valid.
Two whole-program fixups run after that: `SettleParamRootExactness`
(§3.4) and `VerifyLiterals` (§3.12).

A function is checked once per **specialization** (`FnSpec`, `ast.h`), and
a specialization is created at a call site, when a call resolves to a
function and no existing specialization matches. `GetOrCreateSpec`
(`typecheck_calls.h`) is the one place that decides the key:

| Part of the key | What it records |
|---|---|
| `lexparent` | the lexical environment a nested function is declared in: a specialization, or a function value's body as one check of its call sees it (`FnSpec::isfunval`, §3.12) |
| `argtypes` | the concrete parameter types after generic inference |
| `bindings` | the concrete type of each of the function's own type variables, explicit or inferred, in any order: the only record of one no parameter type mentions (`size<u8>()`) |
| `roots` (`RootArg` per parameter) | the reference root *class* of each reference, slice or reference-holding argument, where the class's depth stands (`depthkey`), its writability, `reusable` and grow-shrink provenance, whether it is a `bytes_of` view, and the global pool it is rooted in (§3.4) |
| `litparams` | which parameters are literal parameters (§7.7) |
| `fnvals` | the identity of each bound function value (the block node or named function, plus the environment it captures) |
| `narrowedenv` | which optionals of the lexical environment were narrowed at the call (a nested function or block sees them narrowed) |
| `needs` | the concrete specializations of every `return ... from` target enclosing the call must be the same on this path |

`RootArg::exact` and `RootArg::concrete` are excluded from the key: they are
ANDed over every call site that reaches the specialization, and only codegen
(section 6.10) and the growth checks (§3.10) read them. A back edge into a
specialization still being checked (`inprogress`) reuses it whatever its roots
(§3.11).

`CheckSpecBody` checks one body. It pushes a `Frame` (the function, its
specialization, the lexical specialization and frame index used for free
variable lookup, the call line for diagnostics), creates the parameter
`VarDef`s with their synthetic class roots, records or infers the return
types, seeds the cycle return roots where the function is `recursive`,
clones the body, checks its statements, and treats a value-producing tail as
`return tail`. Per-body state is saved on entry and restored on exit: pending shrinks,
held temporaries, the value-region flag, reachability, the construction
destination, the slot and return flags (§3.8), and narrowings of outer
variables. Only the specialization's effect summaries reach the caller.

**Depth.** Since a body is checked inside the call that first reaches it,
the native stack holds a `CheckSpecBody` activation for every call on the
compile-time call path, and between two of them the frames of the statements
and expressions around the call. Recursion deepens it only so far (a back
edge reuses the specialization in progress, and a recursion whose types
never repeat stops at `MAXNESTEDSPECS`, §3.11 below), but a chain of
distinct functions is as long as a program makes it, a generated one
especially. `CheckSpecBody` first compares the stack pointer with
`stackfloor` (`utils.h`), which the driver sets `STACKHEADROOM` (4 MB) short
of the end of the compile thread's 64 MB stack (§1 above), and below it
reports "compile-time call path too deep for the compiler's stack" with the
number of calls nested. The headroom holds whatever a body nests below the
check and the error's unwinding. How many calls fit depends on the frames
the C++ compiler made: for a chain of functions shaped
`let a: i64[1] = [x]; next(a[0], n - 1) + 1` it is about 4,600 in an MSVC
Debug build, 5,700 in a clang Debug or ASan one, 11,600 in an MSVC Release
one and 18,700 in a clang -O3 one, and each block around the call costs
another 1.6 to 2 KB per call in a Debug build and 0.7 to 1.3 KB in a Release
one. Checking a path that deep takes seconds, and in a Release build
gigabytes (each activation saves the narrowing of every variable in scope),
so increasing the stack size would have limited benefit. The optimizer's `Reach` and BCE's
`BuildCallGraph` walk the same call graph recursively, with smaller frames,
unchecked.

**Frames, scopes and variables.** `frames` is the compile-time call path;
`scopes` is one flat vector for the whole path (a frame records where its
scopes begin), so `CurDepth()` -- the scope count -- increases monotonically
from `main` inward. `vars` is the flat vector of every variable in scope on
the path, with per-frame bases; `LookupVar` searches the current frame's
scopes innermost-out, then what the body sees outside them (`ForOuterVars`):
a nested function's body the variables in scope at its declaration
(§3.12), a function value's body those of the frame it is written in as
they are at the call, and so on outward, marking the variable `captured`;
then the globals by the namespace rules. `localfns` holds nested function
declarations with the scope they were declared in; `Scope::serial` tells a
scope from a later one at the same index. `blockpos` keeps the statement
index of every open block, which the shrink rules' liveness scan (§3.10)
reads.

**Diagnostics.** `TypeCheck::Error` appends the offending source line and
the instantiation chain: every real frame from innermost outward, with the
specialization's argument types (literal parameters marked) and the call
site it was instantiated from (§7.7). The argument types show the binding
of every type variable a parameter type names; the rest of `bindings`,
from an explicit list, and every function value in `fnvals` follow the
function's name in declaration order, since distinct specializations would
otherwise print alike: `size<u8>()` and `size<f64>()` on one line read
`in fn size<T = u8>()` and `in fn size<T = f64>()`, `fn scale<T, U>(t: T)`
reads `in fn scale<U = u8>(f64)`, and `apply(3, wide)` reads
`in fn apply<F = wide>(i64)`. A block has no name, and two can share a
line, so it prints as the file, line and column of its `{`, as in
`in fn apply<F = {block at x.goose:4:24}>(i64)` (the column is
`FunVal::col`; `Line` has none). A value passed on under another generic's
name prints as the function or block it is. A function whose type
variables all appear in its parameter types, and that binds no function
value, prints no list. `DumpInstance` writes a frame's specialization this
way, and names the one a call would create in the polymorphic recursion
error (§3.11). Each type is cut short at 200 characters (`DumpShort`): the
text of one a runaway recursion built can be exponentially longer than the
type. A chain longer than `MAXCHAIN` (20) shows its innermost and outermost
ten and counts the rest.

### 3.2 Types, instantiation, and size classes

`TypeExpr` (`ast.h`) is a kind plus a per-kind detail; the `cq` bit is the
`const` qualifier of §9.5 and is part of type identity (`TypeEq`), except
where a parameter or result is compared (`TopConstEq`: constness of the
type itself is inferred per instantiation, constness nested deeper is part
of the type). `Subst` substitutes generic names through the lexical binding
chain (`LookupBinding`: the current specialization, then its lexical
parents); `ownexclude` keeps a callee's own generic names from resolving to
an enclosing function's same-named binding while a call is unified;
`WithBindings`/`bindonly` restrict substitution to an instantiation's own
bindings while its field types are computed.

`GetStructInst`/`GetEnumInst` (`typecheck_types.h`) instantiate a nominal
type once per argument list (cached on the `TypeStruct`/`TypeEnum` detail
and searched by argument equality for clones), substitute the field types,
validate them, and compute the derived properties every later pass reads:

* the **size class** (`ClassOf`): a resizable field only as the last real
  field, making the struct resizable; any variable-class part makes it
  variable; `varint` fields and varint-width relative references are
  variable-class; a limited array is fixed with a static capacity and
  variable with `[..]`; a fixed-mode enum is fixed, a variable-mode one is
  its `varclass`;
* `flat` (no references anywhere), what threads and queues require;
* `frameobj` (C.2): a resizable-tailed struct whose other fields are all
  fixed-size and free of relative references, and whose tail is an array or
  itself a frame object -- the shape codegen keeps in the frame as a C
  struct with the tail's header last;
* `validated`, which is false while the instantiation is being built and so
  detects a struct that contains itself by value;
* concrete field types; defaults are checked at each construction site
  (`CheckDefaultInit`), with declaration-scope bindings and caller effects.

`ValidateType` enforces the placement rules of §3.3/§3.4 per position
(`ValidPos`): `varint` only in fields, elements and pointees; fixed, limited
and grow-shrink arrays need fixed-size elements; variable and grow-only
arrays may not hold resizables; an enum with non-fixed payloads is only
usable in variable mode.

An array literal uses its destination's type when one is available.
Otherwise it is a `T[k]` for fixed-size elements or a `T[]` for non-fixed
elements, which cannot use `T[k]`. Without an expected element type, the
first element supplies it and later elements must fit it; there is no
search for a common wider element type. `[]` and `null` alone do not infer
a generic argument type. A string literal's natural type is `const u8[:]`;
the pending-array rule below deliberately chooses an owned string element
instead. Array adaptation requires equal element types, not elementwise
numeric conversions. Fixed-array destinations require their exact length;
an arbitrary slice is not implicitly checked and converted into `T[k]`.
A fixed literal at a slice destination is a temporary
codegen holds in a C local and slices whole. A `T[]` literal has no such
temporary: `NoTemporaryLiteral` rejects it at a slice destination, as a
`for` iterable, as the base of a path (`[..]`) and as `bytes_of`'s argument,
the places that would view it (spec §4.2). The whole slice `==` takes of an
operand to compare two array kinds (`WholeSlice`, marked `cmpview`) may
view one: codegen builds the literal on a statement-scoped stack, like a
call result, and the comparison's result holds no view of it.

`append` names its literal argument's type (`AppendedRun`): the run of the
receiver's elements it adds, a `T[k]`, or a `T[]` where they are not
fixed-size, checked with the receiver as its destination (`CheckValueAt`),
as a pushed element is, so a reference in it obeys the store rule and a
relative one may point into the receiver. Any other source is checked as it
stands and must already be an array or slice of the element type; the
elements it adds are copies (`AppendedCopies`). What the references in them
point at is fitted to the receiver as a store (`MustFit` under the
receiver's `Dest`): an array's holder root, as a copy of the whole array
has, and out of a slice or reference, each element as `ContainerRead` reads
it out of the storage viewed.

**Pending arrays.** `var out = [];` (§4.2) gets a grow-only array type whose
element is a private void type (`PendingArray`); the first `push`, `append`,
`format` or whole assignment overwrites the element type *in place*
(`CompletePending`), which completes it in the `VarDef`, in every `Ident`
already checked and in the literal, since all of them share the one type
object. A pending array that reaches a scope exit uncompleted is an error.

**Defaults.** An omitted optional field is null even without an explicit
default. Other omitted fields need a declared default; a constructor does
not implicitly apply `default<T>()` to every missing field. A declared
default resolves in its type declaration's namespace and generic bindings,
with access to globals, not the constructor's locals or sibling fields.
It is evaluated at each construction that uses it, in field order among
the explicit initializers, not once when the type is instantiated.
`default<T>()` recursively applies declared defaults, selects variant zero,
and fills fixed arrays; empty limited arrays do not construct unused slots.
The effects of executing a default must participate in the same lifetime,
construction and optimization checks as an explicit initializer. The
checked expressions are part of that construction's tree, including
`default<T>()` and slice-pool initialization. Unused defaults are checked
when a construction first uses them, not merely on type instantiation.

### 3.3 Values, lvalues, and reference transparency

Every `Check` returns a `Val` (`ast.h`): the type, the constant value where
the expression folds, the literal flags (`strlit`, `emptyarr`, `isnull`,
`unsized`), `lvalue` (denotes storage), `nonneg` (§3.14), the holder fields
(§3.5), what a control construct's branches are (`storagebranches`,
`implicitcopy`, §3.12), and a `Prov` -- the provenance of §3.4.

References are transparent (§3.8), which the checker implements as
*decay*: `CheckV` is the raw per-node check whose result may still denote a
reference; every consumer goes through `CheckValue`, `CheckArg` or `Operand`
(`typecheck_exprs.h`), which load the pointee (`DecayRef`) unless the
destination keeps the reference (`KeepsRef`: a reference-typed destination,
or a whole-array reference meeting a slice parameter). A condition
(`CheckCond`), an integer `match`'s scrutinee (`CheckMatch`) and a `for`
loop's count (`CheckFor`) decay the same way; a reference to an ADT
scrutinee is kept, its tag and payload read where the value lies, and so is
one to the array or slice a `for` iterates in place. The node's `exprtype`
is then the pointee's type, which codegen follows: a written `&x` there
reads as `x` (`Unary::CgX`).
`CheckValue` also:

* retains `copy(x)` as a call, so repeated checking preserves copy intent;
* binds a non-fixed lvalue to a reference destination by rewriting the node
  to a synthesized `&node` (`AutoRef`, `Unary::synth`), so every later pass
  sees an ordinary reference argument, and warns on a redundant user `&`;
* keeps a reference to a non-fixed value undecayed where an un-annotated
  `let`/`var` takes it (`CheckValue`'s `inferred` flag, `IsNonFixedRef`):
  the pointee is a non-fixed lvalue, which such a binding takes by
  reference (§4.1), so the variable is that reference, exactly as with `.=`;
  `CheckVarDecl` keeps a multi-value binding's such results the same way;
* rejects a non-fixed lvalue at a value destination unless it is a `copy`
  or the function's own local being returned, by a `return` or as the
  body's tail (`RequireCopyable`, §4.1; `inreturn`, which the statements,
  conditions, scrutinees and `for` loops inside the returned value clear,
  and a valued `block` or `loop` keeps for its breaks' values), and so a
  branch's value, or a reference to it, where its construct has no
  destination type and the construct's value is a copy of it
  (`CheckValue`'s `branchcopy` flag, `CheckBranchCopy`);
* warns on a branch's redundant `&x` where its construct has no destination
  type and copies the fixed-size pointee (`branchcopy` again);
* fits the value to the destination (`MustFit`/`FitsAt`, §3.5); the node's
  `exprtype` becomes the destination's type, which can be wider than the
  type its operation computes at (an `i8` cast stored into an `i64`), so
  that type is read elsewhere: a cast's `AsCast::totype`, an operator's
  operands' `exprtype`;
* rejects copies of values holding self-relative references
  (`NoRelRefCopy`, §3.13).

Lvalue paths (`CheckLValue`, `LValueBase`) resolve `Ident`/`Dot`/`Index`
chains to an `LVal`: the location's type, its owning variable when it is a
bare name, its provenance, whether it is `let`-bound, whether it starts at a
by-value binding, whether it was reached through a field or element step
(`fromstorage`), whether it is a frame object's tail, whether it is a
`varint`, and, for a path into a temporary, where what the temporary holds
points (`intemp`, §3.6). Crossing a reference on the way (`DerefLValue`)
replaces the provenance by the reference's: a reference *variable*'s
committed binding (§3.7), or, for a reference read out of storage, the
read-back root (`ReadBackLVal`, §3.6); and the rest of the path lies in the
reference's pointee, whose type it notes (`Prov::reached`, §3.5), as a
slice crossed notes its element type (`SliceProvenance`). `ContainerRead` is
the load of a field or element: the load type (`LoadType`: `varint` decodes
to `i64`, a relative reference loads as a plain one, a `const` value loads
as a plain copy), the read-back provenance, and, for a reference or slice,
exactly the writability the slot's type says.

**Held temporaries.** Values evaluated earlier in a statement stay live until
it ends: `HoldValue` pushes a reference, slice, sequence view or holder
value onto `heldtemps` (scoped by `TempScope`), and the shrink checks consult
it (§3.10). A receiver evaluated before its arguments, or a slice taken
before a later operand, is therefore still live while the rest of the
statement runs, in evaluation order.

### 3.4 Roots and provenance

The lifetime system of §9 is one record on every reference-like value:

```
struct Prov {
    VarDef *root;        // the variable whose scope bounds the pointee; null = static data
    bool rootexact;      // root's own storage holds the pointee (else it only outlives it)
    VarDef *rootfrom;    // for an inexact read-back: the container, for diagnostics
    bool writable;       // §9.5
    int reusable;        // the root is a reusable pool (§5.4): RU_SLOTS or RU_SLICES bits
    bool byteview;       // a bytes_of view over typed storage
    VarDef *intogs;      // a grow-shrink array it may point into that the root does not show
    bool cyclelocal;     // may be rooted where a recursive cycle stores nothing, unshown
    bool hidesclass;     // may be a parameter class's pointee the root does not show
    bool slotread;       // read out of a field, an element or a global (§3.10)
    TypeExpr *reached;   // the pointee type of the last reference or slice its path crossed (§3.5)
};
```

**Depth.** Every `VarDef` has a `depth`: 0 for globals, else the scope count
at its creation along the current compile-time call path. Because scopes
accumulate along the path, a callee's locals are always deeper than
anything its caller passed, and "A outlives B" is `Depth(A) <= Depth(B)`
for variables live together on one path. Two sentinels have the largest
depth: `temproot` (what a reference variable not bound yet points at) and
`cycleroot` (the result of a back edge whose root the cycle does not
determine, §3.11).

**Temporaries.** A value made without a name -- a literal or a call's
result, `str`'s text, a popped element -- is rooted at a `VarDef` of its
own (`TempRoot`, `istemp`), one past the depth of the scope it is made in:
codegen frees it when its statement ends (§6.2), or the block whose tail
made it, so it outlives the variables of the scopes that statement opens (a
`for` body over it, a `match` arm on it) and not the ones the statement
itself declares. What it holds is read back where it points (`intemp`,
§3.6), and a store record never names a temporary as the source of what it
holds (`RecordStore`): nothing on record describes a temporary's contents,
so the stored value's own root bounds them.

**Where a root comes from:**

| Expression | Root | Exact |
|---|---|---|
| a variable `x` (`Ident::Check`) | `x` | yes |
| `&lvalue` (`CheckRefOf`) | the lvalue's owner | as the path |
| a reference or slice variable (`RefProvOf`) | its committed binding (§3.7) | its binding's, weakened by rebinds |
| a reference read out of a field or element (`ContainerRead`) | the read-back rule (§3.6) | only with one candidate |
| a slice loaded through a reference to one (`SlotView`) | the reference's, where it bound a slice by reference; for an explicit `&` of a slice variable, that variable's binding; out of a container, or behind a parameter's class of an explicit `&s`, the read-back rule or the class as a bound | as that |
| `a.push(v)`, `a.alloc_ref(v)`, `&a[i]` | `a`'s root | `a`'s exactness |
| `a.alloc_slice(n)`, `a.realloc_slice(s, n)` | `a`'s root | `a`'s exactness |
| `a[lo..hi]` (`SliceExpr::Check`) | `a`'s root | `a`'s exactness |
| a call result (`CallResult`) | every root the callee's returns give (`RetRoot::alts`), mapped (`RetAltVal`: a parameter's class back to the argument's root at this site -- at a back edge, every argument the class's parameters get, merged -- a global or captured local as itself, null as static data) and merged as branches are (`MergeVals`) | only where they all map to one root exactly |
| an `if`, `match`, `block` or `loop` value (`MergeVals`) | the innermost of its branches' roots (`InnerRoot`) | only where they all name one root exactly |
| an array, struct or variant literal, and a call's value result | a temporary (`TempRoot`): whatever views it rather than being built from it views a temporary | no |
| a string literal | static data (null) | yes |
| `null` | none (adapts to any optional) | -- |

**Merged roots.** A value that may be any of several -- an `if`'s branches, a
reference variable's bindings before and after a rebind to another root at
its depth (`CheckRefRebindRoot`), the roots a call's callee returns
(`CallResult`) -- keeps one root, the innermost
(`InnerRoot`): the deeper, and at a tie the checked function's own variable,
since a temporary of the calling statement takes the depth of the function's
outermost scope (`ClassDepth`) but outlives the variables declared there; a
literal's references (`NoteLitElem`) and a container's contents
(`NoteContentRoot`) are bounded the same way. The rules that ask what
storage a root *is* would see only the one kept, so the merge also carries
what the others would have shown them: `intogs`, a grow-shrink array one may
point into (§3.5 rule 3, `GrowShrinkTaint`); `cyclelocal`, that one is rooted
where a recursive cycle may not store (rule 4, `CycleStorable`); and
`hidesclass`, that one is a parameter class's pointee, which a back edge may
give another array than the entry call did (§3.11). They travel with the
value: through a rebind, which keeps what the old binding hid, a load of a
slice through a reference (`DecayRef`, `SlotView`), a parameter
(`RootArg::intogs`, part of the key, makes the parameter's own `intogs` its
class root, or the parameter where it has no class), a function's returns
(`RetAlt::intogs`, `cyclelocal`, `hidesclass`) and the argument a call maps
a result through. A read-back clears them: whatever was stored passed those
rules already.

**Parameters and root classes.** A callee never sees the caller's variables;
it sees *classes*. `GetOrCreateSpec` groups the reference, slice and holder
arguments of a call by their canonical root: distinct exact roots get
distinct classes numbered by depth (so a class number is an outlives rank),
static data is class 0, and an *inexact* root always gets a class of its own
-- sharing a class asserts "the same array", which an inexact root does not
establish. `CheckSpecBody` then creates one synthetic `VarDef` per class,
carrying the call-site root's depth (`classfrom` remembers the root it came
from), and every parameter of the class is bound to it, `rootexact` within
the body: inside the body a class names one array, whatever the call site.
A holder parameter's class only bounds what it holds, by the deepest root
its references point into, so its contents (`contentexact`, and the store
event that stands for them) are that array exactly only where the argument's
references all point into one (`RootArg::heldexact`, part of the key), and
never in a `recursive fn`, whose back edges reuse the body whatever they
pass.
A reference to a slice bound by reference is rooted where the slice points,
but one written as an explicit `&s` of a slice variable is rooted at `s`
itself, so its class stands for the caller's variable and only bounds the
slice loaded through it (`RootArg::viewslot`, part of the key); a slice
parameter given a reference to a slice takes that slice's root
(`LoadSliceArgs`).
A temporary of the calling statement outlives the call, so its class takes
the body's own outermost depth instead (`ClassDepth`): the body may keep it
in its locals, but not in anything of the caller's. Classes are numbered by
these body depths, so a temporary ranks after every variable here too,
whatever depth it shares with one at the call site.
Whether two *different* classes are different arrays only the call sites
know. Codegen's stack-top caching (section 6.10) and the growth checks (§3.10)
ask `RootArg::exact`/`concrete`, which `SettleParamRootExactness` propagates
through the `via` links after every call site has been seen; the shrink rules
have each call judge the pairs of roots its callee could not tell apart
(§3.10, **Parameters' views**). What a body records against a
class -- a store into it (§3.5), a shrink or a growth of it (§3.10) -- a call
maps back to the root the class stands for there (`ClassArgRoot`): a
reference or slice argument's own, and a holder argument's holder root, what
its references point into; never the holder, which the callee received a
copy of.

**Depth keys.** A body is checked with its first call site's class depths
and then serves every call with the same key, so the key has to hold
everything the body's checks compare those depths with. The class numbers
order the classes, which is not all: the store rule (§3.5) lets a class be
stored into another's storage where the two share a depth and not where it
is merely deeper, a reference or slice variable is rebound only between
roots at one depth (§3.7), a value that may come from either of two classes
(an `if`'s, say, or a call's whose callee returns either) takes the deeper
one's root, the first where they tie, and only a class at depth 0, a
global's, may be stored into a global. A nested function also compares its classes with the depths of the
variables it captures, and a function given a function value does the same
while it checks that value's body. So each class carries `RootArg::depthkey`
as well: its body depth itself where that is within `EnvReach`, the scopes
open in the frames of the lexical environments the body can see -- its
`lexparent` and those its function values were written in, which hold every
variable it can name outside itself -- with only 0, the globals, for a body
that sees none; past that, its rank among the distinct depths of the call's
classes beyond, negated. Calls with equal keys agree on every such
comparison, and a call that does not gets a specialization of its own,
checked for its depths. A function called once with a global and once with
a local is split even where its body compares nothing; an extern function,
whose body is C, keeps every key 0. A back edge reuses the specialization in
progress whatever its depth keys, as whatever its classes (§3.11), and they
have no say in whether those classes still describe the arrays it passes
(`concrete`).

A class is a **pool class** (`VarDef::poolclass`) when every member is an
exactly rooted reference to a resizable-class value: no function in a
recursive cycle holds such a value across a call into the cycle (§7.8), so
references in a pool class are exempt from the cycle store rule.
`classpool` is the global pool every member is rooted in, when all agree
(§3.13). Any other class whose members are all exactly rooted references or
slices is a **threaded** candidate (`TypeCheck::ThreadedClass`), exempt in
the same way while every call back into its cycle passes it on (§3.11). A
synthetic class root is told from a variable by its `classfrom`: a variable
whose declaration is being checked has no type yet either.

### 3.5 The store rule and the store record

`FitsAt` is where §9.2's store rule is enforced. The **destination** of the
value being checked is `curdst` (`Dest`: a root, its exactness, whether the
destination is a reference variable itself, which makes the operation a
binding rather than a store, and what the path to it reached: the pointee
type of the last reference or slice it crossed, which the slots filled lie
in, `Prov::reached`); it is set by declarations, assignments, element
arguments (`ElemArg`), appends, and literal fields, and cleared to "no
destination" for call arguments and returns -- a parameter dies before its
argument's root, so an argument is never a store. When a reference, slice,
or **holder** value (a by-value struct, array or payload that contains plain
references or slices, `HoldsPlainRef`) meets a destination with a root:

1. a `cycleroot` value is rejected (pass-down only);
2. the value's root must be at or above the destination's depth, and where
   the destination's root is inexact, at or above the depth of every
   storage that root may stand for (`ShrinkTargets` over the type the path
   reached, or the slot's own where it crossed no reference: the root itself
   and each read-back candidate at its depth or outside, a parameter's class
   as the bound on the caller's storage behind it). Whatever owns the slot
   holds a value of the type reached, and whatever can hold one can hold the
   slot, so of the storage the slot's type admits this leaves out only what
   the slot cannot be in: beside a borrowed context's table of `Input`s, an
   argument list, which can hold an `Input`'s text but no `Input[>..]`;
3. a reference that may point into a grow-shrink array's elements may be
   bound to a variable but never stored (§5.2, `StoredIntoGrowShrink`: by
   its root, `GrowShrinkCanHold` with the byte-view exception `MayBeViewed`,
   by `Prov::intogs`, or for a reference to a slice by what the slice may
   point into); a global reference or slice variable is storage, so binding
   one is a store here;
4. inside a recursive cycle, only a reference into a global, a pool-class
   parameter or a local of an enclosing non-cycle function
   (`CycleStorable`), or into a threaded parameter class where every class
   among the destination's storages is threaded too (`ThreadStorable`,
   `ThreadedChain`, §3.11), and no merged value with `cyclelocal`, may be
   stored, besides a rebind of the activation's own reference variable
   (§7.8);
5. the store is **recorded**, on each of those storages.

Rule 3 does not wait for a destination: a literal's field or element is
storage wherever the literal lands, so `NoteLitElem` applies it to each one
(`StoredIntoGrowShrink`), in an argument or a result too. A back edge's
`cycleroot` may point into a grow-shrink array, and so may a parameter class
created from one (`IsGrowShrinkRoot`), which `NeverStoredError` words as
rule 1. No holder ever holds a reference into a grow-shrink array, then,
which is what lets the §5.2 shrink scan look at variables only (§3.10). Nor
may a variable that points into none be rebound to a value that may
(`CheckRefRebindRoot`): a store or return checked before the rebind -- later
in a loop, through a reference taken to it, in a nested function whose
checked body a later call reuses -- has already let it through.

An assignment through a reference (`PointeeAssign`) stores where the
reference points, which for a reference read out of a field is where the
read-back rule says (`DerefLValue`), not the field's container.

A declaration without a type annotation infers its type from the value, so
`FitsAt` has no destination type to check. `CheckBindingRoot` applies rule 2
instead, to every name in a multi-value declaration as well, and rule 3 to a
global. This prevents a variable from retaining a view of its statement's
temporary. The sentinels pass: a variable may hold what a reference not bound
yet, or a back edge's result, points at.

A branch's value -- an `if`'s branch, a `match` arm, a block's tail, a
`block`'s or `loop`'s breaks -- reaches whatever receives its construct's
value after the scopes the branch opened have ended, and a call's argument
is no exception, although it is no store. `CheckBranchRoot` applies rule 2
to it as the construct closes, against the depth the construct is in: a
variable deeper than that has ended, and so has a temporary made inside
the construct, one deeper than the temporaries of the construct's own
statement, which last (a `match`'s scrutinee among them). A declaration of
such a value meets this rule before `CheckBindingRoot`, so it is what keeps
a variable from viewing a local of the block whose value it is.

The record (`storeevents`, one `StoreEvent` per store, program-wide) is what
the grow-only shrink rule of §5.1 consults: the container, the stored
value's root and exactness, the pointee type, the byte-view flag, the source
container for a holder copy (a copy holds what its source holds), and the
line. `RecordStore` also maintains the container's `contentroot`: the
innermost root stored into it so far (`InnerRoot`), exact only while every
store agrees.
A store into a caller's storage -- through a parameter's class root -- is
kept on the specialization as a `classevent`, and `ApplyCalleeStores`
replays it at every call site with the class mapped back to the root it
stands for there (§3.4), what a holder's references point into for a holder
parameter. Semantically this includes stores through slices: writing a
reference into a viewed element changes the caller's container just as
writing through an array reference does. A permutation may preserve the
container's existing contents provenance, but a new incoming reference
must not disappear from its store effects. Only stores read back from the
same container preserve the existing record without adding an incoming root.
An event keeps the type its destination reached (`StoreEvent::reached`),
and one made through a class that only bounds the caller's storage behind
it is marked `bound`. The body checked such a store against one class, as
deep as the argument's root; where that root is inexact, or the event is a
bound one, the call widens the store to every storage the argument's root
may stand for (`ShrinkTargets` over that type), checks the stored value's
mapped root against each, and records it on each, a class of its own
caller's marked `bound` again. For a callee still being checked (a back
edge) it conservatively records every reference argument as stored into
every storage a reference argument whose pointee can hold references may be,
the pointee's type being what a store through it reaches.

Holder values carry their contents' bound as `holderroot`/`holderexact`
(§9.2's "implicitly generic over the fields' roots"): a literal's is the
deepest root among its reference initializers (`NoteLitElem`,
`HolderFromLit`); a variable's is its `contentroot`, unless a loop around
the read writes the variable, in which case the variable itself is the
bound; a container read's is the container's root, inexact, and out of a
temporary the temporary's own; a
holder parameter is keyed by its holder root class like a reference, and by
whether that class is exactly the one array it points into (§3.4), and the
class (its `ref.root`) bounds what is read back out of it or out of a copy
of it (§3.6).

### 3.6 Read-back roots

A reference read out of a container names a scope, not the storage it points
into; `ReadBackRoot` (`typecheck_types.h`) re-derives the owner exactly as
§9.5 describes:

* a self-relative reference inherits the container's root and exactness; an
  `in pool` reference is rooted at its pool, exactly;
* a container whose own root is a global: the candidates are the globals
  whose storage can hold the pointee type by value (`CanContain`: through
  elements and fields, stopping at references and slices, with a relative
  reference's storage holding its own type, which is what a slice of them
  points at), plus static data when the pointee is `u8` and the reference or
  slice read back is `const`, or nothing else can hold the pointee (a writable
  one is given static data only as a null or an empty slice);
* a container that is an exactly rooted local of the function being checked:
  the candidates are the visible locals declared at the container's depth or
  outside it that can hold the pointee, the pointees of reference and slice
  variables in scope with committed roots at that depth, the globals, and
  static data -- and the class root of every parameter in scope whose
  pointee, or whose by-value contents, lead through references to storage
  that can hold the pointee (`ReachesThroughRefs`). That storage is the
  caller's, which the body cannot enumerate: a holder parameter is a local,
  but what it holds its argument filled, and whatever the body copies it
  into holds the same. The class root only bounds it (`RootCandidates`
  lists it in `bounds`);
* a container reached through a caller's storage, or itself inexact: the
  container's root, inexact;
* a container that is a temporary (a literal or a call result, reached
  without crossing a reference, `LVal::intemp`): nothing in the temporary
  can own what it holds, which came from the literal's initializers or the
  callee's result, so the root is the temporary's holder root, exactly as
  exact (`TempContents`). `for` over a temporary and a `match` binder copied
  out of one take the same answer.

The root is the deepest candidate; it is exact only with exactly one
candidate, no static data and no bound. `ReadBackWhy` turns the candidate
list into the "may point into `pool` or `spare`" diagnostic the rules that
need identity produce, naming a bound as "the caller's storage behind" its
parameter. A byte view read back takes the container's `contentroot` where
one is known.

### 3.7 Reference variables commit to a root

A reference or slice variable is bound at its first non-null binding
(`BindRefProvenance`); before that it reads as `temproot` (`RefRootOf`), and
a null-only optional answers the read-back rule for its own depth
(`RefProvOf`). `CheckRefRebindRoot` implements §9.2's rebinding rule: the
same root keeps everything (an inexact new value only weakens exactness);
another root at the same depth keeps the lifetime bound but makes the
variable inexact; any other depth is an error. Since a loop body is checked
once, a read of the variable's exact root *inside a loop it was declared
outside of* is noted (`RefExactOf` sets `refidentityused`), and a later
same-depth rebind in that loop is then rejected rather than silently
invalidating the earlier read.

`PrebindLoopRefs` handles the `var last: Node? = null;` idiom before a loop:
every `.=` target in the body is scanned syntactically (with the cycle-root
scanner of §3.11), and where all of them resolve to one root the variable
takes it ahead of the loop (`refprebound`); the first real binding confirms
it and supplies the full provenance.

### 3.8 Writability

`const` is the `cq` bit on the type (§9.5); writability of a *value* is the
`writable` provenance bit. The checker keeps the two consistent at slots:
`constslot` (set by `SlotScope` for fields, elements, annotated variables,
assignment targets and literal initializers; cleared for call arguments and
returns) makes `FitsAt` reject a read-only reference or slice landing in a
slot whose type is not `const`. What is read out of a slot is therefore as
writable as the slot's type says (`ContainerRead`), and parameters and
results are generic over constness: `RootArg::writable` is part of the
specialization key, so a function given a literal and the same function
given a buffer are two specializations, and a write through the parameter
is an error only in the first.

`let` prevents whole-binding assignment (`NoLetAssign`, via
`LVal::letbound`) but does not restrict writes to contents. By-value `for`
and `match` bindings are copies; writes to them are rejected by
`NoCopyWrite` using `VarDef::copybind`. A variable's `const` type works the
other way round: a bare name's `LVal::writable` is its *contents'*
writability, which `&x` and the paths into it inherit, while a write of the
variable itself (`=`, a compound assignment, `++`/`--`) is left to those two
rules alone (`WholeWritable`), so a `var s: const u8[:]` re-slices itself.
`&x` of a `const` value is a `const T&`, a slice of read-only storage is a
`const T[:]`, a string literal is `const u8[:]`, a `bytes_of` view is never
writable, and `null` and `default<T>()` count as writable so they fit any
slot.

### 3.9 Flow state: definite assignment and narrowing

Each `VarDef` carries `assigned` and `narrowed` (the `T&` type an optional is
narrowed to). `SaveFlow`/`RestoreFlow`/`MergeFlow` snapshot and join them at
every branch: a fact holds after a join iff it holds in every reachable
branch, with `reachable` tracking divergence (`return`, `break`, `continue`,
`abort`, `exit`, a `guard` else). A branch that diverges has the null
"bottom" type, which unifies with anything (`UnifyBranch`, `MergeVals`), and
reads as `void` once the construct's node is left (`VoidIfBottom`).

`NarrowCond` narrows on `if`/`while`/`guard`/`assert` conditions, through
`!`, `&&` and `||` (a `&&`'s right operand may not run, so what it
un-narrows is recorded as `Binary::rightkills`), and `== null`/`!= null`.
Loops: narrowings of variables the body rebinds (`.=` in the body, in a
function value it calls, in a nested function, or in the `reboundoptionals`
summary of any callee's specialization) are killed before the body
(`KillNarrowingsAssignedIn`); a `while` condition's own narrowings hold in
the body on every iteration; after the loop `FinishLoopNarrowing` errors
when a narrowing the body assumed turned out to be rebound through a call the
name scan could not follow. A callee's rebinds of the caller's optionals
reach the caller through `ApplyCalleeRebinds`, and for a callee still being
checked through the syntactic `InProgressRebinds`.

### 3.10 The shrink rules, and growth and uses during construction

**Grow-only arrays** (§5.1). `pop`, `resize` and `clear` on a `[>..]`
(`CheckBuiltin` → `CheckGrowShrink` → `GrowOnlyShrinkAt`,
`typecheck_builtins.h`), and whole assignment of one or of a value holding
one (`CheckAssign`, `ResizableArrayIn`), pass in this order:

1. the receiver names the array's variable, or a reference variable or
   parameter whose root is known (the array behind it shrinks); not an
   element of another value, and not in a global initializer;
2. not a `reusable` pool;
3. the call is `standalone` (§2) and not inside a value-producing construct
   (`invalue`, set by `ValueRegion` for a valued `if`/`match`/`block`/`loop`
   and for a function value's body);
4. no held temporary of this statement may refer into it
   (`CheckHeldShrinks`), nor, where it is a reference to a slice or to a
   holder, may what it holds; an assignment's location counts as the slot
   alone, since the assignment overwrites what the slot holds;
5. no variable in scope, on any frame, may refer into it: a reference or
   slice variable whose pointee the array's elements can contain (or a byte
   view), rooted at it -- or a `var` at the same depth, or an inexact root
   at or below its depth, or a not-yet-bound variable declared at or below
   it -- **and used afterwards**; a holder variable into which a store of a
   reference into the array is on record (`HolderMayPointInto`, following
   holder copies through their source containers and judging a global source
   by its type) and used afterwards; a reference to a holder whose store
   record says the same, or to a slice that may point into it
   (`HeldRefsMayPointInto`: a slice variable named by an explicit `&` by its
   own binding, otherwise by the reference's root, as a bound, since stores
   through references to the slot may have replaced the slice), and used
   afterwards;
6. for a global receiver, every other global whose type can hold a reference
   to something the array contains counts as holding one;
7. the shrink is recorded for the callers (`NoteShrink`: `shrinkexternals`
   for globals and captured locals, `shrinkparams` for parameters), and so
   are the views still used that only the callers can tell apart from the
   array (`NoteLiveViews`, **Parameters' views** below);
8. inside a loop, the shrink is re-checked when the outermost loop ends
   against stores the rest of the body made (`pendingshrinks`,
   `ResolvePendingShrinks`), since the next iteration reaches it.

**Liveness** (`UsedAfter`) is syntactic: the variable's name occurs in a
later statement of an open block at or inside its scope, in that block's
tail, anywhere in an enclosing loop it was declared outside of, or in what
runs after the shrink within its own statement (`shrinkrest`): a whole
assignment's right-hand side, and the arguments print, str and format render
after the one being checked (**Format overloads** below); a `for` binding is
always live; `MentionsName` follows calls of nested functions by name into
their bodies. The test never depends on what the optimizer proves.

**Grow-shrink arrays** (§5.2): `ShrinkGrowShrink` runs from anywhere (a
local, a reference, a global, a struct's tail, whole assignment) and scans
held temporaries and the visible reference and slice variables only
(`CheckShrinkHolders`): references into such an array can never be stored
(§3.5 rule 3), so checking those variables and temporaries is sufficient.
That includes a reference to a slice variable, whose slice may view the
array: where the reference bound the slice by reference, the slice is at its
root, or, the variable having been rebound, at an array of that root's depth;
a slice variable named by an explicit `&` says so by its own binding, and a
parameter's class of one only bounds it.

**Slot reads** (`Prov::slotread`). For the same reason, a plain reference
or slice loaded out of a field or an element (`ReadBackLVal`, where
`LVal::isslot` says the location is one, not the pointee of a reference), a
global reference or slice variable (`RefProvOf`; rule 3 covers a global's
own bindings) and a `for` binder copying views out of an array (`CheckFor`)
never point into a grow-shrink array's elements, whatever their read-back
roots are. A slice of such a slice, and a reference into what it views, keep
the bit; `MergeVals`, a rebind (`CheckRefRebindRoot`) and a call's result
(`RetAlt::slotread` of every root its returns give, never a back edge's)
keep it only where every value does; and
crossing a reference drops it (`DerefLValue`, `DecayRef`, `Dot::Check`'s
auto-deref, a `for` loop or a builtin member through a reference, a whole
array passed to a slice parameter through one), since what a reference read
out of a field leads to may be a whole grow-shrink array, or a variable
holding a view into one. A slice loaded through a reference to a slice
variable named by an explicit `&` has what the variable's binding has
(`SlotView`). A relative reference never has it: it points within the array
that holds it. The §5.2 scans pass over a held temporary that has it, and
over a variable that has it unless a binding its record does not show could
put a view of the array there (`SlotReadMayRetarget`): one not bound yet, a
`var` rooted at the array's depth, and, inside a loop the `var` was declared
outside of, one rooted at that depth or deeper, which a rebind later in the
body may have left a branch's inexactly rooted value in. Neither exemption
reaches what a reference to a slice leads to (`HeldRefsMayPointInto`). The
§5.1 scans never use it: views of a grow-only array, byte views included,
are stored like any others.

**Inexact receivers.** Both scans run once per array the shrink may free
(`ShrinkThrough` over `ShrinkTargets`). An exact root is the array. An
inexact one -- a merge that kept the deeper of two roots (`MergeVals`), a
read-back (§3.6), a back edge's unknown result -- only bounds it, so the
array may be any one of its type owned at the root's depth or outside it:
every candidate `RootCandidates` finds for that type at the root's depth
(locals, pointees of references in scope, parameter classes, globals, and
the bounds standing for the caller's storage a parameter's references lead
to). The root itself is scanned first, and where it is such a bound, or its
own storage cannot hold the array (a holder it was read out of), the scans
filter pointees by the array's type rather than the root's, and
`NoteShrink` records it as `shrinkparambounds` or `shrinkexternalbounds`
with that type. Diagnostics name the other arrays as the receiver may point
at them.

**Calls** (`ApplyCalleeShrinks`): for a checked callee, each `shrinkparams`
entry becomes a shrink of the argument's root at the call, of every array
it bounds where it is inexact -- for a by-value holder parameter, of the
array its references point into (§3.4), exactly, since only a class that is
one array exactly records such an entry -- and each `shrinkexternals` entry
a shrink of that variable; each bound entry becomes a shrink of every array
of its type that the argument's root (a holder's contents' root) or the
external bounds. The parameter's pointee decides the scan, or for a holder
the array's own type: §5.1 where the array freed is grow-only (a struct's
tail included, `GrowOnlyTail`), §5.2 otherwise. For a callee still being
checked, the summary is incomplete: the callee's body is scanned textually
(`SyntacticShrinks`: `pop`/`resize`/`clear` receivers and assignment
targets, by parameter index, global name, and capture), every grow-shrink
global and every grow-shrink array reachable from the lexical parents'
locals (`LexicalLocals`, a function value's body among the parents) or
through the references an argument or such a local holds
(`ReachedThroughRefs`) counts as shrunk, and a grow-only local the text
names does too.

**Parameters' views** (`FnSpec::liveshrinks`). The scans see the
activation's variables only, and take a parameter's class for an array of
its own: never a global or captured array a caller passed a view of, nor
what another class names, which the arguments for the two may make one array
(an inexactly rooted argument gets a class of its own, §3.4, and a caller may
pass its own classes on). So after the scans `NoteLiveViews` looks again at
what is still used -- a reference or slice variable (`UsedAfter`, which
counts the rest of the statement, `shrinkrest`), a held temporary, a grow-only
holder by its store record (`EachHolderRoot`), and what a reference to a
slice or, for a grow-only array, to a holder reaches -- for a view only the
callers can tell apart from the array (`CallersJudge`): its root is a
parameter's class, or the array is. `NoteLiveShrink` keeps such a pair (a
`LiveShrink`) on the specialization when `MayAliasRoots` does not rule it out
and both roots are classes or storage outside the activation (globals, a
lexical parent's variables and classes), with the array's type where the
shrunk root only bounds it (`LiveShrink::bound`); a store a loop body's later
iteration brings to the shrink is added when the loop ends
(`ResolvePendingShrinks`). At a call, `ApplyCalleeLiveShrinks` maps each
pair's classes onto the roots of the arguments passed for them
(`ClassArgRoot`): two roots the caller can tell apart pass, one root or two
it cannot tell apart are an error at the call, which names the array as the
caller does, and a pair still open for the caller -- one of its roots is the
caller's class -- is kept on the caller's record in turn, under that
parameter's name. A callee in a recursive cycle still being checked has not
recorded all its pairs: every call checked while a cycle is open keeps its
arguments' roots (`cyclesites`), and once the outermost cycle on the path has
been checked they are mapped again until no record grows
(`ResolveCycleSites`). A pair that only a back edge's counted shrinks
produced is `guessed`, which its error says as "may shrink".

**Balanced calls** (§5.2). Each summary entry carries a `ShrinkBalance`,
the worst of the shrinks `NoteShrink` recorded against it: balanced,
assumed balanced (below), or not. A grow-shrink `resize` is balanced where
`ResizesToMark` holds: its length argument names a `let` of the activation
(`ownerspec` is the current real frame's specialization, so a mark a nested
function or a block's writer took before this activation began does not
count) whose initializer was exactly `X.len` (`VarDef::markof`, set by
`CheckVarDecl`), and `SamePath` finds that `X` and the receiver name the
same storage: the same variable, not a `var` reference, then the same
fields, none of them read out as a reference or slice. No other shrink in a
body is balanced: `pop`, `clear`, other resizes, whole assignment, and a
grow-only array's shrinks (`GrowOnlyShrinkAt` records every shrink
unbalanced). The body's own scans are unchanged, a balanced resize
included, and so are the pairs `NoteLiveViews` keeps for it. At a call to a
checked callee, `ApplyCalleeShrinks` first expands every entry into the
arrays it may shrink (`ShrinkTargets`), each with its entry's balance, then
judges them together: a grow-shrink array whose shrinks are all balanced,
and that no unbalanced one may be (`MayAliasRoots`, a bound taken as inexact
and two classes of one activation as one array), is recorded as a balanced
shrink without the §5.2 scan, and without pairs for the callers, since a
view rooted at a class was taken before the call too; every other array is
scanned and recorded unbalanced as before. The skipped scan includes what a
reference to a slice variable reaches (`HeldRefsMayPointInto`): the
variable still holds a slice taken before the call, since writing a view of
the array into it through a reference is a store (§3.5 rule 3).

A back edge is *assumed* balanced for every grow-shrink array it reaches
(`SB_ASSUMED`), unless its callee has already recorded an unbalanced
grow-shrink shrink (`FnSpec::unbalancedshrink`, set by `NoteShrink`). The
checks it skips run anyway (`KeepShrinkChecks`), the §5.2 scan and then
`NoteLiveViews`, whose pairs `NoteLiveShrink` puts aside (`livecapture`)
rather than on their records; the first error ends them. The callee joins
`assumedopen`, and a later call whose verdict rests on an assumed entry is
assumed too and keeps its checks the same way (`assumedshrinks`). When the
last callee in `assumedopen` has been checked (`CheckSpecBody` →
`SettleAssumedShrinks`), every summary the assumptions went into is
complete. They held if no callee assumed of has an unbalanced grow-shrink
shrink, with the assumed entries counted as balanced: a run of the cycle
then never shortens an array, by induction on how deeply it nests its
calls, and every assumed entry becomes balanced. Otherwise the first kept
error is reported, or else every kept pair goes into the record it was
found for and every assumed entry becomes unbalanced: what judging those
calls as shrinks from the start would have given. Those records belong to
calls checked while the cycle was open, which `ResolveCycleSites` maps again
afterwards, so `CheckSpecBody` settles the assumptions first.

**Format overloads** (§3.7). print, str and format check their arguments in
order (`CheckPrintable`): each one's value, then its rendering
(`CheckRenderable`), which specializes the user `format` overload of each
type it meets with that part's roots and permissions, and the builder --
the format call's receiver where the text lands in it, else a temporary --
and applies it as a call there (`UserFormatIn`: `ApplyCalleeShrinks`,
`ApplyCalleeRebinds`, `ApplyCalleeGrows`). Codegen evaluates each argument
just before rendering it (`EmitFormatInto`, `EmitStr`), so while argument i
is checked the ones after it are in `shrinkrest`: a shrink in its evaluation
(a call) or its rendering (an overload) finds a variable they name used
after it. The rest of the argument is rendered around its overloads, and is
held while they are applied (`heldtemps`, whose `render` names the builtin
for the error): the views `HoldValue` holds for any argument (an optional
reference or a slice, a non-fixed array's elements, a holder's references),
and, for a struct, enum or array argument no overload takes whole, a
reference to where it lies, which may be an element of the array an
overload clears (a plain reference argument has decayed to its pointee,
which lies where the reference points). `NoteLiveViews` sees both, so a
function rendering its parameter around an overload, or after one, keeps
the pair for its callers (**Parameters' views** above). A callee body
checked meanwhile starts with an empty `shrinkrest` (`CheckSpecBody`), as
with `heldtemps`: the call site applies its summary against the caller's.

**Growth during construction** (§1.3(4), §4.2). A value built in place at an
array's top or slot is under construction while its expression is checked,
and nothing may grow that array meanwhile: a pushed or pool-allocated
element that is variable-size or holds relative references of either form
(`BuiltInPlace`: `EmitPush` builds such an element in its slot, and
`EmitAlloc` a literal of one; any other fixed-size element is evaluated
before its slot is claimed, §6.5), a call's array result being appended
to a non-limited array (a `T[..]` result too, though `EmitAppend` builds it
on a temporary, §6.5), an appended literal of elements that are
variable-size or hold relative references of either form (which
`EmitAppend` builds in place), and the new contents of a whole
assignment of a resizable
(`CheckAssign`, `PointeeAssign`). Every growth is logged (`NoteGrow` →
`growlog`): `push`, `append`, `alloc_index`/`alloc_ref`, `format`,
`resize`, `to_bytes(a, out)`, whole assignment, and what a callee grows
(`ApplyCalleeGrows`: `growparams` mapped onto the roots their classes stand
for, §3.4, `growexternals` for globals and captured locals, recorded per
specialization by `NoteRootEvent` exactly as shrinks are; a callee still
being checked contributes what its text grows, `SyntacticGrows`, the shrink
scanner with the growth operations; a C function is taken to append to
every builder it is handed). When the constructed expression's check ends,
the growths logged meanwhile are judged against the constructed root
(`CheckGrowsSince`, `MayAliasRoots`): the same root conflicts; two
variables are distinct unless one is inexact and at or below the other's
depth (as in the held-temporary scan); a parameter class is distinct from a
variable of the activation named exactly, may be a global or a captured
local, and two classes of one activation are settled once every call site
has been seen (`growconflicts`, `ResolveGrowConflicts`): distinct only where
both are concrete and exact (§3.4). The log is per activation
(`CheckSpecBody` saves and clears it), so a callee's growths reach the
caller's constructions only through the summary.

**Uses during a whole assignment** (§4.4). The new contents are built over
the old ones, so once the right-hand side is checked, `CheckBuiltUses`
walks it for anything that may use the array: an identifier naming the
array's variable (or the value holding it), or a reference to either
(`ReachesBuilt`: `CanContain` of the array type, then `MayAliasRoots` as for
growth, `AL_DEFER` going to `growconflicts`); and for each call, each
specialization it may run (`spec`, `dispatch`, `fmtspecs`), what that
callee's body and its callees' name outside their own activations
(`NamedOutside`, walking the checked bodies with `RunChildren`, so function
value bodies are included): globals and variables of lexical parents. A
callee still being checked counts as naming every global and every variable
in `vars`. A rebind's target (`r .= …`) is not a use, and a field path
from the lvalue's own variable that parts from the lvalue's path at a flat
field is not one either (`FieldsApart`): `t.chars = f(t.font)`. Slices and
element references are left to the shrink scan, which sees the right-hand
side as after the shrink. A reference parameter's class may be a global or
captured array, so using one while building such an array is an error in
the body, as a growth of one would be.

### 3.11 Recursion

A call that reaches a specialization already `inprogress` is a back edge.
`ValidateCycle` requires the reused specialization's function to be
`recursive` and fully typed parameters from it inward, commits an unknown
return type to "returns nothing", and joins the cycle (`JoinCycle`): every
frame from the cycle's outermost member on the call path (`CycleHead`,
following `FnSpec::cyclelink`) inward is inside a call into the cycle, so
it is marked `incycle` and its cycle linked under that member, and a
variable of non-fixed class in scope in any of those frames is an error at
that frame's call, since every activation would keep it on a data stack of
its own (§7.8). A by-value non-fixed parameter is always in scope there. A
local whose own initializer calls into the cycle holds its stack across the
call too, being built in place: each frame counts the calls into a cycle it
has been inside, and `CheckCycleInit` compares the count across the
initializer once the local's type is known. A call reusing a finished
specialization whose cycle's outermost member is still in progress leads
back into that cycle as well and joins it the same way, so a later call to
a cycle member, or a function that reaches the cycle only through one, is
checked like a back edge; the explicit-type rule stays with back edges,
where inference would cross the cycle. `ValidatePoolArgs`
requires every pool-class and pool-named parameter to be passed the same
ultimate root the entry call passed (`UltimateRoot` follows `classfrom`
chains). The cycle store rule is §3.5 rule 4; the optimizer never inlines
into a cycle member.

**Threaded parameters.** A parameter class other than a pool, all of whose
members its creating call rooted exactly as references or slices, is
threaded (`ThreadedClass`, noted by `NoteThreadedClass`) until a call into
its cycle passes one of its parameters something other than a root exactly
at the same ultimate root, reached through classes that are pools or
threaded themselves (`ValidateThreadArgs`, `ThreadedChain`): in every
activation it then points where its creating call's argument did. Back
edges are checked, and so are calls reaching a finished member of a cycle
still being checked: its body's calls back into the cycle were checked with
what it was first given, and pass on whatever the call gives it. A class
created from another class's parameter, or given one at such a call, is
threaded only while that one is: it is recorded among that class's heirs,
which break with it, and one created from a broken class is born broken,
since each activation of the creating function makes the call anew with
what it was given. A reference rooted at a threaded class whose ultimate
root lies outside every cycle -- a global, or a variable of a function
neither recursive nor in a cycle, as for a free variable -- may be stored
inside the cycle (`ThreadStorable`), where every storage the destination
may be (`ShrinkTargets`) is a variable, a pool, or a threaded class too,
since the store lands where that class points, or leads, in each activation.
The store relies on the classes (`ThreadedClass::relied`): nothing makes a
back edge pass them on, so they are restricted only once a store needs them,
and a call that breaks one afterwards is an error at the first store that
relied on it, naming the call and the parameter (`Unthread`), while one that
broke it before makes the store the error (`CycleStoreError`). Only stores
rely. A merged value or rebound variable that may hide a threaded class is
`cyclelocal`, as for any class, since the store it reaches cannot see what
it hides, and a return rooted at one counts as `local` to `ReturnConflict`.
A back edge's result rooted at a threaded class is given as storable only
while the class stays so (`RetRoot::usedthreads`): a later return that the
cycle could not store breaks those classes instead of being an error, which
only matters to a store of the result.

A recursion whose types never repeat has no back edge: each round is a new
specialization, checked inside the one before, until the native stack runs
out. `GetOrCreateSpec` therefore refuses to create a specialization of a
function that already has `MAXNESTEDSPECS` (16) in progress, which are
exactly the ones on the current path since checking is depth-first (§7.8,
polymorphic recursion). The error names the instantiation the call would
have made, and the chain shows the ones before it.

**Cycle return roots** (`CycleRoots`, `typecheck_cycles.h`): a back edge
reaches a function whose returns are not checked yet, so before a
`recursive fn` body is checked, `Seed` predicts each reference return's roots
by a purely syntactic fixpoint over the returns of the function and of the
functions those returns call. A `RootSet` holds every `RootDesc` a return
may give -- a parameter index, a global, a free variable of an enclosing
function, static data, one of the function's own locals (meaningful only to
itself) -- or is unknown, the top; the join is their union.
`ScanExpr`/`ScanBase`/`ScanCall` read `X.push(...)`, `X.alloc_ref(...)`,
`&X[...]`, field and element steps whose declared types stay inside the
base's storage, subranges, string literals, reference variables through
their bindings, and calls to uniquely named functions through their own
sets, each parameter mapped to what the call passes. The fixpoint iterates
the closure of functions reached (`ReturnRootDescs`, at most 64 rounds); a
scan run outside it, of a loop's rebinds (§3.7), runs that fixpoint for each
function it calls before reading the function's sets.
`ResolveDesc` turns each alternative into a `RetAlt` of the specialization
(`RetRoot::pred`), which a back edge maps as any call maps a return's root
(`RetAltVal`): a back edge reuses the body whatever it passes, so a
parameter class maps through every argument the back edge gives the
class's parameters, merged, where an ordinary call, whose classes group the
arguments as the key's, takes the first. A set that is unknown, or names a
function's own local, predicts nothing (`predunknown`); until a return is
checked, back edges then get `cycleroot`, and after it they map that return,
which stands in for the prediction. A holder result (`HoldsPlainRef`, which
the scan is handed as its `HoldsRefs` callback) is seeded too, by the roots
of what it holds, which `CallResult`, `RecordReturn` and `RetAltVal` map and
check as they do a reference's; the scan reads no holder, so it is always
`predunknown`. What a checked return adds to a holder result's prediction,
the stand-in included, is inexact whatever the return: a back edge's holder
is taken apart by reading its fields, which re-derives their roots out of a
named holder (§3.6), inexactly wherever static data or a parameter's bound
is a candidate too, and an exact stand-in would be weakened by the first
return built from them. A reference result's stand-in stays as exact as its
return: a variable keeps its binding's root (§3.7), so a returned back-edge
result is as exact as it was given, and exactness is what `index_of` and an
exact shrink need. A parameter the key gave static data (class 0) has no
class root for a prediction to name, so a back edge that passes it storage
of its own gets a holder result that outlives nothing (`CallResult`); a
reference result is mapped as predicted there, which parsers passing a
literal key at the entry call rely on (`samples/18_json.goose`). Each real
return is checked against the
prediction as it is recorded (`ReturnConflict`): one it names narrows the
alternative for later back edges, but may not take back what an earlier one
was given (`RetRoot::usedexact`, `usedwritable`, `usedclean` for a
grow-shrink taint -- for a holder, `IntoGrowShrink` over what it holds, as
rule 3 of §3.5 stores it -- `usedstorable` for `cyclelocal`); one it missed
joins it where no back edge has used the prediction yet, and after that only
a root every activation shares and no deeper than any back edge's result
(`useddepth`). A return with `hidesclass` may be the pointee of a parameter
class no alternative names, which no mapping reaches: unless the prediction
names every class, the back edges checked after it get `cycleroot`
(`predlost`), and one after a back edge has used the prediction is an error
(`RetRoot::used`). The callers of the finished specialization map the real
returns' roots (`RetRoot::alts`), not the prediction.

### 3.12 Calls, generics, literal parameters, dispatch, function values

**Resolution** (`CheckCall`, `typecheck_calls.h`): a UFCS call tries the
member builtins first when the receiver is an array, then the functions the
name reaches (a nested function in scope, else the namespace overload set),
then the remaining builtins; a plain call tries functions then builtins, with
a user overload set sharing a builtin's name (`format`) taking the calls it
matches. `ResolveCall` checks the arguments once bottom-up (phase 1),
rewriting non-fixed lvalues to references, tries every candidate
(`TryMatch`, tiers: 0 exact, 1 generic binding, 2 coercion; the unique best
tier wins), falls back to tag dispatch, then undoes that reference for a
parameter that takes the value -- a slice, or a fixed-class type an array of
another kind constructs by copy (`UnrefForValueParam`) -- re-checks each
argument against the resolved parameter type (phase 2, `CheckArg`), applies
the callee's shrinks, growths and rebinds, and maps the result roots
(`CallResult`). Generic
inference is structural (`BindTypes`, through the one coercion generics see:
whole array to slice), untyped parameters bind the argument's natural type
(a reference stays a reference), literal arguments unify last so a typed
argument fixes the type variable, and leftover generics bind function values
in order.

A control construct as an argument has no destination type in phase 1,
where its value would copy the branch taken (§3.3). On the argument's value
path -- the argument itself, a UFCS receiver included, and each branch a
construct on it checks next, a nested construct, a block's tail or a
break's value (`argpath`, `PathScope`, `Scope::onargpath`) --
`CheckBranchCopy` notes that copy on the value (`Val::implicitcopy`)
instead of reporting it, and whether every branch is non-fixed storage or
a reference to it (`Val::storagebranches`, merged by `MergeVals`). Such a
value is not rewritten to a reference, but matches a reference parameter
as an lvalue does (`UnifyArgRaw`), and `BindBranchesByRef` checks it
against that parameter before the specialization and the callee's effects
are keyed on it: each branch binds by reference (`AutoRef`), and the
argument takes the branches' merged root and writability. At any other
parameter phase 2 reports the copy, an untyped one included, which takes
the construct's value type. Tag dispatch binds such a construct at the
dispatch position by reference as well, and reports a copy noted on one it
dispatches by value, which phase 2 does not check again; a member builtin
reports one noted on its receiver, which it takes as checked; a function
value's declared reference parameter takes its provenance from the
argument's check against it.

The tier is the worst match of any argument, not a sum of conversion
costs. Concrete exact matches beat generic exact matches, which beat any
candidate requiring adaptation; two candidates at the same best tier are
ambiguous, without a declaration-order or "more specific" tiebreaker.
Reference transparency and implicit lvalue binding participate in matching;
writing `&` can therefore select a different overload. The expected result
type does not select an overload or infer its generics. Explicit type
arguments bind the initial generic parameters in declaration order. A
selected body's error is an error at that call, not a reason to retry a
worse overload. Dispatch is attempted only when no ordinary candidate
matches, so an overload for the entire ADT takes precedence over its cases.

**Multiple results.** Results are not tuples. An ordinary value use of a
call takes its first result and discards the others; a call statement
discards all of them, while still executing the call. `let a, b = f()`
requires exactly that many results and has no shared type annotation.
`return f()` forwards all results of a multi-result call and adapts each
one to the corresponding declared return type. Reference decay and lifetime
checks apply separately to each received result. Inference fixes a
function's result types from the first checked return; subsequent returns
must fit them, rather than widening the result by a whole-body join.

**Literal parameters** (§7.7): an argument that is a literal (or a literal
parameter passed on) to an untyped parameter, or to a bare type variable no
typed argument binds, makes the parameter a literal parameter: part of the
key (`litparams`), read inside the body as a `Val` with `unsized` set and no
constant value. Every place the value adapts to a type (`FitsAt`,
`UnifyNumeric`, branch merging) records a `LitAdapt` on the owning
specialization; passing it on as a literal records a `LitFlow`;
`VerifyLiterals` checks each call site's literal against the closure of
those records after the whole program is checked. Codegen passes the
parameter at its nominal type and casts at each use.

**Tag dispatch** (`TryDispatch`, §8.2): for each argument position holding an
enum (or a reference to one), every variant type is tried against the
overload set and exactly one candidate must match per variant; one such
position is allowed; each arm is specialized, return counts and types and
the non-dispatch parameter types must agree; a fixed-mode scrutinee
dispatches by value even through a reference; the result's provenance is the
merge of the arms' (deeper root, exact only when the same, writable only if
all are).

**Nested functions** (`DeclareLocalFn`, §7.5): checking a declaration
records a `DeclSite` for the function, under the environment declaring it
(`declsiteof`): the variables in scope there, innermost first, with the
index each holds in `vars`; and the functions it may call, every one
declared in the blocks around the declaration (`blockpos`), the latest at or
before it first and then those after it, followed by those the declaring
body sees outside its own scopes. The frame `CheckSpecBody` pushes for a
specialization keeps the site as `decl`, and `LookupVar` and
`LookupLocalFnEnv` look there past the body's own scopes (`ForOuterVars`,
`ForOuterFns`) instead of in the declaring frame as the call finds it, so a
scope around the call that shadows or adds a name changes nothing and one
specialization per `lexparent` serves every call. `GetOrCreateSpec` rejects
a call that reaches a nested function before its declaration is checked (a
nested function declared earlier calling it), since the variables its site
lists do not exist yet. Rechecking a declaration, loop or match binding
resets its checking state while preserving its `VarDef` identity and
capture flag (`ResetLocal`), so cached specializations still name the
binding codegen declares. `VisibleVars`, which the shrink rules and the
read-back candidates enumerate (§3.6, §3.10), keeps the lexical parents'
frames as the call finds them: a reference handed to the body, a later nested function's
result or one a function value written at the call returns, can point into
a variable the declaration does not see.

**Function values** (`CheckFunValCall`, §7.6) are restricted by `CheckV` to
names and block literals; runtime expressions producing them are rejected
rather than having their effects discarded. Returning a function value or
constructing an array of them is also rejected. A named function value
resolves as a call in the environment its declaration is in, for a nested
function the scope declaring it, whatever function names it
(`LookupLocalFnEnv`, as for a call); a block is cloned into
`Call::fvbody` and checked inline in a frame marked `isfunval` whose lexical
lookups chain to the definer, with parameters as locals bound to the
arguments (reference provenance and literal-ness carried through). The body
as that check sees it is a lexical environment of its own, the frame's
`lexspec`: an `FnSpec` marked `isfunval` (`NewFunValEnv`, outside
`ast.fnspecs`), whose `lexparent` is where the value was written. A nested
function declared in the body has it as `lexparent`, and a function value
written there or a nested function of the body named as one carries it, so
their lookups chain through the body's frame (`FrameOfSpec`, `LexFrame`) to
its parameters and locals (§7.5). Each check clones the body afresh and
makes a new environment with it, so a specialization
keyed on one captures that clone's variables: another call site of the
value, or the same call checked again as an argument, specializes anew
instead of reusing one whose captures codegen never declares. `NamedSpec`
gives the named function around such an environment, which a plain `return`
inside it targets (`CheckReturn`); that
is how HOF-based iteration returns. `LexicalLocals` gives a nested recursive
call's lexical parents' locals, the body's included (§3.10).

**`return ... from`** (`CheckReturn`, §7.9): the target resolves in the
returning function's definition context (a nested function in scope, else
the overload set) and is the innermost frame whose function is in that set;
every specialization between records the target in `needs`, and every path
by which a specialization was later reused (`neededges`) is re-validated
against it (`ValidateNeeds`, `AddNeed`). A long-distance return may carry
only references rooted at globals or static data -- more conservative than
the spec's "rooted at or above the target's frame" (TODO 0d). The returns of
one function may give different roots: `RecordReturn` keeps one `RetAlt` per
distinct root, the guarantees on it ANDed, and every call maps and merges
them (`CallResult`, §3.4).

### 3.13 Relative references and pools

`ResolvePools` binds every `T&<w in pool>` type to its global before any type
is compared; the pool is part of the type's identity (`TypeEq`). `ValidatePool`
requires a `var` grow-only global whose elements can contain the pointee.
`PoolOf(root)` answers which global pool a root is: the global itself, or a
parameter class's `classpool`, agreed by every call site.

The relative store rule in `FitsAt` (§3.9): `null` stores into any optional
relative slot; otherwise the destination must be exact (or name a pool), the
value must be exactly rooted, and the two roots must coincide -- the
destination's root for the self-relative form, the named pool for `in pool`.
The diagnostic names the read-back candidates when the value is inexact.
`index_of` needs the same exactness at the receiver. `self` (`CheckSelfInit`)
is only the whole initializer of a non-optional relative field whose pointee
is the literal's own type; the `in pool` form additionally needs the literal
to be under construction inside that pool. `NoRelRefCopy` rejects copying any
value holding self-relative references except a literal built in place
(`HasRelRefT` excludes `in pool` fields, which copy fine); the same rule
rejects by-value `for` and `match` bindings of such elements and payloads,
the elements `append` copies from anything but a literal (an element that
is itself a self-relative reference included), and a `resize` fill value,
literal or not, which is built once and copied into every slot added.
Varint-width relative references are construction-only.

### 3.14 Arithmetic, constants, and the rest

`UnifyNumeric` (§6.1) unifies operands: equal types stand, a constant adapts
to the other operand's type (fit checked), a literal parameter adapts to a
typed operand, one implicit widening (`ImplicitInt`: wider same signedness,
or unsigned into strictly wider signed) wins, and the `u64`-against-signed
comparison is admitted only when the signed side's `nonneg` bit is set --
a syntactic bit from a non-negative literal, a `.len`/`.cap`, or a `let`
bound to one (`CheckVarDecl` copies it to the `VarDef` of a `let`). The
checker folds constants at the operands' type (`FoldInt`, over the shared
`FoldIntOp` of `ast.h`: unsigned wraps, a shift wraps at its width too, and
a signed result that leaves the type is left unfolded for the runtime to
abort on or wrap; a constant zero divisor is an error here rather than an
abort); `ConstIntValue` evaluates the constant expressions of array sizes,
fill counts and match arms through `let` globals and arithmetic.

Builtins are one X-macro table (`builtins.h`) driving arity, receiver kinds,
provenance requirements and simple signatures; `CheckBuiltin` handles the
custom ones. `print`/`str`/`format` check renderability per type
(`CheckRenderable`) and look up a user `format` overload in the type's
namespace, then globally, specializing it with each rendered argument's
permissions and provenance (`fmtspecs`, `fmtcontexts`). Nested hooks obey
the same constraints. Their effects are applied between rendered arguments
(§3.10, **Format overloads**).
`to_bytes`/`bytes_of` require `ImageSafe` element types (no plain references,
slices or `in pool` references), `from_bytes` the stricter `VerifiableElem`
(every relative reference points at an element or a variant of it); a
`bytes_of` result carries `byteview` provenance, which the shrink scans treat
as able to point into any storage the root could image. Thread entry points
get one specialization each (`EnsureThreadSpec`) with flat parameters, and
`CheckThreadGlobals` walks a thread program's call graph rejecting non-flat
globals.

`embed_shader` is a compile-time graphics extension (`EmbedShader`, `gfx.h`).
With one argument it reads a file relative to the Goose file containing
the call (or an absolute path), inferring the GLSL stage from `.vert`,
`.frag` or `.comp`. With several arguments, the first is `"vert"`, `"frag"`
or `"comp"`; the rest are GLSL parts joined with newlines, with includes
relative to the calling file. Arguments must be string literals or chains
of immutable global names initialized by string literals, not arbitrary
constant expressions or locals. Compilation failure is a Goose compile
error. The result is a static, read-only `const u8[:]` shader blob, with no
runtime reads of the named globals. Its platform blob format and the
native graphics library interface are separate compatibility surfaces
(`gfx/gfx_blob.h`, `gfx/gfx_api.h`, `docs/design/gfx.md`), not general Goose
value layouts.

A pool's kind travels with its provenance as bits, `RU_SLOTS` for `reusable`
and `RU_SLICES` for `reusable[]` (`Prov::reusable`, `RootArg::reusable`), so
merging two branches keeps only what both allow, and the table's
`BF_REUSABLE`/`BF_SLICEPOOL` flags each require their bit. A function checked
standalone (`CheckUnreached`) assumes both. `free_slice` and `realloc_slice`
use `index_of`'s exact-root test (`RootedAtReceiver`) only to leave out a
run-time test: a slice it does not place in the pool is an error when it is
rooted exactly at a global or a variable of the checked function, and
otherwise sets `Call::poolcheck`, which codegen turns into a range test.
`alloc_slice` and `realloc_slice` need an element type with a default value,
and `realloc_slice` one without self-relative references (`HasRelRefT`),
since it may copy the slice.

### 3.15 Evaluation and observable storage

Left-to-right evaluation in spec §2 includes taking a value, not merely
computing a path that C might read later. These distinctions matter in the
presence of aliases (`Snapshot`, `IndexLoc`, `GenSlice`, `Assign::CgStmt`,
`EmitDispatch`):

* An earlier scalar or fixed by-value argument is sampled before later
  operands or arguments run. A reference or slice samples its address or
  view; it does not copy the storage it reaches. Later permitted writes to
  that storage remain visible through the view.
* Indexing and slicing establish the receiver's element region and length
  before evaluating the index or bounds, then evaluate bounds left to
  right. The index's element load follows its index evaluation. A later
  growth does not enlarge the earlier receiver view for this operation;
  an invalidating shrink must be rejected by the lifetime rules.
* Assignment resolves its destination before evaluating its right-hand
  side. Compound assignment also samples the old value first. Whole
  resizable assignment has the clear-before-construction semantics of
  spec §4.4; overlapping permitted copies have memmove semantics.
* A range/count `for` samples its bounds once, before iteration, and runs
  zero times for an empty or reversed range. Array iteration visits
  increasing indices and tests the current array length at each iteration,
  so permitted appends can extend the traversal. A slice's length is its
  own view length, not its owner's current length. A sequential cursor
  advances past the current element even on `continue`.
* A by-value result that is immediately viewed still has its own temporary
  storage through the containing statement. Inlining or selecting a
  constant branch must preserve that copy when replacing it with the
  source lvalue would expose later writes (section 4, "Views of copies").

Effects include hidden calls: field defaults, rendering hooks, every
dispatch arm, and invoked function-value bodies. All must be considered
when checking shrinking, construction conflicts, optional narrowing and
when retaining bounds facts. Optimizations may remove effects only after
the program has passed the language's static checks.

---

## 4. The optimizer

`Optimizer` (`optimize.h`) rewrites the checked bodies in place before BCE
and codegen. Its one hard rule: nothing it does changes what a release build
computes, and an operation that would abort at runtime (division by zero, a
failing `as`, an out-of-bounds index) is never folded into a value or folded
away. The one licence it takes is §6.2's: debug-build overflow detection is
per operation as it executes, so tail-recursion elimination may regroup an
associative chain.

**Order.** `ReachRoots` marks the specializations reachable from `main`, the
thread entry points and global initializers (including expanded defaults), counts
call sites per specialization (`uses`), and records a call-graph postorder
(callees first; a cycle is cut at its back edge). `Analyze` collects, over
every live body first, how often each variable is written and how often its
address is taken (`facts`), so a write in another specialization's function
value is visible before any rewriting. Then: global initializers (fold
only), every specialization in postorder (`SetupBaseCase`, `OptBlock`,
`TailRecurse`, `Scan`), globals again (now able to inline), and a final
reachability pass so specializations whose every call was inlined go dead
and codegen skips them.

**Constant propagation** (`OptStmt`, `Ident::Opt`): a single-name declaration
of a scalar with a literal initializer, never written and never
address-taken, is entered in `consts`; every later use in the same walk
becomes the literal and the declaration is dropped unless captured. Constant
`let` globals propagate into every body. A parameter bound to a literal
argument is substituted at inlining time (`Inliner::BindArg`).

**Folding** (`Opt` per node): integer operators at the operands' checked
width (`FoldIntOp`, `ast.h`, which the checker folds with too: unsigned and
shifts wrap, a signed result only when it fits), comparisons at the
operands' signedness, float operators (at `f32` precision when both
operands are `f32`), `&&`/`||`
with a constant left, `!`, `~`, unary minus, casts (checked casts only when
exact; a float-to-int only in range and integral), `.len` of a fixed array
and `.cap` of a static-capacity limited array on a plain variable receiver,
`if` on a constant condition, `match` on a constant integer, `while false`,
`guard` on a constant, `assert(true)`, and statements after a `return`,
`break` or `continue` in a block.

**Inlining** (`TryInline`): a call is replaced by an `InlineBlock` holding
the callee's parameter bindings as `VarDecl`s (marked `inline_arg`, evaluated
in the caller's scope so an argument temporary outlives the returned value)
followed by a `Cp1` copy of the body; a `Return` whose target is the inlined
function exits the block with its values (`Return::CgStmt`), which is what
keeps early returns, `return from` and function-value returns working through
any nesting. The copy is re-folded so substituted constants cascade. A
trivial result unwraps to a plain expression when its type survives. A call
passed where a slice is expected keeps that slice as its checked type while
the body returns the array; codegen builds the array where the call would
have put its result, a temporary of the caller's scope, and slices it whole
(`InlineBlock::CgAny`), since the body's own scopes release their storage
when it exits.
The thresholds per call site of callee K: inline if K is used once, or its
post-optimization node count is below NC, or count times uses is below NCU
(`-O1`: 8/48, `-O2`: 16/96). Never inlined (`Scan`): a `recursive` function
or cycle member, a `thread_fn`, a function returning more than one value, and
a body that a *separate* live tree still references -- a remaining call to
a nested function or to a specialization with bound function values reaches
its locals as free variables, and a remaining callee that does `return ...
from` it needs its frame. Nothing is inlined *into* a cycle member (its
locals would become the cycle's own).

**Views of copies** (`OptViewed`). The value of a call, of a bare block and
of an `if` or `match` is a temporary copy (§9.2), and the checker takes a
view of it for a view of a temporary of its own (`TempRoot`), which nothing
else in the statement writes or shrinks. Unwrapping an inlined result, or
folding a statement-less block into its tail and an `if` or `match` into
the branch taken, can reduce that value to a path into the storage it was
copied from: harmless where the value is copied out, but where it is viewed
where it stands the view would then see the rest of the statement write or
shrink that storage. Those places are a slice's base, an index's base where
the index runs code (the element is read after it; `CodeFree`), a `for`'s
iterable, the operand of `&` (explicit, or a reference parameter binding
it), a member builtin's receiver (`bytes_of` returns a view of it), and the
base of a field or element that is itself viewed or is an array passed
whole to a slice parameter. There such a path goes back into a block, which
codegen evaluates into a temporary as the construct would have:
`f(get()[..], a.pop())` for `fn get() -> i64[3] { a[0] }` would otherwise
hand `f` a view of the slot the pop frees.

**The nesting limit** (`MAXNEST`, 64). Every inlined body is a C block of
its own, and C compilers limit how deep blocks nest in one function: MSVC
to 128 (C1061), clang outside its MSVC-compatible mode to 256
(`-fbracket-depth`). Under the thresholds alone, a chain of single-use
functions, each calling the next, folds into one body as deep as the chain
is long; and as each inline copies a body with everything already inlined
into it, while the original stays allocated, the cost is quadratic in the
chain's length: 6.5 GB and most of a minute for a chain of 2000 in a Debug
build. So, whatever the thresholds say, a call is inlined only while the C
blocks around it plus those the callee's body nests stay within `MAXNEST`.
Such a chain then folds into one body per 64 levels, each calling the next,
and takes 2 s and 240 MB (0.7 s and 170 MB at `-O0`). Both counts are of
what codegen opens a C block for: every `Block` (a function or inlined body,
an arm, a loop body), one around an `else` that is not a `Block`, the right
operand of `&&` and `||`, a `while` condition (tested inside the loop), the
arguments of a function-value call (bound inside its block) and an array's
fill value (conservatively counted with its repetition loop), and two
around a match arm (a switch and its case); `Around` counts the ones around
a child. `Scan` records the deepest
nesting of each final body (`InlineInfo::nest`, the `nest` of `--specs`),
and the walk keeps the count around the node it is at (`depth`), taken as
the blocks stand when it gets there: one that folds away afterwards only
makes it cautious. The other half of MSVC's limit is for the blocks codegen
opens around runtime work, which are not counted. Ordinary programs stay
far below the limit: the deepest inline in the tests, samples and
benchmarks lands 18 blocks deep.

**Base-case inlining** (`BaseCaseInliner`, `optimize_basecase.h`): a
`recursive fn` whose body *starts* with `if c { return e; }` (or the negated
`guard c else { return e; }`), with every parameter fixed-size, `c` a pure
read of parameters and globals, and `e` calling nothing in the cycle, gets
each direct self-call `f(a...)` rewritten to `{ let p = a; ...; if c[p] {
e[p] } else { f(p...) } }` under the inliner's size thresholds. This removes
half the calls of a complete tree walk. The bindings are marked
`inline_arg`, like an inlined call's, and made in the scope around the block
(`GenInlineArgs`): a slice argument can view a temporary, which has to last
while the callee runs and while the block's value is used. It does not fire
when a statement precedes the base case, on mutual recursion, on
UFCS-spelled self-calls, or on a self-call whose array result is passed
where a slice is expected: the array has to outlive the `if`, and each arm
would build it in a scope of its own.

**Accumulator tail-recursion elimination** (`TailRecursion`,
`optimize_tre.h`, `-O1` and above): a directly self-recursive
specialization returning one machine integer, whose tail returns are
`self(args)` or `E op self(args)` (or `self(args) op E` with `E` pure) with
one associative operator (`+ * & | ^`), becomes `var acc = identity; loop {
... }`: each tail call turns into `acc op= E`, rebindings of the parameters
(all at once through temporaries when several change) and `continue`; every
other return folds `acc` in. Self-calls in non-tail position stay calls, so
`1 + check(l) + check(r)` loses its right spine only. Parameters must be
rebindable (scalars, or plain non-fat, non-pool, non-relative references
whose new value is root-stable), tails inside loops or function-value bodies
are skipped, and a callee that can `return ... from` the function disables
the transform. The `var t = ...; if c { t op= self(x) } t` spelling is
restated as a return first (`AccVarForm`).

---

## 5. Bounds-check elimination

`BCE` (`bce.h`) runs over every live specialization after the optimizer and
marks the `Index` and `SliceExpr` nodes whose runtime check cannot fire;
codegen then omits the check. It is required only to be sound; a program's
meaning never depends on it (§10.5). It is also where two codegen decisions
are taken that are not about checks at all: `ForLoop::fixedlen` and the
per-loop `hoistrefs` (§5.10).

### 5.1 The domain

A flow-sensitive **difference-constraint** domain: facts of the form
`l <= r + c` over *bases*, where a base is the constant zero, an integer
variable, the length of a *place* (an array or slice location), or a
one-shot temporary base for a value the operation itself bounds. A query
"is `l <= r + c` provable" is a shortest path over the fact graph plus the
axioms (`Dist`, Bellman-Ford with saturating weights); the smallest provable
`c` is the distance. Facts are capped at 200 per state (the oldest is
dropped), offsets on variable bases at 2^32, constants at 2^60, and
anything larger is "unprovable" rather than wrong.

Every query uses these **axioms**: `0 <= len <= 2^48` for every length
(§10.4 is what makes `len - 1`, `i + 1` and `len + len` provably free of
overflow), the storage range of every sub-64-bit integer variable, and the
invariants granted by the recording pass (§5.7).

### 5.2 Bases and generations

Every base carries a **generation**: `(v, g)` is the value the variable or
length held while generation `g` was current. A **kill** (`BumpVar`,
`BumpPlace`) opens a new generation rather than deleting facts, so what was
known about the old value stays true about the pinned old value. That is what
makes a `for i in n` loop's entry snapshot sound across iterations, and what
lets an `Index` compare its receiver's length as it was *before* the index
expression ran (`Index::BceWalk` drops the length term if evaluating the
index moved anything).

A length mutation with known direction bridges the generations: a grow adds
`old <= new`, a shrink `new <= old`, so a bound established before a `push`
still holds after it and a `pop` cuts it. A push or a constant-length append
is an exact step (`ExactLenStep`: `new == old + k`); `clear`, `resize(n)`,
a fresh literal, a slice binding and a rebind to a known place set the length
exactly (`ExactLenIs`). Monotone integer variables (§5.7) get the same
bridges across their kills.

### 5.3 Terms

`TermOf` reads a machine-integer expression as *base + offset*: a literal;
an integer variable that is not `u64` or `varint`; `.len` of a place (a
fixed array's length is a constant); `x + c`/`x - c` at `i64` only (narrower
widths wrap below the 64-bit math the facts are stated in); `a % b` and
`a & b`, which land in `[0, b]` on a fresh base when `b` is provably
non-negative (a negative signed mask proves nothing); a cast whose value the
facts already place inside the target's range, which is the identity and
carries its operand's term; `a * b` and `a ± b` with two moving operands,
handled by *intervals*: where both operands have finite constant bounds the
result gets a fresh base bounded by the four corner products or the summed
ranges, stated only when it fits the operation's own width (release-mode
wrapping, §6.2); and the value of an `InlineBlock` or bare block. An
operation's width is its operands' type (`OpType`), not its `exprtype`,
which is the possibly wider slot the value lands in (§3.3). Derived
terms are memoized per node and invalidated by any generation change
(`Derived`), and an expression whose later operand changed tracked state is
not rebuilt from its operands' current names (`effectfulterms`).

A term is admissible as a comparison side (`CmpAdmissible`) only if the
machine comparison equals the mathematical one: a variable with a zero
offset, any constant, a length plus a small constant.

### 5.4 Places and aliasing

A **place** is an array or slice location nameable as a variable plus a chain
of struct fields, with reference crossings marked, and an "ultimate" owner:
owned storage (the root variable), static data, or opaque (a stored
reference read on the way, an inexact root). `UltOf` follows reference and
slice provenance to the owning variable; a synthetic parameter class is
opaque as storage but two references in distinct classes are known distinct.
Storage is **reachable** to a callee or an unknown reference only if its
owner is a global, is captured, or has its address taken (`Reach`): creating
any reference into a variable requires one of those. `AffectedByWrite`
decides whether a write to one place, to owned storage, or to unknown
storage may name another place; distinct plain paths under one owner are
distinct arrays.

### 5.5 Kills

What invalidates a fact: a grow or shrink builtin (the receiver takes the
exact delta, every place it may alias the directional bump); a whole-value
write to a variable or through a chain (`StorageWriteKill`: everything under
that owner, or everything reachable when the target is unknown); a rebind of
a reference or slice variable (`RebindKill`); a pointee write through a
reference (`PointeeWriteKill`); a repeated declaration inside a loop; and a
call. A call with no summary kills everything reachable plus every reachable
integer variable (`KillByCall`); a call with a summary kills exactly what the
summary names (§5.8). Integer writes shift facts in place when the pre-state
provably cannot wrap (`ShiftCore`: `i++` moves every fact about `i` by one)
and kill the variable otherwise; a set `v = e` re-pins `v` and records
`v == e` when `e` has a term that cannot have wrapped.

### 5.6 Facts from control flow

`CondFacts` adds the comparison of an `if`, `while`, `guard`, `assert` or
short-circuit operand (both senses, through `!`, `&&` and `||`), an integer
`match` arm's range, and a `for` header's bounds (`0 <= i < n` against the
snapshots taken at loop entry for ranges and counts, against the re-read
length for arrays and slices). A condition that itself changed tracked state
-- a mutating call inside it -- adds nothing (`HasKillEffects`), since the
comparison ran against pre-kill values. A completed `pop` proves the old
length was at least one; a `resize` states the new length when the count's
term survived the fill value's evaluation.

Joins are the **meet** of the branch states (`Meet`: a fact survives with the
weaker constant, generations take the maximum); a branch that diverges
contributes nothing.

### 5.7 Loops and invariants

Every loop body is walked first in kills-only mode (`StripKills`) to
invalidate whatever an earlier iteration may have changed, then for real;
a `while` condition's facts re-establish at every body entry, and its
negation holds after the loop only if the body has no `break`. Facts
established by an `EarlyBlock` or an `InlineBlock` with early exits are
likewise reduced to their kills at the join.

Since a kill loses the `i >= 0` and `i <= len` facts that the classic
`var i = 0; while i < a.len { ...; i++; }` needs, each body runs in three
modes (`RunSpec`): a **kills** walk producing the whole-body summary of
bumped places and rebound variables; a **recording** walk that, for every
eligible local integer variable (single-name declaration, not captured, not
address-taken, no compound write other than `±constant`), checks that every
write preserves `v >= 0`, that no write can wrap at the variable's width,
and that `v <= len(P)` survives for each place `P` the variable indexes or
bounds and that is never shrunk or re-bound in the body (growth alone never
breaks it); and the **judging** walk, which is granted the survivors as
axioms (`ge0`, `lelen`) and the wrap-free, single-direction variables that
are never plainly assigned as monotone (`mono`). Global integer variables
get the same `>= 0` treatment across the whole program
(`ValidateGlobalInvariants`: the initializer is the base case, every write in
every live body the step), which is what a parse cursor kept in a global
needs.

### 5.8 Across calls

**Effects.** `ComputeEffects` summarizes every live specialization, to a
fixpoint over the call graph, as the storage its body may resize or
overwrite beyond its own locals -- by parameter index for what it reaches
through a reference parameter's class, by variable for globals and captured
locals -- and the integers it may write; a write the walk cannot attribute
makes the summary opaque. An `extern fn` may resize or write whatever it is
handed by reference and nothing else. `CallKills` then kills exactly the
places those effects name at the arguments bound to them, so a length fact
survives a call to a kernel that only reads and writes elements.

**Entry facts.** Every call site (`RecordSite`) records what it proves about
the arguments: each integer argument's term as sampled right after it was
evaluated, each passed array's length (a slice expression's is what its
bounds just stated, a literal's its count), and how they relate pairwise
(`Sites`: a matrix of the weakest `X <= Y + c` any site established).
`SeedEntryFacts` grants a specialization the meet of its sites on entry once
every site has been analyzed -- callers are analyzed before callees for
this -- and only if nothing reaches it another way: a thread spawn, a
`format` overload called by `print`, or any use the optimizer counted that
the call scan did not see makes it opaque. A site inside a recursive cycle
is walked after its callee and contributes nothing. This is what carries
`src.len == W * W` from `main` into `blur(src: u8[>..]&, ...)`.

### 5.9 Judging

`JudgeIndex` marks `a[i]` when `0 <= i` and `i <= len - 1` are provable
against the length as it stood when the receiver was evaluated; `JudgeSlice`
marks `a[lo..hi]` when `0 <= lo <= hi <= len`, each bound in the state it was
evaluated in, and records the slice's length as `hi - lo` when the domain can
name it (both bounds on one base, or a constant lower bound), which the
declaration binding the slice then takes (`slicelen`). Codegen separately
elides a constant index into a fixed array (`IndexLoc`).

### 5.10 What else the pass decides

* `ForLoop::fixedlen`: for an array or slice loop, whether the body's kill
  summary leaves the iterated place alone, in which case codegen reads the
  view once instead of re-reading the length every iteration (section 3.15 makes
  growth during iteration legal, so the re-read is the default).
* `hoistrefs` on every loop: the reference variables the loop indexes whose
  array the body (and a `while` condition) can neither grow, shrink, rewrite
  whole nor reach through a call; codegen reads their base and length into
  locals before the loop (section 6.10).

### 5.11 Verification

`--bce-test` checks `// bce:elide` and `// bce:keep` comments: every check on
such a line must have the annotated outcome. The `test/optimizer/bce*.goose` fixtures
check these decisions and also run as ordinary programs, allowing output
checks to catch incorrect elimination. `--bce-lines` prints the per-line counts
for comparing two builds of the pass; `bench/bce_ab.py` measures the whole
pass against `--no-bce`.

### 5.12 Known gaps

Indices loaded from array contents (`dist[q[i]]`) have no known range and
keep their check. An analysis of array-content invariants was implemented
and measured, then dropped because it produced no measurable speedup
(`bench/adoption.md`). A `u64` variable is never a
base, so a `u64` local loses the range its initializer had (TODO 0a). Loop
exit conditions that are disjunctions are not represented (TODO 0f). A
value read out of a field or element (only variables and lengths are
bases), a value through a call without a summary, and anything after a
shrink stays unproven until re-established.

---

## 6. Code generation: representation

`CodeGen` (`codegen.h`) emits one C file; the runtime (`src/runtime/`) is
embedded in the compiler (`runtime_inline.h`, regenerated by
`--gen-runtime-header`) and prepended. The representation follows Appendix
C, with the choices recorded in Appendix E. The following sections describe
the representations and their implementation.

### 6.1 Values

The C backend targets 64-bit, little-endian hosts with unaligned packed
accesses. Integer and floating widths are their declared widths; `bool`
occupies one byte and valid values encode as 0 or 1. Array metadata is
counted in elements, except the outer serialization frame, which counts
payload bytes (section 6.9). A stored length must represent the actual
count exactly; truncating it would change where subsequent fields start.
Known counts are checked statically; dynamic counts are checked before
storing or patching the length field.

Layout details needed for byte/C compatibility (`FixedSize`, `LayoutFields`,
`LenStore`, `TagStore`):

* A default variable-array length is `u32`; `T[uN]` uses that unsigned
  width, and `T[varint]` uses unsigned LEB128. Static limited-array length
  widths are 1, 2, 4 or 8 bytes at capacities 255, 65535 and 2^32−1.
  Runtime limited arrays store `u32 capacity`, then `u32 length`, then all
  capacity slots; assigning one preserves the receiver's capacity.
* ADT tags are zero-based declaration indices, one byte for up to 256
  variants, otherwise two bytes. A standalone variant payload has no tag;
  a variant reference into an ADT addresses the payload after the tag.
* `pad n` contributes exactly `n` bytes. In a fixed layout, bare `pad`
  aligns the next real field to its scalar storage width (8 for plain
  references and slices; 1 for composite fields). It adds nothing in a
  variable layout or without a following field. There is no implicit
  aggregate tail padding. An empty struct occupies one byte.
* Padding, inactive ADT payload bytes and unused limited-array slots are
  not value-bearing. Structural equality ignores them. Raw byte images
  can include them, so equal values need not serialize byte-for-byte
  identically, and callers must not infer zeroing from fresh OS pages.

* **Fixed-size types** are packed C types (`#pragma pack(1)`): scalars,
  packed structs, fixed and static-capacity limited arrays wrapped in structs
  (`{ T e[k]; }`, `{ len; T e[k]; }`), fixed-mode ADTs as `{ tag; union }`,
  plain references as pointers, slices as `{ data, len }`, relative
  references as their stored integer. An empty slice's `data` is null where
  the slice was zero-filled (`default<T>()`, a default element), and C leaves
  `memcpy` and `memcmp` undefined on a null pointer even for zero bytes, so
  copies and compares out of a slice go through the runtime's `gs_memcpy` and
  `gs_memcmp` (`ArrView::nullable`, `CopyFn`). `CT` emits typedefs on first use;
  struct-like kinds get a forward typedef so a node type can reference
  itself (`NameCT`). Every program name carries the `_g` suffix (`Sanitize`),
  namespaced ones the namespace's length as well.
* **Bytes values** (variable class) are self-describing byte images held as
  a `uint8_t *` to the value's start; a field behind a variable-size field
  is reached by a cursor that walks the intervening sizes (`FieldPtr`,
  `SizeX`, with a generated `gs_size_<T>` walker per dynamic type).
* **Resizable values** are a header in the owning frame -- `gs_rhdr { base,
  len }` -- with the elements on a data stack, or, for a frame object, the C
  struct of the fixed fields with the tail's header (or nested frame object)
  as its last member (`EmitCFields`). Growth bumps the stack top and the
  count; the base never moves.
* **Fat references** (`gs_rref { hdr, stk }`) are references to
  resizable-class values: the header address plus the stack, so a callee can
  push through them. A reference with `reusable`-pool provenance is a
  `gs_pref`, which adds the freelist's header and stack. Whether a parameter
  is fat or pool-shaped comes from the specialization's types and
  `RootArg::reusable`, not from surface syntax.
* String literals are `static const` byte arrays (`StrRaw`), sliced as
  `{ data, len }` or emitted as static `[len][bytes]` images when used as a
  `u8[]` value (`GenStrBytes`).

### 6.2 Stack assignment

Stack assignment is the hidden-argument strategy §10.3 permits: every
function that uses data stacks takes `int64_t gs_sp`, addresses its own
nonfixed locals and temporaries as `GS(gs_sp + k)` with a per-function
constant `k` (`AllocStk`), calls callees with `gs_sp + <indices in use>`
(`SpTop`), and asks the runtime once at entry to have that many stacks
(`GS_ENSURE`, which reserves lazily). Globals own dedicated stacks outside the
indexed block. Scopes mirror C braces (`CScope`): every nonfixed local's base
pointer doubles as the watermark restored at scope exit, and every exit path
-- fallthrough, `break`, `continue`, `return`, propagation -- emits the
restores of the scopes it leaves in reverse declaration order
(`EmitExitRestores`). A statement gets a scope of its own (`SC_STMT`) so a
temporary's stack is free again at the next statement, and so does the
right operand of `&&` and `||`, whose temporaries exist only when it runs;
a local's allocation skips the statement scopes inside its block
(`AllocStk(forlocal)`), and no further: its index is free again once the
block ends, for the locals and calls that follow (what lets a recursive
cycle's functions own scratch, §7.8).

Because a `uint8_t *` store may alias a stack's `top` in C, the tops of the
stacks a function owns are cached in locals where the function grows them
(section 6.10).

### 6.3 Globals and program instances

Every global that is not read-only static data is a member of one
`gs_globals_t` struct per program instance, with the dedicated stacks of the
resizable ones inside it; main's is a static, a worker's is allocated by its
entry thunk and filled from the spawn image (§6.8). Access is `GS_GL->name`,
which is `(&gs_globals_main)` in a program without workers and the
thread-local `gs_gl` with them. A `let` (or `const`) global of a `const`
flat fixed type with a compile-time initializer (`StaticInitX`, up to 256
array elements) is a C static shared by every instance; a `var` of such a
type can be assigned as a whole and is a member like any other. Everything
but the statics is initialized by `gs_init_globals` in declaration order.

### 6.4 The calling convention (C.3)

`SigParams`, in order: the declared parameters (a by-value resizable adds
its `gs_stack *`; a pool parameter is one `gs_pref`; a bytes value is a
`uint8_t *`); the free variables of a nested function or function value
(fixed ones by pointer, resizables as header pointer plus stack, pools as
`gs_pref`); result channels in source result order (out-pointers for fixed
results other than the first fixed result; per nonfixed result a
destination `gs_stack *` and, for a resizable, the count out-parameter or
frame object out-parameter); and `gs_sp` when the function touches stacks.
The C return value is the first fixed result, even when it is not result
zero; with none it is `void`.
`CollectSpecs` computes per specialization, to a fixpoint over the call
graph, the free variables it or its callees need, the stack-owning globals
it can reach, and whether it needs `gs_sp` at all.

### 6.5 Destinations and in-place construction

Every value-producing node is emitted against a destination (`Dst`): discard,
a C lvalue, or the top of a data stack with, for resizable results, the
lvalue that receives the count (or the frame object). `GenAny` routes a node
to its destination; control constructs recurse so every branch constructs at
the same place (§4.3); `GenConstruct` writes a value front-to-back at a stack
top; calls pass the destination stack down as the hidden argument, and a call
in `v.append(f())`, or in `v.push(f())` for a variable-size element, builds
straight at `v`'s top (`EmitPush`, `EmitAppend`). Element-run results (§7.3)
exist for variable *array* results: `EnsureEr` compiles a second, `_er` twin
of the callee that emits raw elements plus a count, so `v.append(f())` is
contiguous; a callee without a twin (a builtin, a dispatch, a `return from`
target) delivers the value form and the receiver slides the length prefix
out with one `memmove` (`EmitSlidePrefix`). A `T[]` result landing in a slot
of another length storage is re-prefixed afterwards (`EmitReprefix`). A
runtime-capacity limited result (`T[..]`) has no run form: `EmitAppend`
builds its image on the destination stack, then compacts its live elements
over the header and discards unused capacity. Inlined calls retain named
result placement for identical packed layouts, and control expressions pass
the same destination through their branches. These packed
layout relocations are the exception in spec §4.3; they do not use a
separate result stack. A fresh result appended to a limited receiver still
requires staging while its final count is unknown, to check capacity before
writing the receiver; spec §4.3 permits this separate bounded-destination
exception.
An appended literal of variable-size elements is built the same way, its
elements at `v`'s top and its count added to the length (`GenArrayLit`);
one of fixed-size elements holding relative references is a fixed array
laid over the slots it fills, at the top or in a limited array's free
slots, and built there (`FixedArrayLitAt`), so each offset is measured from
where it stays. Other fixed-size elements are evaluated into a C temporary
and copied, as a pushed one is stored after it is evaluated.

A stack destination also names the slot's type wherever the receiver knows
it: a local's, a global's, a parameter's, an array literal's element type, a
temporary's own. The value is built as that type whatever the expression's
own type says: a call's variable-array result takes the slot's length
storage, a reference stores a relative slot's offset (§3.9), and `str()` and
`to_bytes()`, whose results are resizable, reserve the slot's length prefix,
write their elements behind it and patch it (`EmitStr`, `OpenRzDest`), so
`[str("a"), str("bc")]` builds each string in its element. A varint field
or element takes the `i64` every varint read decodes to (§3.6) from
whatever produces it -- a control construct or an inlined body through a C
temporary (`CtlValX`), a returned `varint&` through its load (`CallVal0`,
`GenXD`), a fill value once -- and writes it zigzag-encoded
(`EmitVarintStore`). A call reached
through `GenAny`, as a branch's value, is constructed by `GenConstruct` like
any other. A reference's pointee meeting a value slot -- a returned reference
whose value is taken, or the reference an inlined body leaves in the slot --
is built as the slot's type from where it lies (`ConstructFromLoc`): a `u8[]`
pointee takes a `u8[varint]` element's prefix, and an element run's none.
`v.append(copy(x))` appends `x`, whose elements the run copies anyway. The
`_er` twins are compiled last, after the globals' initializers, which can
ask for one too.

**Pushes** (`EmitPush`). The receiver is evaluated, then the argument, then
the element is added (§2), and the argument may itself grow the array
(`v.push(v.push(1))`, `v.push(f(v))`): a fixed-size element is therefore
evaluated before its slot is claimed, so it follows whatever the argument
pushed and the returned reference names it. A variable-size element, and a
fixed one holding relative references of either form (a self-relative
offset measures from where the element lives, an `in pool` `self` is its
position in the pool), are built in the slot instead -- for limited
arrays too -- which is what the checker's growth rule (§3.10) keeps safe.
A pool allocation builds a literal holding relative references at its
slot the same way (`EmitAlloc`): a slot off the freelist is taken first,
and a fresh one at the top is counted only once the element is written, so
nothing the literal's initializers call can read it half-built.

**Adaptation into limited arrays.** `FitsAt` lets any array
or slice of the element type construct a `T[..k]` (§4.2), and codegen copies
into the C value (`AdaptToFixed`, with the capacity check) from whatever
representation the source has: a variable or field through `LoadLoc`, a
slice expression in `SliceExpr::CgX`, a call result through `CallVal0`
(`CallResLoc`: the temporary header of a resizable result, the base pointer
of a variable one, the pointee of a reference result), a node in another
representation than its context wants through `GenXD` (a `copy` source, a
spliced callee body), and a stack slot through `GenArrayFromLoc`. A
bytes-class call result feeding a fixed-class slot is built on a temporary of
its own rather than the slot's stack (`GenConstruct`), since the slot takes
the adapted C value. A runtime-capacity `T[..]` is a bytes value, and an
assignment into one copies out of a value of that layout (`GenPtr`): a slice
or an array of another kind is built as one on a temporary first
(`ConstructFromLoc`), so the source is read in full before the destination
changes, even a view into the destination itself.

**Adaptation into an ADT.** `FitsAt` also lets a variant construct its ADT,
and an ADT construct itself in its other mode (§3.5); the node then carries
the ADT's type, and codegen finds the type the value arrives as where it is
produced. A variable, field or element converts where it is read: into a
fixed-mode ADT as a C value tagged with the variant (`LoadLoc`,
`AdaptToFixed`), into a variable-mode one as the tag followed by the value's
bytes (`GenVarEnumFromLoc`). A call's result, a builtin's element result
included, and an inlined body arrive as the callee's own type (`AdtFrom`):
`GenAdtAdapted` builds a variant headed for a variable-mode slot in place
behind its tag, and anything else into a temporary it then converts. The
inliner keeps the `InlineBlock` of a trivial body whose ADT result changes
mode for this, as it does for a decayed reference. A multi-value call
forwarded by `return`, whose values the checker fits to the function's return
types one by one, delivers each value of another type into a temporary of the
callee's type, converted from there into the return channel
(`GenNormalReturn`).

**Named results** (`DetectNrvo`, `OpenIbNrvo`): when every `return` of a
nonfixed result hands back the same top-level local (`NamedResult`), that
local is allocated at the return destination from its declaration and the
return writes only the count; a resizable local returned as a variable array
reserves the destination's length prefix ahead of its elements and patches
it at the return (`EmitPrefixPatch`, moving the elements up only when a
varint prefix outgrows its one reserved byte). The same binding is made for
an *inlined* callee's named result reaching a construction context, which is
what keeps §7.3's guarantee for the small builders the optimizer inlines. A
function that is the target of a `return from` binds its named result too:
a value returned to it from below is moved over it at the catch (§6.6).

**Exits** (`ExitStart`, `LandValue`). A `return`, a `break` with a value
and a return leaving an inlined body build their value at the top of the
stack they deliver it to, while the receiver expects it where that top was
when the function, inlined body or loop was entered. An exit taken while a
construction on that stack is under way -- inside an element of a literal
headed there, a `str()` argument, an inlined callee whose named result is
bound there -- would leave that part in front of its value. Codegen counts
the constructions open per stack (`openat`: `GenConstruct` for anything but
a control construct, and an inlined body's named result); an exit that
finds more of them open than its scope was entered with takes the top before
building its value and moves the value down to the scope's top on entry
afterwards, with the stack's top and a frame object's tail base following
it. That entry top is declared where the scope begins, once an exit needs
it (`ScopeTop0`, `DstTop0`). An exit at statement level emits nothing more.

### 6.6 Calls, dispatch, function values, `return from`

`EmitSpecCall` builds the argument list by the convention above, flushes and
reloads the cached tops the callee can reach (`SyncReach`), and checks the
long-distance discriminant afterwards where the callee can propagate one
(`EmitRfCheck`). Tag dispatch (`EmitDispatch`) evaluates every argument once
in source order, snapshots a by-value scrutinee before later arguments run,
and switches on the tag with one call per arm sharing the argument
temporaries and return channels. The arms have no element-run form: a
variable-array result arrives in the arms' layout and is slid out of its
prefix for a run receiver, or re-prefixed once after the switch for a slot of
another length storage. A function value is spliced inline
(`EmitFvCall`): its parameters and locals are ordinary locals of the
enclosing function, which what is declared in its body takes as free
variables like any other. An `extern fn` is a direct C call with prototypes emitted for
symbols the runtime does not define.

**The foreign boundary.** `CheckExternSpec` admits scalars and flat
fixed-size values by value, including fixed-mode ADTs and limited arrays
of static capacity. Parameters additionally admit plain non-optional
references and slices of those values, and `u8[>..]&` builders. Results
are at most one by-value scalar or flat fixed-size value: references,
slices and builders cannot be returned by an extern. A builder argument
uses `gs_rref { hdr, stk }`; C may append through `gs_bld_append` but may
not shrink it, retain borrowed arguments beyond the call, or invalidate
Goose's types, roots or lengths. These are obligations on C, not proofs
the Goose compiler performs. Effect analysis assumes C may write passed
storage and append to passed builders, but does not mutate unrelated
Goose globals. The builtin runtime shims obey the same boundary.

`return ... from` uses one thread-local `int32_t gs_rf` (zero except between
a long-distance return and its catch) plus per-target thread-local channels
for in-flight fixed values (`gs_lret_<id>_<i>`) and destination stacks for
nonfixed ones (`gs_fdst_<id>_<i>`, recorded at the target's entry and
restored at its exit). A propagating function's ordinary exit writes
nothing; a call on a propagation path costs one load and a never-taken
branch, and an intermediate frame returns a dummy value after restoring its
watermarks. `--unsafe-no-rf-check` omits the checks for measurement only.
A nonfixed value is built at its channel's top, which the return records
first (`gs_fval_<id>_<i>`): the calls it unwinds may have built there in
the destination the target handed them -- a named result, part of a value,
a length prefix claimed for a callee's elements -- so the target's catch
moves the value down to its destination's top on entry, as an exit does
(`LandValue`). A call's result is fixed up (`EmitReprefix`,
`EmitSlidePrefix`) only after the check, since a value in flight may lie
behind the prefix the fixup moves.

### 6.7 Relative references, pools, literals

A relative load is the field's own address (or, `in pool`, the pool's base
minus one) plus the stored offset, with a null test only for the optional
spelling (`LoadLoc`, `RelOrigin`); a store subtracts the same origin
(`EmitRelStoreAt`). The store's range check is emitted only where a root can
exceed the width: for `in pool` under `#if GS_STACK_RESERVE >= 2^bits`, for
self-relative under `#if GS_STACK_RESERVE > 2^(bits-1)` when no fixed-size
root in the program is wider than the width (`relrootmax`), so a `u32` link
on the default 256 MB reservation stores unchecked. A pool's base is loaded
once per function into a local (`PoolBase`), and element access through a
pool global reads that local too. `self` stores minus the field's own offset
(self-relative) or the value's own pool offset (`in pool`, only where the
literal is built inside the pool). `pop` on an array of relative references
loads the element it removes like any other load, from the slot it leaves.
`resize(n, ref)` evaluates the fill once as a plain pointer and encodes an
offset at each new slot using the same store helper, including its null and
range checks. Aggregate fills containing self-relative references remain
unsupported.

A `reusable[]` pool's operations (`EmitSlicePool`) evaluate the receiver,
then the slice, then the length (`SliceLen`, which aborts on a negative or
unholdable one), and turn a non-empty slice back into an index: by an exact
divide where the checker placed it in the pool, and otherwise
(`Call::poolcheck`) after testing that it lies inside the element region on an
element boundary, in `uintptr_t` arithmetic so that a slice of other storage
compares without undefined pointer arithmetic, aborting with `GS_E_POOLSLICE`.
An empty slice takes index 0. The freelist is the runtime's (§7): its base, count and top go to the
`gs_spans_*` call by address, so a cached top works unchanged. Growth of the
element region is emitted here (`EmitSliceExtend`: count and top, as for a
push), and so are the default values (`EmitDefaultElems`: one `memset`, or
the checked default construction per element for a type with field defaults). A move is one
`memmove`. A pool's freelist entry is 8 bytes for a slot pool and 16 for a
slice pool (`FlEntrySize`), which the thread-spawn image copies.

A fixed struct or array literal that holds relative references is built at
its final address, never in a C temporary that is then copied (`FixedLitAt`,
`FixedLitAtStk`, the `alloc_ref` slot path); a literal with unused limited
array slots is zero-initialized first, and every aggregate-typed C local is
hoisted to the function's opening brace (`HoistAggregateDecls`) -- both
workarounds for MSVC miscompilations described in Appendix E.

### 6.8 Threads and queues

`thread_spawn` packs the flat arguments contiguously on a scratch stack, then
the worker program's globals as `[size][image]` records (a resizable's image
is its count, its fixed fields and its tail's elements; a pool's carries its
freelist), and hands the packet to the runtime; the generated thunk
(`EnsureThreadThunk`) unpacks the arguments onto fresh stacks, allocates the
worker's `gs_globals_t`, fills it from the image, runs the body, and frees
it. Queues are one `gs_queue` per element type, carrying one contiguous image
per value; a missed `qpoll` yields the all-zero image (`ZeroSize`).

### 6.9 Generated walkers

Per type on demand: `gs_size_<T>` (the byte size of a dynamic value),
`gs_eq_<T>` (structural equality, a `memcmp` for gap-free fixed types and
canonical-encoding bytes values that hold no floats, a cursor walk
otherwise), `gs_verify_<T>` (the `from_bytes` verifier of
`docs/design/serialization.md`: one framing pass for fixed elements, a
framing pass setting an element-start bitmap and a link pass for variable
ones), and the tag enums. Text rendering (`codegen_render.h`) formats
scalars through `gs_fmt_*`, copies `u8` bytes, and renders everything else
structurally into a `u8[>..]` builder or through the user `format`
specialization recorded on the call; `print` renders the whole line into a
temporary builder before writing it, so lines from different threads never
interleave.

**Serialization contract.** The following is the observable part of
`docs/design/serialization.md`, independent of how a verifier is organized:

* `to_bytes(a)` emits `ULEB128(payload byte count)` followed by the
  contiguous element region of the source array or slice. It excludes the
  source's outer length/capacity/header and any reusable-pool freelist;
  metadata within each element remains. The two-argument form appends the
  same image. `bytes_of(a)` views just that payload, read-only, with the
  source's lifetime; its length is fixed when the view is taken.
* The image carries no type, element count, version or checksum. Both
  endpoints must agree on the element type and its layout. Plain
  references, slices and pool-relative links at any depth exclude an
  element type from all three operations. Saving a subrange does not make
  self-relative links to excluded elements self-contained.
* `from_bytes<A>(b)` consumes exactly one image: the prefix must account
  for all remaining bytes, with neither truncation nor trailing data.
  The element count is recovered from the payload. `A` may be a grow-only,
  grow-shrink or variable array. Invalid input yields an empty `A` and
  `false`; success copies into independently owned storage and yields
  `true`. It does not retain a view of the input.
* Validation must establish every representation invariant used by
  ordinary reads: bounded lengths, complete elements, valid tags and
  booleans, and valid varints. Varints constructed by Goose use the
  shortest encoding; equality currently relies on that canonical form.
  Limited-array live lengths cannot exceed capacity; unused slots and
  padding carry no typed values to validate. Untrusted offsets and size
  arithmetic must be checked without overflowing before accessing data.
* Every non-null self-relative link must land on an outer element start,
  or on the payload of the correctly tagged variant of that element.
  Interior-field targets are rejected at typecheck time, even if a
  particular image would be valid. A zero offset is null only for an
  optional; a non-optional zero offset still denotes its field address.
  Boolean bytes and varints are checked for canonical encodings, including
  within nested aggregates and array metadata.
* The current variable-element link verifier uses at most one reservation
  of scratch for its start bitmap; an image needing more is rejected with
  `false`. Elements that can occupy zero bytes have no recoverable count:
  the current verifier accepts only an empty payload, as zero elements.
  Ordinary allocation failure remains a runtime failure. Serialization
  on a big-endian host aborts.

**Text compatibility.** Aggregate separators are `, `; struct and payload
forms use `Type { ` and ` }`, without field labels. Nested byte strings
escape quote, backslash, newline, carriage return and tab, while other
bytes remain literal; top-level byte strings are unquoted. Values are
rendered in argument order, so an argument's rendering hook runs before
the next argument is evaluated. Hook discovery currently accepts only
top-level, non-generic two-parameter `format` functions with a
`u8[>..]&` destination and an exact value or plain-reference parameter for
the rendered type. This is narrower than ordinary generic overload
resolution. Reference rendering follows pointees and has no cycle
detection. Finite float formatting currently tries 15 significant decimal
digits, then 17 if needed to recover the promoted `f64`, with redundant
exponent zeroes removed to at least two digits (`gs_fmt_f64`). That is a
round-trip format, not a general shortest-decimal algorithm; see section 11.

### 6.10 Loop-invariant views and stack-top caching

**Views.** An array reached through a reference keeps its base and length
behind that reference, and the C backend reloads both at every access since
any byte store may alias the header. For each reference variable BCE named
in a loop's `hoistrefs`, the view is read into locals before the loop
(`AddView`, `ViewScope`) and every access inside uses them; the length
lvalue that growth writes stays on the real header, so a missed growth could
never miscompile. For a `for` over an array with `fixedlen`, the elements
pointer and length are likewise read once when the length is a memory load.

**Tops.** A stack the function owns keeps its top in a local where the
function grows it, synchronized with memory only where something else can
observe it: flushed before and reloaded after calls that can reach the stack
(`SyncReach`: the argument text plus the callee's reachable globals; "*" for
a callee handed an opaque stack inside a value), and flushed at every exit.
The local is confined to the loops that grow the stack (`PlanTopCaches`), or
the whole body for growth outside every loop, and is materialized by a text
pass over the finished body that resolves markers for loop edges, syncs, and
jumps out of a region (`ExpandTopMarkers`). Soundness needs one spelling per
cached stack: a body holding a fat reference in any variable, binding,
field read or call result has a second spelling for a stack it may also name
directly and caches nothing (`CanCacheTops`) -- unless it qualifies for the
**reference-parameter mode** (`RefTopsOk`): every fat reference in the body
is a parameter named directly, the fat parameters' root classes are distinct
and exact (and concrete when there are several), no by-value resizable
parameter, no nonfixed return destination, no `return from` channel, no
captured resizable, and no global whose type could be one of those
parameters' pointees. In that mode the parameters' `.stk` tops are cached
instead, and every call syncs everything.

---

## 7. The runtime

`src/runtime/runtime.h` is a small C99 runtime covering data stacks,
integer semantics, varints, aborts, text forms; `runtime_threads.h` adds
workers and queues (compiled out unless `GS_NEED_THREADS`); `runtime_os.h` is
the C behind `stdlib/os.goose`, spliced in after the generated types because
it is written against them.

**Data stacks.** Each stack is one reserved region of `GS_STACK_RESERVE`
bytes (default 256 MB, capped at 2^48 by §10.4) plus a `GS_STACK_GAP`
unmapped tail; Windows commits on fault through a vectored handler that
tells a commit from an overrun, POSIX reserves with overcommit and protects
the gap. A thread program's stacks live in a block reached through
thread-local `gs_stks` and are created lazily as `GS_ENSURE` first asks for
them; a worker's are released when it exits. Every region owned by the
current thread program is registered thread-locally so the fault handler
never touches another worker's state.

**Native stacks.** Recursion consumes only the native call stack (spec
§7.8), whose size is set as its thread starts; running out of it ends the
program with `goose runtime error: native call stack overflow` and exit
status 1, as a data stack overrun does. Every thread program's thread
prepares for that as it starts (`gs_native_stack_init`, from `gs_rt_init`
and `gs_thread_main`). On Windows the vectored handler takes
`EXCEPTION_STACK_OVERFLOW`, and `SetThreadStackGuarantee` keeps 64 KB of
each such stack for it: what an overflow leaves without one only just holds
the report and `ExitProcess`. A program TinyCC builds looks the function up,
its `kernel32.def` lacking it. On POSIX the handler runs on an alternate
signal stack, which the runtime allocates for each such thread unless it has
one already (ASan gives every thread its own) and releases with a worker's
data stacks. A fault between 64 KB below the thread's stack and its top is
that stack overflowing, by the bounds read as the thread starts
(`pthread_getattr_np` on Linux, `pthread_get_stackaddr_np` and
`pthread_get_stacksize_np` on macOS; elsewhere there are none, and the
overflow is not reported). A fault that is neither kind gets back the
handler the runtime replaced and recurs under it: the default action, or a
sanitizer's report. A JIT program installs all this on the compiler's main
thread, which it runs on (§1).

**Integer semantics** (§6.2): every operation runs at its type through
`gs_add_i32`-style helpers, which are functions that check the wide result
under `GS_DEBUG` and macros equal to the release expression otherwise
(measured at 13--37% of runtime under a non-inlining backend when they were
functions); division and modulo are always functions, zero-checked, with
Euclidean `%`. `as` goes through `GS_RANGE`/`GS_F2I`/... macros that check in
debug and cast in release; `as!` and release float-to-int use the defined
wrap of `gs_f2iwrap`. Bounds checks are one unsigned compare (`GS_IDX`).

**Slice pools** (§5.4): the `gs_spans_*` functions keep a `reusable[]`
pool's freelist, a sorted run of `gs_span { idx, cnt }` on the freelist's own
stack, handed its base, span count and stack top (the address of a cached top
works as well as the memory form). Placing a run (`gs_spans_alloc`, for
`alloc_slice` and for a slice `realloc_slice` moves) scans the spans in
index order for the first that holds it, falling back to the end of the
array, where a free span reaching it starts the run; growth in place and
freeing find their neighbor by binary
search (`gs_spans_grow`, `gs_spans_free`), and inserting or removing a span
moves the entries above it. The emitted code keeps the element region's
count and top and fills the default values itself.

**The graphics layer** behind `stdlib/gfx.goose` is not part of this runtime:
it is native C in `src/gfx/`, compiled once into a static library over SDL3
that `goose` links for JIT runs and a program links when built from the
generated C. The generated C only declares the `gs_gfx_*` functions it calls,
and `AddGfxSymbols` (`jit.h`) defines them for a JIT run. `docs/design/gfx.md`
describes it. **The physics layer** behind `stdlib/physics.goose` is built
the same way over Box3D, in `src/physics/`, its functions `gs_phys_*` and
their JIT definitions `AddPhysicsSymbols`; `docs/design/physics.md`
describes it.

**Varints**: ULEB128 read/write/size, zigzag for signed positions, the
one-byte fast path macros `GS_ULEB_READ`/`GS_ULEB_SIZE` for length prefixes
(a value field keeps the inline loop, which measured faster), and the bounded
`gs_uleb_check`/`gs_zig_check` the verifier uses.

---

## 8. Testing the analyses

`docs/testing.md` describes the test suite. The following checks cover the
passes described above:

* every positive fixture typechecks, and its dump reparses to the same dump;
* `test/errors_tc/` fixtures carry `// error: <substring>` markers and must
  fail with those diagnostics: this is where the lifetime, shrink,
  construction-growth, cycle, writability and relative-reference rules are
  pinned;
* `test/lifetimes/`, `test/storage/` and `test/codegen/` run the positive
  side of the same rules, including the store record, byte views, pool
  links, named results and the stack-top aliasing cases;
* `// bce:elide` / `// bce:keep` fixtures verify the bounds-check pass
  line by line, and run as programs;
* `test/optimizer/optimize.goose` inspects `--specs` to check which recursive
  bodies became loops and which kept their calls;
* the JIT backend runs everything a second way, and a sanitizer job runs the
  generated C under ASan/UBSan.

---

## 9. Writing fast Goose

This section explains how compiler analysis, copy elimination, and caching
affect performance. Each recommendation refers to the relevant mechanism;
`bench/notes.md` and `bench/adoption.md` provide measurements.

### 9.1 Bounds checks

The analysis of §5 proves an index or slice check when it can relate the
index to the array's length through facts it saw *in this function or at its
call sites*. In practice:

**Proven** (no check emitted):

* constant indices into fixed arrays, and any index into a fixed array whose
  range a condition established (`if k >= 0 && k < 4 { a[k] }`, a `match`
  arm `0..4 => a[k]`);
* `for i in a.len { a[i] }`, `for x, i in a { a[i] }`, `while i < a.len {
  a[i]; i++; }`, and the downward `while d > 0 { d--; a[d]; }` -- the loop
  variable's `>= 0` and `<= len` invariants are inferred for a local counter
  whose every write is an assignment of a provable value or `++`/`--`/`+=
  c`/`-= c` that cannot wrap;
* the same loops over an array that the body *grows*: a `push` never lowers a
  bound (`grow_during_loop` in `test/optimizer/bce.goose`);
* an array filled by a counted loop and then indexed by the same count:
  `for i in n { a.push(...) }` states `a.len == n` afterwards, when no other
  statement in that loop touches `a` and no `break`/`continue` skips an
  iteration;
* `s[s.len - 1]` after `guard s.len > 0`, `s[k]` after `assert(k >= 0);
  assert(k < s.len);` or an early-out `if k < 0 || k >= s.len { return }`;
* reductions: `a[k % a.len]` once `a.len > 0` is known (a `guard`, or the
  loop that filled `a`), `a[k % -4]` against a length above 3, and
  `a[(h & 63) as! i64]` into 64 elements -- a mask bounds its result when it
  is provably non-negative, so `let mask = a.len - 1` also needs `a.len >= 1`
  known; a symbolic negation as divisor (`k % (0 - n)`) is not expressible;
  a `u64` hash reduced with `%` or `&` and cast to `i64` carries its range
  through the cast;
* row-major indexing with bounded counters: `src[y * W + x]` for `y < H`,
  `x < W` and `src.len == W * H` (products and two-term sums of counters with
  constant bounds), and the row-slice form `let row = src[lo..lo + W]` whose
  length the slice's bounds state, so `row[x]` needs no assert;
* checks inside a function called with arrays whose lengths every caller
  knows: `fn blur(src: u8[>..]&, dst: u8[>..]&)` enters with `src.len ==
  W * W` when each call site established it, provided every call site is
  an ordinary call (not a thread entry, not a `format` overload reached by
  `print`) and none is inside a recursive cycle;
* checks after a call to a function that only reads and writes elements:
  the callee's effect summary says it resizes nothing, so the caller's
  length facts survive.

**Kept** (the check stays, and is usually a well-predicted branch):

* an index loaded from a data structure -- `dist[q[i]]`, `pool[slots[k].node]`,
  `out[cursor[s]]`: the analysis tracks no array contents;
* a `u64`-typed index variable (a `u64` is never a base), an index read out
  of a field or element (`n.count`, only variables and lengths are bases),
  and any value that reaches the index through a cast the facts cannot prove
  in range;
* an index whose relation to the length crosses a `pop`, `resize`, `clear`,
  whole assignment, or a call the summary says may resize that array (or a
  call with no summary at all: a thread spawn, a call through an opaque
  site);
* a comparison against a value computed by a call that mutates tracked state
  inside the condition itself;
* an index that is `var + c` at a width narrower than `i64`, and a condition
  whose side is `var + c` (only a bare variable, a constant, or a length plus
  a small constant is admitted as a comparison side, since the addition
  itself may have wrapped before the compare);
* a loop exit condition that is a disjunction (`while i < n && ok`), whose
  negation the domain cannot state.

Practical consequences: state facts with `assert` where the compiler cannot
see them (`assert(a.len == n)` at a function's entry when its callers are
opaque); keep indices in `i64` locals derived from counters and lengths
rather than `u64` or loaded values; write image and matrix kernels either as
row slices or as `y * W + x` against a length the caller states; and check
with `--bce-lines` which lines kept their checks. On MSVC a kept check inside
a hot loop prevents vectorization outright (`blur` is 1.75x slower with its
nine checks); on clang the checks cost little and the loop-view hoist below
is what matters.

### 9.2 Arrays behind references

An array reached through a reference or slice keeps its base and length in
memory the C compiler must reload after every byte store. Two things remove
the reloads:

* **View hoisting**: in a loop that only reads and writes *elements* of the
  array behind a reference variable -- no `push`, `pop`, `resize`, `clear`,
  whole assignment, rebind, or call that can reach it -- the view is read
  once before the loop. This gives a 4x speedup on `blur` under clang. A loop that
  grows the array cannot have it; move growth out of the read loop, or split
  the loop.
* **Stack-top caching**: pushes through a fat reference parameter run with
  the top in a register when the function qualifies (§6.10): every fat
  reference in the body is a parameter, the parameters are provably distinct
  arrays at every call site (exact roots), the function returns nothing
  nonfixed, has no `return from` channel, captures no resizable, and names
  no global that could be one of the parameters' pointees. A function that
  also reads a fat reference out of a field, or holds one in a local, keeps
  the memory form for every stack. For helpers that push frequently, pass
  distinct pools as parameters to enable this optimization.

A function caches the stack tops of arrays it owns where it grows them.
When growth occurs only inside a loop, the cache is confined to that loop.
A loop that only updates elements therefore needs no register for a cached top.

### 9.3 Construction and copies

* A nonfixed value is built at its destination (§4.3): `let x = f()`,
  `v.push(f())`, `v.append(f())`, `g(f())`, `x = f()` and a struct field
  initializer all hand the callee their stack. A `return` of a named local
  costs nothing when every return hands back that one local (`DetectNrvo`),
  including after the optimizer inlines the callee. Returning different
  locals on different paths copies all but one; a multi-name receive (`let
  a, b = f(); return a;`) copies.
* `v.append(f())` for a `T[]`-returning `f` compiles a second copy of `f` in
  element-run form; a builtin or dispatch result there costs one `memmove`
  of the elements over the prefix. A `T[..]` result is built on a temporary
  and costs one copy of its elements.
* A fixed-size element pushed into an array is evaluated first and stored
  after, so `v.push(f(v))` may grow `v` inside `f`. A variable-size element,
  or one holding relative references, is built in its slot, and `f` may then
  not grow `v` -- nor may a callee grow the array a `v.append(f())` or a
  whole assignment `v = f()` is building into (§4.2): a compile error, with
  the callee's growths of its parameters, globals and captures counted.
  The elements of an appended literal, `v.append([f(v), x])`, follow the
  same rule as pushed ones. A whole assignment's `f` may not use `v` at all
  (§4.4); new contents computed from the old are built in a local of their
  own and assigned as its `copy()`, one copy.
* An array or slice of another kind meeting a `T[..k]` -- a local, an
  argument, a field, an assignment, a return -- is an O(length) copy into
  the C value after a capacity check, whatever the source's representation.
* `copy(x)` is a real O(size) copy, and so is any assignment of a non-fixed
  lvalue; the checker forces the spelling so the cost is visible.
* A function returning several values is never inlined. At `-O1` and above,
  a single use or a small body qualifies a function for inlining (thresholds
  of 8 nodes at `-O1`, 16 at `-O2`), subject to the eligibility rules and
  nesting limit in section 4.
* `str(...)` and `format(out, ...)` write straight at the destination's stack
  top; `print` renders into a temporary builder first. Structural rendering
  of aggregates calls a generated walker per type.
* A fixed-size struct literal is a C temporary copied to its destination,
  except one holding relative references, which is built in place.

### 9.4 Integer types

* Arithmetic runs at the operands' width (§6.2): a sum of `u8` taps wraps at
  8 bits; widen each operand (`as u16`) before summing.
* `varint` costs a decode per read; use it for values with a wide or
  unpredictable range and a sized integer for values whose range is known
  (`calc` lost 11% to `varint` numbers that fit a `u8`). Length prefixes decode
  with a one-byte fast path; value fields do not.
* A `u64` local drops out of every bounds proof; compare `u64` hashes with
  `.len`-derived values directly (§6.1 admits it) and reduce with `%` or `&`
  before binding the result to an `i64`.
* `let` bindings of `.len`, `.cap` or literals keep the non-negativity that
  the `u64` comparison rule needs; `var` bindings lose it.
* A literal argument to a generic or untyped parameter is a literal parameter
  adapting to every use in the body; negative literals can adapt to narrower
  types too, provided they fit every recorded use.

### 9.5 Relative references and pools

* Self-relative links (`T&<u32>`) cost nothing measurable against indices on
  structures built once and walked; on a structure that *relinks* (an LRU
  list) each store subtracts the field's own address and each load adds to
  it before the next load can issue, which measured 1.5x against indices.
* `in pool` links (`T&<u32 in pool>`) store as a subtraction from a base held
  in a register and load as `base + off`, let other arrays hold 4-byte links
  into the pool, and copy freely; they are the form for relink-heavy
  structures in a global pool. clang schedules the base-plus-offset load
  better than MSVC.
* An optional link pays a null test per load; a sentinel-terminated
  structure with non-optional links initialized through `self` loads as a
  plain add (`lru_nonopt`: 1.10x under MSVC).
* The store-time range check is gone for `u32` links at the default
  reservation and for any width the reservation cannot exceed; `u16` links
  keep it.
* A structure whose links stay within one root array has no rebasing cost
  when moved or serialized; `bytes_of` is a zero-copy view of it.
* A plain reference parameter outside a recursive cycle has its own root
  class and cannot be stored as a relative link into the pool it came from;
  inside a `recursive` cycle the pool-class analysis identifies it. Helpers
  that link nodes want to live in the cycle, take the pool itself, or use
  `in pool` links (whose root is the named global whatever the parameter's
  class).

### 9.6 Recursion and dispatch

* A `recursive fn` whose body starts with the base case `if c { return e; }`
  has that base case inlined at every self-call (`-O1` and above), halving
  the calls of a complete tree walk; a statement before the test, a UFCS
  self-call, or mutual recursion disables it, and a self-call whose array
  result is passed straight to a slice parameter stays a call.
* A self-recursive integer function whose tail returns fold with one
  associative operator becomes a loop; `1 + f(l) + f(r)` loses its right
  spine. Floats, `%`, returns inside nested loops, and callees that can
  `return from` the function disable it.
* Cycle members are never inlined and nothing is inlined into them.
* Case-function dispatch is one `switch` with a direct call per arm, the
  arms inlinable like any call; `match` is the same `switch` in place.
* `return ... from` costs a thread-local write at the return, a load and
  never-taken branch after every call on a propagation path, and nothing on
  an ordinary return.

### 9.7 Scratch, shrinking, and lifetimes

* A grow-only array can be `clear`ed, `pop`ped or `resize`d wherever the
  checker can see that no reference or slice into it is live afterwards
  (§3.10): use a view for the last time, then shrink; a shrink is a
  statement of its own, not part of a larger expression.
* Scope exit releases storage by restoring a stack watermark. A scratch
  buffer declared inside a loop is reset this way at the end of each
  iteration, without per-element cleanup. In a recursive function, one
  declared in a block that ends before the recursive call reuses the same
  data stack at every level (§7.8).
* `reusable` pools cost nothing per operation beyond the freelist push and
  pop; `free(i)` bounds-checks its index.
* A `reusable[]` pool's `alloc_slice` scans the free spans in index order up
  to the first that holds the request. Adding or removing a span (a free
  that merges with neither neighbor or with both, an allocation that uses a
  span up) moves the spans above it. Both costs grow with the number of free
  spans, of which merging leaves at most one more than there are allocated
  runs between them. A slice handed back costs a range test unless the checker placed it in the pool, and
  new elements cost one `memset` unless the element type has field
  defaults.
* A `realloc_slice` that cannot grow in place scans for space and copies
  the slice. The copy lands at the front of the span it takes, so what is
  left of that span is room to grow into; once none is, a slice grown an
  element at a time is copied at every growth, and growing by a factor keeps
  the copies amortized.
* A thread program copies every global it uses at spawn; keep a worker's
  globals small or pass what it needs as arguments.

### 9.8 Backends

The generated C is the same for every backend; the differences measured in
`bench/notes.md` are the backends'. clang vectorizes checked loops and
schedules base-plus-offset loads; MSVC does neither, and is better at
recursion into a bump allocator. `bench/results.md` reports every row under
both, and the JIT backend (TinyCC) is for running without a toolchain, not
for speed.

---

## 10. Where the compiler is more conservative than the specification

Rules the current implementation applies beyond, or in place of, what the
specification allows, and the shapes the C backend refuses outright:

* A long-distance return may carry only references rooted at globals or
  static data (TODO 0d).
* Inside a recursive cycle, a reference rooted at a caller's fixed-size local
  is pass-down only unless the parameter it came through is threaded (§3.11);
  the spec's cycle store rule is stated the same way and marked for
  refinement (TODO 5). Beyond the spec's rule, a merged value or a rebound
  variable that may be rooted at a threaded class, which the spec stores
  where each of its roots could be, is pass-down only.
* The cycle return-root scan gives up on branch values, overload sets,
  function values and nested functions it cannot resolve by name, and
  reads no holder result at all; until a return is checked, a back edge's
  result is then pass-down only (`cycleroot`), never unsound, and may point
  into a grow-shrink array wherever it is passed (§3.5). A recursion that
  builds its holder result from its children's before any return of it is
  checked (recursing ahead of the base case's return) is refused for that.
* A back edge that passes storage of its own to a reference, slice or
  holder parameter the entry call gave static data gets a holder result
  that may only be passed down: the body was checked with that parameter
  as static data, which no prediction can map to what the back edge
  passes (§3.11).
* A return that may be a parameter class's pointee its root does not show
  (`hidesclass`) makes the back edges after it pass-down only, unless the
  prediction names every class, even where the class is a pool, whose
  argument every back edge must pass as the entry call did (§3.11), and it
  is kept on a merge of any class root, a class of an enclosing function
  included.
* A return whose value is itself merged from branches reaches its callers
  as one root, the innermost, with `cyclelocal` set where the other one's
  root is not a pool, a global or an enclosing function's local in the
  callee (§3.4), so a recursive cycle cannot store the result even where the
  argument behind that other root is a pool; the same function written
  with a `return` per branch is mapped a root at a time.
* A plain reference parameter's root class is identified with a global pool
  only inside a recursive cycle or through the `in pool` form (§9.5 above,
  `bench/notes.md` item 1), so `index_of`, a relative store and an exact
  read-back through such a parameter need a pool some `T&<w in pool>` type
  names. Recording in the specialization key which global every exactly
  rooted reference argument points into, rather than only those, would lift
  that, at the cost of one specialization per distinct global passed.
* Bounds-check elimination tracks no array contents and no `u64` variables
  (§5.12).
* A parameter is never a slot read (§3.10), whatever its argument: the
  specialization key does not record the bit, so inside the callee a
  grow-shrink shrink still counts a view passed in from a field as one that
  may point into the array.
* A shrink of an array in what an inexactly rooted reference points at
  (`c.s.arr.pop()`, with `c.s` read out of `c`) counts as a shrink of every
  array of that array's type the root bounds, not only of those in storage
  that can hold what the reference points at, as a store through it does
  (§3.5): the pairs of roots it leaves its callers (`LiveShrink`) and its
  bound summaries (`BoundShrink`) carry the array's type alone, so narrowing
  the shrink by what its path reached would only move the rejection to the
  call.
* The growth-during-construction rule (§3.10) takes a parameter class to be
  possibly any global or captured local a callee grows, two classes of one
  activation to be one array unless every call site keeps both concrete and
  exact, and a callee still being checked to grow whatever its text names.
* While print, str or format runs a `format` overload, the views an argument
  reads through (an optional reference, a slice, a non-fixed array's
  elements, a holder's references) stay held even where the overload takes
  the whole argument and nothing of it is rendered around the call (§3.10,
  **Format overloads**).
* The C backend rejects: binding, copying or dispatching a *resizable* ADT
  payload; a reference to a resizable nested in a variable-size prefix or an
  ADT payload; copying a resizable value with a variable-size prefix; `==` on
  resizable structs; `self` in a frame object literal; a `format` overload by
  reference on a resizable without its own header; `str`/`to_bytes`/
  `from_bytes` into a fixed-size destination.
* `from_bytes` rejects element types with relative references into fields of
  elements, and fixed or limited destination arrays
  (`docs/design/serialization.md` §7).
* The JIT backend refuses programs that use workers or queues
  (`docs/design/jit_backend.md`).

---

## 11. Audit findings and resolutions

The failures below describe the pre-fix compiler (`316063d`), ordered by
impact. Runtime cases were reproduced at `-O0` and `-O2` through TinyCC and
native MSVC; the construction-copy gap was checked in generated C at both
levels. Each resolution records the corrected contract. An independent
implementation should satisfy those contracts, not reproduce the original
failures.

1. **Length-field overflow corrupts the containing layout.** `EmitLenStore`
   and the prefix-patching paths narrow counts without establishing that
   they fit. For `struct Packet { bytes: u8[u8], tail: i64 }`, constructing
   `Packet { bytes: [1; 256], tail: 42 }` succeeds: `bytes.len` is 0 and
   `tail` reads as 72340172838076673 (eight of the element bytes). A later
   reference field would likewise be read from the wrong location. All
   construction/adaptation paths need to preserve the length invariant,
   including statically known counts (spec §3.3).
   **Resolved:** known overflow is rejected; dynamic length stores and
   prefix adaptations check representability before narrowing.

2. **Field-default execution is missing from effects at its use sites.**
   `CheckFieldDefaults` checks a shared expression separately, while
   `CheckInits` visits only explicit initializers. With a global
   `data: i64[>..<] = [42]` and `struct S<T> { x: i64 = zap() }`, where
   `zap` clears `data` and returns 0, constructing `S<u8> {}` inside
   `if data.len > 0` leaves BCE's old length fact alive. A following
   `data[0]` prints 42; `--no-bce` correctly aborts at length 0. A retained
   element reference is also permitted across the hidden shrink. Defaults
   must contribute effects wherever they execute, not just when their
   type is first instantiated.
   **Resolved:** defaults are checked and expanded into each use site's
   tree, with isolated declaration bindings but the caller's live storage
   and effect context. This includes `default<T>()` and slice-pool fills.

3. **Stores through slice parameters vanish from the caller's lifetime
   record.** `ApplyCalleeStores` skips every slice-parameter destination.
   For `struct Holder { p: i64? }` and
   `fn save(xs: Holder[:], p: i64&) { xs[0].p .= p; }`, a caller can
   `save(holders, data[0])`, clear `data`, and subsequently dereference
   `holders[0].p`. Replacing the cleared element with 99 makes that stale
   reference print 99. A permutation-only optimization cannot justify
   dropping arbitrary incoming stores (section 3.5).
   **Resolved:** slice destinations participate in store replay, including
   recursive calls; only same-container read-backs preserve the old record.

4. **Rendering hooks are given fabricated writable provenance.**
   `UserFormatIn` specializes a by-reference `format` hook using writable
   placeholder arguments, rather than the actual rendered value's
   permissions. A `format(out: u8[>..]&, v: S&)` that assigns `v.x = 99`
   can be invoked by `print(s)` on `const s = S { x: 1 }`; both the hook
   and a subsequent `print(s.x)` print 99. Implicit hook calls must obey
   the same writability and effect rules as explicit calls.
   **Resolved:** hook checking uses the actual argument's permissions and
   roots, retains separate hook sets per rendered argument, and applies
   shrink/growth effects. BCE invalidates facts between hook executions.

5. **The byte verifier admits noncanonical values.** `NeedsVerifyWalk`
   treats booleans as opaque bytes, and `gs_uleb_check` accepts redundant
   high zero groups. `from_bytes<bool[>..]>([1, 2])` succeeds and produces
   a value that renders as `true` but compares unequal to `true`. For
   `struct V { x: varint }`, loading `[2, 128, 0]` succeeds with `x == 0`
   but compares unequal to a normally constructed `[V { x: 0 }]`, because
   `EmitEqBytes` assumes shortest varints. Loaded values must satisfy the
   same representation invariants as constructed ones (section 6.9).
   **Resolved:** verification checks boolean bytes and rejects redundant
   high zero groups in every checked varint, including the image prefix.

6. **An optional self-relative link can silently become null.** With
   `struct Node { next: Node&<u8>?, value: i64 }`, the legal-looking
   `nodes[0].next .= nodes[0]` stores offset zero because the field and
   element start coincide. Reading it gives null. `RelOffset`/`EmitRelStoreAt`
   check the numeric width but not this collision. The null encoding and
   acceptance rules need a consistent resolution; silently losing a
   non-null target is not intended reference semantics (spec §3.9).
   **Resolved:** optional self-relative stores reject a non-null target
   whose offset is zero. The packed encoding is unchanged; non-optional
   zero offsets and pool-relative links retain their existing meaning.

7. **Fill evaluation depends on the destination representation.** For
   `struct Node { value: i64, next: Node&<u8> }`, a fill
   `[Node { value: tick(), next: self }; 3]` calls `tick` three times when
   constructing `Node[3]`, but only once for `Node[>..]`. With an incrementing
   counter, the first array contains 1, 2, 3 and the second 4, 4, 4.
   `FixedArrayLitAt` and `GenArrayLit` disagree about repeated construction
   versus copying a fill. The language needs one evaluation rule for this
   form that preserves relative references; this discrepancy should not
   become a representation-dependent side-effect rule.
   **Resolved:** every fill evaluates its operand once, even for zero
   elements. Literal operands containing relative references capture their
   field values once and encode links at each destination; `self` denotes
   each new element. Variable-size operands also evaluate once.

8. **Float text is not always shortest.** `gs_fmt_f64` prints
   `1.000000000000001` as `1.0000000000000011`, even though the shorter
   spelling reads back to the same value. Its 15/17-digit strategy meets
   round-trip accuracy, but not spec §3.7's shortest-form promise.
   **Open:** the shortest-float formatting fix is deferred; the compiler
   retains its existing 15/17-digit formatting.

9. **Some construction paths still copy whole fresh results.** The
   fallbacks in section 6.5 contradict spec §4.3/§7.3's unconditional
   guarantee. For example, `out.append(make())`, with `make` returning a
   runtime-capacity `i64[..]`, constructs a separate limited-array image
   then copies its live elements into `out`. The generated C contains
   this copy at both optimization levels. This is a performance-contract
   gap, even when the program's values are correct; it is not a license
   for a replacement implementation to ignore the guarantee.
   **Resolved:** fresh runtime-capacity limited results and control
   expressions now construct on the receiving resizable's stack. Required
   packed-layout changes relocate bytes there; they do not materialize a
   separate result. Spec §4.3 allows that relocation, and separately allows
   staging a result of unknown length to check a limited receiver's capacity.
