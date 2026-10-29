# The Goose compiler: implementation notes

> **Deprecated language features:** `reusable` and `reusable[]`. Use
> [`slot_pool<T>`](stdlib.md#slot_pool) for slots and
> [`slice_pool<T>`](stdlib.md#slice_pool) for contiguous runs.
> Builtin behavior and historical examples below remain relevant to existing
> code; removal is planned separately, after the remaining migration gaps.

<details>
<summary>Table of contents</summary>

- [1. Pipeline](#1-pipeline)
- [2. Front end](#2-front-end)
- [3. The typechecker](#3-the-typechecker)
- [4. The optimizer](#4-the-optimizer)
- [5. Bounds-check elimination](#5-bounds-check-elimination)
- [6. Code generation: representation](#6-code-generation-representation)
- [7. The runtime](#7-the-runtime)
- [8. Testing the analyses](#8-testing-the-analyses)
- [9. Writing fast Goose](#9-writing-fast-goose)
- [10. Where the compiler is more conservative than the specification](#10-where-the-compiler-is-more-conservative-than-the-specification)
- [11. Audit findings and resolutions](#11-audit-findings-and-resolutions)
- [12. The language server](#12-the-language-server)

</details>

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
| Optimize | `optimize.h`, `optimize_basecase.h`, `optimize_tre.h`, `optimize_loops.h`, `optimize_reassoc.h` (`Optimizer`) | live specializations | bodies rewritten in place (inlined, folded, loops), liveness and use counts, loop shapes (`ForLoop::stripk`, `sumred`, `While::countdown`) |
| BCE | `bce.h` (`BCE::RunAll`) | live specializations | `Index::nobc`, `SliceExpr::nobc`, `Call::nobc` (a `pop`), `ForLoop::fixedlen` and `lenbound`, per-loop `hoistrefs` and `hoistfields` |
| Codegen | `codegen*.h` (`CodeGen`) | live specializations, globals | one C file, with `src/runtime/` (or the part of it that a program's unit holds) around it |
| JIT | `jit.h` | the C text | the program run in-process through libtcc, when no `-o` was given |

The passes are virtual methods on `Node` (`ast.h`): `Dump`, `Clone1`,
`Children`, `Check`, `Cp1`/`Opt`, `BceWalk`/`BceMark`, `CgX`/`CgAny`/`CgStmt`.
The per-node bodies of each pass live together in one file (`dump.h`,
`clone.h`, `typecheck_nodes.h`, the tail of `optimize.h`, the tail of
`bce.h`, `codegen_nodes.h`) so that each pass can be read in one place. Shared
state and helpers belong to the pass object (`TypeCheck`, `Optimizer`, `BCE`, `CodeGen`),
split across the `typecheck_*.h` and `codegen_*.h` files by topic.

**Ownership.** `Ast` owns every node, type expression, type detail, symbol,
`VarDef`, instantiation and specialization in append-only tables; the trees
only point at each other, and nothing has a destructor. Type expressions are
shared templates: a function body is *cloned* per specialization
(`Node::Clone`), and the clone carries that instantiation's annotations
(`exprtype`, resolved symbols, `VarDef`s). Each node of the clone points to
the node it was copied from, as the source has it (`Node::origin`). The
optimizer's inliner copies with `Cp1`, which preserves annotations and
remaps the callee's `VarDef`s to fresh ones.

**The compile thread.** `Main` runs the phases up to the finished C on a
thread of its own with a 64 MB stack (`RunOnCompilerStack`, `COMPILERSTACK`
in `main.cpp`). The typechecker recurses as deep as the program's
compile-time call path (§3.1 below), and the platforms' main threads get
anything from 1 MB (Windows) to 8 MB (Linux and macOS by default). The stack
is reserved address space, committed only as deep as a compile goes. A JIT
run starts the program back on the main thread, because macOS only lets a
window be created on the main thread. On Windows and macOS the link also
reserves 64 MB for the main thread's stack (`CMakeLists.txt`). On Linux the
main thread has the stack limit the process started with (`ulimit -s`),
which nothing in the executable sets.

**Verbs** (`Main` in `main.cpp`):

* `goose run file.goose [options] [-- args]` compiles the program in this
  process and runs it (`--jit`).
* `goose build file.goose` writes the C to the program's build directory
  under `GOOSE_HOME`, and compiles and links it into an executable beside
  the source (`build.h`: the runtime object compiled once per toolchain and
  configuration, the native modules' response files, `-o`, `--debug`,
  `--standalone`, `-v`, `--dry-run`; `-O2` by default).
* `goose check file.goose` typechecks it (`--check`).
* `goose fetch file.goose` fetches the packages the root declares and stops
  (`--parse`; `--update` fetches tags and branches again).
* `goose toolchain` prints the C compiler, cmake and git a build would use
  (`toolchain.h`: `--cc`, `GOOSE_CC`, `cc` in `GOOSE_HOME/config`, else
  discovery; `--json` for scripts, which also lists the modules whose
  packages are compiled in).
* `goose version` (or `goose --version`) prints the compiler's version and
  the runtime hash. The version comes from `version.cpp`, which includes the
  header that `cmake/version.cmake` writes at each build: `git describe`, or
  the `GOOSE_VERSION` that a release configures with.
* `goose lsp` serves an editor over the Language Server Protocol (§12).
* `goose completion` prints a shell's completion script, made from the
  help's text.
* `goose help --man` prints the help as a man page, made from the help's
  text.
* `goose format` rewrites files in the standard layout (below).

`run` and `build` both build the native part that a program's sidecar
`foo.build` names (`packages.h`: `LoadSidecar`; `native.h`: the C compiled
once per toolchain, a response file for the link, and, for a run, a shared
library whose one export hands the C functions to `RunJit` by name). They
also build the native parts of the packages whose extern fns the program
calls (`NativePackagesUsed`, from codegen's `usedexterns`). An extern bound
from a shared library at run time is skipped, since it comes from no
package's C. Each package comes before the ones it requires, and is built
under `GOOSE_HOME/build/pkg`. A package with a `cmake_target` is built
through a generated wrapper project (`BuildCMakePart`). The wrapper has the
JIT library as a shared target linked to the package's own target, and a
probe executable whose link line, read from CMake's file API reply
(`json.h`, `CMakeLinkClosure`), is the package's link closure; that link
line is written as the package's response file. Processes that build the
same native part at the same time take turns on a lock in its directory,
and the response file and the stamp are each replaced as a whole
(`BuildNativePart`).

A first argument that is not a verb is read as the flags below, so
`goose file.goose` is `goose run file.goose`. The verbs are the documented
command line; the flags are the lower-level options the verbs are built on
(`docs/design/packages.md`). Other processes (the C compiler, cmake, git)
are started through `process.h`, a translation unit of its own over
`third_party/subprocess/subprocess.h`. `progress.h`, another translation
unit, reports the slow steps (a line each, and on a terminal a live line
below them).

**Formatter** (`format.h`). `Format` takes a file's text and returns it
formatted, or the file's parse errors, or, for a result that failed its
own check, an internal error; it writes nothing, and both `RunFormat` and
the language server's `textDocument/formatting` (`FormatDocument`, which
turns the result into edits to the lines that change with `LineChanges`,
a line diff) call it. It parses the file alone (no imports
resolved) with `Parser::roles` set, which records what a token's kind
alone does not say: a `<` or `>` of a generic list (`R_ANGLE_OPEN`,
`R_ANGLE_CLOSE`, twice for a `>>`), a unary operator, a type's suffix, the
`>..<` of an array type, the `!` of `as!`, a trailing block's `{`, and a
bracket whose list may end in a comma (`R_LIST`; not a match whose last arm
ends in a `return` without `from` or a bare `break`, which a comma would
continue). A backtrack drops the roles of what it backtracks over; with
`roles` null, as in every compile, nothing is recorded. The formatter then
lexes the file again, keeping the gap between each two tokens (from
`Lexer::prevend` to the token), whose comments and line breaks it keeps,
and prints the tokens line by line as the input breaks them: each line
indented from the brackets open at its start (a level inside a bracket that
ends its line or a block's `{`, or its offset from a bracket it is aligned
in), or, continuing a statement, kept at its offset from the statement's
first line but no further left than it, so that a chain broken after its
operators can line up under its first operand; and the tokens on a line
spaced by `format::Space`, with a space wherever two tokens written
together would lex as something else. Trailing comments keep their column,
moved with their line; a run of them that shared a column shares one still.
A table is lined up as gofmt lines one up: on consecutive lines at one depth
and indentation that each declare a name with its type (`[const|let|var|...]
name: type [= value]`, a struct's fields, a literal's `name: value`s) after
the same keywords, the types start one space past the longest name, and a
run of `=`s one space past the longest type. `Layout` finds these cells in
a first pass of `Lines` and pads them in a second.
Before the result is used, `CheckSame` lexes both texts (the same tokens,
but for a comma before a closing bracket, and the same comments but for
their whitespace) and parses both (the same `Ast::Dump` and `DumpDocs`).

**Driver flags** (`Main` in `main.cpp`):

| Flag | Effect |
|---|---|
| `--tokens`, `--dump`, `--parse` | stop after lexing, after parsing (dump is parse-level: no resolution), after resolution |
| `--dump-docs` | stop after parsing and print the documentation comments of the program's declarations, fields and variants (`DumpDocs`; §2) |
| `--roundtrip` | after resolution, parse the dump as a program of its own and require it to dump to the same text (`CheckRoundtrip`), then go on to whatever else was asked for |
| `--dump-file f.goose` | also write the dump to a file, and go on |
| `--check` | stop after typecheck, optimization and BCE; no C is written |
| `--diagnostics json` | print the compile's errors and warnings as JSON on stdout (`DiagnosticsJson`): one object each with its severity, message, file, line, column, end and related lines, and the text the terminal would have shown; the progress lines go to stderr. This is the format an editor reads (§2, `docs/design/language_server.md`) |
| `--offline` | fetch nothing (`package_nofetch`, `packages.h`): a declared package whose checkout is not in the cache is an error at its import, which `goose fetch` resolves |
| `-O0`, `-O1`, `-O2` (default) | inlining thresholds (§4); folding and propagation run at every level; base-case inlining, tail-recursion elimination and min/max regrouping need `-O1` or above |
| `--specs` | print every live specialization's optimized body |
| `--stacks` | print the data stack count of the main program and of each worker, the number of regions these add up to, the thread cap that determines `hardware_threads()`, and every function's share (`codegen_stacks.h`); with `--check` it runs the backend for the counts and writes nothing |
| `--stack-reserve size` | the address space reserved per data stack, written as `-DGS_STACK_RESERVE` (K/M/G/T suffixes; at least 64 KB, at most 2^48) |
| `--no-bce` | skip bounds-check elimination (this also loses the loop-view hoist, §6.10, and the restated array loops that need BCE's facts, §6.12) |
| `--bce-test` | verify `// bce:elide` / `// bce:keep` annotations in the sources |
| `--bce-lines` | print elided/kept counts per source line |
| `--unsafe-no-rf-check` | omit the `return from` discriminant checks after calls: a measurement aid, unsound |
| `-o out.c`, `--jit`, `--header`, `-D`, `--include`, `--stdlib`, `--` | output file, in-process run, a C API header for the exports (which builds the C without a `main`, for a C host), a define written into the generated C, a user header, the stdlib directory, program arguments |
| `--<name>-link msvc\|cc` | print the response file of link inputs that a program needs when it uses the blessed package imported as `name` (`gfx`, `audio`, `ui`, `physics`), if that package is compiled into this compiler (`builtin.h`, `BuiltinLinkFile`) |
| `--multi-test a.goose b.goose ...` | compile each file in turn with the other flags, as a separate process compiling that file would (each file gets its own `Ast` and compile thread, which is all the state a compile has). Each file's output on both streams ends in a line `==== goose --multi-test: exit <code> <file>`. With `-o`, a `%` in the output name stands for each file's name. It runs no programs and writes no dump files. The test runner uses it to batch compiler runs (`docs/testing.md`) |
| `--compile-shader f [--shader-source msl\|hlsl]` | hidden: what a shader compiles to, without a program around it |

The debug build of the *generated* C is `-DGS_DEBUG=1` on the C compiler (or
`-DGS_DEBUG=1` to `goose`, which writes it into the file): it enables the
overflow, `as` range and ADT tag checks of §9.3. The Goose compiler's own
build configuration is unrelated.

---

## 2. Front end

**Lexer** (`lexer.h`): a hand-written scanner over a 0-terminated buffer; the
token set is an X-macro table. The buffer is the file as `LoadSource`
(`utils.h`) reads it, minus any leading UTF-8 byte-order mark, so the
source lines that diagnostics echo leave the mark out too. Lexing and parsing
handle the syntax described in §2 and Appendix D: `T&<u8>` is `&` `<` in
type context, and `1..2` lexes as a range because a `.` starts a fraction
only when a digit follows. `LexRawString`
removes the closing line's indentation from each content line of a `"""` string
and normalizes `\r\n` to `\n`. Its token and the resulting `StrLit` record
whether it was multiline, allowing `embed_shader` to report errors at the
corresponding source line. Comments are skipped, with one exception. A run
of `//` comment lines, each on a line of its own and directly below the one
before, that ends on the line above a token is kept as that token's
documentation (`Lexer::doc`, a view from the first `//` to the end of the
last line). The parser attaches it to the declaration, field or variant that
starts with the token (`SFunction::doc` and the like), and `--dump-docs`
prints these. A blank line, a block comment or any token ends a run, and a
comment after code on the same line does not start one.

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
  it can never coincide with a local. Every reference also records the
  namespace it was written in (`Ident::ns`, `TypeName::ns`, `Dot::ns`,
  `Return::ns`), which is where an unqualified name resolves first. A
  declaration may write its own namespace as part of its name, which sets
  `curns` for the rest of the declaration.
* Only the root file's `main` is registered; an imported file's is dropped
  at the declaration (§11.1).
* From the parser onward, a `guard` is an `if` (`ParseGuard`, §6.4). The
  rest of the block containing the guard is parsed as the if's then-block,
  and the guard's `else` block becomes the if's else block. The if takes
  the guard's place: as the block's tail where the rest of the block ends
  in a tail, and as its last statement otherwise. No later pass has a guard
  node, and `--dump` prints the `if`. A guard anywhere other than among a
  block's statements is a parse error.
* `defer s;` parses to a `Defer` node whose body is a block of the one
  statement `s`, which is an assignment, an increment, several targets
  taking a call's results, or an expression statement (`ParseDefer`);
  `defer { … }` keeps its block. `--dump` writes the statement form back
  where the body is one statement. Like a guard, a defer is a parse error
  anywhere but among a block's statements.
* Recovery (`docs/design/language_server.md` §5 D). When a statement
  raises an error, the error is reported (`Ast::Report`) and the statement
  is dropped. `ParseBlockBody` catches the `CompileError` and then:
  * drops any nested function the statement declared;
  * records where in the block the statement was lost (`Block::brokenat`:
    the index the statement would have had; the statements parsed after it
    follow at that index);
  * marks the function being parsed as broken (`SFunction::broken`);
  * skips to the end of the statement (`Resync`): through the next `;`
    outside the braces opened since the error, or up to and including the
    `}` that closes the block. The skip stops short of a keyword that can
    only start a statement (`StartsNext`; `fn` counts only before a name,
    as `fn(` begins a function type), unless that keyword is where the
    lost statement began.

  The `}` and the keyword only count where the lexer's count of open
  braces (`Lexer::DepthBefore`) is back at the statement's, as in
  `SkipBody`. So when the error is inside a struct literal or a match, the
  whole literal or match is skipped, including its `}`. A `;` ends the
  statement even inside a literal or match, which keeps a literal that
  lacks its `}` from taking the statements after it. A literal that lacks
  its `}` and has no `;` before the block's `}` takes that `}` as its own,
  and the block continues past it.

  An error raised after the name of a struct, an enum or a top-level
  function loses only the part of the declaration not yet parsed
  (`ParseMembers`). The declaration is kept with the fields, variants,
  parameters, results and type parameters parsed before the error. The
  member in which the error was raised is dropped, together with what it
  had created (`Ast::DropSince`, from a mark that the parse moves past each
  complete member). The declaration is marked broken (`SStruct::broken`,
  `SEnum::broken`, or `SFunction::brokensig`; such a function has no body).
  A struct or an enum then skips to the `}` that closes its body, keeping
  braces balanced (`SkipBody`, by the lexer's count of open braces,
  `Lexer::DepthBefore`). Where the body lacks its `}`, the skip stops short
  of a keyword that only a declaration starts with, outside the body's
  inner braces. A function skips as a dropped declaration does.

  Any other declaration in which an error is raised is dropped entirely,
  together with the types, functions and declarations it had created
  (`Ast::DropSince` from the `Ast::Mark` taken at its start; a global's
  name is removed from its namespace too). `ParseTopDecl` then skips
  through the next `;` at depth 0 or the `}` matching a `{` at depth 0, or
  to a keyword that only a declaration can start with. An error in a
  nested function's header loses the statement that the nested function
  is, as an error in any statement does.

  A token the lexer rejects is skipped together with the rest of its line,
  or, for a `"""` string, through its closing quotes (`Lexer::scanning`,
  `SkipBrokenToken`). `PeekTok` reads a rejected token as the end of the
  file, so that the error is raised only once, by the scan that reaches
  the token.

  A `.` with no name after it is reported and kept as a `Dot` with an
  empty name (`Dot::incomplete`), and the statement ends there. If the
  rest of the statement then fails to parse, that error is a consequence
  of the first one and is not reported. The `Dot` is kept as the statement
  instead, so that a completion finds it and the checker can give its
  object a type. In a declaration that is kept with its members, only an
  error in the rest of the member holding the `.` counts as such a
  consequence (`KeepMember`).

  Recovery does not apply to the tentative parses (`TryParseTyArgs`, and a
  function value's parameter prefix). While one is under way
  (`Parser::tentative`), nothing is reported or recovered from: a
  tentative parse catches the errors within its own extent and
  backtracks, and the parse that then fails for real does so outside it.
  Once recovery from an error has skipped to the end of the file, the
  blocks still open there report nothing more (`eoferror`).
* `ParseProgram` takes each file's text from the source provider it is
  given, if the provider has that file (`SourceProvider`, `utils.h`: the
  dump that `--roundtrip` parses again, or an editor's unsaved buffers),
  and otherwise from disk (`LoadSource`). Either way, the text has no
  byte-order mark. Under `--offline` (`package_nofetch`, `packages.h`), a
  declared or blessed package whose checkout is not in the cache is an
  error at its import instead of being fetched.
* Imports are collected per file. `ParseProgram` resolves them as follows:
  * `import a.b` against the importing file's package root
    (`Ast::packages`, `Ast::filepkg`: the root file's directory for the
    program, which is package 0; a declared package's directory; and a
    standard library directory for a module found in it), then as a module
    of a package the root declared, then in the stdlib directories
    (`StdlibDirs`);
  * `import .a.b` against the importing file;
  * `import name from "source"` (`Import::source`, `packages.h`) as a
    declaration in the root file, which adds the package, and as a
    requirement elsewhere, checked against the root's declaration.

  It loads each file once, and stable-sorts `ast.globals` by a post-order
  walk of the import graph so an imported file's globals initialize first.
  An import fails when its module is found nowhere, when a package cannot
  be declared, loaded or required, or when a file cannot be opened. A
  failed import is reported at the import (`Ast::Report`, after the errors
  in the importing file's text), and the program continues without what
  the import would have loaded: the other files are parsed, and the names
  the module would have declared are unknown where they are used. A
  package whose declaration or blessed load failed is reported only at
  that import (`failedpkgs`); other imports of it are left out silently.

Import traversal visits a file's imports in source order; an edge to a file
already being visited contributes no second visit. This also determines
the initializer order in import cycles. Within a file, declaration order
is preserved. A global being visible does not make it initialized. Its
`VarDef` exists from the start, so names resolve in any order, but it has
no type and is unassigned until the driver checks its declaration. Naming
the global before then fails definite assignment, whether it is read or
written, and also when that happens in its own initializer or through a
called function (`RequireAssigned`, which `Ident::Check` calls for every
variable, and `CheckLValue` for a global before reading its type). The
standard-library search order in `StdlibDirs` is `--stdlib`,
`GOOSE_STDLIB`, `stdlib/` beside the executable and up to three
ancestors, then `stdlib/` in the working directory. The candidate relative
to the root file is tried before all of these; after it, the first
candidate that exists is used.

**Positions and diagnostics.** Every node, type expression and declaration
records where it starts (`Line`: the file, the 1-based line and the 1-based
byte column, `Parser::CurLine`), and so do fields, variants, parameters,
type parameters and imports. Every node, and every function, struct, enum
and alias declaration, also records where it ends, one past its last byte
(`Node::end`, set by `Parser::Finish` from the lexer's `prevend`, which is
the end of the token before the current one). A node is anchored at its
first token, with two exceptions: a binary expression, assignment, index,
field access or cast is anchored at its operator, and a call is anchored
at its callee. Diagnostics name the anchor's line.

A diagnostic is built as data before it is rendered as text (`Diagnostic`,
`utils.h`). It contains a severity, message, location, and related lines. The
location (`DiagLoc`) records the file and the diagnostic's start and end. For a
node, `LocOf` starts at its earliest token on the anchor's line and ends at the
node's end. The related lines show the instantiation chain
(`TypeCheck::InstantiationChain`), with each entry located at the call it names.
`RenderDiagnostic` produces the terminal output: `file:line: error: message`,
the source line for an error, and the related lines. Lexer errors also get a
caret under their column.

A `CompileError` carries both the text and the data (several diagnostics
for a `checkfree` function's kept checks, `BCE::CheckfreeError`), and ends
the compile where it is thrown. Some errors do not end the compile: the
parser's, per statement and declaration (above), and the checker's, per
root (§3.1). These are reported as they are found (`Ast::Report`) and
kept in `Ast::errors`, and any one of them fails the compile: if there
are any, the driver stops after the parse, or after the check. So the
text and the exit code of a program with one error are the same as when
the first error ended the compile. `Ast::Warn` and `Ast::Report` print the
text, or hand the data to a sink (`Ast::sink`) where one is set. The
driver sets a sink under `--diagnostics json`, which prints every
diagnostic of the compile as JSON on stdout (§1), and the language server
sets one to collect them (§12).

**Resolution** (`resolve.h`): every parse-time `TY_UNRESOLVED` name is
rewritten in place. A struct or enum name becomes `TY_STRUCT`/`TY_ENUM`; an
alias use is substituted with its target (after a structural cycle check
that treats nominal declarations as boundaries), keeping the use site's line
and adding its `const` qualifier; anything else becomes `TY_GENERIC`, which
the typechecker later validates against the generics in scope. Two kinds of
name are instead errors here: a qualified unknown name, since it can only
be a declaration, and a name in a struct's, an enum's or an alias's
declaration that is not one of its type parameters. Variant types
(`Shape.Circle`) are resolved last. After this pass, aliases do not exist
as types and "is this an integer" is a kind comparison.

An error here is reported (`Ast::Report`, in source order, once per name as
written, however many nodes share its detail), and resolution continues
with the use given a type. An unknown name, the use that closes an alias
cycle, and a variant its enum lacks each stand for a struct with no fields
that names nothing (`SStruct::unknown`, `Ast::unknowntypes`, one per name).
A variant that a broken enum lacks is treated the same way, but silently.
Type arguments on an alias use, and a `..` that cannot apply, are reported
and left out; the `..` is left out silently where the alias's type is such
a struct. If resolution reported an error, the driver stops after it, as it
does after the parse; the language server continues to the check.

**Dump** (`dump.h`): `--dump` regenerates source from the parsed tree, and
the test runner checks that dump, reparse and dump again are identical.
Diagnostics print expressions with the same code (`TypeCheck::ExprStr`), as
the user wrote them: under `dumpwritten`, the `&` that the checker inserts
where an lvalue binds by reference (`Unary::synth`, §3.3) is left out.
`--specs` still shows it.

---

## 3. The typechecker

### 3.1 Whole-program checking in call-graph order

The typechecker (`TypeCheck`, `typecheck.h`) checks the program from its
roots. The `TypeCheck` constructor:

* creates a `VarDef` for every global up front (so names resolve in any
  order);
* resolves the pools named by relative reference types;
* validates every non-generic struct and enum declaration;
* checks the global initializers in order (a `var` global declared with
  only a type gets its type's default value there, `DefaultValue`, which
  the rest of the compiler sees as if it were written in the source);
* then checks `main`, then every `thread_fn`, and finally the eligible
  functions that nothing reached (`CheckUnreached`).

That last check covers only top-level functions with fully annotated
parameters, no generics, and no syntactically detected `return from` to an
outside target. Unused generic and nested bodies are checked only when
instantiated, except for the language server's exemplars (below).

The standalone check (`CheckStandalone`) roots each reference, slice and
holder parameter at a class of its own at the globals' depth, with
writable provenance, as a call from the global initializers that passes
distinct globals would. It gives eligible grow-only parameters both
reusable-pool capabilities. A plain reference to the element type of
exactly one pool (§3.13) is instead rooted in that pool, and the
parameters given such a reference share the pool's class, as a call
linking a structure in the pool would pass them. So a body that stores
them in the pool's relative links, or asks the pool for their index,
compiles without a caller. Passing the standalone check is not evidence
that every possible call is valid.
Two whole-program fixups run after that: `SettleParamRootExactness`
(§3.4) and `VerifyLiterals` (§3.12).

A function is checked once per **specialization** (`FnSpec`, `ast.h`), and
a specialization is created at a call site, when a call resolves to a
function and no existing specialization matches. `GetOrCreateSpec`
(`typecheck_calls.h`) is the only place that decides the key:

| Part of the key | What it records |
|---|---|
| `lexparent` | the lexical environment a nested function is declared in: a specialization, or a function value's body as one check of its call sees it (`FnSpec::isfunval`, §3.12); none for a function declared directly in a global initializer, since that initializer's frame has none (§3.12) |
| `argtypes` | the concrete parameter types after generic inference |
| `bindings` | the concrete type of each of the function's own type variables, explicit or inferred, in any order. For a type variable that no parameter type mentions (`size<u8>()`), this is the only record of it |
| `roots` (`RootArg` per parameter) | the reference root *class* of each reference, slice or reference-holding argument, where the class's depth stands (`depthkey`), its writability, `reusable` and grow-shrink provenance, whether it is a `bytes_of` view, and the global pool it is rooted in (§3.4) |
| `litparams` | which parameters are literal parameters (§7.7) |
| `fnvals` | the identity of each bound function value (the block node or named function, plus the environment it captures) |
| `narrowedenv` | which optionals of the lexical environment were narrowed at the call (a nested function or block sees them narrowed) |
| `envreads` | the state, when the body's check began, of each variable outside the body's activation that the body names, or that a callee checked or reused for the body named: whether it is assigned, for a `let` whether it may be assigned (§3.9), where it points (`ref`, `refrootknown`), and what it holds (`contents`). A nested function's free variables are hidden parameters (§7.5); a global initializer's locals count among them when a function declared there, or a function value written there, reads them. A later call that finds any of these variables in a different state gets a body of its own. Filled by `NoteEnvRead` (`LookupVar`) and `NoteCalleeEnvReads` (after each call), shared among a cycle's members (`ShareCycleEnvReads`), and compared by `EnvUnchanged`. A call that reuses the body leaves the same variables as the check left them (`envexits`, `ReplayEnvExits`). A body that stored a value rooted at one of its parameters' classes into such a variable, or bound such a value to it, serves only the call it was checked for (`FnSpec::storesout`, §3.5) |
| `needs` | the concrete specializations of every `return ... from` target enclosing the call must be the same on this path |

`RootArg::exact` and `RootArg::concrete` are excluded from the key: they are
ANDed over every call site that reaches the specialization, and only codegen
(section 6.10) and the growth checks (§3.10) read them. A back edge into a
specialization still being checked (`inprogress`) reuses it regardless of
its roots (§3.11).

`CheckSpecBody` checks one body. It first pushes a `Frame` containing the
function, specialization, lexical specialization, frame index for free-variable
lookup, and call line for diagnostics. It then creates parameter `VarDef`s with
synthetic class roots and records or infers the return types. For a `recursive`
function, it seeds the cycle return roots. Finally, it clones the body, checks
its statements, and treats a value-producing tail as `return tail`.

Per-body state is saved on entry and restored on exit: pending shrinks, held
temporaries, reachability, construction destination, slot and return flags
(§3.8), and narrowings of outer variables. Outer variables assigned by the body
remain assigned if all exits agree, and maybe assigned if any exit may assign
them (§3.9). Only the specialization's effect summaries reach the caller.

**Broken bodies.** A function whose body lost a statement in the parser,
or that holds an incomplete `.` (`SFunction::broken`, §2), is checked up
to that point. `CheckStmts` throws `Abandoned` (`utils.h`) at the lost
statement (`Block::brokenat`), and `IncompleteDot` throws it after
checking the object of the `.`, for the type a completion asks for. For a broken
function's body, `CheckSpecBodyOnce` catches the exception and
closes the state opened during checking: frames, scopes, variables and nested
functions beyond its own, and the type build in progress. It finishes the
specialization from the signature. The specialization may return, its results
use the most permissive roots (`Roots::unknown`, which no rule reads as a
location), and named-result checks are skipped. So a call of a broken function
is checked against its signature
alone, as a call of an extern function is, and the caller's check
continues. A function with no declared results returns none. Nothing is
reported, since the parser already reported the error. A body broken
outside any function (a global's initializer, a default) ends its root
the same way, silently.

**Broken declarations.** An error that follows from a declaration the
parser or resolution already reported is not reported again: it silently
ends the check of the current root (`Abandoned`). Using a name that a
broken struct or enum lacks (`SStruct::broken`, `SEnum::broken`,
`LacksByError`) counts as the parser's error. This covers a field access,
a field or a variant named in a literal, a variant constant, a match arm
and a member call. `CheckV` refuses a value of a type that names nothing
(`SStruct::unknown`, `MentionsUnknown`), and also a value expected to be
of such a type (`MustFit`, a call that no candidate takes, a literal that
leaves out a field of such a type). A function whose header was lost
(`SFunction::brokensig`) offers nothing to check a call against, and no
result to continue with. A call of it ends after its arguments are
checked (`ResolveCall`), and naming it as a value, spawning it or
exporting it ends the same way. Such a function is never a root, even if
it is `main`. Otherwise, a broken struct is the struct of the fields it
has, which are validated and laid out like those of any struct.

**Roots and recovery.** Each root is checked by `CheckRoot`, which
continues past an error in the root. The roots are a global's
initializer, `main`, a thread entry (`EnsureThreadSpec`, then
`CheckThreadGlobals`), a function nothing reached, an export, and each
whole-program fixup. A `CompileError` is reported through `Ast::Report`
(§2), preceded by the pending warnings; an `Abandoned` is a consequence of
an error reported earlier. Either one returns the checker to the root's
entry (`Checkpoint`): the frames, scopes, variables, nested functions,
block positions, body states and cycle rounds beyond the root's own are
closed, a type build in progress is dropped, and reachability and the
globals' flow (assigned, narrowed) are restored to what they were.

The specializations whose checks were in progress when the error occurred
are marked `FnSpec::failed`. A check counts as in progress from the start
of `CheckSpecBodyOnce`, including an extern's validation and the check of
the compiler's own stack. A failed specialization is checked no further
and never reused: `GetOrCreateSpec` throws `Abandoned` for a call whose
key matches one, as `EnsureThreadSpec` and `CheckExport` do for a failed
entry or export. The reported error stands for that call too, and the
root making the call ends silently. A call whose literal arguments rely on
a named constant they may not rely on (`NoteLitArgs`) is in error before
its new specialization is registered, so a later call creates that
specialization anew. The struct and enum instances whose builds the error
cut short are marked failed the same way (`StructInst::failed`,
`EnumInst::failed`), and `GetStructInst` and `GetEnumInst` throw
`Abandoned` for a use of one, instead of handing a later root a half-built
instance. A specialization or an instance that completed before the error
is sound, and is reused like any other. What the root recorded for the
fixups (stores, shrinks, literal checks, cast verdicts) is kept, since it
describes what checked code did. A global whose initializer failed stays
unassigned (`failedglobals`), and a use of it ends its root silently
instead of being reported as a use before assignment.

State that a check sets temporarily is set through a scope guard
(`FlagScope`, `Saved`, `WithBindings`), so the throw of the error unwinds
it. This state includes the bindings an instantiation substitutes with,
the callee's own type parameters that an overload's trial keeps out of the
enclosing bindings (`ownexclude`), the literal adaptations a trial does
not record (`litrecord`), and a builtin call's arguments that no function
took (`builtinfallback`). As a result, the only diagnostics that one error
loses are those of what its root reached after the error (the rest of that
body and of its callers' bodies), and those of the roots whose calls would
reuse the specializations the error cut short, or that use the instances
it cut short. `goose check` prints every error in root order, and stops
before the optimizer if there were any.

A fixup continues past an error item by item (`CheckItem`); an item opens
no frame or scope that would need restoring. Each shrink of a global array
(`CheckGlobalShrinks`), growth conflict (`ResolveGrowConflicts`) and
literal argument (`VerifyLiterals`) reports its own error, and only once,
since an item recorded by code that was checked more than once (a call in
each specialization of its function, or in each round of a cycle) is
repeated. An error before the roots (a declaration's validation) or in
what the driver asked for (no `main`, a header without an export) still
throws.

**Exemplars.** For the language server (`Ast::exemplars`, never set by
`goose check`), `CheckExemplars` runs after the fixups: each top-level
function with type parameters or untyped parameters that no root
specialized is checked once more, as an exemplar
(`docs/design/language_server.md` §11). `CheckExemplar` binds each type
parameter to an opaque type of its own, and makes each untyped parameter's
type an opaque type of its own too (`OpaqueType`: a `TY_GENERIC` with
`TypeName::opaque`, named `T` or `type of x`). It then checks the body
standalone, as `CheckUnreached` does. The checker treats an opaque type as
concrete: `SubstRaw` leaves it alone, `HasGenerics` is false for it,
`ValidateType` accepts it, `TypeEq` treats it as equal only to itself (by
its `TypeName`, which a `const` copy shares), and the size-class and root
queries, which have no case for its kind, treat it as a fixed-size flat
value.

A binary operator with an opaque operand gives `bool` for a comparison and
the opaque type otherwise (`CheckBinaryResult`); so do unary `-` and `~`
and a compound assignment to an opaque target. A member of an opaque value
(`ResolveMemberLValue`), and a call with an opaque argument that no
overload takes (`ResolveCall`, `HasOpaque`), give a value of a single
shared opaque type, `?` (`OpaqueMember`). An `as` from or to an opaque type
gives the target type. Anything else is an error, which ends the exemplar:
`CheckRoot` catches it without reporting it and returns the checker to the
exemplar's entry, and no warning is given while the exemplar is checked
(`quiet`). So the exemplar's clone keeps what it annotated up to its first
error.

The exemplars' calls never reuse a specialization that a real root made
(`GetOrCreateSpec` skips those below `exemplarbase`), because a reuse
merges the call's facts into the facts the key leaves out. Once all
exemplars have run, every specialization they made is moved out of
`ast.fnspecs` and its function's `specs` into `Ast::exemplarspecs`, marked
with the function whose exemplar made it (`FnSpec::exemplar`). Only the
server's index reads them there (§12). `WithBindings` restores the
bindings it set when the function it runs throws, which can happen for an
exemplar's argument types and for a root's struct instantiation.

**Depth.** Since a body is checked inside the call that first reaches it,
the native stack holds a `CheckSpecBody` activation for every call on the
compile-time call path, and between two of them the frames of the statements
and expressions around the call. Recursion deepens it only up to a point (a
back edge reuses the specialization in progress, and a recursion whose types
never repeat stops at `MAXNESTEDSPECS`, §3.11 below), but a chain of
distinct functions can be as long as a program makes it, especially in a
generated program. `CheckSpecBody` first compares the stack pointer with
`stackfloor` (`utils.h`), which the driver sets `STACKHEADROOM` (4 MB)
before the end of the compile thread's 64 MB stack (§1 above). Below that
point, it reports "compile-time call path too deep for the compiler's
stack" with the number of nested calls. The headroom leaves room for
whatever a body nests below the check, and for unwinding the error. How
many calls fit depends on the frames the C++ compiler made, including
local variables from branches that the call path never takes: in a Debug
build those can still occupy stack slots for the whole activation.
`Binary::Check` therefore checks ordinary operands in a small frame;
reference identity, short-circuit flow and result checking live in
separate helpers, so their temporaries do not accumulate along a chain of
calls in arithmetic operands. CI checks and runs a chain of 2,000
functions shaped `let a: i64[1] = [x]; next(a[0], n - 1) + 1`, and requires
a chain of 6,000, each calling the next from 32 blocks deep, to report the
depth error. Each activation also retains its cloned body, variables and
records, so increasing the stack size alone would have limited benefit.
The optimizer's `Reach` and BCE's `BuildCallGraph` walk the same call graph
recursively, with smaller frames and without this check.

**Frames, scopes and variables.** `frames` is the compile-time call path;
`scopes` is one flat vector for the whole path (a frame records where its
scopes begin), so `CurDepth()` (the scope count) increases monotonically
from `main` inward. `vars` is the flat vector of every variable in scope on
the path, with per-frame bases. `LookupVar` searches the current frame's
scopes from the innermost outward, and then what the body can see outside
them (`ForOuterVars`): for a nested function's body, the variables in
scope at its declaration (§3.12); for a function value's body, those of
the frame it is written in, as they are at the call; and so on outward. A
variable found outside the frame's scopes is marked `captured`. Last,
unless a type parameter or a nested function of the same name hides them
(`ScopeNameKind`, §11.1), it searches the globals by the namespace rules.
A name that is not a variable is then a type parameter (`LookupTypeParam`:
the lexical binding chain, innermost first, bound to a type or a function
value), a nested function or a function; `Ident::Check` and
`CheckNamedCall` try these in that order. A call skips over a global
variable, since no call can name a variable (`CheckNamedCall`, and
`thread_spawn`'s first argument). Only a variable in scope stops the
lookup, and a global variable is named in the error only where no function
or builtin of that name exists. `localfns` holds nested function
declarations with the scope they were declared in; `Scope::serial`
distinguishes a scope from a later one at the same index. `blockpos` keeps
the statement index of every open block, which the shrink rules' liveness
scan (§3.10) reads.

**Diagnostics.** `TypeCheck::Error` appends the offending source line and
the instantiation chain: every real frame from innermost outward, with the
specialization's argument types (literal parameters marked) and the call
site it was instantiated from (§7.7). The argument types show the binding
of every type variable that a parameter type names. The rest of `bindings`
(those from an explicit list) and every function value in `fnvals` follow
the function's name in declaration order, since otherwise distinct
specializations would print alike: `size<u8>()` and `size<f64>()` on one
line read `in fn size<T = u8>()` and `in fn size<T = f64>()`,
`fn scale<T, U>(t: T)` reads `in fn scale<U = u8>(f64)`, and
`apply(3, wide)` reads `in fn apply<F = wide>(i64)`. A block has no name,
and two blocks can share a line, so a block prints as the file, line and
column of its `{`, as in `in fn apply<F = {block at x.goose:4:24}>(i64)`
(the column is `FunVal::col`; `Line` has none). A value passed on under
another generic's name prints as the function or block it is. A function
whose type variables all appear in its parameter types, and that binds no
function value, prints no list. `DumpInstance` writes a frame's
specialization this way, and in the polymorphic recursion error it names
the specialization that a call would create (§3.11). Each type is cut
short at 200 characters (`DumpShort`), because the text of a type built
by a runaway recursion can be exponentially longer than the type itself. A
chain longer than `MAXCHAIN` (20) shows its innermost and outermost ten
and counts the rest. Each line of the chain is one of the diagnostic's
related entries (§2), at the call it names.

### 3.2 Types, instantiation, and size classes

`TypeExpr` (`ast.h`) contains a kind and details specific to that kind. Its
`cq` bit represents the `const` qualifier (§9.5) and normally participates in
type identity (`TypeEq`). There are two exceptions. When comparing parameters
or results, `TopConstEq` infers top-level constness per instantiation; deeper
constness remains part of the type. When storing a whole value other than a
reference or slice, `FitsAt` uses the slot's type to determine whether the
contents are read-only.
`Subst` substitutes generic names through the lexical binding chain
(`LookupBinding`: the current specialization, then its lexical parents).
It collapses a reference built on a type argument that is itself a
reference: `T?` makes that reference optional, and `T&` is the reference
itself, loaded where it is relative (there are no references to
references, §3.8). `ownexclude` keeps a callee's own generic names from
resolving to an enclosing function's binding of the same name while a call
is unified. `WithBindings`/`bindonly` restrict substitution to an
instantiation's own bindings while its field types are computed.

`GetStructInst`/`GetEnumInst` (`typecheck_types.h`) instantiate a nominal
type once per argument list (cached on the `TypeStruct`/`TypeEnum` detail
and searched by argument equality for clones), substitute the field types,
validate them, and compute the derived properties every later pass reads:

* the **size class** (`ClassOf`). An explicit `root struct`
  (`SStruct::isroot`) is `SC_ROOT` regardless of its fields. In an ordinary
  struct, a resizable field must be last, with no
  padding after it, and makes the struct resizable. This also applies to a
  payload's resizable field. Any variable-class part makes the type
  variable-class; `varint` fields and varint-width relative references are
  variable-class. A limited array is fixed with a static capacity and variable
  with `[..]`. A fixed-mode enum is fixed; a variable-mode enum uses its
  `varclass`;
* `flat` (no references anywhere, and never a root owner), what threads and queues require;
* `frameobj` (C.2): a resizable-tailed struct whose other fields are all
  fixed-size and free of relative references, and whose tail is an array or
  itself a frame object. This is the shape that codegen keeps in the frame
  as a C struct, with the tail's header last. A root owner is not a frame object;
* `validated`, which is false while the instantiation is being built and so
  detects a struct or enum that contains itself by value, and per variant
  `vbuilt`, which does the same for a variant type held by value in a
  payload of its own enum (`BuildVariant` validates a variant's payload
  when the enum's build reaches it, or earlier where such a payload holds
  the variant). The pointee of a reference or slice waits (`laterpointees`)
  until the outermost instantiation being built is done. So an
  instantiation that is still being built is always one that the type at
  hand contains by value, never one that a reference leads back to (§3.4).
  For example, `enum L { Nil, Cons { next: L? } }` checks the pointee `L`
  once `L` is built, including its fixed-size payloads, and
  `struct A { b: B? }` declared before `struct B { a: A }` builds `B`
  after `A`;
* concrete field types; defaults are checked at each construction site
  (`CheckDefaultInit`), with declaration-scope bindings and caller effects.

A declaration whose fields name it with a larger type argument each round
(spec §3.2) would be instantiated without end: by value through recursion,
and through a reference through `laterpointees`, which would never empty.
So the chain of instantiations that led to an instance is kept:
`typechain` holds the types being built, and a waiting pointee holds the
chain on which it was found. `LimitNestedInsts` refuses an instance whose
chain already holds `MAXNESTEDSPECS` (16) instances of its declaration, as
`GetOrCreateSpec` does for a function (§3.11), and names the first three.

`ValidateType` enforces the placement rules of §3.3/§3.4 per position
(`ValidPos`):

* `varint` only in fields, elements and pointees;
* fixed, limited and grow-shrink arrays need fixed-size elements;
* variable and grow-only arrays may not hold resizables;
* an enum with non-fixed payloads is only usable in variable mode, and so
  is an enum whose payloads hold self-relative references
  (`EnumInst::selfrel`, from `HasRelRefT` over each payload field in
  `BuildVariant`), because fixed mode binds its payloads by value only, and
  a copy does not keep self-relative references (§3.5, §3.9).

A payload-less variant constant of such an enum is variable-mode
(`CheckVariantConst`). A resizable enum also binds its payloads by value
only, since a whole assignment replaces its variant (§4.4, `CheckMatch`).
So `GetEnumInst` rejects an instance whose `varclass` is resizable if a
payload that is not itself resizable holds self-relative references. It
does so at the enum's declaration, once all variants are built. A
resizable payload, which the C backend does not bind at all yet, is left
alone.

**Root owners and component regions.** Root ownership is valid only at a local
or global declaration, with an immediate literal initializer (including the
default initializer of a typed global). `ValidateType` forbids by-value
containment, arguments and results; construction, assignment, copy, image,
equality, FFI and worker checks reject operations requiring a whole value.
References remain ordinary reference types. Recursive-cycle checks use
`NeedsCycleStack`: a root with nonfixed components needs dynamic stacks, while
an all-fixed root does not.

`RootComponent` gives each field a synthetic `VarDef`, sharing its owner's
scope, depth and lifetime but with an independent storage identity. Local
components participate in the ordinary variable/effect machinery; global
components are explicitly included in storage candidates and global shrink
checks. `componentowner`, `componentbase` and `componentfield` identify the
projection, preserved across loop rechecks and optimizer remapping. The
descriptor's own region does not contain the component storage (`CanContain`).
Ordinary packed fields within a component still share that component's region.

A parameter class can also have component projections, without a concrete
storage type. `ProjectRoots`, `ProjectClass` and `MapClass` carry these paths
through calls, returns and external growth/shrink/store summaries. Specialization
keys record when a separate argument is one of another argument's root fields
(`RootArg::ownerparam`/`ownerfield`). Recursive back edges map every parameter
sharing a class, and newly discovered component projections inherit earlier
class uses. Following a stored owner reference collapses the lifetime bound
back to the enclosing class, so linked owners do not produce infinitely deep
synthetic paths.

`RootAlt::projected` proves that a view is inside a particular root field's
own region even when the owner instance is only lifetime-bounded. It permits
excluding sibling fields without claiming exact owner identity. Joins keep the
proof only when both alternatives have it, weakening removes it, and reading
an ordinary reference out of the component loses it. Self-relative reads stay
within the region. `LiveShrink` carries the same proof across calls; the root
domain tests cover its join laws. Holders have per-component store records,
including reference-valued fields, and a whole-owner traversal visits those
records. An alias of the descriptor must not conceal references stored in its
components.

The C backend binds every component through the existing local/global storage
path and constructs a private descriptor of component references. Fixed fields
use pointers, variable fields their byte representation, and resizables their
usual header and stack reference. Direct projections use component locations;
whole-owner references use a descriptor pointer. Reference-valued fields need
the address of their reference slot in the descriptor. Allocation, partial
construction and all scope exits use ordinary component watermarks. Captures
and mutable receivers preserve the existing sampling order.

Immutable exact owner parameters cache component tops and lengths when their
storage is distinct from other arguments and globals. Potentially aliased
owner/owner, owner/array and owner/global paths use the shared memory top.
Call synchronization accounts for all stacks reached through root arguments
and captures; array-view hoisting recognizes component projections. Generated
C checks exercise these decisions. See [the design and measurements](design/root_structs.md)
for v1 boundaries, the repeatable native benchmark and deferred ABI work.

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
A fixed literal at a slice destination is a temporary that codegen holds
in a C local and slices in full. A `T[]` literal has no such temporary:
`NoTemporaryLiteral` rejects it at a slice destination, as a `for`
iterable, as the base of a path (`[..]`) and as `bytes_of`'s argument,
which are the places that would view it (spec §4.2). To compare two kinds
of array, `==` takes a whole slice of an operand (`WholeSlice`, marked
`cmpview`), and that slice may view a `T[]` literal: codegen builds the
literal on a statement-scoped stack, like a call result, and the
comparison's result holds no view of it.

`append` gives its literal argument a type (`AppendedRun`): the run of
receiver elements it adds, which is a `T[k]`, or a `T[]` where the
elements are not fixed-size. The literal is checked with the receiver as
its destination (`CheckValueAt`), as a pushed element is, so a reference
in it obeys the store rule and a relative reference may point into the
receiver. Any other source is checked as it stands and must already be an
array or slice of the element type; the elements it adds are copies
(`AppendedCopies`). What the references in these copies point at is
fitted to the receiver as a store (`MustFit` under the receiver's `Dest`):
for an array, its holder root, as a copy of the whole array has; for a
slice or reference, each element as `ContainerRead` reads it out of the
storage viewed.

**Pending arrays.** `var out = [];` (§4.2) gets a grow-only array type with a
private void element type (`PendingArray`). The first `push`, `append`,
`format`, or whole assignment replaces that element type *in place*
(`CompletePending`). The `VarDef`, previously checked `Ident`s, and literal
all share this type object, so the update completes all of them. A pending array
that reaches a scope exit uncompleted is an error.
The pending array of a `reusable` declaration (`pendingpools`) takes only a
fixed-size element from whatever completes it, as `CheckVarDecl` requires of
an annotated pool (§5.4).

Anywhere else, a `[]` checked without an array type to take is the
fixed-size placeholder `void[0]` (`Val::emptyarr`). A consumer that has a
type checks the placeholder again at that type: an argument is checked
again at the parameter type that overload resolution finds for it
(`UnifyArg`). A construct whose branches are all such literals is such a
placeholder as well (`MergeVals` keeps the flag, as it keeps a null's), so
it takes a parameter's type too. A `[]` beside a branch of another type is
an error, since, unlike a constant, it adapts to no other type. Codegen
could not build the placeholder, so every consumer with no type to give it
rejects it. These consumers do so through `IsUntypedEmptyArray` and
`NoUntypedEmptyArray`:

* the value of a function value's body (`CheckFunValCall`);
* a value that an omitted result type is taken from
  (`CheckInferredResult`);
* a statement;
* a binary operator's operand;
* a member, index or slice of the placeholder;
* a `for`'s sequence;
* a rendered value;
* a member builtin's receiver;
* `qput`'s element;
* an element or fill of an array literal that no destination types;
* a variable's initializer, other than a `var`'s whole `[]`.

The other consumers reject it by its type, which a diagnostic shows as the
literal (`TypeStr`).

**Named initializer order.** `CheckInits` resolves names and checks each
initializer in declaration order, the order codegen evaluates and constructs
them directly in their destinations. It then checks each source-order inversion
with `InitOrderSafe`: both expressions, and any omitted defaults between their
fields, must be free of observable effects, runtime failures and nonlocal exits.
`InitOrderStoreSafe` also checks destination adaptations, including capacities,
length prefixes and relative offsets, at field, argument, return and local slots.
This conservative AST proof follows completed callees with a recursion guard;
unknown calls, mutation, loops and potentially failing operations are barriers.
It is independent of optimization and treats debug-only failures as barriers too.
No source-order staging or extra copies are emitted. The literal keeps its
initializers in declaration order, beside the defaults that were filled in,
and also records the order they were written in
(`StructLit::sourcefieldindices`). So a generic struct literal that is
checked again (in an argument's two phases, a loop's passes or a cycle's
rounds) infers its type arguments from the written initializers alone, in
written order, as its first check did (`StructLit::Check`).

**Defaults.** An omitted optional field is null even without an explicit
default. Other omitted fields need a declared default, unless the literal
ends in `..` (`StructLit::defaultall`, which `default<T>()` also sets on
the literal it synthesizes). A field without a declared default then gets
its type's default value (`DefaultValue`): null for an optional reference,
a `default<T>()` call for another fixed-size type, and otherwise `[]` for
an array, 0 for a varint, or a literal of its struct, variant or first
variant that in turn ends in `..`. A field whose type has no default value
(`HasDefault`: a non-optional reference, an index type, an enum without
variants, or a type holding one of these in a field without a declared
default) is an error that names the field. A declared
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
A parameter's default is checked in the same kind of frame
(`DefaultScope`), at each call leaving it out (§3.12).

Each check of a default checks again what the default constructs and
calls (with their defaults) and the blocks and functions written in it.
So a default that reaches a use of itself there would be checked without
end. `EachDefaultInPlace` walks the frames of the defaults the current
code runs in: from a default's frame to the frame of its construction or
call, from a function value's or nested function's body to the frame it
was written in (`lexframe`), and no further than a top-level function's
body. `CheckDefaultInit` rejects a field default found there
(`Frame::defaultfield`, the declaration's `Field`, so the check covers any
instantiation), and `InParamDefault` rejects a parameter's default found
there (`defaultfn`, `defaultparam`). Both name the chain:
`InstantiationChain` lists the frames of field defaults with the literal
and the type it builds (`LitTypeStr`), as it lists those of parameter
defaults. A default that leads back to itself through a top-level
function's body is a recursion (§3.11). The cache of specializations keeps
that recursion finite, or MAXNESTEDSPECS does where every round makes a
new specialization. Flagging it there would depend on whether the
function's body was first checked inside the default or before it.

### 3.3 Values, lvalues, and reference transparency

Every `Check` returns a `Val` (`ast.h`) containing:

* The type and, if the expression folds, its constant value.

* Literal flags: `strlit`, `emptyarr`, `isnull`, and `unsized`.

* `lvalue`, which says whether the value denotes storage, and `nonneg` (§3.14).

* Holder fields (§3.5).

* Branch information for control constructs: `storagebranches` and
  `implicitcopy` (§3.12), plus `joinslice` and its companions (§3.4).

* A `Prov` recording provenance (§3.4).

References are transparent (§3.8), and the checker implements this as
*decay*. `CheckV` is the raw per-node check, whose result may still denote
a reference. Every consumer goes through `CheckValue`, `CheckArg` or
`Operand` (`typecheck_exprs.h`), which load the pointee (`DecayRef`)
unless the destination keeps the reference (`KeepsRef`: a reference-typed
destination, or a whole-array reference meeting a slice destination). A
condition (`CheckCond`), an integer `match`'s scrutinee (`CheckMatch`) and
a `for` loop's count (`CheckFor`) decay the same way. A reference to an
ADT scrutinee is kept, and its tag and payload are read where the value
lies; so is a reference to the array or slice that a `for` iterates in
place. The node's `exprtype` is then the pointee's type, which codegen
follows: a written `&x` there reads as `x` (`Unary::CgX`). An assignment
to a reference-typed location writes the pointee (`Assign::pointee`); `.=`
rebinds it (`CheckRebind`). In generic code (`InGenericBody`: the current
function or one it is nested in has type parameters or untyped parameters)
`CheckAssign` turns a `.=` on a location of non-reference type into `=` in
that specialization's clone, so one generic body moves elements of any
type, rebinding them where they are references.
`CheckValue` also:

* retains `copy(x)` as a call, so repeated checking preserves copy intent;
* binds a non-fixed lvalue to a reference destination by rewriting the node
  to a synthesized `&node` (`AutoRef`, `Unary::synth`), so every later pass
  sees an ordinary reference argument, and warns on a redundant user `&`;
* keeps a reference to a non-fixed value undecayed where an un-annotated
  `let`/`var` takes it (`CheckValue`'s `inferred` flag, `IsNonFixedRef`).
  The pointee is a non-fixed lvalue, which such a binding takes by
  reference (§4.1), so the variable is that reference, exactly as with
  `.=`. `CheckVarDecl` keeps such results of a multi-value binding the same
  way. `CheckInferredResult` does the same for the value of a return or
  body tail that a result type is inferred from (§7.1), and in the other
  cases too it takes that value as an un-annotated `let` takes its
  initializer: an explicit `&x` as it is, and a non-fixed lvalue other than
  the function's own local (`IsOwnLocal`) by `AutoRef`. Such a reference
  into storage the function owns, or into a temporary, is rejected there
  with a `copy` hint, before `RecordReturn` would reject its root;
* rejects a non-fixed lvalue at a value destination unless it is a `copy`,
  or the function's own local being returned by a `return` or as the
  body's tail (`RequireCopyable`, §4.1; `inreturn`, which is cleared by the
  statements, conditions, scrutinees and `for` loops inside the returned
  value, and kept by a valued `block` or `loop` for its breaks' values). It
  likewise rejects a branch's value, or a reference to it, where the
  branch's construct has no destination type and the construct's value is
  a copy of the branch's value (`CheckValue`'s `branchcopy` flag,
  `CheckBranchCopy`);
* warns on a branch's redundant `&x` where its construct has no destination
  type and copies the fixed-size pointee (`branchcopy` again);
* fits the value to the destination (`MustFit`/`FitsAt`, §3.5). The node's
  `exprtype` becomes the destination's type, which can be wider than the
  type its operation computes at (an `i8` cast stored into an `i64`), so
  the operation's type is read elsewhere: from a cast's `AsCast::totype`,
  or from an operator's operands' `exprtype`. An array, or a reference to
  one, that meets a slice of its element type becomes a whole-array slice
  of it (`FitsAt`), whatever the destination. That slice is `const` where
  the array is read-only, and is not a pool (as `a[..]` is not,
  `Val::reusable`); the store and constness rules then check it as the
  slice it is. A generic struct literal binds a slice field's element type
  through this conversion, as `UnifyArgRaw` does a slice parameter's;
* rejects copies of values holding self-relative references
  (`NoRelRefCopy`, §3.13).

Lvalue paths (`CheckLValue`, `LValueBase`) resolve `Ident`/`Dot`/`Index`
chains to an `LVal`, which records:

* the location's type;
* its owning variable, when it is a bare name;
* its provenance;
* whether it is `let`-bound;
* whether it starts at a by-value binding;
* whether it was reached through a field or element step (`fromstorage`);
* whether it is the tail of a frame object held as a frame object, which
  has a header of its own (`fotail`, `FieldsInFrame`);
* whether it is a `varint`;
* for a path into a temporary, where what the temporary holds points
  (`intemp`, §3.6).

When the path crosses a reference (`DerefLValue`), the provenance is
replaced by the reference's: a reference *variable*'s committed binding
(§3.7), or, for a reference read out of storage, the read-back root and
the writability its slot gives (`ReadBackLVal`, §3.6, §3.8). The rest of
the path then lies in the reference's pointee, whose type the provenance
notes (`Prov::reached`, §3.5); crossing a slice likewise notes its element
type (`SliceProvenance`). `ContainerRead` is the load of a field or
element. It gives the load type (`LoadType`: `varint` decodes to `i64`, a
relative reference loads as a plain one, a `const` value loads as a plain
copy), the read-back provenance, and, for a reference or slice, the
writability its slot gives.

The load of a `varint` is marked (`Val::isvarint`) and read-only. Bound by
reference, it is the `varint&` that its storage type gives (`StorageType`,
which `BindsRef`, `AutoRef`, `FitsAt` and `UnifyArgRaw` bind by), never an
`i64&`. What a reference to a `varint` loads is marked too (`DecayRef`), so
a construct whose branches are such references binds as `varint&` at a
reference parameter. Neither is `i64` storage: a format overload taking an
`i64` by reference is rooted at a read-only temporary (`CheckPrintable`),
which codegen fills with the decoded value (`EmitUserFormat`). A varint
part of a printed value keeps its own type, which no `i64` overload takes.

**The statement's operands.** Values evaluated earlier in a statement stay
live until the operation consuming them ends, and what runs later in the
statement is part of a variable's liveness. Both are read off the tree
instead of being recorded along the way. The checker keeps the path of
nodes it is inside (`nodepath`, one `PathEntry` per node with the count of
its operands evaluated so far, entered by `NodeScope` in `CheckV`,
`CheckStmt`, `CheckStmtExpr` and `CheckLValue`) and the checked value of
every expression that points somewhere (`nodevals`, `RecordVal`).
`ForOperands` lists each node kind's operands in evaluation order, with
how the node consumes them (`HoldKind`: a value, a view of an array's
elements as `==` takes, the elements an index or slice reaches, a
builtin's receiver, an assignment's location, the sequence a `for`
walks). `HeldOperands` walks the path and hands every operand already
evaluated to the shrink checks, as its parent holds it (`HoldAs`: a
reference or slice as it is, a holder as a reference per pointee, the
receiver as a reference to the array or a view of its elements, a
location as the slot alone). `LaterOperands` names the operands not yet
evaluated, and, inside the condition of an `if`, the scrutinee of a
`match` or the sequence of a `for`, the parts that the head leads to
(`AfterHead`; `UsedAfter`, §3.10).

A `for`'s sequence is the only operand held across a construct's parts: it
is held while the loop's body is checked (`HoldForSequence`, §6.5).
Codegen writes the path to the sequence into the loop (`ForLoop::CgStmt`,
`GenLoc`). So each reference or slice that the path loads out of a field
or element (a `Dot` or `Index` of such a type; `&` of a path counts as the
path) is held as a view of the storage it lies in (`Held::reread`). The
sequence itself is held as a view of its elements, unless it is a
variable (which the loop names, so the scans see it) or a resizable array
(whose length the loop reads again). A struct literal lays out all its
initializers, including defaults, before checking the first one
(`CheckInits`), so each field has its own position among them. A call that
is resolving its overload is `discovering`: its operands' values are those
that its phase 2 checks. A call that applies its callee's summary has
consumed its own operands, which are checked through the callee's
parameter pairs (§3.10). A callee body checked in the meantime starts with
an empty path (`CheckSpecBody`), since the caller's statement belongs to
the caller: the call site applies the summary against it.

### 3.4 Roots and provenance

The lifetime system of §9 is one record on every reference-like value:

```
struct RootAlt {         // one place the value may point
    VarDef *root;        // the variable whose scope bounds the pointee; null = static data
    bool exact;          // root's own storage holds the pointee (else it only outlives it)
    VarDef *from;        // for an inexact read-back: the container, for diagnostics
    bool slotread;       // read out of a field, an element or a global (§3.10)
    bool classread;      // read out of the storage a parameter's class stands for (§3.10)
};
struct Roots {
    vector<RootAlt> alts;   // every place the value may point, one per root
};
struct Prov : Roots {
    bool writable;       // §9.5
    int reusable;        // the root is a reusable pool (§5.4): RU_SLOTS or RU_SLICES bits
    bool byteview;       // a bytes_of view over typed storage
    TypeExpr *reached;   // the pointee type of the last reference or slice its path crossed (§3.5)
};
```

A value that may point at several places (an `if`'s branches, a
variable's bindings, a function's returns, the candidates that a read out
of a container has, §3.6) has one alternative per root, and every rule
checks each alternative:

* for a store, every alternative must outlive the destination (`FitsAt`);
* a shrink frees what any alternative points into (§3.10);
* a value is storable inside a recursive cycle only where every
  alternative is (`CycleStorable`);
* the value names one array (`Roots::Exact`) only when it has a single,
  exact alternative.

`Roots::Root` is the innermost alternative's root: the one scope that
every alternative outlives. Diagnostics name it, and a single bound stands
for it. Empty roots mean a value with no provenance (a null, or no
reference at all), which every rule accepts as it would accept static
data.

`Roots::Same` compares alternatives as a set, independent of the order
branches or recursive rounds discover them. Ordinary equality compares all
provenance, including `unknown`; `Prov` adds its permissions and view flags
through defaulted equality. Two explicit projections cover the other uses:
cycle convergence ignores `from`, whose local containers are recreated,
while global reachability compares only roots, exactness and source
containers, which are the fields that analysis reads. A merge of alternatives
reports change by comparing the complete result with its previous value, so
weakening any provenance fact feeds back into the surrounding analysis.
Joining a previously unknown discovery state also reports a change, even
before that state has any alternatives; joining it again is idempotent.

**Depth.** Every `VarDef` has a `depth`: 0 for globals, else the scope count
at its creation along the current compile-time call path. Because scopes
accumulate along the path, a callee's locals are always deeper than
anything its caller passed, and "A outlives B" is `Depth(A) <= Depth(B)`
for variables live together on one path. One sentinel has the largest
depth: `temproot` (what a reference variable not bound yet points at).

**Temporaries.** A value made without a name (a literal or a call's
result, `str`'s text, a popped element) is rooted at a `VarDef` of its
own (`TempRoot`, `istemp`), one past the depth of the scope it is made in.
Codegen frees it when its statement ends (§6.2), or when the block whose
tail made it ends. So it outlives the variables of the scopes that the
statement opens (a `for` body over it, a `match` arm on it), but not the
variables the statement itself declares. What a temporary holds is read
back where it points (`intemp`, §3.6). A store record never names a
temporary as the source of what it holds (`RecordStore`): nothing on
record describes a temporary's contents, so the stored value's own root
bounds them. A popped element that is itself a reference or slice is not
such a temporary, but the reference or slice it holds. It is read as an
element of the receiver is read (`ContainerRead`): it points where the
element pointed, and is as writable as its slot.

The by-value result of an `if`, `match`, block, loop or bare `{ }`, of a
function value's call, and of `copy(x)` and `default<T>()` is also such a
temporary, except where `default<T>()` gives a null reference or an empty
slice, which are static data. Codegen builds it in storage of its own
(`CtlValX`, or `GenLoc` for any other value it addresses), and copies a
branch's value there even where the branch names a variable. `TempCopy`
roots a construct's value, the value of a function value's call
(`CheckFunValCall`) and a default there, and `CheckBuiltin` roots a copy
there. What the temporary holds keeps pointing where the source's
contents do. Like a call's result, the temporary is read-only and not an
`lvalue`, so nothing binds it by reference or writes it in place. A slice
value keeps its roots and writability, pointing where the branch's slice
does, but not the branch's slot (`Val::slot`): the slot holding the slice
is the temporary, and a `format` overload that takes the slice by
reference is given that temporary (`CheckPrintable`). A reference
parameter binds a construct's branches themselves instead
(`BindBranchesByRef`, §3.12). Where the optimizer folds a construct to the
branch taken, `OptViewed` keeps a viewed value a copy (section 4, "Views
of copies"). This is what lets the shrink rules treat a view of it as a
view of the temporary alone.

A construct whose branches are arrays and slices of one element type is
not such a copy. Its value is a slice of that element type (spec §6.4),
and each array branch becomes a whole-array slice of itself (`FitsAt`,
spec §3.10), rooted where the array is, so `CheckBranchRoot` holds the
array to the construct's scope. With a slice destination type, the
branches are checked against it. Without one, which branches join is
known only once all of them are checked. So where branches join, the
construct's `Check` (`CheckJoin`) checks them twice:

* First as they are, with the construct on `joinpath`: the construct and
  each branch value it checks next, including a construct there, like
  `argpath` (§3.12). There `JoinBranches` merges a slice and an array, or
  arrays of two types, into a value that has only a slice type
  (`Val::joinslice`; `joinhasslice` where a branch is a slice of its own).
* Then a second time against that slice type, from the flow state before
  the first check, with no destination for stores and no typed slot
  (whatever receives the value checks it).

A construct on `joinpath` is part of the first construct's check: it
leaves its value uncopied (where nothing joins, applying `TempCopy` is up
to the first construct) and leaves its joins to the first construct too.
Until the first check is known to stand, its warnings are held back
(`BodyState::joinprobes`, as loop passes hold theirs, §3.7), and the
copies of non-fixed storage it would report are noted (`CheckBranchCopy`
defers them on `joinpath` as on `argpath`) and reported after it. The
growths and shrinks it logged for the values under construction around
the construct (`growlog`, §3.10) are dropped, and the second check logs
them again. Arrays of two types with no slice among them join only for a
parameter declared a slice, so a construct on `argpath` leaves such a join
to its call (`NoArrayJoin`, §3.12), and every other construct reports
such branches as mismatched. Each `break` after the first is checked
against the first `break`'s type (spec §6.4), unless the first one's
value joined arrays (`CheckBreak`). Where the construct has no
destination type, a `break` that agrees with a slice type is not stored
anywhere by that agreement, as a branch of an `if` would not be.

**Where a root comes from:**

| Expression | Root | Exact |
|---|---|---|
| a variable `x` (`Ident::Check`) | `x` | yes |
| `&lvalue` (`CheckRefOf`), or an lvalue bound by reference (`AutoRef`, a slice's slot being `Val::slot`) | the lvalue's owner | as the path |
| a reference or slice variable (`RefProvOf`) | its committed binding (§3.7); for a global `var` used in a function's body, the read-back rule (`GlobalVarRead`) | its binding's, weakened by rebinds; the read-back's |
| a reference read out of a field or element (`ContainerRead`), a popped reference or slice element, a `for` binder of one, the reference element `a.push(v)` or `a.alloc_ref(v)` adds | the read-back rule (§3.6) | only with one candidate |
| a slice loaded through a reference to one (`SlotView`) | for a reference to a slice variable, that variable's binding; behind a parameter's class that has a class for the slice its slot holds (below), the binding of the variable standing for that slice (`VarDef::heldslice`); out of a field or element, the read-back rule; behind any other parameter's class or a temporary, the root as a bound | as that |
| `a.push(v)`, `a.alloc_ref(v)`, `&a[i]` of an element that is no reference | `a`'s root | `a`'s exactness |
| `a.alloc_slice(n)`, `a.realloc_slice(s, n)` | `a`'s root | `a`'s exactness |
| `a[lo..hi]` (`SliceExpr::Check`) | `a`'s root | `a`'s exactness |
| a call result (`CallResult`) | every root the callee's returns give (`RetRoot::alts`), mapped and then united as branches are (`MergeVals`). The mapping (`RetAltVal`) takes a parameter's class back to the argument's roots at this site (at a back edge, to every argument the class's parameters get), a global or captured local to itself, and null to static data. At a back edge, static data only bounds the result: the key gave a parameter static data, but the back edge may give it other storage | only where they all map to one root exactly |
| an `if`, `match`, `block` or `loop` value of reference or slice type (`MergeVals`), array branches joined as a slice included (`CheckJoin`) | every one of its branches' roots, an array's where it is stored | only where they all name one root exactly |
| an array, struct or variant literal, and a call's value result | a temporary (`TempRoot`): anything that views it, instead of being built from it, views a temporary | no |
| any other `if`, `match`, `block`, `loop` or bare `{ }` value, a function value's call, and a `default<T>()` other than a null reference's or an empty slice's (`TempCopy`); `copy(x)` | a temporary (`TempRoot`), holding what the value it copied held | no; a copy's yes |
| a string literal | static data (null) | yes |
| `null` | none (adapts to any optional) | -- |

**Merged roots.** A value that may be any of several values (an `if`'s
branches, a reference variable's bindings before and after a rebind to
another root at its depth, `CheckRefRebindRoot`, or the roots a call's
callee returns, `CallResult`) has the union of their alternatives
(`Roots::Add`). Two alternatives never share a root. When an alternative is
added for an existing root, the merge keeps the weaker
exactness. It retains the source container (`RootAlt::from`, §3.6) only if
both alternatives were read from that container. For example, merging a
reference into a parameter's storage with a reference read out of that storage
can yield either. The store record describes only the second. A literal's references
(`NoteLitElem`) and a container's contents (`VarDef::contents`) are united
the same way. The rules that ask what storage a root *is* (the grow-shrink
store rule, §3.5 rule 3, `GrowShrinkTaint`, and the cycle store rule, rule
4, `CycleStorable`) ask every alternative, so a merge does not need to
carry anything beside the alternatives it keeps. At a call, a parameter's
class stands for the argument's whole set of alternatives
(`ClassArgRoots`), and that set is what the callee's stores, shrinks and
growths map back to.

**Parameters and root classes.** A callee never sees the caller's variables;
it sees *classes*. `GetOrCreateSpec` groups the reference, slice and holder
arguments of a call by their root. Distinct exact roots get distinct
classes, numbered by depth (so a class number is an outlives rank), and
static data is class 0. An argument that is not exact (one with several
alternatives, or an inexact one) always gets a class of its own, at the
depth of its innermost alternative, because sharing a class asserts "the
same array", which such an argument does not establish.

What a class's grow-shrink array can hold is part of the key: the element
types of the grow-shrink arrays its root holds (`RootArg::gselems`; a
class passed on names its own root's). The body's store rule and shrink
scans test a pointee against these types (`GrowShrinkCanHold`), so that
every call site a body serves gets the same answer. For example,
`k(bag.n)`, where `bag` holds only an array of `Pt`s, and
`k(others[0].w)`, where `others` holds `i64`s, reach bodies of their own,
and only the first may store its `i64&`. A class whose grow-shrink array is
not its own root's (`RootArg::gsvia`, also part of the key) is taken to
hold anything a grow-shrink array could. Such an array can be another
alternative's, one viewed by a slice the argument refers to, or one in
what an inexact alternative's root leads to that the argument may point
into or that its pointee holds, including a whole grow-shrink array read
back out of a parameter's storage. A reference or slice argument gets no
grow-shrink class if its pointee neither holds a grow-shrink array nor can
lie in the elements of a grow-shrink array that its roots hold (or, for an
inexact argument, lead to) (`GrowShrinkCanHold`, `BoundReachesGrowShrink`).
An example is a `u8[>..]&` read out of a struct whose tail is a
`u8[>..<]`.

An argument whose root holds a grow-shrink array, or whose pointee holds
one, still points into no grow-shrink array's elements where every place
it may point (for a holder, every place its references may point) is a
slot read (§3.10). The key records this (`RootArg::slotread`). In the
body, the parameter (or the holder's contents) is then a slot read, which
may be stored, while what the body reaches through it is still checked
against the class's grow-shrink array.

`CheckSpecBody` then creates one synthetic `VarDef` per class. It carries
the call-site root's depth, the key's grow-shrink facts (`classfrom`
remembers the root it came from), and the types its parameters'
references lead to (`classreach`). Every parameter of the class is bound
to it exactly within the body, so inside the body a class names one
array, whatever the call site. A holder parameter's class only bounds
what the holder holds, by the deepest root its references point into. So
the holder's contents (`contentexact`, and the store event that stands for
them) are exactly that array only where all of the argument's references
point into one array (`RootArg::heldexact`, part of the key), and never in
a `recursive fn`, whose back edges reuse the body whatever they pass.

A reference to a slice is rooted at the slot holding the slice, whether it
binds a slice lvalue by reference (`RefSliceArgs` gives the argument the
slot's roots, as `AutoRef` gives the phase-2 check) or is written `&s`. So
its class stands for a slot of the caller's: a variable, a field or an
element. The body reaches that slot through references alone when the
slot is a slice variable beyond the variables the body can name
(`EnvReach`, so not a global), a temporary, or a caller's class that
itself has one of these (`HasView`). In that case, if the body's function
is not `recursive`, the slice the slot holds when the call is made
(`Val::held`) is a class of its own. That class is numbered with the
parameters' classes and keyed the same way (`FnSpec::views`; its
`gselems` are its root's), and its `classreach` is what the slice leads
to.

The body binds a variable standing for that slice to the class
(`VarDef::heldslice`). A load through the slot's class sees this variable
(`SlotView`), and every store that may write the slot joins it (below). A
call maps the class back to what its argument's slot held, as it maps a
parameter's class to the argument (`RetAltVal`, `ApplyCalleeStores`,
`ApplyCalleeLiveShrinks`, `NoteClassUses`, `NoteRootEvent`). Any other
slot's class only bounds the slice loaded through it. A slice parameter
given a reference to a slice takes that slice's root (`LoadSliceArgs`).
Every store event (`AddStoreEvent`, including a callee's as mapped at its
call) into the class of a slot that has such a variable joins the stored
roots to that variable (`NoteSlotStore`). So does a store event into any
other class or bound that can hold a slice of the variable's type, since
that may be the same slot, where some call site passes the slot to both.
A store checked in a body nested in the function, or in a function value
written there, joins the slot's class as a bound instead, since the
stored roots are that inner body's own; the call that reaches the inner
body maps its record to precise roots. A loop feeds a join back as it
does a rebind (`NoteFact`), and a nested body that reads the variable
reads it outside its activation (`FnSpec::envreads`).

A temporary of the calling statement outlives the call, so its class takes
the body's own outermost depth instead (`ClassDepth`): the body may keep it
in its locals, but not in anything of the caller's. Classes are numbered by
these body depths, so a temporary ranks after every variable here too,
whatever depth it shares with one at the call site.

Only the call sites know whether two *different* classes are different
arrays. Codegen's stack-top caching (section 6.10) and the growth checks
(§3.10) ask `RootArg::exact`/`concrete`, which `SettleParamRootExactness`
propagates through the `via` links after every call site has been seen.
The shrink rules have each call check the pairs of roots that its callee
could not tell apart (§3.10, **Parameters' views**). A call maps what a
body records against a class (a store into it, §3.5, or a shrink or a
growth of it, §3.10) back to the root the class stands for at that call
(`ClassArgRoot`). For a reference or slice argument, that is the
argument's own root. For a holder argument, it is the holder root, which
is what the holder's references point into, and never the holder itself,
of which the callee received a copy.

**Depth keys.** A body is checked with its first call site's class depths
and then serves every call with the same key, so the key has to hold
everything the body's checks compare those depths with. The class numbers
order the classes, but the checks compare more than that order:

* the store rule (§3.5) lets a class be stored into another's storage
  where the two share a depth, but not where it is merely deeper;
* a reference or slice variable is rebound only between roots at one
  depth (§3.7);
* a value that may come from either of two classes (an `if`'s value, say,
  or the value of a call whose callee returns either) takes the deeper
  class's root, or the first one's where they tie;
* only a class at depth 0, a global's, may be stored into a global.

A nested function also compares its classes with the depths of the
variables it captures, and a function given a function value does the same
while it checks that value's body. So each class also carries
`RootArg::depthkey`. Where the class's body depth is within `EnvReach`,
the depth key is that depth itself. `EnvReach` covers the scopes open in
the frames of the lexical environments the body can see: its `lexparent`
and the frames its function values were written in, or a global
initializer's frame for a function declared, or a block written, directly
in one (`LexFrame`). These frames hold every variable the body can name
outside itself; for a body that sees none, the reach covers only 0, the
globals. Past that reach, the depth key is the class's rank among the
distinct depths of the call's classes beyond it, negated. Calls with
equal keys agree on every such comparison, and a call that does not agree
gets a specialization of its own, checked for its depths. A function called once
with a global and once with a local is split even where its body compares
nothing; an extern function, whose body is C, keeps every key 0. A back
edge reuses the specialization in progress regardless of its depth keys,
as it does regardless of its classes (§3.11), and the depth keys play no
part in deciding whether those classes still describe the arrays the back
edge passes (`concrete`).

A class is a **pool class** (`VarDef::poolclass`) when every member is an
exactly rooted reference to a resizable-class value: no function in a
recursive cycle holds such a value across a call into the cycle (§7.8), so
references in a pool class are exempt from the cycle store rule.
`classpool` is the global pool every member is rooted in, when all agree
(§3.13). Any other class whose members are all exactly rooted references or
slices is a **threaded** candidate (`TypeCheck::ThreadedClass`), exempt in
the same way while every call back into its cycle passes it on (§3.11). A
synthetic class root is told apart from a variable by its `classfrom`,
since a variable whose declaration is being checked has no type yet
either.

### 3.5 The store rule and the store record

`FitsAt` enforces §9.2's store rule. The **destination** of the value being
checked is `curdst`, a `Dest` that holds:

* a root and its exactness;
* whether the destination is itself a reference variable, which makes the
  operation a binding rather than a store;
* what the path to the destination reached: the pointee type of the last
  reference or slice it crossed, in which the filled slots lie
  (`Prov::reached`).

`curdst` is set by declarations, assignments, element arguments
(`ElemArg`), appends, and literal fields. It is cleared to "no destination"
for call arguments and returns, because a parameter dies before its
argument's root, so an argument is never a store. When a reference, slice,
or **holder** value (a by-value struct, array or payload that contains plain
references or slices, `HoldsPlainRef`) meets a destination with a root:

1. a value that points nowhere yet (`Roots::unknown`: a cycle's first
   round, §3.11, or a loop's discovery pass, §3.7) passes, and is checked
   again once it points somewhere;
2. every root of the value must be at or above the destination's depth.
   Where the destination has several alternatives or an inexact one, every
   root must also be at or above the depth of every storage those
   alternatives may stand for. `ShrinkTargets` finds that storage over the
   type the path reached, or over the slot's own type where the path
   crossed no reference: each exact alternative itself, and for an inexact
   alternative its root and each read-back candidate at its depth or
   outside it, with a parameter's class as the bound on the caller's
   storage behind it. Whatever owns the slot holds a value of the type
   reached, and whatever can hold such a value can hold the slot. So, of
   the storage the slot's type admits, this leaves out only storage the
   slot cannot be in, such as an argument list beside a borrowed context's
   table of `Input`s: the argument list can hold an `Input`'s text but no
   `Input[>..]`;
3. a reference that may point into a grow-shrink array's elements may be
   bound to a variable but never stored (§5.2). A global reference or slice
   variable is storage, so binding one is a store here.
   `StoredIntoGrowShrink` decides whether a reference may point into a
   grow-shrink array's elements:
   * by any of its roots except a slot read's (`GrowShrinkTaint`, §3.10),
     or for a holder by any root of its contents except a slot read's,
     using `GrowShrinkCanHold` with the byte-view exception `MayBeViewed`;
   * for a reference's inexact root, which only bounds the reference, by a
     grow-shrink array in what that root's storage leads to through
     references (`BoundReach`);
   * for a reference to a slice, by what the slice may point into;
4. inside a recursive cycle, only these may be stored, and every
   alternative of the value must be one of them (a rebind of the
   activation's own reference variable is exempt, §7.8):
   * a reference into a global, a pool-class parameter or a local of an
     enclosing non-cycle function (`CycleStorable`);
   * a reference into a threaded parameter class, where every class among
     the destination's storages is threaded too (`ThreadStorable`,
     `ThreadedChain`, §3.11);
5. the store is **recorded** on each of those storages, with one event per
   alternative of the value.

Rule 3 does not wait for a destination. A literal's field or element is
storage wherever the literal lands, so `NoteLitElem` applies rule 3 to each
one (`StoredIntoGrowShrink`), also in an argument or a result. A parameter
class created from a root that holds a grow-shrink array may also point
into a grow-shrink array (`IsGrowShrinkRoot`), unless every argument it
stands for is a slot read (§3.4). So may the holder result that a back edge
gets for a parameter to which the entry call gave static data (§3.11). As a
result, no holder ever holds a reference into a grow-shrink array. This is
what lets the §5.2 shrink scan look at variables only (§3.10), and what
lets the references held by a holder read out of a field or an element
count as slot reads. Nor may a variable that points into no grow-shrink
array be rebound to a value that may point into one
(`CheckRefRebindRoot`): a store or return checked before the rebind (later
in a loop, through a reference taken to it) has already let it through.

An inexact root names a scope, not the storage that owns the pointee.
Examples are a read-back through a parameter (`t.a[0]` for `t: Top&` with
`a: Cell[>..<]&`, rooted at `t`'s class), the contents of a holder copied
out of a container, and a result that a callee read out of its parameter.
What an inexact root bounds may lie in the root's own storage or in any
storage its references lead to, at any remove, and so may a grow-shrink
array the value points into. `BoundReach` gives those types:
`ReachedThroughRefs` of a variable's type or, for a parameter's class, of
its parameters' types (`VarDef::classreach`). Rule 3 counts every
grow-shrink array among them whose elements can hold the pointee
(`BoundReachesGrowShrink`), and the diagnostic says the array is reached
through the root. A holder's own references were each checked where they
were stored, so for a holder the rule looks no further. A byte view counts
every grow-shrink array a byte view can cover, but only where no field,
element or global has held it (`Prov::freshview`). Rule 3 lets a byte view
into a field, element or global only where it covers no grow-shrink array,
so a byte view loaded out of storage never covers one, whatever per-value
`byteview` a recursive call's u8 views or a holder's contents give it.
`bytes_of` makes a byte view that no field, element or global has held.
Variables, branches, rebinds, references to slice variables (`SlotView`),
returns and calls keep that property (a call result that is its argument
has the argument's), while a back edge's u8 result has it whatever its
record says.

An assignment through a reference (`PointeeAssign`) stores where the
reference points. For a reference read out of a field, that is where the
read-back rule says (`DerefLValue`), not the field's container. Through a
reference to a slice, it is the slot the reference names (`Dest::slot`),
which may be a slice variable's own slot. Each storage the store lands in
that is a slice variable is assigned the value as `s = v` would assign it
(`StoreIntoSlot`, `RebindSliceVar`, §3.7), whether the reference names the
variable exactly or only may name it. Where a reference to a slice has an
inexact root, the storage it may name includes:

* each slice variable of the slot's type, at that depth or outside it, that
  a reference has been made to (`VarDef::slotref`, set by `SlotRoots` and
  `&s`; before that, nothing can name the variable);
* every global slice variable, since a function checked later may make a
  reference to it.

These are `RootCandidates`' `slots`, which a read-back of a reference to a
slice passes as well (§3.6).

A parameter's class stands for a slot of the caller's, and the event says
so (`StoreEvent::slot`). The call assigns each slice variable among the
storages its argument may be (`ApplyCalleeStores`) what the callee stored,
as seen from the call. A slice the callee loaded through another
reference-to-slice parameter, whose slice's elements hold no references, is
the slice that argument's slot holds (`SlotView`); anything else at a class
is that class's argument, as a bound. The call repeats this until no
binding changes, since the callee's record keeps no execution order. A body
nested in the functions whose variables it names may also be storing into
one of those variables through its class. So at the store, each slice
variable of the slot's type there, at the class's depth or outside it, that
a reference has been made to is assigned the value, except for what the
body loaded through the class itself, which the slot held already. A body
naming such a variable is checked again for a call after a reference to
the variable is made (`EnvRead::slotref`).

A declaration without a type annotation infers its type from the value, so
`FitsAt` has no destination type to check. `CheckBindingRoot` applies rule 2
instead (also to every name in a multi-value declaration), and applies rule
3 to a global. This prevents a variable from retaining a view of its
statement's temporary. The sentinels pass: a variable may hold what a
reference that is not bound yet, or a back edge's result, points at.

A branch's value (an `if`'s branch, a `match` arm, a block's tail, a
`block`'s or `loop`'s breaks) reaches whatever receives its construct's
value after the scopes the branch opened have ended. This holds for a
call's argument too, although an argument is no store. `CheckBranchRoot`
applies rule 2 to the branch's value as the construct closes, against the
depth the construct is in. A variable deeper than that has ended, and so
has a temporary made inside the construct, which is one deeper than the
temporaries of the construct's own statement; those last (a `match`'s
scrutinee among them). A declaration of such a value meets this rule before
`CheckBindingRoot`, so this rule is what keeps a variable from viewing a
local of the block whose value it is.

The grow-only shrink rule of §5.1 consults the record (`storeevents`: one
`StoreEvent` per alternative of each stored value, program-wide). Each
event holds:

* the container;
* the stored alternative's root and exactness;
* the pointee type;
* the byte-view flag;
* the source container for a holder copy. A copy holds what its source
  holds. An inexact read-back's source is the container it came out of, and
  a read-back out of a parameter's class is bounded by the class, except
  for what came out of the storage the class stands for alone, which holds
  what that storage holds (`StoreEvent::classread`, §3.10 **Class reads**);
* the line.

A source is the single container the value came out of, whose stores say
what the value holds (`StoreSource`). A value that may lie in any of
several places, or anywhere a root only bounds, names no source
(`HolderSource`, which a `for` or `match` binder's copy takes too).
`ReadBackRoot` and `SlotView` keep `RootAlt::from` only for a container
named exactly, and a merge keeps it only where every value it joins names
that container. A value out of a reference or slice variable names no
source either, since the variable holds what its binding says, not what a
store put there. The stored alternative's own root bounds such a value. A
value read back out of the container and stored back into the container's
own storage adds nothing and makes no event. A value stored into storage
that the container only leads to (`StoreEvent::bound`), which may outlive
what the value points into, is an event like any other, with the container
as its source (a call replays it as well). `RecordStore` also maintains the
container's `contents`: the union of every root stored into it so far. A
root at a parameter's class is marked where every store of it there put
views that the class's storage holds (`AddContents`, §3.10 **Class
reads**).

A store into a caller's storage (through a parameter's class root) is kept
on the specialization as a `classevent`. `ApplyCalleeStores` replays it at
every call site with the class mapped back to the root it stands for there
(§3.4): for a holder parameter, to what the holder's references point into,
and where the class only bounded the value, only as bounds, with no source
of their own (`Bounds`). Each replayed event adds to the container's
`contents` as a store of the caller's own does, and a loop around the call
feeds that back in the same way (`NoteFact`, §3.7). A class the callee
named as a source maps to the single container the argument names exactly,
if there is one. For a reference to a slice, that is the struct or array
whose field or element is the slot, but not a slice variable, whose slot
the callee's record bounds instead. For the class of the slice a slot holds
(§3.4), it is the single container that slice names exactly. An event for
what came out of a class's storage alone keeps that meaning where the
container is a class of the caller's. Events that are alike except for
that mark share one entry on the specialization, which keeps the mark only
where each of them has it (`AddStoreEvent`), and a cycle's rounds compare
the mark (`SameRecord`). A place the mapping names exactly is left out
where its storage cannot hold the stored reference's pointee by value,
under any qualifier or storage form (`MayHoldValueOf` over `MaySameType`).
For example, a view that a caller's holder holds may stand for a `Link&`
the callee pushed into the `Link[>..]` that one of the holder's references
names, but never for a reference into the `u8[>..]` that another names. So
only the arrays that can hold a `Link` must outlive the storage the `Link&`
lands in.

A nested function's store through a parameter of the function it is
declared in, or a function value's store through a parameter of the
function it is written in, is into a class of that outer function, not of
the callee. The call keeps such a store on the caller's own record, with
its value's roots mapped, until it reaches the record of the function whose
class it is; that function's callers map it. Such a body's store into a
variable outside its activation (a nested function's store into its
parents' variables, or a function value's into those of the frame it was
written in) is on that variable's own record. Where the value is rooted at
a class of the body being called (the nested function's own, or that of
the function a function value is handed to), the call maps the event onto
its arguments in place (`ApplyCalleeStores`), and maps the variable's
`contents` with it. The class means nothing once the activation has ended,
and a merge or a copy of the variable holds what the call passed. What the
call passed is marked where it is one of the views that the storage of a
class of the caller's holds (`AddContents`). No class of the callee's is
left there, marked or not. A reference or slice variable that such a body
rebinds to a value rooted at the class is bound the same way: the call
takes the class out of its `ref` and adds what the class stands for there,
as the call's result would map it (`RetAltVal`). What the mapping adds
reaches the loops around the call (`NoteFact`). The class, as the body
recorded or bound it, reaches only the loops and rounds of that activation
(`ActivationFloor`, `AddBinding`): a loop calling the body checks the body
again in each pass, and the class is new in each pass. The events and
bindings are mapped for one call, so no other call reuses such a check
(`FnSpec::storesout`). A global's `contents` are left as they are, and a
check that stores into globals alone may still be reused: the stores into
globals are judged through the calls that passed each class (§3.10, item
5).

This includes stores through slices: writing a reference into a viewed
element changes the caller's container just as writing through an array
reference does. A permutation may preserve the container's existing
contents provenance, but a new incoming reference must not disappear from
its store effects. Only stores read back from the same container preserve
the existing record without adding an incoming root.

An event keeps the type its destination reached (`StoreEvent::reached`),
and an event made through a class that only bounds the caller's storage
behind it is marked `bound`. The body checked such a store against one
class, as deep as the argument's root. Where that root is inexact, or the
event is a bound one, the call widens the store to every storage the
argument's root may stand for (`ShrinkTargets` over that type). It checks
the stored value's mapped root against each of those storages and records
the store on each, marking it `bound` again where the storage is a class
of the caller's own. A stored value that is one of the views the storage
of a callee's class held (a `classread` event out of that class) maps to
what each place the class's argument names exactly holds: a local holder's
`contents` (`HeldAt`), or a view that a class of the caller's holds. It
does not map to the argument's roots as bounds. The call applies those
events after its other events, and repeats them until what they store
stops growing, since its other stores may add to the storage they read
(`mappedheld`). For a callee still being checked (a back edge), the call
replays the record that the callee's cycle made in its previous round
(`RecordOf`, §3.11), and replays nothing in the first round.

Holder values carry their contents' roots as `Val::contents` (§9.2's
"implicitly generic over the fields' roots"):

* a literal's contents are those of its reference initializers
  (`NoteLitElem`, `HolderFromLit`);
* a variable's contents are its `contents`, marks included (§3.10 **Class
  reads**). They hold a loop's later stores in its next pass (§3.7), and
  are none where only nulls and empty slices were stored there so far (so
  `var x = N { link: null };
  return x;` returns nothing rooted in `x`). A global's contents are rooted
  at the globals (the null root, inexact);
* a container read's contents are the container's own `contents` where the
  value lies in one local holder exactly, and otherwise the container's
  roots, inexact (`HeldAt`). Read out of a field or an element they are
  slot reads (§3.10), as a `for` or `match` binder's copy's, a popped
  element's and what `append` copies out of a slice are; read out of a
  temporary they are the temporary's own;
* a holder loaded through a reference (`DecayRef`) is a container read of
  where the reference points, and its contents are no slot read;
* a container read's source is the container only where the holder lies in
  exactly one container (`Val::holderfrom`). Where that is the storage a
  parameter's class stands for, the holder's contents there are views that
  storage holds (`RootAlt::classread`, `MarkClassCopy`, §3.10 **Class
  reads**);
* a copy's contents (`TempCopy`) are its source's;
* a holder parameter is keyed by its contents' class like a reference, by
  whether that class is exactly the one array they point into, and by
  whether they are slot reads (§3.4). The class (its `ref`) bounds what is
  read back out of the parameter or out of a copy of it (§3.6).

### 3.6 Read-back roots

A reference read out of a container names a scope, not the storage it points
into. `ReadBackRoot` (`typecheck_types.h`) re-derives the owner as §9.5
describes, once, at the read, with one alternative per candidate:

* a self-relative reference inherits the container's root and exactness; an
  `in pool` reference is rooted at its pool, exactly;
* a container whose own root is a global: the candidates are the globals
  whose storage can hold the pointee type by value. `CanContain` looks
  through elements and fields, stopping at references and slices, and takes
  a relative reference's storage to hold its own type, which is what a
  slice of relative references points at. Static data is a candidate too
  when the pointee is `u8` and the reference or slice read back is `const`,
  or when nothing else can hold the pointee (a writable reference or slice
  is given static data only as a null or an empty slice). A function's body
  checked for an earlier global's initializer, before some global's
  declaration is checked, cannot ask that global's type, and the body's
  check also serves the calls made after the global exists. So every
  read-back there, of a local container as well, gets the inexact null root
  as a bound for the globals (`RootCandidates`), like a global holder's
  contents;
* a container that is an exactly rooted local of the function being
  checked: the candidates are
  * the visible locals declared at the container's depth or outside it that
    can hold the pointee;
  * the pointees of reference and slice variables in scope with committed
    roots at that depth;
  * the globals, and static data;
  * the class root of every parameter in scope whose pointee, or whose
    by-value contents, lead through references to storage that can hold the
    pointee (`ReachesThroughRefs`).

  For a reference to a slice, the slice variables among the candidates are
  those §3.5 describes: a local once a reference to it has been made, and
  every global. The storage those parameters lead to is the caller's, which
  the body cannot enumerate. A holder parameter is a local, but its
  argument filled what it holds, and whatever the body copies it into holds
  the same. The class root only bounds that storage (`RootCandidates` lists
  it in `bounds`).

  But a holder's `contents` are what every store into it since it was made
  put there (`AddContents`). This includes a store through a reference to
  the holder, at each place an inexact reference may name (`ShrinkTargets`),
  and a store by a callee, a nested function or a function value, as its
  call maps it (`ApplyCalleeStores`). A value read out of the holder where
  it is checked (`ReadBackLVal`) is one of the contents' roots
  (`ContentsReadBack`) where each of those roots is one of these:
  * a variable's own storage, exactly;
  * static data;
  * a view that the storage of a parameter's class holds (its mark, §3.10
    **Class reads**);
  * a parameter's class bounding what its argument held (the event a holder
    parameter's contents start with where its argument points into several
    arrays, `RootArg::heldexact` false).

  An exact root stays exact, as it was stored, except for a variable whose
  own storage cannot hold the value's pointee type (`CanContain`), which a
  store of some other view put there. A marked root is one of the views, as
  a value read out of the storage is. A class bound stays a bound: the
  caller's storage, which the caller filled before the call and which can
  hold nothing of the activation's own. So what `words` or `split` made of a
  local buffer views only that buffer, and what they made of a string
  literal views only static data. The rules that need identity (`index_of`,
  relative stores, class grouping, BCE's `UltOf`) take a reference stored
  exactly as rooted where it was.

  A store later in a loop body reaches the read on the next iteration. A
  store of anything else there adds a root to the holder's `contents` or
  takes the mark off a root, which the loop feeds back (§3.7), and the
  loop's next pass checks the read with that store on record. Contents with
  a root that only bounds what was stored (a holder copied out of a slot,
  whose contents its container bounds) take the candidates. A `for` loop
  binding references or views read out of the holder reads them this way at
  the start of each of its passes (`CheckFor`), since every element any
  iteration reads was stored before the loop or by an earlier pass's body,
  which the loop feeds back. A holder of a frame that a nested function's or
  a function value's body is written in (`OuterLocal`: a frame on the path
  the body names) is read the same way. Its stores are on record as the
  declaring body's are, and the body is checked again for a call that finds
  the holder holding something else (`envreads`, §3.1);
* a container reached through a caller's storage, or itself inexact: the
  candidate is the container's root, inexact, and the value counts as read
  out of that container (`RootAlt::from`) only where the root is the
  container itself. Where that container is a parameter's class named
  exactly, the value is one of the views held by the storage the class
  stands for (`RootAlt::classread`, §3.10). That storage is the caller's
  holder a reference parameter names, or the elements of the caller's array
  a slice parameter views;
* a container that is a temporary (a literal, a call result or a copy,
  reached without crossing a reference, `LVal::intemp`): nothing in the
  temporary can own what it holds, which came from the literal's
  initializers, the callee's result or the copy's source. So the root is
  the temporary's holder root, with the same exactness (`TempContents`). A
  `for` over a temporary and a `match` binder copied out of one get the same
  answer.

Each candidate is an alternative of the value: a variable's own storage
exactly, a parameter's class as a bound, and static data as the null root.
The value names one array only when there is exactly one candidate. For a
value read out of a slot (the `slotread` of §3.10), an exact candidate
whose grow-shrink array could hold the pointee (`GrowShrinkCanHold`) is
left out. Rule 3 of §3.5 keeps every reference rooted there out of slots,
whatever else of that candidate could hold the pointee, so the candidate
owns nothing a slot holds. A bound stays, since it carries the lifetime,
and so does the owner of a whole grow-shrink array, which a slot may refer
to. What is left names the array exactly more often, which `index_of`,
relative stores and class grouping see. The arguments, results and
bindings the read reaches name no such array: a parameter's class made from
the read may be stored, and a caller's shrink pair passes it (§3.10,
parameters' views). Which arrays the value may point into is settled here,
so an array that comes into scope later at the same depth is not among
them. `ReadBackWhy` turns the
alternatives into the "may point into `pool` or `spare`" diagnostic the
rules that need identity produce, naming a bound as "the caller's storage
behind" its parameter. A byte view read back takes the container's
`contents` where they are known.

### 3.7 Reference variables commit to a root

A reference or slice variable is bound at its first non-null binding
(`BindRefProvenance`); before that it reads as `temproot` (`RefRootOf`). A
null-only optional reads as null, with no roots, where every binding that
can come before the read has been checked: anywhere for a local, in the
initializers for a global (`ownerspec` and `CurRealFrame().spec` both null
there), and for a global `let`. A nested function's or a function value's
body that reads a local of a frame around it is checked again for a call
that finds the variable in another state (`envreads`, §3.1). A binding such
a body makes reaches the loops and cycle rounds around the call
(`NoteFact`). A call that reuses a check made for an earlier pass's or
round's incarnation of the variable binds the variable as the check did
(`ReplayEnvExits`). A read of a global `var` in a function's body goes
through `GlobalVarRead` instead (below). `CheckRefRebindRoot` implements
§9.2's rebinding rule: the same roots keep everything (an inexact new value
only weakens exactness); another root at the same depth joins the
variable's alternatives; any other depth is an error. A store through a
reference into a slice variable's slot binds the variable the same way,
whether it is a `let` or a `var` (§3.5, `StoreIntoSlot`), where the
variable is bound already. A variable bound to what points nowhere yet is
left to the pass or round that binds it.

**Global `var`s** (`BoundAnywhere`). A function checked after a body that
reads a global `var` of reference or slice type may bind that global, and
the body's check serves every call (`NoteEnvRead` keys a body by the locals
it reads outside its activation, not by globals). So in a function's body
(`CurRealFrame().spec` set), reads of such a global, `RefProvOf` and
`RefRootsOf` alike, do not use its binding but `GlobalVarRead`. That is
`ReadBackRoot` of the global as a container of itself, read as a slot
(§3.6): the globals that can own the pointee (exact where there is one, and
the pool for an `in pool` reference), less the grow-shrink arrays that
could hold the pointee, which the store rule keeps every binding of a
global out of (§3.5, rule 3). Each alternative is a slot read with `from`
the global, which `ReadBackWhy` words as a global any function may bind.
The read's `writable` is the binding's, which the global's type decides
(§3.8), and so is its `reusable`, which says how the variable is
represented (`PrefVar`). Its `byteview` is that of the bindings so far. A
`const u8` global of this kind always has static data among its
candidates, so no identity rule takes it as exact. The shrink of a global
array that a later byte-view binding makes the variable view is judged
against the recorded bindings (`NoteGlobalBinding`, `CheckGlobalShrinks`).
A self-relative global `var` only ever holds null and keeps the variable
rules.

**Loops** (`CheckLoopPasses`). A loop body is checked repeatedly until what
it feeds back to the loop's head settles: the roots its rebinds give the
variables declared outside it, the stores into their contents, and the
narrowings and assignments its back edges (the end of the body, every
`continue`) drop. Each pass starts from the join of the loop's entry state
with the previous pass's back edges, which is the state every iteration but
the first starts in. So on the second pass, a read earlier in the body than
a rebind sees every root the rebind gives the variable, and a shrink
earlier in the body than a store sees the store on record (**Grow-only
arrays** below). A fact fed back is noted as it is recorded, against every
enclosing loop the variable is declared outside of (`LoopPass`). `NoteFact`
is called from `BindProv`, `CheckRefRebindRoot`, and wherever a store adds
to a container's `contents` or takes a class's mark off them (`AddContents`
in `RecordStore` and in a call's replay of what its callee stored,
`ApplyCalleeStores`). A pass that changed no fact and read no variable
before its binding was checked against the settled facts, so its errors
stand and it is the last pass.

Every pass before that is *discovery*. In a discovery pass, a reference
variable read before any binding (`var last: Node? = null;` before
`loop { if last { last.next .= child; } … last .= child; }`) points nowhere
yet (`RefProvOf` gives it no roots), and every rule lets such a value pass
(`FitsAt`, `CheckRootedAtReceiver`, `BindRefProvenance`, a call's
specialization keyed `RootArg::unknown`, the shrink scans through
`UnboundIsBottom`). The
pass after the last one that changed anything reads such a variable as it
would be read outside a loop. A rule that errs where a value cannot point
somewhere (`free_slice` given a run of an array the receiver can be none
of, §3.14) needs every place the value may point, and a later pass can
still add to those. So in a discovery pass such a rule lets the value pass
(`VerdictDeferred`), and every loop still discovering then runs the settled
pass that judges it. A holder declared inside the loop carries only its
current pass's store events (`LiveEventBase`), since an earlier pass's
events are those of a previous iteration. A holder declared outside the
loop carries them all, and a store an earlier pass recorded is reported as
reaching the shrink on the next iteration (`CarriedEvent`). A pass's
warnings are kept back until the loop's last pass, as a construct's first
check of its branches keeps its own (`WarningsHeld`, §3.4). A callee body
checked from inside a pass runs its own loops' passes (`CheckSpecBody`
clears `looppasses`). A fact it notes about a variable outside it also
feeds the loops of the bodies it was called from (`outerbodies`), since a
nested function or a function value rebinds, and stores into, the
variables of the frames around it. A store or a binding of a value rooted
at one of the body's parameters' classes is a fact for its own loops and
rounds alone (`ActivationFloor`): the loops around the call see what the
call maps it to (§3.5).

### 3.8 Writability

`const` is the `cq` bit on the type (§9.5); writability of a *value* is the
`writable` provenance bit. The checker keeps these consistent at slots through
`constslot`. `SlotScope`
sets it for fields, elements, annotated variables, assignment targets, and
literal initializers. It is cleared for calls (including overload resolution)
and returns. When set, it makes `FitsAt` reject a read-only reference or slice
stored in a slot whose type is not `const`.
What is read out of a slot is therefore as writable as the slot's type says
(`ContainerRead`), and parameters and results are generic over constness:
`RootArg::writable` is part of the specialization key, so a function given
a literal and the same function given a buffer are two specializations, and
a write through the parameter is an error only in the first.

`const` is shallow, so that holds whatever the path to the slot is. The
load, a path crossing the reference there (`DerefLValue`) or stepping into
the slice (`SliceProvenance`), a `for` binding of the element, and a field
being rendered (`CheckRenderable`) all take `SlotLoadWritable`'s answer
rather than the path's writability. So `h.r.n = 7` and `inc(h.r.n)` agree
for a `const h` whose `r` is an `S&`. The slot itself stays as writable as the
path: `h.r .= y` is rejected, and `&` of a slice field of `h` is a `const`
reference. A self-relative reference is the exception: it points within the
value or array holding its slot (spec §3.9), so it is no more writable than
the path to the slot, or `inc(c.link.v)` would write a `const c` built with
`link: self`.

`let` prevents whole-binding assignment (`NoLetAssign`, via
`LVal::letbound`) but does not restrict writes to contents. By-value `for`
and `match` bindings are copies; writes to them are rejected by
`NoCopyWrite` using `VarDef::copybind`. A `for` element that is itself a
reference binds as the reference it holds either way, loaded and rooted by
the read-back rule where it is relative. `&x` only makes it a binding that
writes through (`ForLoop::CgStmt` loads it, never the slot's address). A
variable's `const` type works the other way round. A bare name's
`LVal::writable` is its *contents'* writability, which `&x` and the paths
into it inherit, while a write of the variable itself (`=`, a compound
assignment, `++`/`--`) is left to those two rules alone (`WholeWritable`).
So a `var s: const u8[:]` can re-slice itself. An un-annotated variable
takes that type wherever its initializer is a read-only reference or slice
whose type does not mark it read-only (a `u8[:]` result or parameter that
is read-only in this specialization). `CheckVarDecl`'s `Finish` adds the
`const`, so a later assignment of a read-only value fits; in a writable
specialization the same declaration stays `u8[:]`. A field's or an
element's `LVal::writable` is the writability of the path to it, which
assigning the slot as a whole takes. The contents of a field or element of
a `const` type other than a slice are read-only however they are reached,
as a variable's are: by a step into them (`ResolveMemberLValue`,
`CheckLValue`'s index), by `&x` (`CheckRefOf`), by `x[..]`, or by binding
the value by reference, passing it or growing it (`ContainerRead`).
`&x` of a `const` value is a `const T&`, a slice of read-only storage is a
`const T[:]`, a string literal is `const u8[:]`, a `bytes_of` view is never
writable, and `null` and the `default<T>()` of a reference or slice count
as writable so they fit any slot.

A reference to a slice names the slot holding the slice. What is loaded
through the reference (its pointee, `DecayRef`; a path crossing it; a
builtin's receiver; a `for` over it; a slice argument) is that slot's slice
(`SlotView`). That slice is as writable as the reference, the binding of a
slice variable or of the variable standing for a view's slice
(`VarDef::heldslice`, keyed with the view, §3.4), and the slice's type
allow. Behind a parameter's class without a view, an inexact root or a
read-back, only the reference is left to say how writable the slice is. So
a writable reference is only ever made to a slot whose slice is writable or
of a `const` type: `&x` of a slice variable is no more writable than its
binding (`CheckRefOf`), as binding the variable by reference is
(`AutoRef`), and a field's or an element's slice is writable unless the
slot's type says `const`. What a store through the reference writes is the
slot, so the reference is also no more writable than `&` of the slot. A
slice lvalue bound by reference (`SlotRoots`, for `AutoRef`, `FitsAt` and
`RefSliceArgs`) is the reference that `&x` of it makes (`Val::slot`). That
reference is read-only through a by-value binder, a `const` value, a
`const T&` or a call's result, and a variable's is no more writable than
its binding. A slice of a `const` type stays read-only through a writable
reference, so a writable reference may re-point a field of that type.

Any other pointee loaded through a reference (`DecayRef`) lies where the
reference points and is as writable as the reference, as a path crossing
it is (`DerefLValue`). A `format` overload taking that pointee by reference
is handed it there (**Format overloads**), and when a construct joins an
array behind the reference into a slice, the slice views the array there
(`JoinBranches`, spec §6.4). A varint's pointee is read-only, since it
loads as the `i64` it decodes to, which an overload taking an `i64&` would
otherwise write (spec §3.6).

### 3.9 Flow state: definite assignment and narrowing

Each `VarDef` carries `assigned`, `maybeassigned` and `narrowed` (the `T&`
type an optional is narrowed to). `SaveFlow`/`RestoreFlow`/`MergeFlow` snapshot
and join these facts at every
branch. They cover all variables that the intervening code can name: globals,
variables in the current frame and its lexical parents, and variables in the
frames where callable function values were written (`NamedFrames`). No other
frame's variables can change during that code. A fact holds after a join iff it
holds in every reachable
branch, and a variable is maybe assigned iff it is maybe assigned in some
reachable branch. `reachable` tracks divergence (`return`, `break`,
`continue`, `abort`, `exit`, and calls proven not to resume their caller).
A local snapshot entry keeps its index, variable identity and facts
together; restoring or joining it never transfers a departed local's facts
to a later variable at the same index. `JoinFlow` computes a snapshot over
the currently nameable variables without mutating their live state;
`MergeFlow` installs that result.

A loop or `block` is left in the join of the states at its exits. Each
reachable `break` out of it joins its state into the construct's scope
(`NoteBreak`, `Scope::breakflow`). `JoinBreakFlow` joins that with the
construct's own exit (the end of a block's body, a `while`'s condition
found false, a `for`'s head once its iterations run out), or, for a
`loop`, which has no exit of its own, takes the breaks' join alone. A
callee's check leaves the variables it can name narrowed as it found them,
assigned where all of its exits agree, and maybe assigned where any exit
may have assigned them (`CheckSpecBodyOnce`, `BodyExits`, `NoteExit`),
rather than as the end of its text leaves them. Its exits are each
`return`, the tail and a reachable end. Variables it found assigned stay as
they were. Where no path may reach the end of its text, as after an `if`
whose branches both return, every variable holds every fact and may be
assigned on no path. A function value's `return`, or a `return from` in a
callee, exits the body it names (`Frame::exits`). Every body checked in the
frames between records what its activation had assigned by then and what
it may have assigned since it began (`FnSpec::outerexits`), and a call that
reuses that body applies this again (`ReplayOuterExits`). A body with no
exit its caller reaches leaves the variables as its end did: the code after
the call never runs. A branch that diverges has the null "bottom" type,
which unifies with anything (`UnifyBranch`, `MergeVals`), and reads as
`void` once the construct's node is left (`VoidIfBottom`). An `if` that
ends its block (as the `if` a guard parses to does) and whose `else`
diverges is marked `IfExpr::flat` (`CheckIf`, by `blockpos`). Codegen puts
its then-block after the `else` in the C block the `if` is in (§6.2), and
the inliner counts no block for it (§4).

`FnRecord::mayreturn` retains `BodyExits::reached` when the body finishes.
Direct calls consume it after applying the callee's effects; dispatch joins
it across all cases. A missing recursive record conservatively may return,
and `SameRecord` includes this fact in convergence. It describes Goose
control flow: long-distance returns still use ordinary C returns and their
existing flag propagation, so it must not become a C `noreturn` annotation.
`CheckStmts` stops at the first unreachable statement, truncates the
specialization clone's statement list and removes its dead tail. Later
passes therefore see only checked nodes, without dead-node annotations.
The source body remains available to other specializations. Pruning uses
proven exits, never a provisional assumption that recursion cannot return.
Argument rechecks start from the call's entry reachability, including its
receiver; an exit found in a later argument during discovery must not erase
an earlier argument's effects. The continuation still requires the arguments
to complete, including the dispatch argument that phase 2 does not recheck.

A `let` declared without an initializer is assigned, or bound with `.=`,
only where it is not maybe assigned (`NoLetReassign`, §4.4). This is
judged once the value is checked, since computing the value may assign the
`let` too. Every assignment and binding sets the bit. A `&&`'s or `||`'s
right operand may run, so what it assigns is maybe assigned after the
condition. A loop's head joins its back edges (§3.7), so a `let` declared
outside the loop and assigned in it is maybe assigned in the next pass,
where the assignment errs. A nested function's key holds the bit only for
a `let` (`EnvIs`), whose check reads it. A call that reuses the body leaves
a `var` maybe assigned where it was or where the check left it
(`ReplayEnvExits`).

`NarrowCond` narrows on `if`/`while`/`assert` conditions, through
`!`, `&&` and `||` (a `&&`'s right operand may not run, so what it
un-narrows is recorded as `Binary::rightkills`), and `== null`/`!= null`.
In loops, a rebind in the body (a `.=`, a callee's `reboundoptionals`)
drops the narrowing from that point on. The body is checked again in the
join of the loop's back edges with its entry (`CheckLoopPasses`, §3.7), so
a use that relied on the narrowing errors there. A `while` condition runs
before every iteration, and its own narrowings hold in the body each time.
A callee's rebinds of the caller's optionals reach the caller through
`ApplyCalleeRebinds`. For a callee still being checked, they come from the
record of its cycle's previous round (§3.11), and there are none in the
first round.

### 3.10 The shrink rules, and growth and uses during construction

**Grow-only arrays** (§5.1). `pop`, `pop_n`, `resize` and `clear` on a
`[>..]` (`CheckBuiltin` → `CheckGrowShrink` → `GrowOnlyShrinkAt`,
`typecheck_builtins.h`), and whole assignment of one or of a value holding
one (`CheckAssign`, or `PointeeAssign` through a reference, with
`ResizableArrayIn`), pass in this order:

1. the receiver names the array's variable, or a reference variable or
   parameter whose root is known (the array behind it shrinks), or is a
   struct's tail field on such a path. A tail field's roots, those of the
   storage the struct lies in, are the receiver's own (`Val`'s). The
   receiver is not a temporary, and is not in a global initializer. An
   assignment's location is such a receiver on the same terms
   (`ShrinkRootsKnown`, `GrowOnlyAssignKnown`);
2. not a `reusable` pool;
3. no operand evaluated earlier in this statement and still held may refer
   into the array (`CheckHeldShrinks` over `HeldOperands`, §3.3). Where
   such an operand is a reference to a slice or to a holder, or a slice of
   slices or of holders, what it holds may not refer into the array either.
   An assignment's location counts as the slot alone, since the assignment
   overwrites what the slot holds, and so does a slot that a `for` loop
   reads again, whose sequence is held itself. The shrink may be anywhere
   in its statement. The node path holds whatever the enclosing expressions
   evaluated before it, also inside the value of an `if`, `match`, block or
   loop. A function value's body is checked on the path of the statement
   calling it (`CheckFunValCall`), whose own caller applies the shrink
   against its statement from the summary;
4. no variable in scope may refer into the array. This covers:
   * a reference or slice variable whose pointee the array's elements can
     contain (or a byte view), rooted at the array (or a `var` at the same
     depth, or an inexact root at or below its depth, or a not-yet-bound
     variable declared at or below it), **and used afterwards**;
   * a holder variable into which a store of a reference into the array is
     on record (`HolderMayPointInto`, following holder copies through their
     source containers and judging a global source by its type), or a store
     of a reference to a slot that may hold one (`StoredSlotMayPointInto`),
     and used afterwards;
   * a reference to a holder whose store record says the same, or to a
     slice slot that may hold a slice into the array, and a slice whose
     elements, slices or holders, may hold one (`HeldRefsMayPointInto`),
     and used afterwards.

   What such a slot holds depends on what it is:
   * a local holder named exactly holds what its record says (behind a
     reference or slice variable, one that is no `var`), and an array's
     elements that a slice views count among these;
   * a parameter's class named exactly holds what the activation's own
     stores into it put there, and the rest is for its callers to judge
     (**Parameters' views** below). The class stands for the caller's
     holder a reference parameter names, or the caller's array whose
     elements a slice parameter views, whether a variable or a holder's stored
     reference or slice names it; a by-value holder parameter's contents
     count among these where they are that one array (`RootArg::heldexact`,
     §3.4). A view read out of that storage, in an operand still held (item
     3) or a variable that is no `var`, points where the storage's views do
     and is judged the same way. So is a holder of the activation's that
     keeps such a view or was copied out of that storage alone (**Class
     reads** below);
   * a class that a holder's stored reference to a slice names is a slot
     that may be a slice variable, whose binding no store record describes
     (`StoreEvent::sliceref`, `RefSlots`). So it holds whatever is at its
     depth or outside it, as below;
   * behind a reference variable, a parameter's class that has a class of
     its own for the slice its slot holds: the class holds what the
     binding of the variable standing for that slice says (§3.4), and, as
     for a slice of such values, what that slice's elements hold;
   * any other slot holds whatever is at its depth or outside it, except a
     global slot that a holder refers to, or a global holder that a
     reference or slice leads to, which item 5 judges.

   A slot's references may lead into the array at any remove, so both
   count every type they reach (`ReachedThroughRefs`), except for a
   reference to a whole resizable value: that is the path to the value,
   which no array's elements hold. A holder whose type has no slot for a
   view into the array's elements holds no such view, whatever its record
   says (`CanHoldViewInto`). A view derived from a stored path lies in such
   a slot, and its store is unrecorded where it came out of the same
   container. The array's elements are those of the storage the shrink
   names (`ShrunkType`): the root's own type, the bound's type where the
   root only bounds it, and for a parameter's class, which has no type, the
   type the callee's record or the receiver gives the array. So a callee
   replacing an argument's `u8[>..]` through a reference field frees no
   `i64` that a holder of the caller's may refer to.

   A whole assignment's scan counts a path as leading into the array for a
   variable that its right-hand side, which runs while the array is
   rebuilt, may take a reference out of (`ExposedLater`: named there other
   than to read a field path whose value holds none). An in-place walk's
   scan does the same (`pathslive`). For any other variable, used only once
   the assignment is done, a path is as valid as after a `clear`. The
   storage that shrinks is a holder like any other. The array's own
   variable is scanned where the fields before its tail can hold references
   (`RefsBesideTail`), and a reference to that storage (a reference
   parameter's class included) is judged by that storage's record rather
   than as live outright. The variables scanned are the body's and its
   lexical parents', and in a function value's body also those of the
   function running it (`ShrinkScanVars`). Every caller's variables are
   judged at its call instead, the first caller's too (**Calls** below),
   where the rest of its statement is in view;
5. for a global receiver, the other globals are judged once every function
   has been checked (`NoteGlobalShrink` keeps the first shrink of each
   array, with its instantiation chain, for `CheckGlobalShrinks`). A global
   that the record leaves able to hold a reference into the array fails the
   shrink, and the error names the store. The record followed is the stores
   as made (`madestores`, which a call's in-place mapping of its callee's
   events, for the first call only, leaves alone) and the bindings of
   global references and slices (`NoteGlobalBinding`, beside `VarDef::ref`).
   It is followed through the containers a copy came out of (`src`,
   `from`), and through the calls that passed a parameter's class
   (`classuses`, which `NoteClassUses` fills at every call, back edges
   included), with a class standing for every argument its calls gave it.
   A root that bounds the storage and names no container it was read out of
   (static data a back edge returns included, which `RetAltVal` makes
   inexact) may be any array its pointee fits. A store through a reference
   into a global slice variable's slot is both an event on the variable and
   its binding (§3.5);
6. The shrink is recorded for callers by `NoteShrink`, using `shrinkexternals`
   for globals and captured locals, and `shrinkparams` for parameters.
   `NoteLiveViews` also records still-used views that only callers can
   distinguish from the array (**Parameters' views** below). `NoteShrinkEvent`
   logs the shrink for surrounding values under construction (**Growth during
   construction** below).

Inside a loop, a store later in the body than the shrink (a callee's store
included) is on record when the body is checked again (`CheckLoopPasses`,
§3.7). So item 4 finds the store, and names it as reaching the shrink on
the next iteration (`CarriedEvent`).

**Emptied holders** (`Kill`). A whole assignment of a value holding no
references (`NoteKill` from `CheckAssign`, and from `PointeeAssign` through
a reference variable) empties the part of a holder its location names
(`KillPath`): a variable's own storage, or the pointee of a `let` reference
or a reference parameter, followed by the field names stepped into, in the
one container the location lies in exactly. The kill keeps the index of the
event at which the value began to be checked. It is stored in the body state
(`BodyState::kills`) as part of the flow state (`FlowState::kills`). A
branch join keeps a kill that both sides made, at the earlier index; a
loop's head takes the join of its entry with its back edges; and an `&&` or
`||` keeps a kill that its right operand did not end. A store into the
container that no event records (`RecordStore`: a holder's own references
put back into it, a value pointing nowhere yet) ends the container's kills
(`DropKills`). So does a call that may write into the part a kill names
(`CallMayWrite` from `ApplyCalleeShrinks`), and a function value's call
ends them all (`CheckFunValCall`). A call ends kills because a callee's own
unrecorded stores, and a nested function's stores replayed in place under
the events of its first check, show in no event after the kill.

Only a reference to storage holding the part, or lying in it, can write
there. So a call may write there:

* where an argument points at storage that may be the container
  (`MayAliasRoots`), with a pointee that may hold the part's type or lie in
  it;
* where any reference an argument leads to through its references, whatever
  its root, has such a pointee (`ReachedThroughRefs`, `MayHoldValueOf`
  either way);
* where the callee can name the caller's variables: a nested function, or a
  function with a function value bound (`FnSpec::fnvals`) or handed to it.

A struct of the caller's own, built from the holder's reference fields and
handed to a callee that rebuilds the tree those fields lead to, leaves the
holder's emptied tree empty. Item 4 then scans a holder from the earliest
kill on (`KilledFor`), where kills empty every slot of it that can hold a
view into the array's elements (`CanHoldViewInto`). A holder variable is
scanned from its own kills. A reference to a holder
(`HeldRefsMayPointInto`), a parameter's class included, is scanned from the
kills through that reference; since its pointee type is the container's
own, those kills are the container's own. The pairs `NoteLiveViews` keeps
leave out the class's contents where its kills empty them (`HeldViews`).
This replaces the outright verdict on a reference to the storage whose
references lead to the shrunk array: a view derived from the path there
would have been stored since then, unrecorded, and that store ends the
kill. A whole assignment whose right-hand side may follow such a path
(`pathslive`) takes no kill.

**Calls in order** (`FnRecord::ShrinkOrder`, `NoteShrinkOrder`). Beside each
grow-only shrink's entry, a body records:

* how many of its class events were on record at the shrink, at its latest
  occurrence (a loop's later passes count the earlier passes' stores);
* the parts of reference parameters' pointees that every occurrence had
  emptied, as a parameter index and a path.

A shrink recorded on a lexical parent's record, or in a body that a
recursive cycle's rounds replace, records every event and no kill. A call
(`ApplyCalleeShrinks`) replays the callee's class events first, noting for
each event it adds the class event it came from (`ApplyCalleeStores`'s
`origins`). It then judges each grow-only shrink as it stood:

* with the events from class events made after the shrink set aside
  (`CallOrder`, `StoredLater`, in `HolderMayPointInto` and
  `EachHolderRoot`);
* with the callee's kills mapped onto the parts of the caller's holders the
  arguments name, each from the call's first event on (`argkill`: the
  argument's path by `KillPath`, through its implicit `&`, in the one
  container it points into, which no nested function or function value
  names).

The caller's own record puts the shrink after the class events of its own
that the call added or met from the callee's earlier ones (`Replay`,
`AddStoreEvent`). It keeps the mapped kills where they are kills of its own
parameters, so a function looping over such calls through a reference
parameter is judged the same way by its callers. A body's kills judge only
the variables it names: a nested function's scan of a lexical parent's
reference finds no kill made through its own parameter, which the call
sites alone could tell is the same storage.

**What a class leads to** (`ShrinkTargets`' `classbound`). Two kinds of
shrink free the class's own target, a bound, or any array of the type at
its depth or outside it, globals included:

* a shrink through a reference read out of the storage behind one of the
  activation's parameter classes (an inexact alternative at the class, read
  out of it or only bounded by it);
* a callee's shrink of an array the argument only leads to, where the
  argument is such a class.

The other arrays (`ShrinkTarget::viaclass`) are scanned against the
activation's own variables but not recorded (`GrowOnlyShrinkAt`'s
`viaclass`: no `NoteShrink`, `NoteShrinkOrder`, `NoteLiveViews` or
`NoteGlobalShrink`). The class's bound stands for them at every call, where
the argument names them. A call whose argument for such a bound is a
reference or slice naming one local holder exactly takes the arrays from
that holder's record (`HeldAt`, its contents, which by then include the
callee's own stores there). In that record:

* a variable named exactly whose type can hold the array, and that leads to
  no such array through its references, is the array itself;
* a variable that can neither hold nor lead to one is left out
  (`MayHoldValueOf`), and any other variable only bounds the array;
* a parameter's class of the activation bounds the array as what the
  storage behind the class leads to, with its candidates `viaclass` like
  those above;
* a reference that the holder's own root only bounds, derived from what the
  holder holds, adds only the holder's own storage, where that can hold the
  array: it is what a callee read out of the holder's references and stored
  back.

A holder holding anything else (a lexical parent's class, an inexact root
of another variable) leaves the argument's own roots to bound the array.

**`for` loops** (§6.5). A `for` around the shrink holds what it walks while
its body is checked (`HoldForSequence`, §3.3). Item 4 and the grow-shrink
scan below meet the loop's sequence, and the references and slices on the
path to it, as held operands, whatever statement of the body the shrink is
in, and name the loop (`Held::loop`). A callee's shrinks meet them at the
call through its summary, a function value's shrinks meet them at the call
that runs it, and the pairs `NoteLiveViews` keeps carry them to the
callers. A sequence whose elements hold references leads to what those
point into, whatever the loop's binding copies out of it. A resizable
array iterated itself is not held: the loop reads its length again on every
iteration, and its elements never move.

**Liveness** (`UsedAfter`) is syntactic. A variable is used afterwards
where its name occurs:

* in a later statement of an open block at or inside its scope, or in that
  block's tail;
* anywhere in an enclosing loop it was declared outside of;
* in a part of its own statement that has not yet been evaluated
  (`LaterOperands`, §3.3). This includes a whole assignment's right-hand side, a
  call's later arguments, a literal's later fields, and the arguments that
  print, str, and format will render after the current one (**Format overloads**
  below). If the shrink occurs in a construct's head on the path, the scan also
  covers the code that head leads to (`AfterHead`). Such heads include an `if`
  condition, a `match` scrutinee, and a `for` sequence; an else-if has its own
  path for this purpose.

A `for` binding is always live. `MentionsName` follows calls of nested
functions by name into their bodies. The body of a function value being
checked (an `isfunval` frame's own block) counts as a whole for a variable
declared outside it, since the function running it may call it again. Only
what is checked in the variable's own frame, or in a frame lexically nested
in it, counts (`NamesFrame` over `lexframe`: a nested function's body, or a
function value's body written there). A path entry records the frame
checking it (`PathEntry::frame`), and a block or a loop is in its scope's
frame (`FrameOfScope`). The same name in any other frame (the function
running a function value's body, or a callee checked inside its first
caller's check) is another variable, whose own frame judges it.

The scopes that an exit leaves are cut where:

* the shrink is a statement of the innermost open block, of the current
  frame;
* a later statement of that block (or its tail) is a `return` or a `break`;
* no `continue` that leaves its statement (`EscapingContinue`) is in the
  shrink's statement or the statements between.

For a `return`, the cut is the function's frame from its scope base; for a
`break`, it is the loop or `block` that `FindBreakScope` finds. The loops
among the cut scopes are not scanned, nor are the blocks inside them except
the innermost one, up to the exit statement; a function value's body still
counts as a whole. The test never depends on what the optimizer proves.

**Grow-shrink arrays** (§5.2): `ShrinkGrowShrink` runs from anywhere (a
local, a reference, a global, the tail of a struct or of a variant type's
payload, whole assignment) and scans only held temporaries and the
reference and slice variables in scope (`CheckShrinkHolders`), including,
inside a function value's body, those of the function running it
(`ShrinkScanVars`). References into such an array can never be stored
(§3.5 rule 3), so checking those variables and temporaries is sufficient. A
root holds such an array where its type is one, or has one in such a tail,
at any depth (`ContainsGrowShrink`, whose element types `GrowShrinkElems`
gives). An enum's payloads are not looked into, since a resizable ADT's
payload is never bound by reference (§8.1). The variables scanned include a
reference to a slice variable, whose slice may view the array. The
variable's own binding says where, as the binding of the variable standing
for the slice a parameter's slot holds does where there is one (§3.4); any
other parameter's class, which stands for a slot of the caller's, only
bounds it.

**Slot reads** (`RootAlt::slotread`). For the same reason, these never point
into a grow-shrink array's elements:

* a plain reference or slice loaded out of a field or an element
  (`ReadBackLVal`, where `LVal::isslot` says the location is a field or an
  element, not the pointee of a reference);
* a global reference or slice variable (`RefProvOf`; rule 3 covers a
  global's own bindings);
* a `for` binder copying views out of an array (`CheckFor`).

Their read-back leaves out the storage that could own them there (§3.6),
though what still bounds them may hold such an array. A slice of such a
slice, and a reference into what it views, keep the bit. `MergeVals`, a
rebind (`CheckRefRebindRoot`) and a call's result (every root its returns
give, never a back edge's) keep it only where every value has it. Crossing
a reference drops it (`DerefLValue`, `DecayRef`, `Dot::Check`'s
auto-deref, a `for` loop or a builtin member through a reference, a whole
array meeting a slice destination through one), since what a reference read
out of a field leads to may be a whole grow-shrink array, or a variable
holding a view into one. A slice loaded through a reference to a slice
variable has what the variable's binding has (`SlotView`), and a slice that
a container holds, loaded through a reference to its field or element, is a
slot read of its own. A relative reference never has the bit, since it
points within the array that holds it.

A holder has the bit on its contents where it was read out of a field or an
element (`ContainerRead`), copied out of one by a `for` or `match` binder,
popped, or copied out of a slice by `append` (`AppendedCopies`): every
reference it holds was stored there, while the container still bounds
them. A variable bound to such a holder, a literal or a merge holding it
(`RecordStore`, `NoteLitElem`, `MergeVals`), and a call's result where
every return's contents have the bit (`RecordReturn`, `CallResult`, never
at a back edge) keep the bit, and a parameter keeps it through the key
(§3.4). A holder loaded through a reference (`DecayRef`) does not have it.
The store rule passes over a value with the bit (`GrowShrinkTaint`, and for
a holder's contents `StoredIntoGrowShrink`, §3.5 rule 3). So a variable
bound to such a value points into no grow-shrink array and may not be
rebound into one, and a holder that has the bit is stored wherever its
contents' roots outlive the destination. The §5.2 scans pass over a held
temporary that has the bit. They also pass over a variable that has it,
unless a binding its record does not show could put a view of the array
there (`SlotReadMayRetarget`). That is the case for a variable not bound
yet, for a `var` rooted at the array's depth, and, inside a loop the `var`
was declared outside of, for a `var` rooted at that depth or deeper, in
which a rebind later in the body may have left a branch's inexactly rooted
value. Neither exemption reaches what a reference to a slice leads to
(`HeldRefsMayPointInto`). The §5.1 scans never use the bit: views of a
grow-only array, byte views included, are stored like any others.

**Class reads** (`RootAlt::classread`). A reference or slice read out of
the storage a parameter's class stands for, with the class named exactly
(§3.6), is one of the views that storage holds, never a reference into it.
This covers an element of the caller's array a slice parameter views, a
field of the caller's holder a reference parameter names, and a `for`
binder copying either. Such a view points where the storage's views do,
which is what the activation stored there and what its callers stored
there. A slice of such a view, and a reference into what it views, keep the
mark. `MergeVals`, a rebind and a call's result keep it only where every
value has it (`Roots::Add`). So a value that may also be the storage's own
element is judged by depth as before. An example is a reference to one of
the storage's elements merged with a reference read out of one: that is
one alternative for the class, as inexact as the read and read out of
neither (`RootAlt::from`). A bound (`Weaken`, `Bounds`), crossing a
reference, and a back edge's result (`ClearReads`) drop the mark. A call's
result has it where the callee read the view out of its parameter's storage
and the argument names a class of the caller exactly (`RetAltVal`).

For a grow-only array, the scans judge such a view (in an operand still
held or a variable that is no `var`) and what it leads to by the
activation's stores into the storage (`ClassReadMayPointInto`:
`HolderMayPointInto` on the class). `NoteLiveViews` keeps the pair about
what the storage holds for the callers (`HeldViews`,
`LiveShrink::contents`). It does so even where the class is the shrunk
array, since the caller may have made the storage's references point into
the array's elements. At a call, such a pair keeps the mark where the
argument has it, which hands the question to the caller's callers. A
callee's view into what an argument with the mark views (`show(out, w)`,
with `w` read out of `words`) becomes a pair about what that storage holds,
of a type leading to what the view points at (`ApplyCalleeLiveShrinks`).
The §5.2 scans do not use the mark: such a view is a slot read, which they
pass over.

A store of such a view into a holder (`H { s: words[0] }`, `h.s = r.s`) is
marked on the store record (`StoreEvent::classread`, in `RecordStore`), and
so is the store of a holder copied out of that storage alone. Such a copy
is a `for` or `match` binder's copy, `hs[0]`, `pop`, `append` or `copy` of
one, or the holder a reference parameter names loaded by value, wherever
the source is the class exactly (`HolderSource`). What the holder got there
is something the storage holds. A call keeps the mark where the class the
event names maps exactly onto a class of the caller's
(`ApplyCalleeStores`). It also gives the mark to a callee's store of a
value rooted at one of the callee's classes, where the argument for that
class is a view with the mark: `put(b.vars, key(b.keys))` stores what
`b`'s storage holds, as the same store in the caller would. For a grow-only
array, the scans judge such an event by the activation's stores into the
storage (`HolderMayPointInto` on the class, as for a copy of a variable's
contents). They judge a slice's slot that what came out of there refers to
by the class's depth, as for a slot the class names itself
(`StoredSlotMayPointInto`). `NoteLiveViews` keeps the pair about what the
storage holds for the callers, of the holder's type, whose references lead
where what came out of there does (`EachHolderRoot`, `RecordedViews`).

A copy's contents carry the mark where the copy lies in that storage alone
(`MarkClassCopy`: `hs[0]`, `pop`, the holder a reference parameter names
loaded by value). So a merge of copies (`if c { hs[0] } else { hs[1] }`), a
literal holding a copy (`Q { h: hs[0] }`) and a call's result that is a
copy (`first(hs)` for `fn first(hs: H[:]) -> H { return hs[0]; }`,
`RetAltVal`) hold views of the storage too, and storing any of them is
marked as above. Each of these keeps the mark at a class only where every
value it joins there has it (`Roots::Add`, `RecordReturn`). Beside a
reference into the storage itself (`Q { h: ns[1], at: ns[0] }`), or beside
a node loaded through a view read out of the storage, what it holds there
is judged by depth as before. A variable's contents keep the mark at a
class where every store of it there put such views (`AddContents`: a copy,
a `for` or `match` binder's included, a view read out of the storage, a
value holding either, and a callee's such store as its call maps it). So a
merge or a literal holding the variable, a copy of it, and a value read out
of it (`let n = ns[1]; let nx = n.next;`, §3.6, `ContentsReadBack`) hold
views of the storage too. A store of anything else there takes the mark
away (`Roots::Add`), which a loop around it feeds back as it does a new
root (§3.7): a read earlier in the body is checked again with the store on
record.

**Inexact receivers.** Both scans run once per array the shrink may free
(`ShrinkThrough` over `ShrinkTargets`), one per alternative of the
receiver's roots. An exact alternative is the array. An inexact alternative
(a container reached through a parameter, §3.6, or a back edge's unknown
result) only bounds the array. So the array may be any array of its type
owned at the root's depth or outside it: every candidate `RootCandidates`
finds for that type at the root's depth (locals, pointees of references in
scope, parameter classes, globals, and the bounds standing for the caller's
storage a parameter's references lead to). The root itself is scanned
first. Where the root is such a bound, or its own storage cannot hold the
array (a holder it was read out of), the scans filter pointees by the
array's type rather than the root's type, and `NoteShrink` records it as
`shrinkparambounds` or `shrinkexternalbounds` with that type. Diagnostics
name the other arrays as arrays the receiver may point at.

**Calls** (`ApplyCalleeShrinks`): for a checked callee, each `shrinkparams`
entry becomes a shrink of the argument's root at the call, and of every
array that root bounds where it is inexact. For a by-value holder
parameter, the entry becomes a shrink of the array the holder's references
point into (§3.4), exactly, since only a class that is one array exactly
records such an entry. Each `shrinkexternals` entry becomes a shrink of
that variable. Each bound entry becomes a shrink of every array of its type
that the argument's root (a holder's contents' root) or the external
bounds. The parameter's pointee decides the scan, or for a holder the
array's own type: §5.1 where the array freed is grow-only (a struct's tail
included, `GrowOnlyTail`), §5.2 otherwise. For a callee still being
checked, the record read is the one its cycle's previous round made
(`RecordOf`, §3.11), and there is none in the first round: a back edge then
shrinks nothing, and the round after applies what the first recorded.

**Parameters' views** (`FnRecord::liveshrinks`). The scans see only the
activation's variables, and take a parameter's class for an array of its
own. They never take it for a global or captured array that a caller passed
a view of, nor for what another class names, though the arguments for the
two may make them one array (an inexactly rooted argument gets a class of
its own, §3.4, and a caller may pass its own classes on). So after the
scans, `NoteLiveViews` looks again at what is still used, for a view that
only the callers can tell apart from the array (`CallersJudge`). Such a
view's root is a parameter's class, or the array is one, and what a class's
storage holds counts whichever array shrinks. `NoteLiveViews` looks at:

* a reference or slice variable (`UsedAfter`, which counts the rest of the
  statement, `LaterOperands`). Inside a function value's body this includes
  a variable of the function running it (`ShrinkScanVars`), whose
  parameters' pairs are its own record's;
* an operand still held (`HeldOperands`);
* a grow-only holder, by its store record (`EachHolderRoot`; what came out
  of a class's storage alone counts as what that storage holds, **Class
  reads** above). As the scan does, it follows a stored reference to a slot
  holding references through to what that slot holds (`StoredViews`);
* what a reference to a slice reaches, or, for a grow-only array, what a
  reference to a holder, or a slice of holders or of slices, reaches
  (`HeldViews`). For a holder named exactly by a value that is no `var`,
  that is what its store record says, as for the holder itself. For a
  parameter's class named exactly, it is what the activation's own stores
  into it say and what its callers put there (a view of its own,
  `LiveShrink::contents`), as for a view read out of such storage (**Class
  reads** above). For any other except a global one (a class that a
  holder's stored reference to a slice names included), it is whatever its
  root bounds.

For a grow-shrink array a slot read is no such view, since the scans pass
over it (**Slot reads** above), though what a reference to a slice leads to
may be one. `NoteLiveShrink` keeps such a pair (a `LiveShrink`) on the
specialization when `MayAliasRoots` does not rule it out and both roots are
classes or storage outside the activation (globals, a lexical parent's
variables and classes). The pair carries the array's type where the shrunk
root only bounds it (`LiveShrink::bound`). `NoteLiveShrink` also keeps a
pair about what a class's storage holds (`LiveShrink::contents`) wherever
the array is outside the activation, since what the callers stored there
outlives that storage. A store that a loop body's later iteration brings to
the shrink is on record when the body is checked again (§3.7).

At a call, `ApplyCalleeLiveShrinks` maps each pair's classes onto the roots
of the arguments passed for them (`ClassArgRoot`). Two roots the caller can
tell apart pass. One root, or two roots the caller cannot tell apart, are
an error at the call, which names the array as the caller does. A pair
still open for the caller (one of its roots is the caller's class) is kept
on the caller's record in turn, under that parameter's name. A pair about
what a class's storage holds becomes the views that the values held by the
argument's storage lead to, as the caller sees them (`HeldViews`). Each of
these views is judged as a pair is:

* a holder or array of the caller's or of a parent's, by its store record,
  where what the callee stored into it is on record by then
  (`ApplyCalleeStores`);
* a temporary, which the callee cannot write, by what made it
  (`TempContents`); a temporary that nothing records (a slice of a call's
  result) counts as holding anything;
* a class of the caller's, or a view that a class's storage held, by the
  caller's own stores into that storage, and as such a pair in turn;
* a global, by the judgement of the globals (item 5);
* anything else, by its root, as a bound.

A callee in a recursive cycle still being checked has the pairs its cycle's
previous round recorded, which the call maps (§3.11), and none in the first
round. A class the callee shrank, mapped onto an argument whose root only
bounds it, may be any array of the shrink's kind in what that root leads to
(`BoundReach`), as well as the root's own array. Where the class stands for
a reference parameter's resizable pointee, it is a value of that type
there, which becomes the pair's bound, as `ApplyCalleeShrinks` takes it. A
pair about a parameter's own value whose argument is a slot read passes for
a grow-shrink array: no reference into one is ever stored.

**Balanced calls** (§5.2). Each summary entry carries a `ShrinkBalance`,
the worst of the shrinks `NoteShrink` recorded against it: balanced or
not. A grow-shrink `resize` is balanced where `ResizesToMark` holds, which
requires both of these:

* its length argument names a `let` of the activation whose initializer
  was exactly `X.len` (`VarDef::markof`, set by `CheckVarDecl`).
  `ownerspec` is the current real frame's specialization, so a mark that a
  nested function or a block's writer took before this activation began
  does not count;
* `SamePath` finds that `X` and the receiver name the same storage: the
  same variable, not a `var` reference, then the same fields, none of them
  read out as a reference or slice.

A writable reference can still change the `let` (§4.4), so a mark with
`refwrite` (`NoteWritableRef`, §3.14) does not count as a mark. A mark that
a balanced resize accepted is marked as relied on (`markuse`), which makes
a writable reference noted later an error. The resize's balance is recorded
by then, and a loop checked in one pass, or a nested function checked once,
can run the write before the resize's next run, which nothing judges again.
No other shrink in a body is balanced: `pop`, `pop_n`, `clear`, other
resizes, whole assignment, and a grow-only array's shrinks
(`GrowOnlyShrinkAt` records every shrink unbalanced). The body's own scans
are unchanged, a balanced resize included, and so are the pairs
`NoteLiveViews` keeps for it.

At a call to a checked callee, `ApplyCalleeShrinks` first expands every
entry into the arrays it may shrink (`ShrinkTargets`), each with its
entry's balance, and then judges them together. A grow-shrink array whose
shrinks are all balanced, and that may not be the array of any unbalanced
shrink (`MayAliasRoots`, with a bound taken as inexact and two classes of
one activation as one array), is recorded as a balanced shrink without the
§5.2 scan. It also gets no pairs for the callers, since a view rooted at a
class was taken before the call too. Every other array is scanned and
recorded unbalanced as before. The skipped scan includes what a reference
to a slice variable reaches (`HeldRefsMayPointInto`): the variable still
holds a slice taken before the call, since writing a view of the array into
it through a reference is a store (§3.5 rule 3).

A back edge applies the balances its cycle's previous round recorded for
its callee (§3.11), and none in the first round. The round after the last
one that changed a record judges every call with the settled balances.

**Format overloads** (§3.7). print, str and format check their arguments in
order (`CheckPrintable`): each one's value, then its rendering
(`CheckRenderable`). For each type encountered during rendering, the checker
specializes its user
`format` overload with that part's roots and permissions. The builder is the
format call's receiver if the text is written there, or a temporary otherwise.
`UserFormatIn` applies the overload as a call using `ApplyCalleeShrinks`,
`ApplyCalleeRebinds`, and `ApplyCalleeGrows`. It also uses `NoteWritableRef`
when a variable is taken whole by a writable reference.
Codegen evaluates each argument just before rendering it (`EmitFormatInto`,
`EmitStr`), so while argument i is checked the ones after it are later
operands (`LaterOperands`). A shrink in its evaluation (a call) or its
rendering (an overload) finds a variable they name used after it.

The rest of the argument is rendered around its overloads, and is held
while they are applied (`renderarg`, the one operand of print, str or
format that `HeldOperands` holds, with `Held::render` naming the builtin
for the error). What is held is:

* the views held for any argument (an optional reference or a slice, a
  non-fixed array's elements, a holder's references);
* for a struct, enum or array argument that no overload takes whole
  (`renderwhere`), a reference to where it lies, which may be an element of
  the array an overload clears (a plain reference argument has decayed to
  its pointee, which lies where the reference points).

The parts that `CheckRenderable` is walking in place where it meets the
overload are held too (`renderwalks`): a variable-mode ADT, whose tag
codegen reads once, and an array of a size that is not fixed, whose count
codegen reads once, each as a reference to where it lies (`Held::inplace`).
Such a reference is not the path to a resizable that a reference to a
resizable otherwise is. A shrink of either kind of the storage it lies in
may free its elements or rebuild it, as a whole assignment rebuilds every
variable-size part of the value it assigns (`ShrinkMayMove`), and a slot
may hold the path it was reached through. `NoteLiveViews` considers all of
these, so a function rendering its parameter around an overload, or after
one, keeps the pair for its callers (**Parameters' views** above;
`LiveShrink::inplace`). A callee body checked meanwhile starts with an
empty path (`CheckSpecBody`): the call site applies its summary against the
caller's statement.

An overload taking a value by reference is given the place where the value
lies, as writable as the path to it. A reference argument's pointee, with
its fields and elements, lies where the reference points and is as
writable as the reference (`DecayRef`, §3.8), and what the overload stores
there is judged against that storage. That pointee is storage whatever
roots the reference has (`Val::pointee`). An argument that is no storage
lies in the temporary that codegen renders it from (`GenLoc`), and so do
its parts. A call's result, a struct or array literal and a control
construct's value are rooted at such a temporary already. Any other such
argument has no roots (an operator's result, a scalar literal, a cast, a
variant constant, a `.len`, a literal parameter), which every rule would
take for static data outliving whatever an overload stores a reference
into, so it is rooted at a fresh temporary (`CheckPrintable`).

An overload taking a slice by reference is given the slice's slot, which
codegen passes (`EmitUserFormat`), bound as `RefSliceArgs` binds a slice
lvalue (`SlotRoots`, `NoteHeld`). Every slice rendered has a slot
(`Val::slot`):

* the variable, field or element it was read from;
* where a reference argument points (`DecayRef`), a reference a call
  returns included, which codegen holds and renders the pointee of
  (`RenderedLoc`);
* for a nested slice, the part it lies in (a field's struct, an element's
  array or slice, a reference's pointee), as writable as that part is;
* for a value that is no storage, a temporary (`CheckPrintable`): the value
  a call returns, and the value of a control construct or of a function
  value's call. That value is a copy of what its branch gives even where
  the branch names a variable (§4.1), and keeps none of that branch's slot
  (`TempCopy`); a reference parameter binds the branch itself instead
  (§3.12).

What the overload stores there rebinds the caller's variable or is recorded
in its container (`ApplyCalleeStores`), and it re-points no slot that `&`
could not write. The temporary is read-only (§9.5), and codegen passes a
temporary wherever the optimizer reduces the argument to the storage it was
copied from (`OptRendered`, **Views of copies**). The parts of a
fixed-mode ADT's payload lie in a temporary too: the copy codegen renders
them from (§3.15). `CheckRenderable` takes them out of a `TempCopy` of the
ADT, whose contents are the ADT's, so an overload given such a part by
reference can neither keep it nor write it, as nothing may refer into the
payload itself (§3.5). No fixed-mode payload holds self-relative references
(`ValidateType`), so the copy keeps every link. An overload that takes a
value holding self-relative references by value (`UserFormatIn`), which
`EmitUserFormat` would copy, is rejected.

A type that reaches itself through references renders every level below the
first with a function of its own that runs no overload (§6.9). So
`CheckRenderable` stops at a type already on its path and notes it. Once
the argument's overloads are known, `CheckPrintable` rejects it if one of
them renders a part of such a type at any depth (`OverloadedPart`), and the
error tells the program to give the type an overload of its own. Every
level is then read as the first is, and a payload's parts lead to no
overload through such a type, which leaves the payload rule above
complete.

**Growth during construction** (§1.3(4), §4.2). A value built in place at an
array's top or slot is under construction while its expression is checked,
and nothing may grow or shrink that array meanwhile. Such values are:

* a pushed or pool-allocated element that is variable-size or holds
  relative references of either form (`BuiltInPlace`: `EmitPush` and
  `EmitAlloc` build such an element in its slot, whatever expression gives
  it; any other fixed-size element is evaluated before its slot is claimed,
  §6.5);
* a call's array result being appended to a non-limited array (a `T[..]`
  result too, though `EmitAppend` builds it on a temporary, §6.5);
* an appended literal of elements that are variable-size or hold relative
  references of either form (which `EmitAppend` builds in place);
* the new contents of a whole assignment of a resizable (`CheckAssign`,
  `PointeeAssign`).

Every growth is logged (`NoteGrow` → `growlog`): `push`, `append`,
`alloc_index`/`alloc_ref`, `format`, `resize`, `to_bytes(a, out)`, whole
assignment, and what a callee grows. `ApplyCalleeGrows` applies a callee's
growths: `growparams` are mapped onto the roots their classes stand for
(§3.4), and `growexternals` cover globals and captured locals; both are
recorded per specialization by `NoteRootEvent`, as shrinks are. A callee
still being checked contributes what its text grows (`SyntacticGrows`, the
shrink scanner with the growth operations). A C function is taken to
append to every builder it is handed. Every shrink, of either kind of
array, is logged too, as a `GrowEvent` with `shrink` set
(`NoteShrinkEvent`). It is logged per target in `ShrinkThrough` for a
builtin's or a whole assignment's shrink, and in `ApplyCalleeShrinks` for
a callee's, a balanced one included, since even a resize back to a mark
drops the top onto the value.

When the constructed expression's check ends, the growths and shrinks
logged meanwhile are judged against the constructed root
(`CheckGrowsSince`, `MayAliasRoots`):

* the same root conflicts;
* two variables are distinct unless one is inexact and at or below the
  other's depth (as in the held-temporary scan);
* a parameter class is distinct from a variable of the activation named
  exactly, and may be a global or a captured local;
* two classes of one activation are settled once every call site has been
  seen (`growconflicts`, `ResolveGrowConflicts`): they are distinct only
  where both are concrete and exact (§3.4).

Whatever the roots, an event never conflicts with a construction when both
arrays are resizables of different types. Each event and construction
carries the type of its storage's single resizable (`GrowEvent::arr`,
`SoleResizableIn`: the array, or the end of a struct's tail chain), and a
callee's record keeps the types with `growparams` and `growexternals`. A
stack holds one resizable and resizables are never elements (§1.3, §3.4),
so a resizable of another type neither is nor holds the array the value is
built in. Where the type is unknown, it is null and the roots decide: a
variable ADT whose payloads hold different resizables, a limited array
(which may lie inside the value being built), or a holder's or an
external's shrink in a callee's record. The log is per activation
(`CheckSpecBody` saves and clears it), so a callee's growths and shrinks
reach the caller's constructions only through the summary.

**Uses during a whole assignment** (§4.4). The new contents are built over
the old ones, so once the right-hand side is checked, `CheckBuiltUses`
walks it for anything that may use the array:

* an identifier naming the array's variable (or the value holding it);
* a reference to storage that may be or hold the location assigned
  (`ReachesBuilt`: `CanContain` of the array type for a variable, and
  `MayHoldBuilt` of the pointee for a reference, then `MayAliasRoots` as
  for growth, with `AL_DEFER` going to `growconflicts`). `MayHoldBuilt`
  accepts the location's type, a resizable on its tail path, or, for a
  field path, storage holding its holder by value;
* for each call, and each specialization it may run (`spec`, `dispatch`,
  `fmtspecs`), what that callee's body and its callees' bodies name outside
  their own activations (`NamedOutside`, walking the checked bodies with
  `RunChildren`, so function value bodies are included): globals and
  variables of lexical parents.

A callee still being checked counts as naming every global and every
variable in `vars`. A rebind's target (`r .= …`) is not a use. Nor is a
field path from the lvalue's own variable that parts from the lvalue's path
at a field whose references, at any remove (`ReachedThroughRefs`), reach no
storage `MayHoldBuilt` accepts (`FieldsApart`): for example
`t.chars = f(t.font)`, or `s.path = find(s.start)` for a `start: Cell&`.
Where the lvalue ends in a reference (`d.r = …`, `PointeeAssign`), the
location is its pointee, which `MayHoldBuilt` takes to lie in anything that
can hold its type, and a field that can hold it by value is not a field
apart. Any other field path is a use of the variable, and of what the field
holds as well: where its references may reach the location, they point
where the variable's storage bounds, or for a local holder where its record
says (`VarDef::contents`), against the array's roots. Slices and element
references are left to the shrink scan, which treats the right-hand side as
coming after the shrink. A reference parameter's class may be a global or
captured array, so using one while building such an array is an error in
the body, as a growth of one would be.

### 3.11 Recursion

A call that reaches a specialization already `inprogress` is a back edge.
`ValidateCycle` requires the reused specialization's function to be
`recursive`, and every function from it inward on the call path to have
fully typed parameters. It commits an unknown return type to "returns
nothing", and joins the cycle (`JoinCycle`). Every frame from the cycle's
outermost member on the call path (`CycleHead`, following
`FnSpec::cyclelink`) inward is inside a call into the cycle, so it is
marked `incycle` and its cycle is linked under that member. A variable of
non-fixed class in scope in any of those frames is an error at that
frame's call, since every activation would keep it on a data stack of its
own (§7.8). A by-value non-fixed parameter is always in scope there. A
local whose own initializer calls into the cycle also holds its stack
across the call, because the local is built in place. Each frame counts
the calls into a cycle it has been inside, and once the local's type is
known, `CheckCycleInit` compares the count before and after the
initializer. The statement's unnamed non-fixed values count the same way
(§7.8).

`CheckV` marks every node whose value is of non-fixed class and is not
storage (`Node::nftemp`, `NoteTemp`). The phase-2 argument check also
marks an argument that a by-value non-fixed parameter copies. At a call
into the cycle, `CycleCallTemps` walks each joining frame's statement,
using the node path of the body state its statements are checked in
(`Frame::bodyidx`, `StateOf`). It looks for a marked node among the
operands evaluated before the call, and reports the frame's call with it.
The search (`FindTemp`) stops at the constructs whose parts run in scopes
of their own, and includes the head that an `if`, `match`, `while` or
`for` evaluated before the branch or body the call is in. A value under
construction is caught after the fact: `NoteTemp` compares the frame's
count of calls into the cycle before and after the check of an array or
struct literal of non-fixed class, and of `print` and `str`, as
`CheckCycleInit` does for an initializer.

The call's own non-fixed result is checked by its position
(`CycleCallResult`, `BuiltInPlace`). Following each joining frame's path
outward from its call, through the constructs whose value is the call's
value, the path must reach one of these:

* a `return` (or the body's tail) of the same result types, where codegen
  forwards the result to the caller's destination (for a long-distance
  return, into its channels);
* a `push` or `append`, which builds the result at the receiver's top (and
  which §1.3(4) then rejects as a growth the callee makes).

Anywhere else, the caller takes a temporary for the result before the
call. A function value's body and a default are checked in frames of their
own, on the path of the statement that takes them, so a call there is
checked the same way up to the top of the body or default. A default's
value is the argument or field that takes it. A function value's body's
value is the value of its call, whose destination `EmitFvCall` takes
before the body runs. That is known once the body's check ends: `NoteTemp`
then uses the same path walk, in the frame making the call, to check a
call of a function value whose non-fixed value was built across a call
into the cycle.

Codegen asserts the outcome. At every call emitted with a stack index,
`NoStackAcrossCycleCall` fails with an internal error if the callee is in
the caller's cycle and any of the caller's stack indices is in use, so a
gap in the checker cannot silently take a stack per activation. For the
same reason, a fixed value above `NATIVE_VALUE_LIMIT` is a native local
inside a cycle function (`LargeFixedOnStack`, §10.3) rather than a
data-stack slot.

A call that reuses a finished specialization whose cycle's outermost
member is still in progress also leads back into that cycle, and joins it
the same way. So a later call to a cycle member, or to a function that
reaches the cycle only through a member, is checked like a back edge. The
explicit-type rule applies only to back edges, where inference would cross
the cycle. A function marked this way was in the cycle from its first
statement, so the stores its body made before it was marked are inside
the cycle too. The cycle's next round checks the body again with the mark
set (**Rounds** below), and the error names the call that joined the
function to the cycle (`FnSpec::joinedat`). `ValidatePoolArgs` requires
every pool-class and pool-named parameter to be passed the same ultimate
root the entry call passed (`UltimateRoot` follows `classfrom` chains).
The cycle store rule is §3.5 rule 4; the optimizer never inlines into a
cycle member.

**Threaded parameters.** A parameter class other than a pool is threaded
(`ThreadedClass`, noted by `NoteThreadedClass`) when the call that created
it rooted all of its members exactly, as references or slices. It stays
threaded until a call into its cycle passes one of its parameters
something other than an exact root at the same ultimate root, reached
through classes that are themselves pools or threaded
(`ValidateThreadArgs`, `ThreadedChain`). While the class is threaded, it
points in every activation to where its creating call's argument did.
Both back edges and calls that reach a finished member of a cycle still
being checked are checked this way: the member's body's calls back into
the cycle were checked with what the member was first given, and they pass
on whatever the new call gives it. A class created from another class's
parameter, or given one at such a call, is threaded only while that other
class is. It is recorded among that class's heirs, which break when it
breaks, and a class created from a broken class starts out broken, since
each activation of the creating function makes the call anew with what it
was given.

A reference rooted at a threaded class may be stored inside the cycle
(`ThreadStorable`) if the class's ultimate root lies outside every cycle:
a global, or a variable of a function that is neither recursive nor in a
cycle, as for a free variable. Every storage the destination may be
(`ShrinkTargets`) must then be a variable, a pool, or a threaded class
too, since the store goes where that class points, or leads, in each
activation. Nothing requires a back edge to pass a class on, so breaking
the class matters only once a store needs it. A call that breaks the class
(`Unthread`, which names the call and the parameter in
`ThreadedClass::why`) makes every store relying on it an error at the
store (`CycleStoreError`). The stores checked before that call are
reported by the cycle's next round, which finds the class broken
(**Rounds** below). A merged value or rebound variable that may be rooted
at a threaded class relies on it like any other value: every one of its
roots must be storable.

A class is also threaded, but `bounded` (`ThreadedClass::bounded`), when
each of its members' arguments was read through one pool class or one
class threaded exactly (the argument has one alternative, inexact, rooted
there). What a bounded class points at lies in storage that the pool or
threaded class leads to, outside the cycle, which makes it storable. But
that may be different storage in each activation, so `ThreadedChain`
accepts it for no destination, and a class created from a bounded class
is bounded as well. A call into the cycle keeps a bounded class threaded
if it passes one alternative at the same ultimate root, exact or not.

A recursion whose types never repeat has no back edge: each level of the
recursion is a new specialization, checked inside the one before, until
the native stack runs out. `GetOrCreateSpec` therefore refuses to create
a specialization of a function that already has `MAXNESTEDSPECS` (16) in
progress. Since checking is depth-first, those are the ones on the
current path (§7.8, polymorphic recursion). The error names the
instantiation the call would have made, and the chain shows the ones
before it.

**Rounds** (`CheckSpecBody`). A back edge reaches a function whose
completion, returns, shrinks, growths, stores and rebinds are not recorded
yet, so a cycle is checked in rounds until what its members record
settles. The head (the outermost member on the call path that found the
cycle) runs the rounds. Each round moves every member's `FnRecord` into
`FnSpec::prev` and leaves a fresh record in its place. This snapshots only
the return roots and call effects, plus the event range used to replay
stores; the specialization's identity, parameters, lexical environment and
annotated body stay in `FnSpec`. Wherever a call applies its callee's
record while the callee is in progress, `RecordOf` reads the previous
record: `CallResult`, `ApplyCalleeShrinks`, `ApplyCalleeStores`,
`ApplyCalleeGrows`, `ApplyCalleeLiveShrinks`, `ApplyCalleeRebinds`,
`NamedOutside`.

The round marks the members `stale` and checks the head's body again on
the same clone. A stale member is checked again when a call reaches it
(`GetOrCreateSpec`), with the parameters and class roots its first round
made (`FnSpec::classroots`), so that the records and the threaded classes
name the same objects in every round. A member that the last round does
not reach stays stale, with the empty record that round started for it.
(The calls to it may lie after a call that the previous round's records
say never returns, in unreachable code that the round drops.) A call made
after the cycle is checked then makes a specialization of its own instead
of reusing that member. The variables outside the cycle's activations
start each round in the state the previous round left them in. So a `let`
declared outside the cycle that one round may assign is maybe assigned in
the next round, where assigning it is an error (§3.9), since every
activation shares it.

In the first round a back edge has no record. It may return, it applies no
other effects, and its result points nowhere yet (`Roots::unknown`). Every
rule lets such a result pass (`FitsAt`, `CheckRootedAtReceiver`,
`BindRefProvenance`, the shrink scans), and the same holds for a holder's
contents read out of one, a variable bound to one, and a call given one,
whose specialization is keyed `RootArg::unknown`. A round was checked
against settled facts, so its errors stand and it is the last round, when
it:

* changed no member's record (`SameRecord`: normal completion, return
  roots, shrinks and their balances, growths, stores through classes,
  pairs, rebinds);
* broke no threaded class; and
* noted no fact about a variable declared outside the cycle's activations
  (`CycleRound`, marked by `NoteFact`). Such a variable is one variable at
  every level of a nested function's recursion, which a round may have
  read for the next level before it rebound it.

Completion proofs accumulate, but pruning a newly unreachable continuation
can remove other effects. An internal limit of 8 rounds stops a cycle that
does not settle.

A back edge's result is the union of the roots that the cycle's returns
give, mapped through the back edge's own arguments (`RetAltVal`). A
parameter class maps through every argument the back edge gives the
class's parameters, united; an ordinary call, whose classes group the
arguments as the key's do, takes the first. The result is read-only where
any return is, and a byte view where any u8 view may be. It is no slot
read (§3.10), and neither are a holder's contents, since not every return
is checked yet. A parameter that the key gave static data (class 0) has no
class root for a record to name. So a back edge that passes it storage of
its own gets a holder result rooted at the activation itself, which may
only be passed down (`CallResult`). A reference result is mapped as
recorded, which parsers that pass a literal key at the entry call rely on
(`samples/18_json.goose`). What a return records leaves out the container
the value was read out of (`RootAlt::from`), since the next round's
activation would treat that container as its own.

### 3.12 Calls, generics, literal parameters, dispatch, function values

**Resolution** (`CheckCall`, `typecheck_calls.h`): a UFCS call tries the
member builtins first when the receiver is an array, then the functions the
name reaches (a nested function in scope, else the set of the namespace the
receiver's struct or enum type is declared in where that declares the name,
else the namespace overload set: `Ast::LookupMethods`, §11.1), then the
remaining builtins. A plain call tries functions, then builtins; a
user overload set that shares a builtin's name (`format`) takes the calls
it matches. `ResolveCall`:

* checks the arguments once bottom-up (phase 1), rewriting non-fixed
  lvalues to references;
* tries every candidate (`TryMatch`, tiers: 0 exact, 1 generic binding, 2
  coercion, 3 `INTTOFLOAT`; the unique best tier below 3 wins);
* falls back to tag dispatch, then to a builtin of the name, then to the
  unique candidate converting an integer argument to a float;
* undoes the reference rewrite for a parameter that takes the value: a
  slice, or a fixed-class type that an array of another kind constructs by
  copy (`UnrefForValueParam`);
* re-checks each argument against the resolved parameter type (phase 2,
  `CheckArg`);
* applies the callee's shrinks, growths and rebinds, and maps the result
  roots (`CallResult`).

Generic inference is structural (`BindTypes`), through the only coercion
generics see: whole array to slice. Untyped parameters bind the argument's
natural type (a reference stays a reference). Literal arguments unify
after the typed ones, so that a typed argument fixes the type variable. A
`[]` or `null`, which fixes none, unifies after all of them. Leftover
generics bind function values in order.

A control construct used as an argument has no destination type in phase
1, where its value would copy the branch taken (§3.3). The argument's
value path (`argpath`, `PathScope`, `Scope::onargpath`) is the argument
itself, including a UFCS receiver, and each branch that a construct on the
path checks next: a nested construct, a block's tail or a break's value.
On that path, `CheckBranchCopy` notes the copy on the value
(`Val::implicitcopy`) instead of reporting it. It also notes the `&x`
branches of a fixed-size `x` whose `&` the copy makes redundant
(`Val::refcopies`), and whether every branch is storage (a variable, field
or element of any size class, including a slice's slot) or a reference
(`Val::storagebranches`, merged by `MergeVals`). The value itself is not
an lvalue, and a slice's value is no exception (`TempCopy`), so nothing
that binds an lvalue by reference treats the copy as storage. It still
matches a reference parameter as an lvalue does (`UnifyArgRaw`).
`BindBranchesByRef` checks it against that parameter before the
specialization and the callee's effects are keyed on it: each branch
binds by reference (`AutoRef`), and the argument takes the branches'
merged root and writability.

A slice parameter views a construct's array branches where they lie (spec
§6.4). So `BindBranchesByRef` checks the argument against a slice
parameter in the same way where phase 1 left either an array (a copy of
the branch taken) or arrays of two types joined as a slice with no slice
among them. `CheckJoin` leaves such a join to the call on `argpath`
(§3.4): those arrays join only at a parameter declared as a slice, and
`NoArrayJoin` reports them anywhere else, including a failed resolution
and a builtin's UFCS receiver (which a member builtin takes as checked). A
`[]` that phase 1 left as the root-less placeholder (Pending arrays) is
also checked against a slice parameter this way (`ViewedWhole`), which
makes it the temporary array of the parameter's elements that the slice
views (spec §4.2). Keyed on the placeholder instead, the parameter would
point nowhere yet, which no rule reads, and the callee could keep a view
of the temporary.

`BindBranchByRef` does this per argument, including for a parameter
default (`AddParamDefaults`, before `UnifyArg` judges it). A function
value's call (`CheckFunValCall`) does the same for its block's parameters.
At any other parameter, including an untyped one (which takes the
construct's value type), phase 2 reports the copy, and `BindBranchByRef`
reports the redundant `&`s as the copy's (`RefCopyWarnings`). At a
reference parameter, phase 2 warns about the `&`s because the branch binds
by reference without them. Tag dispatch also binds such a construct at the
dispatch position by reference. For a construct it dispatches by value,
which phase 2 does not check again, tag dispatch reports the copy and the
redundant `&`s noted on it. A member builtin reports those noted on its
receiver, which it takes as checked. A function value's declared reference
parameter takes its provenance from the argument's check against it.

Phase 2 checks each argument again, and so resolves each call nested in
it again, including that call's own two phases. These also check their
arguments again, as a cycle's next round (§3.11) checks its body again: a
builtin rendering a UFCS receiver the call checked (`CheckPrintable`), tag
dispatch binding the argument it dispatches on by reference, and a builtin
that a failed resolution falls back to. Unlike a loop's earlier passes and
a construct's first check of its branches (`WarningsHeld`, §3.4, §3.7),
none of these checks is withdrawn: the warnings of each one stand, and
they are the same every time. A default and a function value's body are
cloned anew at each check of their call, as a function's body is per
specialization. But every clone of a node names that node
(`Node::origin`), and a warning prints once for the node as the source has
it, at the first check that gives the warning (`PrintWarning`).

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
A candidate converting an integer argument to a float (`UnifyArgRaw`'s
`MatchInfo::INTTOFLOAT`) does not count as an ordinary candidate here. It
is taken only after dispatch and after a builtin sharing the name, and a
dispatch case counts it only where the variant has no other match. That
way, every call that resolved before integers converted to floats still
resolves as it did. Literal arguments unify in two groups: first the float
ones (`LitFloat`: constants, literal parameters and `Val::litfloat`
values), then the integer ones, which convert to the float type the first
group bound. A `[]` or `null` (`Val::emptyarr`, `Val::isnull`, including a
construct of either) unifies after both groups: it binds no type variable
and only substitutes the ones the others bound (`SubstOwn`), so it matches
wherever it stands among them. A generic
candidate it matches ranks as a coercion, as a concrete one does: `f([], n)`
beside `fn f(xs: i64[], x: i64)` and `fn f<T>(xs: T[], x: T)` is ambiguous,
like `f(n, [])` for the same overloads with the parameters swapped.

**Multiple results.** Results are not tuples. An ordinary value use of a
call takes its first result and discards the others; a call statement
discards all of them, while still executing the call. `let a, b = f()`
requires exactly that many results and has no shared type annotation.
`return f()` forwards all results of a multi-result call and adapts each
one to the corresponding declared return type. Reference decay and lifetime
checks apply separately to each received result. Inference fixes a
function's result types from the first checked return; subsequent returns
must fit them, rather than widening the result by a whole-body join. A
result inferred as a reference would be inferred from the first return
checked, and a back edge is reached before that return (§7.8). So a
function in a cycle may not infer a reference result
(`NoInferredRefResult`). `JoinCycle` checks this for each function joining
a cycle, and `RecordReturn` for a function already in a cycle when it
infers its result.

**Floating generic context** (§7.7): `Call::Check` passes the expected type
through named, UFCS and named-function-value calls to `ResolveCall`.
After ordinary bottom-up resolution, `ContextualFloatCall` recognizes a
single declared generic return `T` inferred as `f64` from adaptable numeric
value arguments. A trial match with typed `f32` arguments must select the
same function with only that binding changed. Explicit arguments for `T`,
already typed numeric values, compound parameters mentioning `T`, and defaults for
those parameters are boundaries. The checker records eligible call nodes
in `contextualfloats` even without an expected type, so a containing call
can recognize eligible nested computations. This does not mark their
values as literal-like: ordinary overload selection and inferred bindings
still see `f64`. At an `f32` destination, the trial's parameter types and
bindings replace the chosen match; phase 2 rechecks the arguments at those
types, recursively refining nested calls. These are ordinary typed `f32`
specializations, with no literal parameters for the refined type variable.
No runtime cast of an already computed `f64` result implements the change.

**Literal parameters** (§7.7): a literal argument (or a literal parameter
passed on) to an untyped parameter, or to a bare type variable that no
typed argument binds, makes the parameter a literal parameter. A literal
parameter is part of the key (`litparams`), and is read inside the body as
a `Val` with `unsized` set and no constant value. Every place the value
adapts to a type (`FitsAt`, `UnifyNumeric`, branch merging) records a
`LitAdapt` on the owning specialization, and passing it on as a literal
records a `LitFlow`. After the whole program is checked, `VerifyLiterals`
checks each call site's literal against the closure of those records.
Codegen passes the parameter at its nominal type and casts at each use.

**Parameter defaults** (§7.1): `TryMatch` binds the written arguments to
the leading parameters (`MatchInfo::nwritten`). Function values for the
leftover generics may follow as few as the parameters without a default.
Each parameter left out has the type that the arguments and explicit type
arguments give it, so a default decides no type variable and plays no part
in ranking. `AddParamDefaults` clones each missing default into
`Call::args` where its argument would be (`firstdefault`, `ndefaults`). It
checks the default as a written argument is checked: in the discovery
phase for the specialization's key, where `UnifyArg` must accept it at its
parameter's type, and against its parameter in phase 2. But it checks the
default in a frame of its own (`InParamDefault`, the `DefaultScope` a
field default gets):

* its type bindings are the function's own, and those of the enclosing
  functions that no inner type parameter hides, with no function values
  (`ParamDefaultEnv`);
* its lookups hide every local of the calling code;
* its effects and the values live around it are the caller's, as a
  written argument's are.

`DefaultScopeName` rejects a name that the declaration's own scope would
make a parameter or a type parameter bound to a function value. For a
nested function, it also rejects a name that would be a variable or
function of its `DeclSite`, or such a type parameter of a function around
it. A default that leads to a call taking that default again before a
top-level function's body is an error rather than an endless check
(`EachDefaultInPlace`, §3.2). `CheckCall` erases the inserted defaults
before it checks a call again, so every check (an argument's two phases, a
loop's passes, a cycle's rounds) resolves the call as written.
`Call::Clone1` drops them, and a diagnostic prints the call without them
(`dumpwritten`). Tag dispatch and rendering hooks use no defaults: they
give every argument (`TryMatch`'s `defaults` off). Later passes see
ordinary arguments.

**Tag dispatch** (`TryDispatch`, §8.2): for each argument position holding
an enum (or a reference to one), every variant type is tried against the
overload set, and exactly one candidate must match per variant. Only one
such position is allowed. Each arm is specialized, and the arms' return
counts and types and their non-dispatch parameter types must agree. A
fixed-mode scrutinee dispatches by value even through a reference, and
each case takes the payload as a match binder would (§8.1): a case taking
a fixed-mode payload by reference, or a payload holding self-relative
references by value, is an error. The result's provenance is the merge of
the arms' provenance (the deeper root, exact only when the roots are the
same, writable only if all arms are). The cases get every argument
written: a call that would dispatch only by leaving parameters to their
defaults is an error. Once the cases are chosen, a dispatched call goes
through the same steps as a resolved call:

* each case's shrinks and growths are applied with the argument nodes
  (`ApplyCalleeShrinks`, which takes them at every call);
* its casts are judged (`JudgeCallCasts`);
* the arguments are checked again in order (`RecheckArgs`, which
  snapshots the dispatched value in its place);
* the cases' checkfree rule, rebinds and completion are applied together
  (`CalleesReturn`).

**Nested functions** (`DeclareLocalFn`, §7.5): checking a declaration
records a `DeclSite` for the function, under the environment declaring it
(`declsiteof`). The site lists:

* the variables in scope there, innermost first, with the index each holds
  in `vars`, except those the function's own type parameters hide
  (§11.1);
* the functions it may call: every function declared in the blocks around
  the declaration (`blockpos`), followed by those the declaring body sees
  outside its own scopes, each with the `Scope::serial` of the block
  declaring it (`OuterFn`). The blocks come innermost first. Within one
  block, the functions declared at or before the declaration come first,
  latest first, followed by those after it.

A name's nested functions are the overload set of one scope
(`LookupLocalFns`). In the enclosing code, they are the ones declared so
far in the innermost of its scopes that declares the name, latest first.
In a body, they are the ones of the first scope serial its lookup meets,
in the site's order. Of those with equal parameter types, the first one
met is kept (a later declaration shadows), and the set is then put back in
declaration order. A set bound as a function value is kept once in
`localfnsets`, so equal sets are one `FnValBind::set`, and `TryDispatch`
passes the set's environment to each case's specialization as
`ResolveCall` does.

The frame that `CheckSpecBody` pushes for a specialization keeps the site
as `decl`. Past the body's own scopes, `LookupVar` and `LookupLocalFns`
look in the site (`ForOuterVars`, `ForOuterFns`) rather than in the
declaring frame as the call finds it. So a scope around the call that
shadows or adds a name changes nothing. A specialization per `lexparent`
serves every call that finds the variables the body names as its check
found them (`envreads`, §3.1). A call gets a specialization of its own
where one of those variables:

* points, or holds references, elsewhere;
* is assigned where it was not, or the other way round; or
* is a `let` that may be assigned where it could not be, or the other way
  round.

`GetOrCreateSpec` rejects a call that reaches a nested function before its
declaration is checked (a nested function declared earlier calling it),
since the variables its site lists do not exist yet. A global
initializer's frame has no environment, so a function declared directly in
one has no `lexparent`, as a top-level function has none.
`LexFrame(env, sf)` tells the two apart by `SFunction::isnested`, and
gives such a function the initializer's frame, 0, as `LexFrame(fb)` does
for a block written there. The function's body's lookups continue in that
frame, through its site, and the check above, `narrowedenv` and the depth
keys (`ExternalOptionals`, `EnvReach`) cover that frame's variables as
they do a function's.

Rechecking a declaration, loop or match binding resets its checking state
while preserving its `VarDef` identity, capture flag and the marks of
§3.14 (`ResetLocal`), so cached specializations still name the binding
codegen declares. A loop's next pass, or a cycle's next round, finds such
a variable as its own, as the first pass did, so a specialization checked
then serves it. A call that reuses such a specialization sets the variables it
read to the state its check left them in (`envexits`, `ReplayEnvExits`;
assigned where its exits agree, §3.9), as the check itself did the first
time. It also notes a changed fact for the loops and rounds around it
(`NoteFact`). The same holds where the flow of another branch dropped an
assignment the check made. Before that, the call takes the exits of the
bodies outside it that its check took (`ReplayOuterExits`).

`VisibleVars`, which the shrink rules and the read-back candidates
enumerate (§3.6, §3.10), keeps the lexical parents' frames as the call
finds them. A reference handed to the body, or the result of a later
nested function or of a function value written at the call, can point into
a variable the declaration does not see. Inside a function value's body,
the §5.2 scan and `NoteLiveViews` also include the frame of the function
running the body and that frame's lexical parents (`ShrinkScanVars`),
since the body runs in the middle of one of that function's statements.

**Function values** (`CheckFunValCall`, §7.6) are restricted by `CheckV` to
names and block literals; runtime expressions producing them are rejected
rather than having their effects discarded. Returning a function value or
constructing an array of them is also rejected. A named function value
resolves as a call does, in the environment its declaration is in (for a
nested function, the scope declaring it), whichever function names it
(`LookupLocalFns`, as for a call). A name with overloads binds the whole
set (`FnValBind::set`, part of the value's identity and so of the
specialization key), and each call of the value resolves against that set
(`CheckFunValCall` hands it to `ResolveCall`). A block is cloned into
`Call::fvbody` and checked inline, in a frame marked `isfunval` whose
lexical lookups chain to the definer. Its parameters are locals bound to
the arguments, with reference provenance and literal-ness carried through.
The body's value is checked without a destination type, so a `[]` there
has no element type (Pending arrays, §3.2). Its receiving storage is
carried separately (`callresultdsts`), and supplied while checking its
tail.

To that check, the body is a lexical environment of its own, the frame's
`lexspec`: an `FnSpec` marked `isfunval` (`NewFunValEnv`, outside
`ast.fnspecs`), whose `lexparent` is where the value was written. A nested
function declared in the body has this environment as its `lexparent`. A
function value written in the body, or a nested function of the body
named as a function value, carries it. So their lookups chain through the
body's frame (`FrameOfSpec`, `LexFrame`) to the body's parameters and
locals (§7.5). Each check clones the body afresh and makes a new
environment with it, so a specialization keyed on one environment captures
that clone's variables. Another call site of the value, or the same call
checked again as an argument, therefore specializes anew instead of
reusing a specialization whose captured variables codegen never declares.
`NamedSpec` gives the named function around such an environment, which is
what a plain `return` inside it targets (`CheckReturn`). This is how
HOF-based iteration returns. A call path (the frames, or a path that a
reuse recorded, `neededges`) names the frame checking such a body by the
specialization calling the body. Where that specialization is the target
of a `return ... from` below the body, the target needs nothing of itself
(`AddNeed`, which walks a path as `ValidateNeeds` does, `NeedOnPath`).

**`return ... from`** (`CheckReturn`, §7.9): the target resolves in the
returning function's definition context (a nested function in scope, else
the overload set) and is the innermost frame whose function is in that
set. Every specialization in between records the target in `needs`, and
every path by which a specialization was later reused (`neededges`) is
re-validated against it (`ValidateNeeds`, `AddNeed`). A long-distance
return from a named function may carry only references rooted at globals
or static data. This is more conservative than the spec's "rooted at or
above the target's frame" (TODO 0d).

A return from a function value's body, plain or `from`, gives the target
what returning out of each call it leaves would give, in turn
(`ReturnThroughCalls`). A value rooted at such a call's parameter classes
points where that call's arguments did, mapped as the call's own result
would be (`RetAltVal`, through the arguments the frame keeps,
`Frame::argvals`). So a block may return the element that the function
calling it handed it (`json::each(n) { c => return c; }`). A value rooted
in a variable of such a call, including the body's own locals, is an
error. What is left is rooted where the target names it, which its calls
map in turn. The target's record then holds what that one call gave it, so
no other call reuses the check of a call that such a return left
(`FnSpec::storesout`, as for what `ApplyCalleeStores` maps in place). A
later pass of a loop may hand it other arguments, and a later round of a
cycle the target is in records its returns anew. The returns of one
function may give different roots: `RecordReturn` keeps every distinct
root (`RetRoot::alts`), with the writability ANDed, and every call maps
and unites them (`CallResult`, §3.4).

### 3.13 Relative references and pools

`ResolvePools` binds every `T&<w in pool>` type to its global before any type
is compared; the pool is part of the type's identity (`TypeEq`). `ValidatePool`
requires a `var` grow-only global whose elements can contain the pointee.
`PoolOf(root)` answers which global pool a root is: the global itself, or a
parameter class's `classpool`, agreed by every call site.

The relative store rule in `FitsAt` (§3.9): a null stores into any
optional relative slot. A null is a value with no roots (the literal, a
null-only variable, a parameter given null, a call returning only null).
It is also an alternative rooted exactly at static data where that
alternative can only be null: the value is writable, or
`StaticCanContain` says no literal holds its pointee (a read-only `u8`
reference may point into a string literal). Such alternatives drop out of
a merged value. For the rest, the destination must be exact (or name a
pool), the value must be exactly rooted, and the two roots must coincide:
the destination's root for the self-relative form, and the named pool for
`in pool`. The diagnostic names the read-back candidates when the value
is inexact.

Named returns use a symbolic `Dest::result` and result index, separately
from the empty destinations of argument slots. `RelativeDestination`
records a required relationship in `FnRecord::relativestores` when a
relative initializer depends on that result location, or a block's
receiving parameter class must agree with captured storage. Each call
maps both ends through `RetAltVal` and validates them against its own
receivers (`CheckCalleeRelative`); forwarding records the requirement in
the caller's summary. This applies to reused specializations and to every
round of a recursive cycle (`SameRecord`). It does not equate parameter
classes with the concrete roots of whichever call checked the body first.
For a nonlocal return, intervening classes map through their active calls
to the target, with the same no-reuse rule as nonlocal reference returns.
Operands and statements have no receiving destination; valued breaks keep
their construct's destination in `Scope::breakdest`.

Result requirements also reject resize fills and representation adapters
that would copy the value. `ResultLayoutFits` admits an identical type
(ignoring top-level constness) or a variant constructing its ADT payload.
Self-contained `self` and null links need no receiving-root requirement,
so their existing representation adaptations remain available.

`index_of` needs the same exactness at the receiver. `self`
(`CheckSelfInit`) can only be the whole initializer of a non-optional
relative field whose pointee is the literal's own type. The `in pool` form
also needs the literal to be under construction inside that pool. Nothing
in a `resize` fill value is, since the fill value is built once and copied
into every slot the resize adds (`Dest::fill`). `NoRelRefCopy` rejects
copying any value holding self-relative references (`HasRelRefT` excludes
`in pool` fields, which copy fine), except values that are not copies
(`BuiltWhereReceived`):

* a literal built in place;
* a default (`B_DEFAULT`, including a `..` field's default: nulls, an
  empty limited array, `self` where a field's default says so);
* the by-value result of a call of a Goose function;
* a block's fresh result;
* the body's named result.

`BuiltWhereReceived` follows value-producing branches and breaks;
`CheckFunValCall` checks the tail's copy restrictions before exposing its
value as fresh. A call is judged by the callee's declared result
(`Call::rettypes`), not by the value meeting the destination: a reference
result decays to a copy of its pointee there.

The named result is `return x`, or a tail `x`, where:

* x is a local declared alone at the top of the body (`AllowNamedResult`,
  for the value `retvalnode` says the return checks);
* the result is not fixed-size and has x's layout, or is the variable
  array that x's resizable elements make;
* x's type is not a resizable struct whose fields before the tail hold a
  self-relative reference, or a value that a self-relative reference in
  the tail may point at (`NamedResultKeepsLinks`), since a frame object's
  head reaches the caller as a copy (C.3).

A result type still to be inferred is taken to be x's type. Only the whole
body can tell whether every return gives x. So `CheckNamedResultUses`, at
the end of `CheckSpecBody`, runs codegen's structural test
(`NamedResultOf`, as `CodeGen::NamedResult`; a return it cannot place
counts against it), and if the test fails it reports each return of x. A
body relying on the named result is marked (`FnSpec::relnamedresult`) and
never inlined (`Optimizer::Scan`), so its result is built by
`DetectNrvo`'s named result at every call. Codegen fails loudly if a
return or an inlined body's exit ever copies such a local instead
(`NoSelfRelCopy`).

`NoRelRefCopy` runs:

* wherever a value meets a typed destination (`CheckValue`);
* at an un-annotated declaration;
* at each decayed reference result of a multi-value binding or of a
  forwarded multi-value return (a value result, including `from_bytes`'
  array, is built at the receiver);
* at a return inferring the result type (`CheckInferredResult`);
* at `copy(x)`.

A rejected un-annotated declaration of storage or of a reference result
suggests the `.=` binding it most likely meant. The same rule rejects
by-value `for` and `match` bindings of such elements and payloads, the
elements that `append` copies from anything but a literal (including an
element that is itself a self-relative reference), and a `resize` fill
value, literal or not, which is built once and copied into every slot
added. Varint-width relative references are construction-only.

`NoRelRefTempRead` closes the other way out of a root. A value that is not
storage and is materialized in a temporary (`TempContents`: a popped
element, or a control construct's value, which copies its branch's value)
may be a copy. Unless such a value is `BuiltWhereReceived`, a field or
element holding self-relative references may not be read out of it
(`Dot::Check`, `CheckLValue`). Nor may the value be sliced (except as the
view `==` compares), iterated, bound by a `match` reference binder or
rendered by `print`/`str`/`format` (`CheckPrintable`). The check is made
where a path leaves the value itself, so a scalar field of a popped
element reads as usual.

A value is never relative (`FitsAt` refuses a relative value anywhere but
an identical slot), so neither is a result. `CheckSpecBody` rejects a
result type written as a relative reference. A result type that a type
argument makes relative gets its loaded plain reference instead
(`ValueType`): the callee's returns are checked against that type, and the
call yields it. `default<T>()` of an optional relative reference is typed
the same way, as the plain null, so it exists at the varint width too. A
variable or parameter is a relative slot only where its declared type is
written as one (an alias use is a copy of the aliased type, so it counts).
Where the written type is anything else, `DeclType` gives a local's
annotation (`CheckVarDecl`) and a parameter's type (`UnifyArgRaw`, and
`TryMatch` for a left-out one) the same `ValueType`. So `let t: T` and
`v: T` over an array of links hold the plain reference, bound and checked
as one, and a store of it back into an element meets the element's
relative slot in `FitsAt` like any other plain reference. A field or
element of type `T` stays relative.

**Index and bound types** (spec §3.11) name a global the same way. A
`TY_INT` carries a `TypeIndex` detail outside the union (`TypeExpr::ix`),
which copies of the type share. `ResolveIndexTypes` binds it to its global
before any comparison and collects those globals in `indexarrays`;
`ValidateIndexType` then requires a grow-only `var` with fixed-size
elements. `TypeEq` and `SameNum` compare the array and the kind
(`SameIndex`), so the type is fail-closed wherever a type is compared. The
places where it could leak are those that pick an integer result by its
storage:
* `Operand` strips it (`Ast::NoIndex`) from the value an operator computes
  with, while the operand's `exprtype` keeps it for the bounds-check pass, so
  no arithmetic result has one;
* `FitsAt` takes only a value of the same array, at most as wide (an index
  where a bound is wanted), never a constant, a flexible literal or a literal
  parameter, and `Converts` agrees for the cast judgements;
* `UnifyBranch` joins two of one array at the wider width (a bound if either
  is one) and anything else as plain integers, and `MergeVals` adapts a
  constant branch to the plain type;
* `CompoundAssign` and `CheckIncDec` reject an index-typed target
  (`NoIndexUpdate`). `ElementwiseOK` rejects a struct or fixed array with
  an index-typed leaf, whose elementwise result would keep the type, and
  `HasDefault` gives an index type no default;
* `AsCast::Check` makes a conversion into one never redundant and rejects
  `as!`, and `InitOrderSafe` lets no named initializer cross one, whatever
  the widths; `CheckFor` gives a binder the index type when both range
  bounds, or the count, are index or bound types of one array;
* `GrowOnlyShrinkAt` rejects any shrink of a named array, which covers whole
  assignments and callees' shrinks applied at the call;
* `HoldsIndexT` keeps the type out of queues, thread arguments and C
  signatures (`ExternValueOk`), and `IsU8` is false for one, so no text
  sink writes into an array of them.

Codegen erases the type. The C type is the storage's type. `Mangle`
numbers the array into the name, so that per-type artifacts such as a
`from_bytes` verifier stay apart, and the verifier checks each value
against the array's current length. A conversion wraps its operand in
`IndexTypeCheck` (`GS_IDX`, or `<= len` for a bound) unless the
bounds-check pass set `AsCast::nobc`. The optimizer folds no such
conversion, and `DivisorKey` and `Speculatable` treat one as the check it
is.

`checkfree` (spec §7.14) is a parse-time flag on `SFunction`, inherited by
nested functions. The typechecker's half is the call rule
(`CheckfreeCallee`), applied at a direct call, to each case of a dispatch
and each `format` overload a print reaches, and, at a block call, to the
function the block was written in. All of these are judged against the
function whose text the call is in (`TextFn`: a block's frame names its
writer). The bounds-check pass does the rest (§5.11).

### 3.14 Arithmetic, constants, and the rest

`UnifyNumeric` (§6.1) unifies operands:

* equal types stand;
* a constant adapts to the other operand's type (fit checked);
* a literal parameter adapts to a typed operand;
* one implicit widening (`ImplicitInt`: wider same signedness, or unsigned
  into strictly wider signed) wins;
* the `u64`-against-signed comparison is admitted only when the signed
  side's `nonneg` bit is set. This is a syntactic bit from a non-negative
  literal, a `.len`/`.cap`, or a `let` bound to one (`CheckVarDecl` copies
  it to the `VarDef` of a `let`, and the `let` its initializer read, if
  any, as `nonnegfrom`).

Since a writable reference can still change a `let` (§4.4), the
comparison marks each `let` along the `nonnegfrom` chain as relied on
(`RelyOnNonneg`, `nonneguse`). `CheckRefOf` and `AutoRef` mark a variable
bound to a writable reference (`NoteWritableRef`, `refwrite`; not for an
identity comparison, `index_of` or a `const T&` destination), as
`UserFormatIn` marks a variable that print, str or format hands whole to a
`format` overload taking it by a writable reference (§3.7). Whichever mark
comes second is an error. The marks do not follow the flow, and
`ResetLocal` keeps them, as it keeps a balanced resize's `markuse`
(§3.10), so neither a loop's later pass, a cycle's later round nor a
nested function checked once gets past them.

An integer meeting a float takes the float's type, and an `f64` that takes
its type from float literals (`LitFloat`) adapts to an `f32` operand. The
checker folds constants at the operands' type (`FoldInt`, over the shared
`FoldIntOp` of `ast.h`). Unsigned arithmetic wraps, a shift also wraps at
its width, and a signed result that leaves the type is left unfolded, for
the runtime to abort on or wrap. A constant zero divisor is an error here
rather than an abort. `ConstIntValue` evaluates the constant expressions
of array sizes, fill counts and match arms through `let` globals and
arithmetic. When it is given the use it evaluates for (`ConstUse`:
`ConstIntOrError` and a match's `PatternValue` pass one, a `for` range's
probe for a literal end does not), it marks every global it takes at its
initializer's value as relied on, including through other globals'
initializers (`RelyOnConstant`, `constuse`). `NoteWritableRef` checks that
mark as it does `nonneguse`: whichever of the two marks comes second is an
error (§11.1).

A named constant (§3.1: an untyped `let` or `const` global whose
initializer's value is a constant; `CheckVarDecl` sets `VarDef::constlit`
and keeps the value) reads as that constant (`Ident::Check`). The read
carries the global as `Val::constfrom` through folds and `-`/`~`, and
through the initializer of a named constant that names another
(`VarDef::constfrom`). Every place the value adapts to another type
(`MustFit`, `UnifyNumeric`, `MergeVals`, a literal argument,
`NoteLitArgs`) marks that chain as relied on (`RelyOnNamed` →
`RelyOnConstant`). So does a fold of two named constants, for the constant
whose chain the fold drops. `FoldInt` leaves a zero divisor from a named
constant to run time. An adapted read is emitted at its use's type
(`Ident::CgX`), as a literal parameter's is, where the optimizer did not
already propagate the value. An array size is evaluated once per type and
cached (`ArraySize`), which does not affect the marks, since they do not
follow the flow. `ConstIntValue` takes a name to be the global of that
name, since a size is evaluated wherever its type is compared (for
example, `TypeEq` of a callee's parameter type while a caller is checked).

`ConstName` checks a size, fill count or pattern in its own scope, where
it is written, and makes a local, type parameter or nested function of
the name an error (§3.3). It does so at:

* a declaration's annotation (`CheckVarDecl`);
* a call's type arguments and trailing block's parameters (`CheckCall`);
* a literal's type (`StructLit::Check`);
* a fill count (`FillCount`);
* a nested function's declaration (`DeclareLocalFn`);
* a pattern (`PatternValue`).

`SignatureNames` makes sure the sizes in every signature name none of its
own parameters or type parameters, and `TypeParamSizes` makes sure those
in a generic type's fields name none of its type parameters. A type that
the checker writes into a default it makes (`DefaultCall`,
`DefaultValue`: `implicit` calls and literals), and an alias's use
(`TypeExpr::aliasuse`), were written and checked elsewhere.

Integers become floats (§6.3) in a node of their own. `ToFloat` wraps the
integer expression in an implicit `AsCast` wherever a destination
(`CheckValue`, after `FitsAt`) or an operator (`RetypeOperands`) converts
it. The conversion then survives whatever the optimizer makes of the
integer below it (an inlined call, or a block reduced to its tail, which
would otherwise hand a float operator an integer operand). A later check
of the node sees the integer again (`AsCast::Check` of an implicit cast
checks its child) and retargets the cast rather than wrapping it twice;
diagnostics print the expression without it (`Written`).

A float computed only from float literals and integers carries
`Val::litfloat` (`Binary::Check`, unary minus, and a block's tail through
`TempCopy`) and the node flag of the same name. Such a float is typed at
the literals' `f64`, and `RetypeFlex` retypes it wherever it meets an
`f32` destination or operand: each flagged node, the implicit casts of its
integers, and its literals take the new type, while a constant part keeps
its nodes, which fold at full precision and round once. An untyped `let`,
local or global, bound to such a float (or a float constant) is marked
`VarDef::constlit`, as an integer named constant is (`CheckVarDecl`).
`Ident::Check` reads it as `Val::litfloat` (with `constflt`'s value as a
constant) and `Val::constfrom`, which binary and unary float operations
and float branch joins carry on. Where it meets an `f32` (`MustFit`,
`UnifyNumeric`, `MergeVals`), `RelyOnNamed` marks it, and `RetypeFlex`
gives the identifier the `f32` type, at which `Ident::CgX` reads the `f64`
variable rounded. `ResetLocal` keeps `constuse` across a loop's passes, so
a writable reference after a relying use is an error in each pass.

An integer computed from constants, with a shift by a non-constant count
among its operations (§6.1: `1 << k`, `(1 << k) - 1`, `~(1 << k)`),
carries `Val::flexint`, the range of those constants in `litlo`/`lithi`,
and the node flag of the same name (`NumericBinary`, `NumericUnary`). Such
an integer is typed `i64`, and adapts as a constant does where the range
fits (`FitsAt`, `UnifyNumeric`). `RetypeFlexInt` retypes it there
(`MustFit`, `RetypeOperands`, a block's tail in `RetypeBranch`): each
flagged node, a shift's left operand but not its count, and the constants
below take the new type. `~c` of a constant `c >= 0` is also such an
integer, with `Val::notconst` saying it is exactly i64's `~c`. Unary `-`
takes it as that constant, and an untyped `let` global of it is a named
constant with `VarDef::constnot`, read back as such a value. Its value at
a narrower use is `~c` wrapped, which is what `Ident::CgX`'s cast and the
optimizer's propagated copy (`CloneLit`, `WrapStorage`) give.
`ConstIntValue` also reports `~c` of a literal or of a named constant (an
annotated global is not one) with `Val::notconst`, with i64's `~c` as its
value for the arithmetic around it. A match pattern is `~c` wrapped to the
scrutinee's width where `c` fits it (`PatternValue`), and an annotated
global's initializer is `~c` wrapped to the global's type. A `const for`
entry stays the `~c` it is (`ExpansionConstant`), which adapts where the
expansion uses it.

A construct's numeric branches join in `MergeVals` (§6.4). Branches that
are all integer constants give a `Val::litint`, the range of the
constants, which adapts wherever a constant would (`FitsAt`,
`UnifyNumeric`) to a type the whole range fits. Floats of literals, with
integers among them, give a `litfloat`. A literal branch beside a typed
one takes the typed branch's type. A break passes its value's literal form
on (`CheckBreak`), with the named constants it was read from
(`Val::constfrom`). A break with a literal value leaves the later breaks
unconstrained, as a slice join does. The `block` or `loop` records its
valued breaks (`EarlyBlock::breaks`, `LoopExpr::breaks`). The branches'
nodes take the joined type once the construct's first check settles it
(`CheckJoin` → `RetypeBranches`): an integer branch in a float construct
converts in a node of its own, a constant takes the type, and a float of
literals is retyped as above. A literal-like construct that meets a type
later is retyped the same way (`RetypeFlex`, `RetypeOperands`).

A `for` binder's written type (§6.5, `ForLoop::vartype`) makes a range's
bounds values of that type (`CheckValue`), and a count a value fitted to
it (`MustFit`). The range's or the count's `exprtype` is the type the loop
counts at. That is the binder's type, except for a literal end one past
the type's largest value (`pastend`). Such a loop counts at i64, whose
counter cannot overflow at the end, and binds the binder to a copy of the
counter each iteration (`ForLoop::CgStmt`). `pastend` does not evaluate an
end that names anything (`ConstExprNames`), since a name is not a literal.
`ConstIntValue` would fold the global of each name, which a local, a type
parameter or a nested function of the name may hide. It would also fail on
a signed overflow or a division by zero that the end's own check, like any
expression's, leaves to run time (§6.2). A typed index binder (`idxtype`)
must hold every index the sequence can have, and is likewise a copy of the
i64 index.

**Redundant casts** (§6.3) are judged as the checker goes, check by check.
`AsCast::Check` follows a cast (`NoteCast`) where its operand converts to
its type implicitly (`Converts`: `FitsAt`'s rules for numbers, recording
nothing), outside generic code. Generic code (`InGenericCode`) is a
function with type or untyped parameters, a function value's body, and
what is declared in either or is a default of one. For a followed cast,
`castalts` maps the node to a `CastAlt`: the value the node would have
without the cast. Each consumer of a followed value judges it:

* A typed destination (`CheckValue`, a pushed element: `JudgeCastAt`)
  wants the value to convert to its type as the cast's value does
  (`SameReach`). A float of literals, or a construct of constants or of
  branches the destination types, must be computed at the type it is now,
  and an integer rounding to the cast's float type passes only where that
  is the destination's type.
* A destination with no type (an inferred `let`, a rendered argument)
  wants the same value, so only an identity cast passes.
* An operator (`JudgeBinaryCasts`, `JudgeUnaryCast`) computes its value
  without the cast with `NumericBinary` or `NumericUnary`. These are the
  arithmetic of `Binary::Check` and `Unary::Check` factored out, with a
  trial mode that changes no node and reports no error (`unifytrial` in
  `UnifyNumeric`).
* A call (`JudgeCallCasts`) resolves again without the cast
  (`MatchCandidates`). It must find the same function, parameter types,
  bindings and literal parameters (`SameMatch`), with nothing ranking
  alike, where the function ranked best or was the one match converting an
  integer to a float. For a dispatched call (§8.2) it must also find no
  overload taking the value, and the same case for every variant. Then the
  argument meets its parameter as a typed destination. A builtin that a
  call falls back to for lack of a matching function judges nothing
  (`builtinfallback`).
* A cast around the node judges it too (`NoteCast`); that verdict holds
  only while the outer cast stays.

Where an operator's value without the cast differs from its own value
only in what its consumer can still tell, the operator's node is followed
in turn (`FollowCast`). That is a float of literals yet to settle
(`CastAlt::settle`, which must be the type the value now computes at), or
a constant. A value that decides a type before it meets one (a pending
array's first element, or a generic struct literal's field) is judged as
having no destination type. `casttyped` marks the nodes whose value
changes when a followed cast is deleted. Where both of an operator's
operands are such nodes, each deletion is judged together with the other
as well: both casts may go where the value survives all three ways,
otherwise the right operand's may. Two arguments of a call whose overloads
or generics resolve them together are judged the same way (more than two
are not judged). A verdict is held as a warning is (`WarningsHeld`). Once
the whole program is checked, `ReportRedundantCasts` warns about each cast
as written (`Node::origin`) that every check found redundant, and that was
not judged at a cast which gets a warning itself.

Builtins are defined by one X-macro table (`builtins.h`), which drives
arity, receiver kinds, provenance requirements, simple signatures, and the
arrays a builtin grows and shrinks. A growth of the receiver, or of the
`out` argument that `to_bytes` and `thread_wait` take (`BF_OUTARG`), is
logged under the table's name for it (`grows`, `NoteGrow`). A
`BF_SHRINKS` receiver gets the shrink checks, and an `e` or `a` argument
completes a pending `var x = []` receiver.
`CheckBuiltin` handles the custom ones. `print`/`str`/`format` check
renderability per type (`CheckRenderable`) and look up a user `format`
overload in the type's namespace, then globally, specializing it with each
rendered argument's permissions and provenance (`fmtspecs`, `fmtcontexts`).
Nested hooks obey the same constraints. Their effects are applied between
rendered arguments (§3.10, **Format overloads**).
`to_bytes`/`bytes_of` require `ImageSafe` element types (no plain references,
slices or `in pool` references), `from_bytes` the stricter `VerifiableElem`
(every relative reference points at an element or a variant of it). A
`bytes_of` result carries `byteview` provenance, which the shrink scans
treat as able to point into any storage the root could image. Thread entry
points get one specialization each (`EnsureThreadSpec`) with flat
parameters, and `CheckThreadGlobals` walks a thread program's call graph,
rejecting non-flat globals. No call makes such a specialization:
`GetOrCreateSpec` rejects every call that resolves to a `thread_fn` (by
name, as a member, through a function value, as a dispatched case or as a
`format` overload), while a plain function of the same name stays
callable. `thread_spawn` takes the one `thread_fn` its
first argument names, and a name with several is ambiguous: the worker's
arguments do not choose among them.

`embed_file(path)` reads a file when the program is compiled
(`EmbedFile`). The path is a string literal or a `let`/`const` global
holding one, relative to the file containing the call. The bytes are kept
in `Ast::shaders` under `"file\n"` followed by the path, and emitted as
static data the way a shader blob is.
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
(`src/gfx_blob.h`, and the gfx package's `gfx_api.h` and `docs/gfx.md`), not general Goose
value layouts.

A pool's kind is carried with its provenance as bits: `RU_SLOTS` for
`reusable` and `RU_SLICES` for `reusable[]` (`Prov::reusable`,
`RootArg::reusable`). Merging two branches keeps only what both allow (a
construct choosing among pools of one kind is a pool reference, whose
`gs_pref` each branch builds, §6.1), and the table's
`BF_REUSABLE`/`BF_SLICEPOOL` flags each require their bit. Only a binding
whose type can carry the freelist, a plain reference to a grow-only array
(`CarriesPool`), keeps the bits. `BindProv` drops them for any other
variable, and `GetOrCreateSpec` for any other parameter, so an optional
reference to a pool is an optional reference to an array. A pool reference
variable keeps its first binding's bits, since codegen gives it one
representation for its life (§6.1): `CheckRefRebindRoot` rejects a rebind
to a value lacking one of them. A function checked standalone
(`CheckUnreached`) assumes both bits, which no reached binding has, and
there a rebind narrows them instead, since the rebind settles what calls
could pass.

`free_slice` and `realloc_slice` use `index_of`'s exact-root test
(`RootedAtReceiver`) only to leave out a run-time test. If the test cannot place
the slice in the pool, the checker may still prove
that it belongs elsewhere. It reports an error when the slice is rooted
exactly at a global or a variable of the checked function, and every receiver
alternative names a different such global or variable. Loops defer this
decision to their settled pass (§3.7).
Otherwise the call sets `Call::poolcheck`, which codegen turns into a
range test. `alloc_slice` and `realloc_slice` need an element type with a
default value, and `realloc_slice` also needs one without self-relative
references (`HasRelRefT`), since it may copy the slice.

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
  side. Compound assignment also samples the old value first. Some
  destinations cannot be moved by the right side (`StableDest`): a
  variable, or a field of a fixed-size value or an element of a fixed-size
  array reached down from a variable, at an index that takes no check,
  with no reference that can be rebound. Such a destination is stored to
  as it is written, after the right side has run, rather than through a
  pointer taken first. That makes it a member store, which the C
  compiler's type-based alias analysis keeps apart from other structs'
  fields of the same type (a loop updating `s.w` keeps `n.e` in a
  register), whereas a store through a `double *` may be to any double.
  Because the store is written like the loads of the same element, gcc's
  loop invariant motion also sees it as one location. Only then does gcc
  keep an element that a loop reads and writes (`a[i] = a[i] + x` in a
  loop over `j`) in a register, which lets the loop vectorize. A right
  side that runs nothing, writes nothing and cannot fail (a variable, a
  constant: `QuietValue`) cannot move any destination either, and is
  stored as written too; a destination used before any statement runs
  keeps an index variable in place rather than reading it into a
  temporary (`GenLocNow`). Whole
  resizable assignment has the clear-before-construction semantics of
  spec §4.4; overlapping permitted copies have memmove semantics.
* A range/count `for` samples its bounds once, before iteration, and runs
  zero times for an empty or reversed range. Array iteration visits
  increasing indices and tests the current array length at each iteration,
  so permitted appends can extend the traversal. A slice's length is its
  own view length, not its owner's current length. A sequential cursor
  advances past the current element even on `continue`. A body that
  re-points a slice or rebinds a reference on the path moves the traversal
  to the elements the path now leads to, at the next index, as the re-read
  length does (`ForLoop::CgStmt`). A sequential cursor walks there from
  the start of those elements, and the start of elements behind a varint
  length prefix is computed again.
* print, str and format render a value where it lies, around its format
  overloads (§3.7). An array's count and elements are read once, when its
  rendering begins. A struct, variant or enum reached through a reference
  that can be rebound is resolved to its address before its first part
  (`RenderLoc`, `PinLoc`). If an overload re-points the slice or rebinds
  the reference, the rest is still rendered from what the rendering began
  with, which the checker holds in the meantime (`renderarg`). An overload
  may overwrite a fixed-mode ADT's payload with another variant (§3.5). So
  wherever the argument has overloads, the payload is copied into a C
  local of its variant's type once the tag is read, and its parts are
  rendered from there, as the variant's literal (`RenderVariant`). It is
  never rendered through an overload for the variant type, which the
  checker only specializes for values of that type. A variable-mode ADT's
  payload is rendered in place, by the tag read once: the checker holds
  the payload, and an array's elements, while the overloads of their parts
  run (`renderwalks`).
* A by-value result that is immediately viewed still has its own temporary
  storage through the containing statement. Inlining or selecting a
  constant branch must preserve that copy when replacing it with the
  source lvalue would expose later writes (section 4, "Views of copies").

Effects include hidden calls: field defaults, rendering hooks, every
dispatch arm, and invoked function-value bodies. All must be considered
when checking shrinking, construction conflicts, optional narrowing and
when retaining bounds facts. Optimizations may remove effects only after
the program has passed the language's static checks.

### 3.16 `defer`

A `defer` (§6.6) is checked where it is written (`CheckDefer`). Its body is
a block in a scope of its own kind (`SK_DEFER`), so a `break` or `continue`
looking for its loop (`FindBreakScope`) and a `return` of any kind
(`CheckReturn`, `InDeferBody`) stop at it and are rejected, a `return` in a
function value written in the body included, since the value's frame is
lexically inside the body's scopes (`Frame::lexframe`). The flow state the
body is checked in is the statement's less the narrowing of every `var`,
which an assignment between the statement and an exit may undo; the flow
after the statement is the flow before it, since the body's assignments
happen at exits that nothing in the block observes. The body's stores,
growths and shrinks are recorded like any statement's, and the lifetime
rules see the variables live at the statement, which are at least those
live at any exit.

Two rules need the whole program and run after every root (`CheckDefers`).
The body may call nothing that uses long-distance return machinery: a
specialization with a `return … from` site or on a propagation path
(`FnSpec::needs`), one that catches one, or one that calls either, which is
one flag per specialization computed as a fixpoint over the call graph; the
error names the chain of calls. A body runs in the epilogue of a frame a
long-distance return passes through, while `gs_rf` and the target's
channels hold the return in flight (§6.6 below), and a contained throw and
catch in a callee would clear `gs_rf` on its way out, so the rule covers
the catch too. And the body may not name the local a function returns by
name (`NamedResultOf`, for a non-fixed result): that local is built at the
result destination, and the return records its count before the body runs.
Every identifier a body names of a variable declared outside it is recorded
as the body is checked (`NoteDeferMention`), through the function values
and nested functions written in it; a program without a `defer` pays one
test per identifier for this (`deferbodies`). A nested function the body
calls, or a named function it passes as a value, may have been checked
before the `defer` and be reused there, so what such a callee names is read
from its record of the variables outside its activation
(`FnSpec::envreads`), which every call site fills, reused or not.

---

## 4. The optimizer

`Optimizer` (`optimize.h`) rewrites the checked bodies in place before BCE
and codegen. Its hard rule is that nothing it does changes what a release
build computes, and that an operation that would abort at runtime
(division by zero, a failing `as`, an out-of-bounds index) is never folded
into a value or folded away. The only licence it takes is the one §6.2
gives: debug-build overflow detection is per operation as it executes, so
tail-recursion elimination may regroup an associative chain.

**Order.** `ReachRoots` marks the specializations reachable from `main`,
the thread entry points and global initializers (including expanded
defaults), counts call sites per specialization (`uses`), and records a
call-graph postorder (callees first; a cycle is cut at its back edge).
`Analyze` first collects, over every live body, how often each variable is
written and how often its address is taken (`facts`), so that a write in
another specialization's function value is visible before any rewriting.
After that, in order, come:

* global initializers (fold only);
* every specialization in postorder (`SetupBaseCase`, `OptBlock`,
  `TailRecurse`, `Scan`);
* globals again (now able to inline);
* a final reachability pass, so that specializations whose every call was
  inlined go dead and codegen skips them;
* loop shaping over the live bodies.

**Constant propagation** (`OptStmt`, `Ident::Opt`): a single-name declaration
of a scalar with a literal initializer, never written and never
address-taken, is entered in `consts`; every later use in the same walk
becomes the literal and the declaration is dropped unless captured. Constant
`let` globals propagate into every body. A parameter bound to a literal
argument is substituted at inlining time (`Inliner::BindArg`). A variable
that print, str or format renders counts as address-taken where a `format`
overload takes it by reference (`HookedByRef`), since the overload is
handed the variable itself (**Format overloads**) and may keep a reference
to it or write it.

**Folding** (`Opt` per node) covers:

* integer operators at the operands' checked width (`FoldIntOp`, `ast.h`,
  which the checker folds with too: unsigned operations and shifts wrap,
  and a signed result is folded only when it fits);
* comparisons at the operands' signedness;
* float operators (at `f32` precision when both operands are `f32`);
* `&&`/`||` with a constant left operand;
* `!`, `~`, and unary minus (where the result fits, as for the binary
  operators; the `u64` literal 2^63 negates to exactly `i64.min`, so a
  minimum written as a literal never reaches the checked negation at run
  time);
* casts (a checked integer cast only when exact; a float-to-int only in
  range and integral; a conversion to a float of anything but a `u64`
  above `i64.max`);
* `.len` of a fixed array and `.cap` of a static-capacity limited array on
  a plain variable receiver;
* `if` on a constant condition, `match` on a constant integer,
  `while false`, `assert(true)`;
* statements after a `return`, `break` or `continue` in a block.

**Inlining** (`TryInline`): a call is replaced by an `InlineBlock` holding
the callee's parameter bindings as `VarDecl`s, followed by a `Cp1` copy of
the body. The bindings are marked `inline_arg` and evaluated in the
caller's scope, so that an argument temporary outlives the returned value.
A `Return` whose target is the inlined function exits the block with its
values (`Return::CgStmt`), which keeps early returns, `return from` and
function-value returns working through any nesting. The copy is re-folded
so substituted constants cascade. A trivial result unwraps to a plain
expression when its type survives. A call passed where a slice is expected
keeps that slice as its checked type, while the body returns the array.
Codegen builds the array where the call would have put its result, a
temporary of the caller's scope, and slices it whole
(`InlineBlock::CgAny`), since the body's own scopes release their storage
when it exits. The body's `return` may deliver another type than the call
was checked as: the reference itself where the call site decayed it to its
pointee, or a slice where a limited array is wanted. There the destination
loads or adapts the value as its type asks (`GenXD`, `CallVal0`). Every
lvalue destination an inlined body can reach names that type (`Dst::t`),
including the temporary that receives a function value's call's value and
the global an initializer sets. A standard library body copied into the
program's own code has its library nodes anchored at the call
(`Inliner::AtCallSite`), so a check in the copy fails at the line that called
the library, as a check in a call left standing does (§6.4, `gs_at`).

At each call site of a callee K, K is inlined if it is used once, or its
post-optimization node count is below NC, or count times uses is below NCU
(`-O1`: 8/48, `-O2`: 16/96). An `inline fn` (§7.13 of the spec,
`SFunction::isinline`) passes these thresholds at every call site wherever
inlining is on. A call site inside a loop raises NCU fourfold
(`loopdepth`, `LOOPNCU`). Such a call runs once per iteration, so there a
small function wrapping inlined work is worth its copies rather than
staying a call in the loop. Neither the raise nor the single-use rule
applies in a cold branch (`colddepth`): the else of a guard (an
`IfExpr::flat`), or an arm that ends in a `return`, a `break`, `abort` or
`exit` where the other path continues. Such an arm runs at most once per
run of its function or loop, mostly on an error path. A callee used only
there, and too big for the size rules, would grow the hot path of its
caller to save at most one call. So it stays out of line, and is declared
`GS_NOINLINE` in C (`FnSpec::outofline`), since the C compiler otherwise
inlines a static function with one caller whatever its size. A branch that
a constant condition decides is folded away first and is not cold.

The single-use rule also gives way to the caller's own inlining
(`OptBody`). Take a function F with several call sites, and a callee used
once on a branch of F (an `if` or `match` arm, the right operand of `&&`
or `||`) outside F's loops, and too big for the size rules. Inlining that
callee would put its whole body into every copy of F. So F is first
optimized without such callees. Then, in the order the walk met them, each
one is inlined while F stays small enough to inline at its call sites in a
loop (count times F's uses below LOOPNCU times NCU, or count below NC), or
regardless where F never was small enough. The other callees keep their
calls, which every copy of F repeats, so from then on they take F's number
of uses for their own inlining; a second walk of F inlines the rest. A run
of F need not take the branch, so the call left there is cheaper than the
call to F that inlining the callee would bring back. A callee that F calls
on every run is inlined into F as before, since calling it instead of F
saves nothing.

`Scan` never inlines:

* a `recursive` function or cycle member;
* a `thread_fn`;
* a `simd` function (its callers would run the baseline only, §6.13);
* a function returning more than one value. Where that is the only reason
  (`InlineInfo::c_inlinable`) and the thresholds pass at a call site, the
  function is declared `GS_INLINE` in C instead (`FnSpec::cinline`), so
  the C compiler inlines it: always_inline under gcc and clang,
  `__forceinline` under MSVC;
* a body that a *separate* live tree still references in a way a copy
  cannot follow. A remaining call to a specialization with bound function
  values reaches the body's locals as free variables, and a remaining
  callee that does `return ... from` the body needs its frame.

A remaining call to a nested function also reaches the body's locals, but
only through the pointers the call passes (§6.4), so such a body is
inlined like any other (`InlineInfo::nestedcalls`):

* the copies of its captured variables stay captured;
* a captured parameter is bound rather than substituted;
* each copied call to a nested function records the copies it passes in
  place of the variables the callee names (`Call::fvremap`,
  `Inliner::RemapFreeVars`, composed through further copies; a nested
  function inlined from such a call names those copies, `Inliner::outer`).

Codegen passes `Call::FreeVarArg` for each of the callee's free variables,
and follows those edges when it collects a body's free variables
(`CollectSpecs`). BCE's call kills name the copies the callee's effects
reach (`CallKills`). Those records, and the provenance of the copied
variables, are completed once the copy is whole, as the outermost
`Inliner::Cp` returns (`Inliner::Finish`). The provenance is pointed at
the copies of the variables it names, so that BCE's aliasing (`UltOf`,
§5.4) follows a copied reference to the array the copy declares. This is
done for inlining and base-case inlining alike, and after the scopes that
copy their variables late (a `for` copies its own after its body). Such a
body is not inlined into a global initializer, whose locals belong to no
specialization. Nothing is inlined *into* a cycle member (its locals would
become the cycle's own).

**Views of copies** (`OptViewed`). The value of a call, of a bare block and
of an `if` or `match` is a temporary copy (§9.2), and the checker treats a
view of it as a view of a temporary of its own (`TempRoot`), which nothing
else in the statement writes or shrinks. Unwrapping an inlined result, or
folding a statement-less block into its tail and an `if` or `match` into
the branch taken, can reduce that value to a path into the storage it was
copied from. That is harmless where the value is copied out. But where the
value is viewed where it stands, the view would then see the rest of the
statement write or shrink that storage. Those places are:

* a slice's base;
* an index's base where the index runs code (the element is read after
  it; `CodeFree`);
* a `for`'s iterable;
* the operand of `&` (explicit, or a reference parameter binding it);
* a member builtin's receiver (`bytes_of` returns a view of it);
* the base of a field or element that is itself viewed, or is an array
  that a slice destination takes whole.

In those places such a path, or `&` of one (which codegen addresses as the
path, `GenLoc`), is put back into a block, which codegen evaluates into a
temporary as the construct would have. Otherwise `f(get()[..], a.pop())`
for `fn get() -> i64[3] { a[0] }` would hand `f` a view of the slot the
pop frees. An argument that print, str or format renders while user
`format` overloads run is one more such place (`OptRendered`). It is read
where it stands around the overloads, and handed where it lies to an
overload that takes it, or a part of it, by reference, and the overloads
may write the storage a path names or re-point it. There a path of any
type is put back into a block, a slice's or a scalar's path as well, so
that an overload is given the temporary that the checker took the
argument to be (**Format overloads**).

**The nesting limit** (`MAXNEST`, 64). Every inlined body is a C block of
its own, and C compilers limit how deep blocks nest in one function: MSVC
to 128 (C1061), clang outside its MSVC-compatible mode to 256
(`-fbracket-depth`). Under the thresholds alone, a chain of single-use
functions, each calling the next, folds into one body as deep as the chain
is long. Each inline copies a body with everything already inlined into
it, while the original stays allocated, so the cost is quadratic in the
chain's length: 6.5 GB and most of a minute for a chain of 2000 in a Debug
build. So, whatever the thresholds say, a call is inlined only while the C
blocks around it plus those the callee's body nests stay within `MAXNEST`.
Such a chain then folds into one body per 64 levels, each calling the next,
and takes 2 s and 240 MB (0.7 s and 170 MB at `-O0`). Both counts are of
what codegen opens a C block for:

* every `Block` (a function or inlined body, an arm, a loop body; not a
  flat `if`'s then-block, §6.2, which `Around` takes back);
* one block around each of: an `else` that is not a `Block`, the right
  operand of `&&` and `||`, a `while` condition (tested inside the loop),
  the arguments of a function-value call (bound inside its block) and an
  array's fill value (conservatively counted with its repetition loop);
* two around a match arm (a switch and its case).

`Around` counts the ones around a child. `Scan` records the deepest
nesting of each final body (`InlineInfo::nest`, the `nest` of `--specs`).
The walk keeps the count around the node it is at (`depth`), taken as the
blocks stand when it gets there; a block that folds away afterwards only
makes the walk more cautious. The other half of MSVC's limit is for the
blocks codegen opens around runtime work, which are not counted. Ordinary
programs stay far below the limit: the deepest inline in the tests,
samples and benchmarks is 18 blocks deep.

**Base-case inlining** (`BaseCaseInliner`, `optimize_basecase.h`) applies
to a `recursive fn` whose body *starts* with `if c { return e; }`
(`SetupLeading`; or the negated `if c { … } else { return e; }` that
`guard c else { return e; }` parses to), where every parameter is
fixed-size, `c` is a pure read of parameters and globals, and `e` calls
nothing in the cycle. Under the inliner's size thresholds, each direct
self-call `f(a...)` is rewritten to
`{ let p = a; ...; if c[p] { e[p] } else {
f(p...) } }`. A UFCS self-call
`a1.f(a2...)` is the same call: its receiver binds as the first argument,
as the checker left it (including an `&` it binds by reference), and the
recursing arm's receiver is that binding. This removes half the calls of a
complete tree walk. The bindings are marked `inline_arg`, like an inlined
call's, and made in the scope around the block (`GenInlineArgs`): a slice
argument can view a temporary, which has to last while the callee runs and
while the block's value is used. The guarded form
(`SetupGuarded`) is the shape other languages build trees in: `s1...; if c {
... } s3...; return e;` with every self-call inside the `if`, which has no
else. Where `c` does not hold, the body is `s1; s3; e`, so a self-call
becomes `{ let p = a; ...; if c[p] { f(p...) } else { s1; s3; e }[p] }`,
under the same size rule counted over `c`, `s1`, `s3` and `e`. The call
site evaluates `c` before `s1` runs and the callee after, so `c` may only
compare literals and parameters the body never assigns (`GuardOK`): such a
test gives the same answer at both points and cannot abort. `s1`, `s3` and
`e` may not return, jump out, call into the cycle or call a function that
may `return ... from` this one, whose copy would leave the caller's frame.
Neither form applies to mutual recursion, or to a self-call whose array
result is passed where a slice is expected: the array has to outlive the
`if`, and each arm would build it in a scope of its own.

**Accumulator tail-recursion elimination** (`TailRecursion`,
`optimize_tre.h`, `-O1` and above): a directly self-recursive
specialization returning one machine integer, whose tail returns are
`self(args)` or `E op self(args)` (or `self(args) op E` with `E` pure) with
one associative operator (`+ * & | ^`), becomes `var acc = identity; loop {
... }`. Each tail call turns into `acc op= E`, rebindings of the parameters
(all at once through temporaries when several change) and `continue`;
every other return folds `acc` in. Self-calls in non-tail position stay
calls, so `1 + check(l) + check(r)` loses its right spine only. Parameters
must be rebindable (scalars, or plain non-fat, non-pool, non-relative
references whose new value points outside the activation). Tails inside
loops or function-value bodies are skipped, and a callee that can
`return ... from` the function disables the transform, as does a `defer`
anywhere in the body, whose run after a tail call's return the loop could
not reproduce. The recursion keeps
each caller's locals and by-value parameters alive and unchanged under its
call, whereas the loop reinitializes and rebinds them for the next
iteration. So the loop cannot carry a reference that a tail call passes
down into them (§7.8). `Locate` accepts a path into a global, a variable
of an enclosing function, or what a reference or slice parameter refers
to. It also accepts a path through what such storage holds, or through
what the activation's copy of it holds (`c.next` of an arm's `Cons c`).
Here a `let` stands for its initializer, and an arm's binding for its
scrutinee. A call's result, which may be a reference to anything the
callee was handed or a nested function captures, is not followed. The
`var t = ...; if c { t op=
self(x) } t` form is restated as a return first (`AccVarForm`).

**Min/max regrouping** (`Reassociator`, `optimize_reassoc.h`, `-O1` and above)
applies inside loops to nested integer calls of one specialization shaped like
std's `min` or `max`: `if x < y { return x; } return y;`, or returning `y`
first. An operand must read a variable declared outside the innermost loop.
That loop must assign the variable, write one of its elements or fields, or
take its address with `&`. Such an operand usually carries the previous
iteration's result, as
the insertion cost of an edit distance does. The C compiler keeps the
chain grouped as written, so every comparison the operand passes through
is a compare and a conditional move on the loop's critical path.
`Call::Opt` offers each call to the pass before its arguments, while the
chain is still calls. Where the carried operands are not already combined
last, the chain becomes a block. The block binds every non-literal operand
to an `operandN` local in written order, so each is evaluated once and in
order. It then combines the other operands first and the carried ones
last, reusing the chain's own calls. Integer min and max are associative
and commutative, so the value is the same; a wrong guess about which
operand carries the dependence has no effect beyond the regrouping itself.

**Loop shaping** (`LoopShaper`, `optimize_loops.h`, every level). Some
loops compile to C that clang cannot make fast, although it can make an
equivalent restatement of the same loop fast. Once the final bodies are
known, this pass marks such loops. It rewrites no tree, and codegen
restates a marked loop only where its own conditions hold too (§6.12).
Each mark records a fact about the loop as written that makes the
restatement compute the same values and abort at the same point:

* `ForLoop::stripk` and `lanemods`: the counter starts at 0 and steps by 1
  (an array's `i64` index, a count's variable, or a range's counter from a
  literal 0 at the range's own type), and the body takes it `% K` for a
  constant `K` from 2 to 16 (the `lanemods` are those with the first `K`
  found). In a block of `K` iterations starting at a multiple of `K`,
  `i % K` is the position in the block (§6.2's `%` is Euclidean and the
  counter is never negative), and the `%` could not abort. The loop
  variable must not be captured.
* `ForLoop::sumred`: the body is the single statement `s += e`. Here `s` is
  a float local that is not global, not captured and never the operand of
  `&` (the facts `Analyze` collects: a reference to a scalar takes an
  explicit one). `e` reads no `s` and has no effect but a possible abort:
  it is made of literals, variables, operators, casts, index and field
  reads, `if`, and inlined bodies of `let`s of scalars whose returns leave
  only them. Floating-point addition is not associative, so clang keeps
  such a loop scalar. Computing the terms of a block of iterations first,
  and then adding them to `s` one at a time in order, changes no bit of any
  result. A term that aborts does so at the same iteration, and what it
  leaves unadded is a local nothing reads afterwards.
* `While::countdown`: `while v > 0 { v--; ... }` (or `0 < v`, or `v -= 1`
  first), where `v` is a `var` integer like `s` above that no other
  statement of the body assigns or steps. The trip count is `v`'s value at
  entry, and the decrement can neither wrap nor overflow.

A body over 300 nodes is not marked, since the first two restatements emit
the body twice.

---

## 5. Bounds-check elimination

`BCE` (`bce.h`) runs over every live specialization after the optimizer. It
marks the `Index` and `SliceExpr` nodes whose runtime check cannot fire, and
codegen then omits those checks. The pass is required only to be sound: a
program's meaning never depends on it (§10.5). The pass also makes some
codegen decisions that are not about checks: `ForLoop::fixedlen` and
`lenbound`, and the per-loop `hoistrefs` and `hoistfields` (§5.10).

### 5.1 The domain

The pass uses a flow-sensitive **difference-constraint** domain. Its facts
have the form `l <= r + c` over *bases*. A base is the constant zero, an
integer variable, the length of a *place* (an array or slice location), or
a one-shot temporary base for a value that the operation itself bounds. To
answer the query "is `l <= r + c` provable", the pass finds a shortest path
over the fact graph plus the axioms (`Dist`, Bellman-Ford with saturating
weights); the smallest provable `c` is the distance. A state holds at most
200 facts (the oldest is dropped first). Offsets on variable bases are
capped at 2^32 and constants at 2^60; anything larger is treated as
"unprovable" rather than giving a wrong answer.

Every query uses these **axioms**:

* `0 <= len <= 2^48` for every length (this cap, from §10.4, is what makes
  `len - 1`, `i + 1` and `len + len` provably free of overflow);
* `len <= k` for a place that is a limited array of static capacity `k`
  (`Place::cap`);
* the storage range of every sub-64-bit integer variable;
* the invariants granted by the recording pass (§5.7).

A slice bound to a whole field takes the length of the field's array: the
length at the place that the field's `.len` names, however the use
converted the field (`BoundLenOf`). A slice bound to a whole variable or
field declared as such a limited array is stated to be no longer than `k`
at its declaration (`DeclCap`).

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
`old <= new` and a shrink adds `new <= old`. So a bound established before a
`push` still holds after it, but not after a `pop`. A push or a
constant-length append is an exact step (`ExactLenStep`: `new == old + k`);
`clear`, `resize(n)`, a fresh literal, a slice binding and a rebind to a
known place set the length exactly (`ExactLenIs`). Monotone integer
variables (§5.7) get the same bridges across their kills.

### 5.3 Terms

`TermOf` reads a machine-integer expression as *base + offset*. It
recognizes:

* a literal;
* an integer variable, unless it is a `varint` or a `u64` that is not of
  an index type (`IntBaseVar`, §5.11);
* `.len` of a place (a fixed array's length is a constant);
* `x + c`/`x - c` at `i64` only (narrower widths wrap below the 64-bit math
  the facts are stated in);
* `a % b` and `a & b`, which lie in `[0, b]` on a fresh base when `b` is
  provably non-negative. A negative signed mask proves nothing, so a mask's
  term must also be one that cannot have wrapped (`n + 1` at i64's top is
  negative);
* `a / b` and `a >> k`, on a fresh base (`QuotTerm`). A shift by a constant
  maps the dividend's constant bounds through. When the dividend is
  provably non-negative and unwrapped, the quotient lies in `[0, a]`
  wherever `b` is also non-negative and unwrapped (a completed division had
  `b != 0`; a shift count is masked). For such a dividend, the quotient
  also lies below `a` once `a >= 1` and a constant divisor is at least 2
  (truncating division and either shift agree there; a heap's parent
  `(i - 1) / 2` is below its child), and a constant divisor divides the
  dividend's constant bounds as well;
* a cast whose value the facts already place inside the target's range.
  Such a cast is the identity and carries its operand's term;
* `a * b` and `a ± b` with two moving operands, handled by *intervals*.
  Where both operands have finite constant bounds, the result gets a fresh
  base bounded by the four corner products or by the summed ranges. These
  bounds are stated only when they fit the operation's own width
  (release-mode wrapping, §6.2). A sum is also related to an operand whose
  partner varies within a small range (`i * 4 + b` for `b` in `0..4` lies
  in `[i * 4, i * 4 + 3]`);
* the value of a bare block or of an `InlineBlock` (its tail or final
  return), where no other return leaves the inlined body with a value of
  its own (`BlockValueTerm`).

An operation's width is its operands' type (`OpType`), not its `exprtype`,
which is the possibly wider slot that receives the value (§3.3). Derived
terms are memoized per node and invalidated by any generation change
(`Derived`). An expression whose later operand changed tracked state is not
rebuilt from its operands' current names (`effectfulterms`).

A value read out of storage or returned by a call (`Index`, `Dot`, `Call`)
has no base, but a type narrower than 64 bits still bounds it
(`TypeRangeTerm`: a byte loaded from an array indexes a 256-entry table).
`a >> c` maps its operand's constant bounds through the shift (`QuotTerm`:
that byte shifted right by two indexes a 64-entry table). These constant
bounds of a one-shot base are axioms of that base (`tmpival`) rather than
facts, so they never compete for the fact cap.

**Multiples and quotients.** A product by a constant `m >= 1` (`x * m`,
`x << k`) whose operand's bounds show that it fits its type is a *Lin*
base, and so is a floor quotient (`x >> k` whatever the sign, or a
non-negative `x / m`). A Lin base remembers its operand term and `m`
(`Flow::lins`, kept per path because `ShiftCore` renames an operand in
place). Two Lin bases with the same `m` relate as their operands do
(`LinRelate`): `x <= y + c` gives `x * m <= y * m + c * m` for products,
and `x / m <= y / m + ceil(c / m)` for quotients. This is what bounds
`r[2 * i + 1]` by `r.len == 2 * n`, and `flags[j >> 5]` by
`flags.len == (n + 31) >> 5`, where `i < n` and `j < n`. A multiple or
quotient of an operand base that already has a Lin base is that Lin base
plus an offset (`ScaledTerm`, `QuotTerm`). So `buf[4 * i..4 * i + 4]` is
known to be four long, and `(y - 1) * W..y * W` is known to be ordered. A
quotient is related only once its operand is known to be the value the
machine shifted: either an operand that cannot have wrapped, or a sum with
a positive offset whose quotient is non-negative (a wrapped `i64` sum lies
near `-2^63`). A completed check or a length that bounds a quotient also
bounds its operand (`QuotBack`). For example, `resize((n + 31) >> 5)` puts
`n` below `2^53`, which is what keeps a sieve's `i << 1` and `j += i` from
wrapping.

A term is admissible as a comparison side (`CmpAdmissible`) only if the
machine comparison equals the mathematical one. That is the case for any
constant, a bare base, a length plus a small constant, and any other base
plus an offset where the facts show that the `i64` addition forming it did
not wrap (`NoWrap`). So the lookahead `i + 1 < s.len` inside
`while i < s.len` is a fact, and so is the start of `for j in i + 1..n`. A
range endpoint is judged in the state it was evaluated in. This also keeps
a counted push loop over a range that wrapped to empty from claiming its
iterations.

### 5.4 Places and aliasing

A **place** is an array or slice location that can be named as a variable
plus a chain of struct fields, with reference crossings marked. A place
also has an "ultimate" owner: owned storage (the root variable), static
data, or opaque (when a stored reference is read on the way, or the root is
inexact). Crossing a root reference variable, including the fields after
it, does not count as reading a stored reference: the place lies in the
storage that reference points into. `UltOf` follows reference and slice
provenance to the owning variable. A synthetic parameter class is opaque as
storage, but two references in distinct classes are known to be distinct. A
reference in one of the body's own parameter classes was formed by the
body's caller before the body ran, so it names no storage of a variable the
body declares (`HandedApart`).
A slice stores its length in its slot, which may be a variable, field, or
element. A place reached through a reference-to-slice is therefore owned by
the slot at which that reference is rooted (§3.4). In slot mode, `UltOf` stops
at the slice variable instead of following it to the viewed array.
Storage is **reachable** to a callee or an unknown reference only if its
owner is a global, is captured, or has its address taken (`Reach`), since
creating any reference into a variable requires one of those.
`AffectedByWrite` decides whether a write to one place, to owned storage, or
to unknown storage may name another place; distinct plain paths under one
owner are distinct arrays.

A loop over a slice or limited array held by an element of a tracked array
(`for x in g.adj[v]`) has a place of its own (`ElemPlaceOf`). That one place
stands for every element of the array. Its length lies in the array's
storage, as it does for a reference to an element, so the place is killed
only by a write to an element (including a refill, §5.5) or an overwrite of
the array. As for any array's place, a grow or shrink of an array of
another type leaves it alone (`DistinctTypes`). So does a grow or shrink of
the array itself, which moves no element; a shrink leaves the slots it
drops as they were. The place's owner is the array's owner. Where the array
is reached through a reference variable and fields alone, that owner is the
variable or parameter class that the reference's exact root names. So, as
for any place behind one of the body's parameters, writes to storage owned
by the body's own variables leave the place alone (`HandedApart`), even
where the body hands a reference to one of those variables to a callee.

### 5.5 Kills

These invalidate facts:

* a grow or shrink builtin. The receiver takes the exact delta, and every
  place it may alias takes the directional bump;
* a whole-value write to a variable or through a chain
  (`StorageWriteKill`). This kills everything under that owner, or
  everything reachable when the target is unknown. For a chain of fields
  from a reference variable, the owner is the one its exact root names
  (`ChainOf`); as for a place, crossing the root itself does not count as
  reading a stored reference;
* a rebind of a reference variable (`RebindKill`);
* a `defer` body (`Defer::BceWalk`), which runs at every exit of its block
  rather than where it is written: its kills apply where it is written and
  again where its block ends (`Block::BceWalk`), and the loops and
  `block`s a break leaves strip their whole body's kills, this body's
  among them. Its own checks are judged without the facts of its position,
  which a write before an exit may have undone;
* a reference stored into a field or an element, either by rebinding one
  held there or by storing a value that holds one (`RefSlotWriteKill`).
  This kills every place whose path reads a stored reference
  (`Place::readsref`) and may read it there, whatever name the path starts
  from: `p.r` is `h.r` where `p = &h`. Where the place is a slice held in
  a field, that field does not count as reading a stored reference;
* a slice stored into a slot, by assigning a slice variable or through a
  reference to a slice (`SlotWriteKill`). This kills every slice place that
  slot may be: the variable, each reference to it, and any reachable slot
  when the slot is unknown. The arrays the slices view keep their lengths;
* any other pointee write through a reference (`PointeeWriteKill`) or store
  into a field, unless the value holds no length and no reference (a struct
  of scalars changes no length wherever it is stored);
* a store of a value holding a length (`HoldsLen`) into or inside an array
  element, or a grow or shrink of an array inside an element
  (`ElementLvalKill`). No place lies in an element, but a reference to or
  into an element measures what the element holds. So this kills every
  such place over the storage the elements lie in (never a resizable
  array's place, since no element is a resizable array), or anything
  reachable where a reference held by the element leads out of it;
* a *refill*, which counts as such a store into the receiver's elements
  (`GrowShrinkKill`). A refill is a limited array's growth or a pool's
  allocation: both store elements into slots that a reference kept across
  a shrink or a free may still name, and such a slot stays a value of the
  element type. A resizable array never shrinks while a reference into it
  lives, so its growth fills fresh slots only;
* a repeated declaration inside a loop;
* a call.

A call with no summary kills everything reachable plus every reachable
integer variable (`KillByCall`); a call with a summary kills exactly what
the summary names (§5.8). Integer writes shift facts in place when the
pre-state provably cannot wrap (`ShiftCore`: `i++` moves every fact about
`i` by one), and kill the variable otherwise. An assignment `v = e` re-pins
`v` and records `v == e` when `e` has a term that cannot have wrapped.

An array has one type, and a reference reaches only an array of its own
type (array kinds do not convert at a reference). So a grow or shrink
resizes no place whose array type is surely another (`DistinctTypes` in
`GrowShrinkKill`; the elements a refill stores are a separate kill).
Likewise, a callee's write through an argument that is an array of
scalars, which holds no other array, kills only places of that array's type
(`OtherArray` in `KillArgStorage`). A shrink of a reference parameter's
array thus leaves a global of another type alone.

A directional bump records that the new length is at least (or at most) the
old one only where a fact names the old length (`BumpPlace`). A growth
through a reference parameter may be a growth of any global array. Adding a
fact for every such place in the program would push this body's own facts
out of the state, which holds `MAXFACTS` (200) facts and drops the oldest
first.

### 5.6 Facts from control flow

`CondFacts` adds the comparison of an `if`, `while`, `assert` or
short-circuit operand (both senses, through `!`, `&&` and `||`), and an
integer `match` arm's range (the hull of a listed arm's values and ranges).
It also adds a `for` header's bounds, `0 <= i < n`: against the snapshots
taken at loop entry for ranges and counts, and against the re-read length
for arrays and slices. A `!=` alone bounds nothing, but it is strict
against a side that the facts already order: past
`if xs.len == 0 { return; }` the length is at least one. A condition that
itself changed tracked state (through a mutating call inside it) adds
nothing (`HasKillEffects`), since the comparison ran against pre-kill
values. Changes to locals that the condition declares itself, such as the
parameter of a predicate inlined into it, do not count, since its
comparisons cannot name them. A completed `pop` proves the old length was
at least one, and a `pop` where the facts already put the length at one or
more loses its emptiness check (`Call::nobc`), as after
`if s.len != 0 { s.pop() }`. A completed `pop_n(n)` proves `0 <= n <= old length`
(`CheckedFacts`), and a constant `n` steps the length down exactly. A
`resize` states the new length when the count's term survived the
evaluation of the fill value. Neither `pop_n` nor `resize` states anything
when evaluating its count moved the receiver (`recvmoved`), since the
receiver was resolved before the count ran.

A completed check is also a fact (`CheckedFacts`): the program continues
past `a[i]` only with `0 <= i < len`, and past `a[lo..hi]` only with
`0 <= lo <= hi <= len`. So a repeated index, or an index inside an earlier
slice's bounds, needs no check of its own. The index's term is the machine's
value there even where an `i + c` could have wrapped: a wrapped `i64` lies
outside `[0, 2^48]`, which the check rejects.

Joins are the **meet** of the branch states (`Meet`: a fact survives with the
weaker constant, generations take the maximum); a branch that diverges
contributes nothing.

### 5.7 Loops and invariants

Every loop body is walked first in kills-only mode (`StripKills`), to
invalidate whatever an earlier iteration may have changed, and then for
real. A `while` condition's facts are re-established at every body entry,
and the condition's negation holds after the loop only if nothing in the
loop can `break`. A `break` in the body can leave after any part of the
body has run. So what evaluating the condition stated (a completed check,
or the length a `pop` leaves) holds after such a loop only as far as the
body's kills leave it intact. A `break` in the condition itself leaves with
only part of the condition run, where only the state at the top of an
iteration holds. Facts established by an `EarlyBlock` or by an
`InlineBlock` with early exits are likewise reduced to their kills at the
join. A counted loop whose body pushes onto an array a fixed number of
times per iteration states the array's length after the loop. It does not
when another statement of the body can change that length (including a
push onto another place that may name the same array, such as a reference
to it), or when a `break` or `continue` can cut an iteration short
(`HasIterationJumps`). A `continue` inside a `block { }` counts, since it
goes to the loop around the block; the block binds only the breaks inside
it.

A kill loses the `i >= 0` and `i <= len` facts that the classic
`var i = 0; while i < a.len { ...; i++; }` needs. So each body runs in
three modes (`RunSpec`):

* a **kills** walk, which produces the whole-body summary of bumped places
  and rebound variables;
* a **recording** walk. For every eligible local integer variable
  (single-name declaration, not captured, not address-taken, no compound
  write other than `±constant`), it checks that every write preserves
  `v >= 0` and that no write can wrap at the variable's width. It also
  checks that `v <= len(P) + c` survives for each place `P` that the
  variable indexes or bounds and that is never shrunk or re-bound in the
  body (growth alone never breaks it). The declaration decides `c`: what
  its value needs, but at least 0 (`var j = 1` before
  `while j + 4 <= a.len { ...; j += 4; }` stays within `len + 1`, which
  keeps `j + 4` from wrapping). Where the value starts below the length, a
  second candidate for `c` is -1: a probe index `i = (i + 1) & mask` with
  `mask == len - 1` stays within it. A value whose term may have wrapped,
  such as `x - 5` for an `x` near i64's bottom, preserves neither bound and
  decides no `c`;
* the **judging** walk. It is granted the surviving invariants as axioms
  (`ge0`, `lelen`), and the wrap-free variables that move in a single
  direction as monotone (`mono`). Each `+= c`/`-= c` by a literal that the
  facts shift by (below 2^32), and each `++` and `--`, moves its variable
  in the direction of its sign. A plain assignment `v = e` counts where the
  recording walk proved `e <= v` or `v <= e` just before it
  (`RecordSetDir`), so `i = (i - 1) / 2` keeps a sift-up index below where
  it started. The declaration does not count, since it starts the variable
  over; a step by a larger literal counts as `v = v + c`.

Global integer variables get the same `>= 0` treatment across the whole
program (`ValidateGlobalInvariants`). The initializer is the base case,
and every write in every live body and in every global's initializer is
the step. This is what a parse cursor kept in a global needs. The writes
are checked with every candidate assumed at once. So when a candidate is
dropped, the writes are checked again in another round, until no more
candidates are dropped. A global whose address is taken anywhere,
including in an initializer, is not a candidate.

**Step counters.** A counted `for` loop (a range, a count, or an array or
slice walk with an index) moves its index by exactly one per iteration.
Take a local integer that the body changes only by unit steps: `v += s`,
where `s` is a literal 0 or 1 or a branch all of whose arms are
(`lt += if less { 1 } else { 0 }`), or `v++`. If at most one of these steps
can run per iteration (`StepsOf`: for an `if` or `match`, only the arm with
the most steps counts, and a step in a nested loop or a function value
counts as unbounded), the variable cannot gain on the index. So where
`v <= start + c` holds as the loop starts, `v <= i + c` holds at the top of
every iteration, and the loop states this beside the index's own bounds
(`StepCounters`, `ForLoop::BceWalk`). That gives the `lt <= i` that the
swap index of a branchless partition needs, or the same bound for a
compaction's write cursor. The index and the counter must be neither
captured nor address-taken, so that nothing else writes them. For the
invariants, a unit step itself counts as a shift by one, which is the worst
case both for wrapping and for `v <= len(P)`. Otherwise a unit step states
`old <= new <= old + 1` where it cannot wrap (`StepWrite`).

### 5.8 Across calls

**Effects.** `ComputeEffects` summarizes every live specialization, to a
fixpoint over the call graph. The summary lists the storage that the body
may resize or overwrite beyond its own locals, and the integers it may
write. Storage reached through a reference parameter's class is listed by
parameter index, and globals and captured locals are listed by variable. A
write that the walk cannot attribute makes the summary opaque. An
`extern fn` may resize or write whatever it is handed by reference, and
nothing else. `CallKills` then kills exactly the places those effects name,
at the arguments bound to them, so a length fact survives a call to a
kernel that only reads and writes elements. No callee resizes a slice
argument or a reference to one (`KillSliceArg`). Through the reference, the
callee may store another slice into the slot, which is that slot's kill
(§5.5). Through either, it may write the elements the slice views. Such a
write kills places reached through a reference into an element (only those
measure a length there), and only where the element type holds a length
(`HoldsLen`, `ElementWriteKill`). It also kills places that read through a
reference an element holds, where the element type holds a reference
(`HoldsRef`, `RefSlotWriteKill`). A class of this body's own parameters
names none of the body's variables (`KillClassWrite`). But the class that
an inlined body's parameter kept stands for this body's arguments to the
inlined body, so storage named through that class may be anything
reachable.

**Entry facts.** Every call site (`RecordSite`) records what it proves about
the arguments:

* each integer argument's term, sampled right after the argument was
  evaluated;
* each passed array's length (for a slice expression, what its bounds just
  stated; for a literal, its count);
* how these relate pairwise (`Sites`: a matrix of the weakest
  `X <= Y + c` that any site established).

Once every site has been analyzed (callers are analyzed before callees for
this), `SeedEntryFacts` grants a specialization the meet of its sites on
entry. It does so only if nothing reaches the specialization another way.
A thread spawn, a `format` overload called by `print`, an `export fn`'s C
wrapper (which passes whatever its C caller does), or any use the optimizer
counted that the call scan did not see makes the specialization opaque. A
site inside a recursive cycle is walked after its callee and contributes
nothing. This is what carries `src.len == W * W` from `main` into
`blur(src: u8[>..]&, ...)`.

### 5.9 Judging

`JudgeIndex` marks `a[i]` when `0 <= i` and `i <= len - 1` are provable
against the length as it stood when the receiver was evaluated.
`JudgeSlice` marks `a[lo..hi]` when `0 <= lo <= hi <= len`, with each bound
judged in the state it was evaluated in. It also records the slice's length
as `hi - lo` when the domain can name it (both bounds on one base, or a
constant lower bound); the declaration binding the slice then takes that
length (`slicelen`). Codegen separately elides a constant index into a
fixed array (`IndexLoc`). It also merges leading checks in the two arms of an
`if` (`LeadingCheck`,
`SameCheck`). Both checks must test the same index, a variable or constant,
against the same array, a variable or field path. Neither arm may evaluate
anything before its check. An assignment through the element as each arm's
first statement is one example. One check then
runs after the condition, ahead of the branch, and reports the location of
the arm the condition selects (`EmitHoistedCheck`), so it fails exactly
where either arm's check would have. The arms' addresses are then
unchecked, which lets the C compiler turn the two stores into one store of
a select (the merge step
`if a[i] <= b[j] { out[k] = a[i]; ... } else { out[k] = b[j]; ... }`).

### 5.10 What else the pass decides

* `ForLoop::fixedlen`: for an array or slice loop, whether the body's kill
  summary leaves the iterated place alone (for a sequence that an element
  holds, this is the element place of §5.4). If it does, codegen reads the
  view once. Otherwise it re-reads the length, and the slices and
  references on the way to it, every iteration; section 3.15 makes growth
  during iteration legal, so the re-read is the default.
* `ForLoop::lenbound` and `lenexact`: for an array or slice loop, the
  smallest constant that the facts bound the length by once the body's
  kills are stripped (so the bound holds wherever an iteration tests the
  length), and whether the facts prove the length equal to it. Codegen may
  bound the loop by that constant (§6.12).
* `hoistrefs` on every loop: the reference variables that the loop indexes
  and whose array cannot be grown, shrunk, rewritten whole or reached
  through a call by the body, or by a `while` condition or a `for` loop's
  iteration expression (which runs after the views are read). Codegen reads
  their base and length into locals before the loop (section 6.10).
* `hoistfields` on every loop: the same, for the grow-only and grow-shrink
  arrays that the loop indexes in fields of a struct that a variable holds
  or references, reached by fields alone (`FieldIndexed`). They are
  recorded as (variable, field indices). A reference that the body binds
  only to another variable (`BoundRefs`), such as an inlined callee's
  parameter, is seen rebound at every iteration by a kill summary. Such a
  reference is replaced by that variable, and the place checked against the
  summary is the one under the variable.
* `Binary::specidx` on a `&&` or `||` whose right operand contains an index
  and is made only of operators, variables, fields, elements, casts and
  constants (`ProbeShape`): whether every bounds check in that operand
  holds without what the left operand establishes. Codegen needs this to
  evaluate the operand unconditionally (section 6.11). The judging walk
  probes such an operand once more (`ProbeRight`), from the state after the
  left operand. The probe takes no facts from the left operand, nor from
  any `&&` or `||` nested in the operand, and the flow, the memo of derived
  terms and the slice length are restored afterwards. A probe's judgments
  only count failures, and an index that the types alone keep in bounds
  (`IndexInRangeByType`) is not a failure. The operand is then walked as
  usual, so the program's own elisions are the ones it would have without
  the probe. A nested `&&` probes its own right operand in the state that
  its enclosing operands' facts allow. That result holds where the
  enclosing operator short-circuits; where it does not, codegen asks the
  enclosing operator's probe, which took none of those facts.
* `Binary::nonneg` on a signed `/`, `%` or `>>` whose divisor is provably
  at least 1 and whose left operand is provably non-negative, by its term
  or as a difference `a - b` with `0 <= b <= a` (`JudgeUnsigned`). Codegen
  computes such an operation in unsigned arithmetic, which gives the same
  value, with no zero or overflow check. Release builds compute signed
  arithmetic with wrapping (§6.2), so the C compiler cannot derive such a
  sign from the operations itself.

### 5.11 Verification

`--bce-test` checks `// bce:elide` and `// bce:keep` comments: every check on
such a line must have the annotated outcome. The `test/optimizer/bce*.goose`
fixtures check these decisions and also run as ordinary programs, allowing
output checks to catch incorrect elimination. `--bce-lines` prints the
per-line counts for comparing two builds of the pass; a check in standard
library code inlined into the program's counts on the line of the call
(`Inliner::AtCallSite`). `bench/bce_ab.py`
measures the whole pass against `--no-bce`. `bench/bce_fuzz.py` runs random
programs built from loop shapes the pass targets, with off-by-one
mutations, through the JIT with the pass on and off. An elided check that
should have fired makes the two runs differ. Within a few hundred programs,
the fuzzer finds a former bug in the pass, which read an inlined body with
several returns as the value of its last return.

**Index and bound types.** Every array that an index type names gets a
place in each body from the prescan on (`IndexPlaces`), so every kill of
its length reaches it. A variable of an index type gets the axioms
`0 <= v` and `v <= len(a) - 1` (`<= len(a)` for a bound type) against the
length's *current* generation (`IndexBoundOf` in `DistsFrom`). So does a
one-shot base standing for a value of an index type that is read out of
storage or comes from a call, a branch or a conversion (`IndexTypeTerm`,
`tmplen`). The array never shrinks, so a value below any earlier length is
below the current one, and no bridge facts are involved. A `u64` of an
index type is a base, since it is below a length. So every write of one
kills it, as it does any other integer variable (`IntBaseVar`): an
assignment, a callee's write to it through a reference or as a global, or a
write through a reference that may name it. `JudgeIndexCast` judges a
conversion into an index type as `a[x]` would be judged, sets
`AsCast::nobc`, and states the completed check's facts.

**checkfree.** A kept check belongs to the source text it is written in, as
for `--bce-test`. The source lines that each `checkfree` function spans
(`CheckfreeSpans`) cover every specialization and every inlined copy of the
function, and whatever the optimizer restated the check as. A caller's
argument inlined into the callee's block still belongs to the caller. Each
kept check on such a line is recorded once, with the reason it was kept
(`KeptInCheckfree`, `KeptWhy`): a value read out of storage, a `u64`
variable, a term the domain cannot relate, the lower or the upper bound,
and, for an index of another array, the length relation an `assert` would
state. The driver reports them all as errors (`CheckfreeReport`). Under
`--no-bce` nothing is judged, and the driver warns about that.

### 5.12 Known gaps

Indices loaded from array contents (`dist[q[i]]`) have no known range and
keep their check, unless what is stored has an index type (§3.13). An
analysis of array-content invariants was implemented and measured, then
dropped because it produced no measurable speedup (`bench/adoption.md`). A
`u64` variable is never a base unless it is of an index type, so a plain
`u64` local loses the range its initializer had (TODO 0a). Loop exit
conditions that are disjunctions are not represented (TODO 0f). A value
read out of a field or element stays unproven until re-established (only
variables and lengths are bases, though a narrow type still bounds such a
value). So does a value through a call without a summary, and anything
after a shrink.

---

## 6. Code generation: representation

`CodeGen` (`codegen.h`) emits one C file. The runtime (`src/runtime/`) is
embedded in the compiler (`runtime_inline.h`, regenerated by
`--gen-runtime-header`), and the driver assembles the file around what
codegen emits (§7 says which parts of the runtime go where). The
representation follows Appendix C, with the choices recorded in Appendix E.
The following sections describe the representations and their
implementation.

### 6.1 Values

The C backend targets 64-bit, little-endian hosts with unaligned packed
accesses. Integer and floating widths are their declared widths; `bool`
occupies one byte and valid values encode as 0 or 1. Array metadata is
counted in elements, except the outer serialization frame, which counts
payload bytes (section 6.9). A stored length must represent the actual
count exactly; truncating it would change where subsequent fields start.
Known counts are checked statically; dynamic counts are checked before
storing or patching the length field.

Generated scalar and pointer typedefs carry one-byte alignment on GCC and
Clang. References and slices retain that alignment when a packed field's
address escapes its containing struct; aggregate types use `#pragma pack(1)`.
Their scalar and pointer members use naturally aligned types so C ABI
classification recognizes members crossing an eight-byte boundary, including
on Clang 18; packing still gives every field its byte-exact offset.

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
  aggregate tail padding. A struct with no bytes of its own (no fields, and
  no `pad n`: `EmptyLayout`) occupies one byte, the `gs_empty` byte that C
  gives it. A variant with no bytes of its own occupies none behind its
  ADT's tag and has no member in a fixed-mode ADT's union. Its own C struct
  still has that byte, which is why spec §3.4 keeps its type out of fields
  and elements.
* Padding, inactive ADT payload bytes and unused limited-array slots are
  not value-bearing. Structural equality ignores them. Raw byte images
  can include them, so equal values need not serialize byte-for-byte
  identically, and callers must not infer zeroing from fresh OS pages.

* **Fixed-size types** are packed C types (`#pragma pack(1)`): scalars;
  packed structs; fixed and static-capacity limited arrays wrapped in
  structs (`{ T e[k]; }`, `{ len; T e[k]; }`); fixed-mode ADTs as
  `{ tag; union }`; plain references as pointers; slices as
  `{ data, len }`; and relative references as their stored integer. A
  wrapped array has one slot where `k` is 0, since C has no empty arrays.
  That slot would take bytes the layout does not count, which is why spec
  §3.4 keeps such arrays out of fields and elements. An empty slice's
  `data` is null where the slice was zero-filled (`default<T>()`, a default
  element). C leaves `memcpy` and `memcmp` undefined on a null pointer even
  for zero bytes, so element-run copies and compares go through the
  runtime's `gs_memcpy`, `gs_memmove` and `gs_memeq` (`CopyFn`,
  `GenRangeEq`). `gs_memeq` compares runs of up to 16 bytes inline, as two
  overlapping word loads per side. It compares runs of up to 256 bytes 16
  bytes at a time, with the last 16 overlapping, and calls `memcmp` only for
  longer runs. Keys and names are mostly that short, and a call, with
  `memcmp`'s own dispatch on the length, costs more than the compare itself
  (a fifth of the time, on lookups of strings of about 60 bytes). The longer
  compares are in a separate function (`gs_memeq_long`). Inside `gs_memeq`
  itself, they made it too large for the C compiler to inline at compares
  of a known short length, such as a key against a literal. `gs_memcpy`
  copies the same way: inline up to 256 bytes, and with `memcpy` beyond
  that. String building that appends many runs of a few dozen bytes ran up
  to a fifth faster without the library call. `CT` emits typedefs on first
  use. Struct-like kinds get a forward typedef, so that a node type can
  reference itself (`NameCT`), and get their body at the first use by value
  or through a pointer (`PointeeLv`). A struct that the program reaches only
  through references gets its body at the first access through one. A
  slice of a struct-like element is typedef'd over the element's forward
  name and emits the element's body right after it. So a type holding a
  slice of itself (`enum Op { Loop { body: Op[:] } }`) comes out in order,
  whichever of the two a signature names first. A variant type emits its
  enum; the enum's union holds the variant's struct by value, and so emits
  the struct first. Every program name carries the `_g` suffix
  (`Sanitize`), and a namespaced name also carries the namespace's length.
  A type's mangle names its C type, its per-type functions (`gs_eq_<T>`,
  `gs_size_<T>`, ...) and its queue. The mangle writes the type's parts in
  a row, so some pairs of types come out the same (`E<F..>` and `E<F>..`,
  `E<G.B>` and `E<G>.B`, `A<B>` and a struct `A_g_B`). The first type to
  produce a given mangle keeps it, and a later type with other parts gets a
  suffix, `_m1` and up (`Mangle`, `SameMangle`). So does an instance's
  prefix of tag constants when another instance produced the same prefix
  first (`EnsureTagEnum`).
* **Bytes values** (variable class) are self-describing byte images held as
  a `uint8_t *` to the value's start; a field behind a variable-size field
  is reached by a cursor that walks the intervening sizes (`FieldPtr`,
  `SizeX`, with a generated `gs_size_<T>` walker per dynamic type).
* **Resizable values** are a header in the owning frame (`gs_rhdr { base,
  len }`) with the elements on a data stack. A frame object is instead the C
  struct of the fixed fields, with the tail's header (or nested frame
  object) as its last member (`EmitCFields`). Growth bumps the stack
  top and the count; the base never moves. A frame object type used as the
  tail of a resizable that is not a frame object is stored as bytes of that
  value, like the value's other fields (`FoBytes`). A literal builds it that
  way directly. Anything else builds it as a C frame object whose elements
  are placed behind room left for the fixed fields (`GenFoAsBytes`). A
  whole read loads it into a C frame object that views the elements in
  place (`FoView`). The base is a `uint8_t *`, so an element is generally
  reached at a byte offset, `*(T *)(base + i * size)`. A number of 2, 4 or 8
  bytes is instead indexed through a pointer to its type, `((T *)base)[i]`
  (`IndexLoc`), which keeps the scale in the address. With a byte offset,
  clang folds `(j >> 5) * 4` into `(j >> 3) & -4` and loses the scaled
  addressing mode (nsieve's bit array: 150 against 138 ms).
* **Fat references** (`gs_rref { hdr, stk }`) are references to
  resizable-class values: the header address plus the stack, so a callee can
  push through them. A reference with `reusable`-pool provenance is a
  `gs_pref`, which adds the freelist's header and stack. Whether a parameter
  is fat or pool-shaped comes from the specialization's types and
  `RootArg::reusable`, not from surface syntax. A variable is pool-shaped
  (`PrefVar`) by its binding's bits, which its rebinds keep (§3.14). Each
  binding and rebind stores a whole `gs_pref` (`GenPrefVal`). An `if`,
  `match` or block that chooses among pools builds a `gs_pref` in each
  branch, into a temporary; the construct is generated with that temporary
  as its destination, asking for the pool form (`Dst::pool`).
* String literals are `static const` byte arrays (`StrRaw`), sliced as
  `{ data, len }` or emitted as static `[len][bytes]` images when used as a
  `u8[]` value (`GenStrBytes`).

### 6.2 Stack assignment

Stack assignment uses the hidden-argument strategy that §10.3 permits.
Every function that uses data stacks takes `int64_t gs_sp`, and addresses
its own nonfixed locals and temporaries as `GS(gs_sp + k)` with a
per-function constant `k` (`AllocStk`). It calls callees with
`gs_sp + <indices in use>` (`SpTop`), and asks the runtime once at entry to
have that many stacks. (The thread program reserves all of them as it
starts, `gs_stack_block`, using the compiler's count.) Globals own
dedicated stacks outside the indexed block. Scopes mirror C braces
(`CScope`): every nonfixed local's base pointer doubles as the watermark
restored at scope exit. Every exit path (fallthrough, `break`, `continue`,
`return`, propagation) emits the restores of the scopes it leaves, in
reverse declaration order (`EmitExitRestores`). A `defer` registers its
body in its block's scope beside the restores (`RegisterDefer`), and every
exit emits the body where it meets it in that order: after the restores of
what was declared after it, before those of what was declared before it
(`EmitRestores`, `EmitDeferBody`). The body is emitted once per exit, in a
C block of its own, with its declarations named afresh each time
(`NameScope`); a scope without a `defer` emits as it always did. A
statement gets a scope of
its own (`SC_STMT`), so that a temporary's stack is free again at the next
statement. So does the right operand of `&&` and `||`, whose temporaries
exist only when it runs. A local's allocation skips the statement scopes
inside its block (`AllocStk(forlocal)`), but no further: its index is free
again once the block ends, for the locals and calls that follow. This is
what lets a recursive cycle's functions own scratch (§7.8).

One scope has no braces: the then-block of a flat `if` (§3.9). Its `else`
cannot complete normally, so the code after `if (!c) { else }` runs exactly
where `c` holds. The then-block's contents are emitted there, in the C
block that contains the `if`, and its restores still close its scope. So a
run of guards nests no C blocks, however long it is (MSVC stops at 128
levels, clang at 256). C names are unique within a function (`T`,
`Unique2`), so what the then-block declares clashes with nothing there.

Because a `uint8_t *` store may alias a stack's `top` in C, the tops of the
stacks a function can name are cached in locals where the function grows
them (section 6.10).

### 6.3 Globals and program instances

Every global that is not read-only static data is a member of one
`gs_globals_t` struct per program instance, which also holds the dedicated
stacks of the resizable globals. Main's struct is a static; a worker's is
allocated by its entry thunk and filled from the spawn image (§6.8). Access
is `GS_GL->name`, where `GS_GL` is `(&gs_globals_main)` in a program
without workers, and the thread-local `gs_gl` in a program with them. A
`let` (or `const`) global of a `const` flat fixed type with a compile-time
initializer (`StaticInitX`, up to 256 array elements) is a C `static const`
shared by every instance, which the C compiler folds into the code reading
it. The initializer may name earlier globals of that kind, anywhere a
literal could stand. C cannot read another object in an initializer, so the
named global's own initializer is written out in its place (`gstatic`
keeps them). A `var` of such a type can be assigned as a whole and is a
member like any other. Everything but the statics is initialized by
`gs_init_globals` in declaration order.

### 6.4 The calling convention (C.3)

`SigParams` lists, in order:

* the declared parameters (a by-value resizable adds its `gs_stack *`; a
  pool parameter is a `gs_pref`; a bytes value is a `uint8_t *`);
* the free variables of a nested function or function value (fixed ones by
  pointer, resizables as header pointer plus stack, pools as `gs_pref`);
* result channels in source result order (out-pointers for fixed results
  other than the first fixed result; per nonfixed result, a destination
  `gs_stack *` and, for a resizable, the count out-parameter or frame
  object out-parameter);
* `gs_sp`, when the function touches stacks;
* `const gs_loc *gs_at`, for a standard library function
  (`SpecInfo::located`).

`gs_at` is where the program called the library: each check and `abort` in
the function's own lines reports it (`LocArgs`), and a call to another
library function passes it on, so a failure names the line of the program's
code that the chain of library calls started from. A call from the program's
code passes a static `gs_loc` of its own line (`CallerLoc`); a worker's thunk
passes its function's line. The program's function values that a library
function runs have the program's own lines, and report those. The pointer is
read only on a failing path. A runtime entry point
that reports at the caller's place takes one too (`SFunction::takesloc`: the
slice pool externs). Exports and `main` take none.

The C return value is the first small fixed result eligible for native
return, even when it is not result zero; with none it is `void`. A fixed
aggregate containing relative references (`NeedsResultLocation`) always
uses a typed output pointer. Returns, forwarding and case dispatch pass
that pointer through; a stack receiver claims the fixed slot and supplies
its address (`ConstructCall`). A block gets the same lvalue destination.

Slices, fat references, and a pool's `gs_pref` are passed member by member
(`ParamFields`), whether declared as parameters or passed as captured pools.
Each member becomes its own C parameter: `data` (as `void *`) and `len` for a
slice; `hdr` and `stk` for a fat reference; and `hdr`, `stk`, `fl`, and
`flstk` for a pool.
The body's opening lines put the members back together into a local of the
struct's type, under the parameter's name (`paramcopies`). Every caller
passes each member of its argument's temporary (`PushArg`). In the Windows
x64 convention, a struct of 16 or 32 bytes passed whole is passed by
reference to a copy made by the caller (in AArch64's convention, so is a
struct above 16 bytes). Clang then reloads the struct through that pointer
after every byte store the body makes, since the store may alias the copy:
a slice walk that stores bytes reloads the slice at every iteration. And
where a caller stores the copy's fields one by one and the callee reads the
copy as one vector, to hand it on, that load waits for the stores to
retire. A recursive descent parser that handed its parser state down spent
half its time there, with its parameters copied into locals at entry.
Passed as members, no part of these values is in memory that the body can
reach.

A multi-value binding takes each fixed result directly into its variable
(`EmitCallInto`). The exception is a reference result that the checker
decayed to a copy of its fixed-size pointee (§4.1): that result arrives in a
temporary and is loaded through it, as the destination's type (`Dst::t`)
asks. A resizable or bytes result is built where a declaration of its
variable alone would build it: on a data stack of its own, with a reusable
pool's freelist beside it (`DeclStackLocal`, which `BindLocal` also uses
for declarations). A return that forwards a call's results, to the
function's caller or to a `return from` target's channels, converts each
result that the checker adapted (`GenForward`, §6.5).

`CollectSpecs` computes, per specialization and to a fixpoint over the call
graph, the free variables it or its callees need, the stack-owning globals
it can reach, and whether it needs `gs_sp` at all. A body without `gs_sp`
counts its stack indices from 0, which are the outermost callers' stacks.
So a body needs `gs_sp` wherever it builds anything on a stack. That
includes a node of a nonfixed type, and these cases, which no node's type
shows:

* a call's result built as its own nonfixed type where the checker fitted
  the call to a fixed-size slot (`str()` passed as a slice or returned as a
  `u8[..16]`, `to_bytes()` passed as a slice);
* a payload that a match arm copies by value;
* a variable-mode scrutinee that a dispatch copies for its by-value arms;
* a variable-mode value (including a reference result's pointee) adapted to
  a fixed-mode ADT;
* anything rendered structurally into a limited array.

`EmitSpec` rejects a body that allocates a stack without `gs_sp` as an
internal error. A global initializer's locals belong to no specialization.
They are free variables of every function that names them, and
`gs_init_globals` passes them as a function passes its own.

### 6.5 Destinations and in-place construction

Every value-producing node is emitted against a destination (`Dst`): discard,
a C lvalue, or the top of a data stack together with, for resizable
results, the lvalue that receives the count (or the frame object). `GenAny`
routes a node to its destination. Control constructs recurse, so that every
branch constructs at the same place (§4.3). `GenConstruct` writes a value
front-to-back at a stack top. Calls pass the destination stack down as the
hidden argument, and a call in `v.append(f())`, or in `v.push(f())` for a
variable-size element, builds directly at `v`'s top (`EmitPush`,
`EmitAppend`). Element-run results (§7.3) exist for variable *array*
results: `EnsureEr` compiles a second, `_er` twin of the callee that emits
raw elements plus a count, so that `v.append(f())` is contiguous. A callee
without a twin (a builtin, a dispatch, a `return from` target) delivers the
value form, and the receiver slides the length prefix out with one
`memmove` (`EmitSlidePrefix`). A `T[]` result that ends up in a slot of
another length storage is re-prefixed afterwards (`EmitReprefix`). A
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
An appended literal of variable-size elements is built the same way: its
elements go at `v`'s top and its count is added to the length
(`GenArrayLit`). An appended literal of fixed-size elements holding relative
references is a fixed array laid over the slots it fills, at the top or in
a limited array's free slots, and is built there (`FixedArrayLitAt`), so
each offset is measured from where the element stays. Other fixed-size
elements are evaluated into a C temporary and copied, in the same way that
a pushed element is stored after it is evaluated.

A stack destination also names the slot's type wherever the receiver knows
it: a local's type, a global's, a parameter's, an array literal's element
type, or a temporary's own type. The value is built as that type, whatever
the expression's own type says. A call's variable-array result takes the
slot's length storage, and a reference stores a relative slot's offset
(§3.9). `str()` and `to_bytes()`, whose results are resizable, reserve the
slot's length prefix, write their elements behind it and patch it
(`EmitStr`, `OpenRzDest`), so `[str("a"), str("bc")]` builds each string in
its element. A varint field or element takes the `i64` that every varint
read decodes to (§3.6) from whatever produces it, and writes it
zigzag-encoded (`EmitVarintStore`). The producer can be a control construct
or an inlined body (through a C temporary, `CtlValX`), a returned
`varint&` (through its load, `CallVal0`, `GenXD`), or a fill value (once).
A call reached through `GenAny`, as a branch's value, is constructed by
`GenConstruct` like any other. A reference's pointee that meets a value
slot (a returned reference whose value is taken, or the reference an
inlined body leaves in the slot) is built as the slot's type from where it
lies (`ConstructFromLoc`). For example, a `u8[]` pointee takes a
`u8[varint]` element's prefix, or no prefix in an element run.
`v.append(copy(x))` appends `x` itself, since the run copies `x`'s elements
anyway. The `_er` twins are compiled last, after the globals' initializers,
which can also ask for one.

**Pushes** (`EmitPush`). The receiver is evaluated, then the argument, and
then the element is added (§2). The argument may itself grow the array
(`v.push(v.push(1))`, `v.push(f(v))`). So a fixed-size element is evaluated
before its slot is claimed: the element then follows whatever the argument
pushed, and the returned reference names it. For an element that is itself
a reference, the push instead returns the reference the element holds
(loaded from the slot if it is relative), as `alloc_ref`'s result does
(`NewElemResult`), since there are no references to references (§3.8). A
variable-size element, and a fixed-size one holding relative references of
either form, are instead built in the slot, for limited arrays too; the
checker's growth rule (§3.10) is what keeps this safe. (A self-relative
offset is measured from where the element is stored, and an `in pool`
`self` is the element's position in the pool.) A relative-reference element
is the plain reference it is given, evaluated first, and stores the offset
from its slot. A pool allocation evaluates or builds its element the same
way (`EmitAlloc`; both go through `ElemValue` and, at a slot named by a
pointer, `StoreElemAt`). A slot from the freelist is taken before the code
that builds an element there runs, and a fresh slot at the top is counted
only once the element is written. So nothing that the element's
initializers call can read it half-built.

**Adaptation into limited arrays.** `FitsAt` lets any array or slice of the
element type construct a `T[..k]` (§4.2). Codegen copies into the C value
(`AdaptToFixed`, with the capacity check) from whatever representation the
source has:

* a variable or field, through `LoadLoc`;
* a slice expression, in `SliceExpr::CgX`;
* a call result, through `CallVal0` (`CallResLoc`: the temporary header of
  a resizable result, the base pointer of a variable one, the pointee of a
  reference result);
* a node in another representation than its context wants, through `GenXD`
  (a `copy` source, a spliced callee body);
* a stack slot, through `GenArrayFromLoc`.

Where the limited array is already a C lvalue (an assignment's target, a
local's initialization, any fixed destination: `LeafAny`), the elements go
directly into its slots instead of through a temporary of its type
(`GenIntoLimited`, `CopyIntoLimited`). The source's count and elements are
read before anything is written, and they are moved as `memmove` moves
them, since the source may be a view of the destination's own slots. A
bytes-class call result feeding a fixed-class slot is built on a temporary
of its own rather than on the slot's stack (`GenConstruct`), since the slot
takes the adapted C value. A runtime-capacity `T[..]` is a bytes value, and
an assignment into one copies out of a value of that layout (`GenPtr`). A
slice or an array of another kind is first built as such a value on a
temporary (`ConstructFromLoc`), so the source is read in full before the
destination changes, even when the source is a view into the destination
itself.

**Adaptation into variable-size arrays.** Any array or slice of the element
type also constructs a variable, resizable or runtime-capacity limited
array (§4.2). A call's fixed-size array or slice result is a C value, and
it is constructed as the slot's type from where it lies (`ConstructCall`,
`ConstructFromLoc`), as a returned reference's pointee is. A variable-size
result is handed the slot only where the call's convention delivers it in
the slot's layout (`CallBuildsAt`). That is the case for:

* the result's own layout, for the same type;
* a variable array's elements, as a run or behind the slot's length prefix
  (`EmitReprefix`);
* a resizable function result, copied from behind a header of its own;
* a builtin's resizable result, behind a variable array's prefix
  (`EmitStr`, `OpenRzDest`).

Any other result is built on a temporary of its own and constructed from
there: a runtime-capacity limited array, or a variable array or `str()` for
a runtime-capacity limited slot. An inlined callee's `return` builds its
value in the caller's slot, typed as the callee's result. The inliner
replaces a call whose body only returns a value with that value.
`GenConstruct` builds an array that is not fixed-size (an array literal
included, `GenArrayLit`) in the slot, as the slot's type. It takes a
fixed-size array or slice as its own type first, as a C value, so that a
limited array's capacity is checked as the callee's return checks it. It
then constructs the slot's type from that C value. A `[..cap]` literal that
takes another type is empty. It is built as its own type on a temporary,
which checks the capacity's range, and then copied.

**Adaptation into an ADT.** `FitsAt` also lets a variant construct its ADT,
and an ADT construct itself in its other mode (§3.5). The node then carries
the ADT's type, and codegen finds the type that the value arrives as where
the value is produced. A variable, field or element converts where it is
read: into a fixed-mode ADT as a C value tagged with the variant
(`LoadLoc`, `AdaptToFixed`), and into a variable-mode ADT as the tag
followed by the value's bytes (`GenVarEnumFromLoc`). A call's result
(including a builtin's element result) and an inlined body arrive as the
callee's own type (`AdtFrom`). `GenAdtAdapted` builds a variant headed for
a variable-mode slot in place behind its tag, and builds anything else into
a temporary that it then converts. For this, the inliner keeps the
`InlineBlock` of a trivial body whose ADT result changes mode, as it does
for a decayed reference. Consider a multi-value call forwarded by `return`
or `return from`, whose values the checker fits to the function's return
types one by one. It delivers each value of another type into a temporary
of the callee's type, which is then converted into the return channel
(`GenForward`). The exception is a variable-size or resizable variant,
which is built in the channel behind its variable-mode ADT's tag, as
`GenAdtAdapted` builds one (`VariantBehindTag`).

**Named results** (`DetectNrvo`, `OpenIbNrvo`). When the returns of a
nonfixed result hand back one top-level local and no other local
(`NamedResultOf`, `clone.h`), that local is allocated at the return
destination from its declaration on, and its return writes only the count.
A return of any other value (`[]`, `str(...)`, a parameter, a call's
result) keeps the local there, provided that building that value cannot
reach the local (`MayReachLocal`: the value names neither the local nor a
variable whose provenance may lead into it, and the local is not captured).
Such a value is an exit that finds the local in front of it (see **Exits**
below). The local counts as a construction open on the destination from its
declaration on, so the value is built behind the local's elements and moved
down over them. An inlined body's own scope is entered as if the local were
not open yet. A return may name a top-level local while another return
keeps that local off the destination (another local, a value that may reach
it, a forwarded multi-value call). The checker then warns at that return
once the body is checked (`NamedResultCopyWarning`), since the copy this
causes does not show anywhere in the source. A resizable local returned as
a variable array reserves the destination's length prefix ahead of its
elements and patches it at the return (`EmitPrefixPatch`). The elements are
moved up only when a varint prefix outgrows its one reserved byte. The same
binding is made for an *inlined* callee's named result that reaches a
construction context, which is what keeps §7.3's guarantee for the small
builders the optimizer inlines. A function that is the target of a
`return from` binds its named result too: a value returned to it from
below is moved over the named result at the catch (§6.6).

**Exits** (`ExitStart`, `LandValue`). A `return`, a `break` with a value,
and a return leaving an inlined body all build their value at the top of
the stack they deliver it to. The receiver, however, expects the value
where that top was when the function, inlined body or loop was entered. An
exit can be taken while a construction on that stack is under way: inside
an element of a literal headed there, a `str()` argument, or an inlined
callee whose named result is bound there. Such an exit would leave the
partly built construction in front of its value. So codegen counts the
constructions open per stack (`openat`: `GenConstruct` for anything but a
control construct, and a named result from its declaration on). An exit
that finds more of them open than its scope was entered with takes the top
before building its value. Afterwards it moves the value down to the
scope's top on entry, and the stack's top and a frame object's tail base
follow the value. That entry top is declared where the scope begins, once
an exit needs it (`ScopeTop0`, `DstTop0`). An exit at statement level emits
nothing more.

### 6.6 Calls, dispatch, function values, `return from`

`EmitSpecCall` builds the argument list by the convention above, and flushes
and reloads the cached tops the callee can reach (`SyncReach`). Where the
callee can propagate a long-distance return, it checks the long-distance
discriminant afterwards (`EmitRfCheck`). Tag dispatch (`EmitDispatch`)
evaluates every argument once, in source order, and passes the arguments
the cases share by the convention an ordinary call uses (`EmitArg`). It
snapshots a by-value scrutinee before later arguments run, and switches on
the tag, with one call per arm sharing the argument temporaries and return
channels. The arms have no element-run form. A variable-array result
arrives in the arms' layout, and is slid out of its prefix for a run
receiver, or re-prefixed once after the switch for a slot of another length
storage.

A function value is spliced inline (`EmitFvCall`): its parameters and
locals are ordinary locals of the enclosing function, and anything declared
in its body takes them as free variables like any other. The call is
checked against its destination, and the body's value is checked without
one, so the two can be different array or slice types: an array where a
slice is expected (spec §3.10), or any array or slice constructing an array
of another kind (spec §4.2). The body then builds its value as its own type
in a temporary. That temporary is sliced whole or copied as the type the
destination names, or as the call's type where the destination names none.
The call's value is a copy (spec §4.1), so such a slice never points into a
variable the body names.

An `extern fn` is a direct C call, with prototypes emitted for called
symbols that the runtime does not define. A member of an
`extern "lib" { ... }` block (`SFunction::dylib`, spec §7.10) is a call
through a static function pointer `gs_dl<N>_<symbol>`, where N is its place
among the used externs; the pointer is declared in the prototypes' place.
After the prototypes come two tables:

* `gs_dllibs[]`: the libraries that the used members bind from, in the
  order of their first declaration, each with the sidecars' three platform
  names (`DlSpelling`: per platform, the program's name takes precedence
  over the package's, and 0 means the runtime derives one);
* `gs_dlbinds[]`: a row per used member.

`gs_program_init` hands both tables to `gs_dl_bind` right after
`gs_rt_init`, before the queues and globals. Nothing is emitted for a
program with no such member. Codegen emits the types of every checked
extern signature, including unused ones and the complete pointee types of
plain references, before user headers. A reusable `--include` adapter can
therefore use its interface's types independently of which functions the
program calls.

**The foreign boundary.** `CheckExternSpec` admits scalars and flat
fixed-size values by value, including fixed-mode ADTs and limited arrays
of static capacity. Parameters additionally admit plain non-optional
references and slices of those values, and builders (`T[>..]&`) of them.
Results are at most one by-value scalar or flat fixed-size value:
references, slices and builders cannot be returned by an extern. A builder
argument uses `gs_rref { hdr, stk }`, whatever its elements; C may write
its elements and append whole ones at the stack top (`gs_bld_append` for
bytes) but may not shrink it, retain borrowed arguments beyond the call, or
invalidate Goose's types, roots or lengths. These are obligations on C, not
proofs the Goose compiler performs. Effect analysis assumes C may write
passed storage and append to passed builders, but does not mutate unrelated
Goose globals. The builtin runtime shims obey the same boundary.

`return ... from` uses one thread-local `int32_t gs_rf` (zero except between
a long-distance return and its catch), plus per-target thread-local
channels: channels for in-flight fixed values (`gs_lret_<id>_<i>`), and
destination stacks for nonfixed ones (`gs_fdst_<id>_<i>`, recorded at the
target's entry and restored at its exit). A propagating function's ordinary
exit writes nothing. A call on a propagation path costs one load and a
never-taken branch, and an intermediate frame returns a dummy value after
restoring its watermarks and running its `defer` bodies (§6.6), which the
checker keeps free of this machinery (§3.16). `--unsafe-no-rf-check` omits
the checks, for measurement only.
A fixed relative aggregate's `gs_lret` channel is a pointer to the target's
receiving storage, saved and restored at target entry/exit. The nonlocal
return constructs through it, and the catch has no value to copy.
A nonfixed value is built at its channel's top, which the return records
first (`gs_fval_<id>_<i>`). The calls it unwinds may have built things
there, in the destination that the target handed them: a named result, part
of a value, or a length prefix claimed for a callee's elements. So the
target's catch moves the value down to its destination's top on entry, as
an exit does (`LandValue`). A call's result is fixed up (`EmitReprefix`,
`EmitSlidePrefix`) only after the check, since a value in flight may lie
behind the prefix the fixup moves.

### 6.7 Relative references, pools, literals

A relative load is the field's own address (or, `in pool`, the pool's base
minus one) plus the stored offset, with a null test only for the optional
form (`LoadLoc`, `RelOrigin`). A store subtracts the same origin
(`EmitRelStoreAt`). Into an optional slot, the store tests the value for
null unless the value is a plain `T&`: a node of that type, a call declared
to return one, or a block or `if` all of whose values are one
(`RelValue`). Into a self-relative optional slot, the store aborts on a
non-null target at offset zero (§3.9), unless no target can be at the
slot's own address. That exception applies when the slot is a field at a nonzero
offset in a fixed
struct and does not point at a reference. A target at the slot's address would
have to be a part of the struct starting there, and the slot itself is the
only such part. `RelSlotApart` establishes this from the assignment's field
path or the literal's layout.

Whatever produces the value, the store encodes a plain reference. A
branch's or an inlined body's value for a relative slot is computed into a
plain-reference temporary (`CtlValX`). A call's result is encoded where it
is stored (`ConstructCall`). A frame object literal stores an `in pool`
field, a C member of the object, through the same helper
(`GenFrameObjLit`). A local, global or parameter declared with a relative
type is such a slot too. Its initializer or argument is encoded into it
(`BindLocal`, `EmitGlobalInit`, `EmitArg`), at varint width on a data stack
as any variable-size value is (`GenConstruct`). A `var` global without an
initializer starts as `null` (`DefaultValue`). An identity comparison
(`.==`) reads the variable as the address it encodes, as every other read
does. A call's value is plain too, since `default<T>()` of a relative
reference is the null pointer. So where `default<Rel[N]>()` fills its
temporary's elements with that null pointer, the lvalue store encodes it
(`Call::CgAny`). The store's range check is emitted only where a root can
exceed the width: for `in pool` under `#if GS_STACK_RESERVE >= 2^bits`, and
for self-relative under `#if GS_STACK_RESERVE > 2^(bits-1)` when no
fixed-size root in the program is wider than the width (`relrootmax`). So a
`u32` link on the default 2 GB reservation is stored unchecked. A pool's
base is loaded once per function into a local (`PoolBase`), and element
access through a pool global reads that local too. `self` is stored as
minus the field's own offset (self-relative), or as the value's own pool
offset (`in pool`, only where the literal is built inside the pool). `pop`
on an array of relative references loads the element it removes like any
other load, from the slot it leaves. `resize(n, ref)` evaluates the fill
once as a plain pointer and encodes an offset at each new slot using the
same store helper, including its null and range checks. Aggregate fills
containing self-relative references remain unsupported.

A `reusable[]` pool's operations (`EmitSlicePool`) evaluate the receiver,
then the slice, then the length (`SliceLen`, which aborts on a length that
is negative or too large to hold). They turn a non-empty slice back into an
index. Where the checker placed the slice in the pool, this is an exact
divide. Otherwise (`Call::poolcheck`), the operation first tests that the
slice lies inside the element region on an element boundary, and aborts
with `GS_E_POOLSLICE` if it does not. The test uses `uintptr_t` arithmetic,
so that a slice of other storage compares without undefined pointer
arithmetic. An empty slice takes index 0. The freelist is the runtime's
(§7): its base, count and top are passed to the `gs_spans_*` call by
address, so a cached top works unchanged. A slice that ends the element
region grows without a call. Growth of the element region is emitted here
(`EmitSliceExtend`: count and top, as for a push), and so are the default
values (`EmitDefaultElems`: one `memset`, or the checked default
construction per element for a type with field defaults). A move is one
`memmove`. A pool's freelist counts 8-byte slot indices for a slot pool and
`GS_SPAN_NODE`-byte tree nodes for a slice pool (`FlEntrySize`); this is
what the thread-spawn image copies.

A fixed struct or array literal that holds relative references is built at
its final address, never in a C temporary that is then copied (`FixedLitAt`,
`FixedLitAtStk`). A literal with unused limited array slots is
zero-initialized first, and every aggregate-typed C local is hoisted to the
function's opening brace (`HoistAggregateDecls`). Both are workarounds for
MSVC miscompilations described in Appendix E. A zero-length array's literal
is zero-initialized too (`HasUninitSlots`): nothing writes the one slot its
C struct has, and copying a C object that nothing wrote is undefined. For
the same reason, a struct or variant literal without fields zeroes its C
object (`StructLitAt`): its `gs_empty` byte or its pads.

A string literal read as a `u8[]` value, of any length storage, is an image
in static data (`GenStrBytes`). A string literal read as a runtime-capacity
`u8[..]` is built on a temporary as any other value is (`GenConstruct`),
which gives it the capacity a literal has: its length.

### 6.8 Threads and queues

`thread_spawn` packs the flat arguments contiguously on a scratch stack,
followed by the worker program's globals as `[size][image]` records (a
resizable's image is its count, its fixed fields and its tail's elements;
a pool's image also carries its freelist). It then hands the packet to the
runtime. The generated thunk (`EnsureThreadThunk`) unpacks the arguments
onto fresh stacks, allocates the worker's `gs_globals_t`, fills it from the
image, and runs the body. The runtime frees the instance
(`gs_thread_release`). `thread_spawn_contained` is the same spawn with the
runtime's contained flag set (§7). `thread_wait` is emitted into a
temporary, so a discarded answer still waits. `thread_wait(id, msg)`
appends the message the runtime hands over to `msg`, as `to_bytes(a, out)`
appends its image (`AppendBytes`), and then frees it (`gs_free_msg`).
Queues are one `gs_queue` per element type, carrying one contiguous image
per value; `main` creates them (`gs_qinit`) before anything else runs. A
missed `qpoll` yields the all-zero image (`MinBytes`). A queue is a linked
list of `malloc`'d nodes under a mutex, with a condition variable for its
receivers, and a count of its messages. `gs_qput` allocates and copies
before it takes the lock, and checks the queue's limit under the lock; a
refusal frees the node. A timed `qget` (`gs_qwait`) waits for a deadline on
a monotonic clock, and a miss yields the zero image, as a missed `qpoll`
does. On POSIX the condition variable's clock is set to `CLOCK_MONOTONIC`,
on macOS the wait is for a relative time
(`pthread_cond_timedwait_relative_np`), and on Windows the runtime reads
`GetTickCount64`. A value of no bytes (spec §3.4) is sent through a queue
or a thread's arguments as no bytes, and the receiver zeroes the object C
gives it rather than reading past the image.

### 6.9 Generated walkers

Codegen generates these per type, on demand:

* `gs_size_<T>`: the byte size of a dynamic value.
* `gs_eq_<T>`: structural equality. It is a `memcmp` for gap-free fixed
  types of more than four scalars, and for canonical-encoding bytes values
  that hold no floats, pads or runtime-capacity limited arrays. Otherwise
  it is a cursor walk, which steps over pads and over a limited array's
  slots past its length. A fixed value of at most four integer or bool
  scalars compares field by field, through its `gs_eq_<T>`, which the C
  compiler inlines. Through `memcmp`, an operand just built in registers
  would be stored field by field and read back in one wider load, which
  store forwarding cannot serve. A static-capacity limited array of such
  elements compares its lengths first. Then, for up to 64 bytes of slots,
  it compares whole 8-byte words of both values' own storage with the
  bytes past the length masked off, which takes no branch on the data. A
  last word that would end past the slots ends at the value's end instead,
  reaching back into slots already compared or into the length, which are
  equal on both sides by then.
* `gs_verify_<T>`: the `from_bytes` verifier of
  `docs/design/serialization.md`. It is one framing pass for fixed
  elements, and for variable elements a framing pass that sets an
  element-start bitmap, followed by a link pass.
* The tag enums.

Text rendering (`codegen_render.h`) formats scalars through `gs_fmt_*`,
copies `u8` bytes, and renders everything else structurally into a
`u8[>..]` builder or through the user `format` specialization recorded on
the call. `print` renders the whole line into a temporary builder before
writing it, so lines from different threads never interleave. A struct,
variant or ADT that is met again inside its own rendering (a type that
reaches itself through references or slices) is rendered from there by
`gs_render_<T>`, one function per type (`RenderFn`). This function takes
the builder and a reference to the value, as an overload taking the value
by reference does. It calls itself, or the functions of the other types of
a mutual recursion, for each level below. No overload renders a part of
such a type (§3.7), so none of the rendering depends on the call. The
function takes no stack index either: the builder's stack is the only one
it writes, it opens no stacks of its own, and since it runs no overload it
hands no stack on. So the overloads a rendering runs are all called by the
function that prints, above its live stacks (`EmitUserFormat`). Some
overload parameters are keyed to a reusable pool by the overload's
specialization (`IsPoolParam`), such as the value rendered or the builder a
`format` writes into. Such a parameter is handed the pool's `gs_pref`
(`PrefArg`), as a call's argument for such a parameter is (`EmitArg`). A
resizable tail with no header of its own is rendered in place one level
more, up to the reference to the next.

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
detection: a cyclic value recurses through its render functions until the
native stack runs out (§7, **Native stacks**). An integer's text comes
from the runtime's digit loop (`gs_fmt_i64`, `gs_fmt_u64`): the digit
count, then the digits from the right, two per division by 100. printf's
format parsing and locale handling take several times as long as this
conversion. Finite float formatting (`gs_fmt_f64`, `gs_fmt_f32`) writes the
fewest significant decimal digits that read back as the same value of the
float's own type. Of those, it writes the ones nearest the value, with a
tie going to the even digit. This is what Python's `repr` and Ryu give for
a double, and the same rule applied to an `f32`, so an `f32` 0.1 prints as
`0.1` rather than as the digits of the `f64` it widens to. The digits come
from Burger and Dybvig's free-format algorithm in integer arithmetic
(naturals of at most 36 32-bit limbs) rather than from the C library, so
every backend prints the same text. msvcrt, which TinyCC links on Windows,
rounds a `printf` tie away from zero where other libraries round it to
even. And no search over correctly rounded `printf` outputs finds a power
of two's shortest text where that text lies above the value (section 11).
The digits are laid out as C's `%g` lays them out at a precision of
max(15, digit count). That is in exponent form, with at least two exponent
digits, below 1e-4 or from that power of ten up (`1e-05`, `5e-324`,
`1e+15`, `1.7976931348623157e+308`), and positionally otherwise
(`123456789012345.6`, `9007199254740992.0`), with `.0` after a whole number
(`1.0`, `2147483600.0`). The compiler's own text for a double (`CatOne`),
which C float literals and dumps use, is the same text, with its digits
from `std::to_chars`. Infinities and NaNs are written by the runtime
(`inf`, `-inf`, `nan`) rather than by the C library, which differs between
backends: msvcrt writes `1.#INF` and `-1.#IND`, and others print a NaN's
sign.

**Output.** `print` renders its whole line, including the newline, into a
statement-stack builder and hands it to one `fwrite` (`gs_out_bytes`) on an
unbuffered stdout. So nothing printed is lost to an abort, and no other
thread's line ends up inside it. On Windows the runtime puts stdin, stdout
and stderr in binary mode as it starts (`gs_rt_start`). A newline is then
written as the one byte it is everywhere else, not as the `\r\n` that the
CRT's text mode writes a byte at a time. And `read_stdin` gets the bytes
that came in; text mode would hand them over with each `\r\n` turned into
`\n`, and only up to the first Ctrl-Z. (`read_line` removes a `\r` before
the newline itself.)

### 6.10 Loop-invariant views and stack-top caching

**Views.** An array reached through a reference keeps its base and length
behind that reference, and the C backend reloads both at every access since
any byte store may alias the header. For each reference variable BCE named
in a loop's `hoistrefs`, the view is read into locals before the loop
(`AddView`, `ViewScope`), and every access inside the loop uses them. The
length lvalue that growth writes stays on the real header, so a missed
growth could never miscompile. An array in a field named in `hoistfields`
is read out the same way (`AddFieldView`), keyed by the variable that an
alias stands for (`FieldRoot`) and the field indices. Every field path in
the loop with that key reads the locals (`UseFieldView`), whichever alias
it is written through. For a `for` over an array with `fixedlen`, the
elements pointer and length are likewise read once when the length is a
memory load.

**Tops.** Where the function grows a stack that it can name, it keeps that
stack's top in a local. The local is synchronized with memory only where
something else can observe it: it is flushed before and reloaded after
calls that can reach the stack, and flushed at every exit. What a call
reaches (`SyncReach`) is the argument text, the callee's globals' stacks,
and the stack each reference argument carries, where the argument's
expression says which stack that is (`HandedStacks`). That is the case for
a reference taken of a variable or of a frame object's tail in one, or one
held by a parameter, an alias or a captured reference. A call reaches
everything ("*") when the callee is handed a value holding a reference (in
a field, a payload, an element, or the array a reference refers to), or
captures a reference or a pool. It also reaches everything when a cached
stack may be a global's or the destination of a `return ... from` target
up the chain, and the callee names a global or can propagate such a
return. A callee with no stack among its arguments and none of its own,
such as a pure helper, reaches nothing and needs no sync. The local is
confined to the loops that grow the stack (`PlanTopCaches`). For growth
outside every loop, it is confined to the innermost block around the
growth, which is the whole body for growth at the body's top level. So a
stack grown in one branch of a long function does not keep a local live,
synced at every call, through the rest of the function. The local is
materialized by a text pass over the finished body, which resolves markers
for region edges, syncs, and jumps out of a region (`ExpandTopMarkers`).

Soundness requires that each cached stack is named in only one way, and a
fat reference held anywhere could break that: its `.stk` names a stack that
the body may also name directly. So which stacks are cached is decided
class by class (`PlanTopClasses`). Each class is cached only where nothing
the body holds can be a second name for one of its stacks:

* the function's own indexed stacks, `GS(gs_sp + k)`, which no caller
  expression can name. They are not cached if the body holds a fat
  reference that could be rooted at one of its own locals: one in a local
  variable that is not an alias (below), one read out of a field or an
  element, or one that a call returns. A reference taken of a variable and
  handed on, to a call or into a value passed to one, does not count, since
  the call syncs;
* the globals' dedicated stacks. These also require that there is no
  fat-reference or pool parameter and no captured reference, since either
  may be rooted at a global;
* a nested function's captured resizables' stacks (`<fv>_stk`). These have
  the same requirement, and also require that there is no `return ... from`
  in the body: a captured variable may be its function's named result,
  built at that function's destination, which is where such a return builds
  its value;
* the return destinations, `gs_dst<i>`, which a caller may hand over as a
  global's stack, a parameter's or a captured variable's. These are cached
  only where the body names none of those, returns to no gs_fdst channel,
  and is not a `return from` target;
* the reference parameters' `.stk`, in the **reference-parameter mode**
  (`RefTopsOk`). This mode requires that every fat reference in the body is
  a parameter (or an alias of one) named directly, or one taken of a
  variable and handed on. The fat parameters' root classes must be distinct
  and exact (and concrete when there are several). There must be no
  by-value resizable parameter, no nonfixed return destination, no
  `return from` channel, no captured resizable, and no global whose type
  could be one of those parameters' pointees.

The function's own stacks never overlap with another class, and no two of
the other classes that could share a stack are cached together.

**Aliases.** Some fat reference variables are not C variables at all
(`refalias`, `aliaspath`, `FindRefAliases`). Such an alias is a fat
reference variable that cannot be rebound and is bound to one of:

* a resizable variable (`&v`);
* a frame object's tail reached from a resizable variable by fields
  (`&v.f.g`; not through a reference field, which can be rebound);
* a parameter, a captured reference or another alias (an inlined callee's
  `A&` parameter, a base-case inlining's copy of a reference argument, a
  function value's reference parameter).

Every use of an alias reads what it stands for, its pointee being that
location (`VarLoc`, `DerefLoc`), and a use as a value makes the `gs_rref`
on the spot. So the stack and the header are still named in only one way,
and a local's header does not escape through a pointer that nothing needs.
An inlined `push_n` or heap operation on a local is therefore cached as a
push on the local itself is. An alias has a view of its own where BCE named
it in a loop, never its target's view.

The same forwarding applies to immutable root references: an alias names an
owner's existing descriptor or an immutable parameter/captured reference. This
keeps each component's stack and header spelling stable when library helpers
are inlined into an out-of-line caller. Root parameters still need distinct,
exact root classes to cache their components; mixed owner/component parameters,
possibly aliasing globals, mutable receivers and references read from storage
retain the conservative fallback. Calls that receive a root synchronize the
component caches. No singleton-owner assumption or non-aliasing ABI promise is
made. An alias initialized from a rebindable reference must keep its sampled
value and is not forwarded to that reference.

**Counts.** A push bumps the count too. Where the header is reached through
memory (a reference parameter's `p.hdr->len`, a global's, a captured
array's), a byte store may alias the count as it may alias the top. So
where the stack is cached, so is the count (`LenSlot`, noted at the growth
sites, `NoteLen`), over the same extent and synchronized at the same
points. The expansion replaces the count's single name with the local. It
keeps a slot only where the header is mentioned nowhere else in the extent,
other than as its base or between a call's flush and reload; where it finds
another mention, it expands again without that slot. A frame object's tail
is not cached this way, since a whole copy of the object reads its count
too. Neither is a local's header, which the C compiler keeps in registers
itself.

**Constructions.** Some values of several fields are built in place at the
top of a stack that no class caches, such as a pushed literal of
variable-size elements (or of elements holding relative references)
through a captured reference. Their fields are still written through a
local, and the top is stored once after them (`MarkConsBegin`,
`ConsRegion`). Nothing may grow or shrink that stack while the value is
under construction (§3.10), so nothing else that names the stack moves its
top meanwhile, and every call among the field initializers syncs the top,
whatever the call reaches.

**Fills and stores.** `resize(n, v)` of a resizable fills the new slots
through a cursor of its own and stores the top once. Where every
value-bearing byte of a literal fill is the same, the fill is one `memset`
(`UniformFillByte`). A pushed element that is an aggregate of 2, 4 or 8
bytes is stored as one integer of that width (`StoreWhole`). If it were
stored field by field, a load of the whole element soon after (a heap's
sift, a pop) could not be forwarded from the narrower stores. A cached top
exposes that stall, since the load's address is ready at once.

**Divisors.** An unsigned `/` or `%` by a divisor that the loop cannot
change multiplies instead of dividing (`LoopDivisible`, `HoistDivisors`,
`FixedDivX`), and so does a signed one that `Binary::nonneg` marks (§5.10).
So does an unsigned `/=` or `%=` (`AsDivision`). A signed `/=` or `%=`
divides, since BCE judges no compound assignment's operands. The divisor
must be an integer variable, a field of a fixed-size struct variable
(through any number of struct fields), or an integer conversion of either
(`DivisorKey`: `x / (t.total as u64)`). Its variable must be bound before
the loop, not inside it or by the loop itself, and nothing may be able to
write it while the loop runs:

* the loop, including its condition, writes no part of it (tail-recursion
  elimination assigns a function's parameters in the loop it makes);
* no reference to it is made anywhere in the function (`RefdLocals`: a
  `&`, written or inserted where an lvalue binds by reference, a `.=`, a
  by-reference `for` or `match` binding, a function value's reference
  parameter, an argument that a user `format` overload gets);
* no other function's code reaches it, as it would if the variable were
  captured or a named result built at its destination.

A `let` is as writable through a reference as a `var` (§4.4), so being a
`let` does not settle this. A global, which any function may write, and a
variable of global initialization code qualify only as an integer `let`
that no writable reference is bound to (`VarDef::refwrite`).

Ahead of the outermost loop that the divisor is fixed for, `gs_divu_gen`
computes libdivide's magic number and shift for it once (one 128-by-64-bit
division). Each division is then the high half of a product, adjusted and
shifted (`gs_divu_q`); a remainder is the dividend minus the quotient times
the divisor. Operands of 32 bits or fewer (`NarrowDivision`) use a 32-bit
magic number instead (`gs_divu32_gen`, one 64-by-32-bit division, and
`gs_divu32_q`). They get their own magic number even where a 64-bit
division by the same divisor shares the loop (`DivisionKey`). Each such
division is then the high half of a 32-by-32-bit product, which a C
compiler vectorizes; x86-64 has the 64-by-64-bit product only as a scalar
instruction. A loop summing `u32` remainders runs about seven times as fast
with AVX-512, and a loop reducing a million `u32`s in place by `x %= m`
runs 4.3 times as fast at x86-64's baseline (clang-cl 22).

A zero divisor gets `GS_DIVU_NONE`, under which every division uses the
plain operator and its zero check, so the abort happens where and when a
division by zero runs. `GS_DIVU_NONE` is also used for a divisor whose
conversion would fail (the divisor is computed ahead of the loop without
the conversion's check, `DivisorValue`), and for a 64-bit magic number
under a C compiler without 128-bit products (TinyCC). Where a zero divisor
is the only case that can be left without a 32-bit magic number
(`DivMagic::zeronone`: no conversion is checked), the division instead
fails outright under `GS_DIVU_NONE`. gcc copies that test of a
loop-invariant value out of the loop. The plain division's own zero test
would leave a loop exit, after which the loop's stores might not run, and
gcc holds such stores in registers, as vectorizing needs, only where they
always run. Computing the magic number costs about one division, so it is
worth it from a loop's second division on.

### 6.11 `&&` and `||`

A `&&` or `||` whose value only decides a branch (the condition of an `if`,
a `guard`, a `while` or an `assert`) compiles to a chain of conditional
branches on its operands (`CodeGen::GenBranch`). This is the shape a C
compiler gives its own operators: `while i < n && y & (1 << i) != 0 { i++; }`
becomes `if (!(i < n)) goto brk;`, then the right operand's statements,
then `if (!(<right>)) goto brk;`. A `&&` branches to the target for false
as soon as an operand is false, and a `||` branches to the target for true
as soon as an operand is true. An operator nested in an operator of the
other kind branches past its own right operand to a label of its own, and a
`!` swaps the targets. An inlined call of a `bool` function in such a
condition branches too (`CodeGen::BranchCall`). Its `return`s and the end
of its body branch on the value they give, and a constant value either
jumps or does nothing, as a C compiler finds the C once it has inlined it.
For example, `if plain(key)`, over a body that returns false from inside a
loop, jumps from that loop to the else-arm. An `if` with an `else` branches
to the else-arm, which the then-arm jumps over. A guard, whose else cannot
complete normally, branches past the else to the then-contents that follow.
Each operand at a leaf of the chain is evaluated in a scope of its own.
Where the operand builds temporaries on data stacks, its truth value is
taken into a `uint8_t` and their restores follow at once, ahead of its
branch, so neither path skips them. A branch out of an inlined call
restores what its scopes and its arguments hold on the way, as its
`return` does.

As a value (stored, passed or returned), the left operand is stored in a
`uint8_t` temporary, and the right operand is emitted inside an `if` on it,
short-circuit style, setting the temporary. In a condition, that flag (or
an inlined call's result held for the branch) is a value that the C
compiler may or may not see through, and clang's choices went both ways on
it. plb2's nqueen, whose inner loop is the `while` above, ran 4-8% slower
under clang 20 and 22 with the flag than with the chain; the chain is as
fast as the loop written with a `break`. Over the LangArena port
(`bench/kostya`), chains for `&&` and `||` alone left the suite's overall
speed unchanged. A CSV parser got 11-15% faster and a Huffman encoder
12-17% faster, but a JSON writer got 7% slower and a word counter 9-14%
slower, in both cases where a small `bool` function's result decided the
branch. With those calls branching as well, the suite ran 1.5% faster in
geomean. The JSON writer was level, and the word counter 5% slower, where
clang now reloads a 64-bit constant inside the loop it tests whitespace
in. The `bench/run_bench.py` programs whose C changed ran 1.5% faster in
geomean under v145 and clang, `lru` and `calc` 7% faster under v145, and
none slower.

In both forms, the right operand may also be evaluated unconditionally.
This happens where the operator is in the body of a loop that can be
straight-line code, and evaluating the right operand can neither fail nor
have an effect, whatever the left operand gave (`CodeGen::Speculatable`).
The operand is then combined with the left operand's 0 or 1 by `&` or `|`,
and a condition branches on that one value. A C compiler keeps the branch
of a short circuit, whether as a flag or as a chain, and in a loop that
branch blocks if-conversion and vectorization. The compiler does if-convert
and vectorize the unconditional form: a loop counting the positions of a
32-byte block where `p1[j] == a && p2[j] == b` ran 5.4 times as fast in
that form as in the chain, which clang 22 leaves scalar. The operator's
value is the same either way, and the short circuit that the specification
promises (§6.1) skips nothing observable in such an operand: the operand
has no effect to skip and no failure to avoid.

The loop in question is the innermost one around the operator in its
function. It qualifies when neither its body nor a `while` condition
contains a loop, a call, `break`, `continue` or `return`
(`CodeGen::StraightCode`). This is recorded on the loop's scope once its
condition is emitted, since a `while` condition is the loop's exit.
Elsewhere, the short circuit's early branch is as good. Ahead of a branch
that remains, it is better: the left operand usually decides the branch in
one compare, while the unconditional form makes the branch wait for every
compare. Measurements from before these limits were added: in the sift
loops of an A* search's heap, where the comparator
`a.f < b.f || (a.f == b.f && a.v < b.v)` decides a `break`, the
unconditional form ran the search 25-30% slower. In a JSON parser's `while`
conditions, where `c <= ' '` alone ends nearly every whitespace skip, it
ran 30% slower.

What qualifies is defined over the operand's tree, which may have at most
24 nodes:

* constants;
* variables, except an optional reference used other than as its own null
  test or truth value;
* the comparisons, and `!`, `&&` and `||`, over those;
* integer `& | ^ << >>`;
* `+ - *` on unsigned integers, which wrap in every build, and on floats
  (and float `/`);
* casts that cannot fail in any build (to a float, or an integer widening
  or `as!`);
* fields of fixed structs reached through such storage;
* elements whose bounds check the types settle (a `u8` into a `[256]`), or
  whose check BCE found to hold without the left operand's facts
  (`Binary::specidx`, section 5.10, and `Index::nobc`);
* inlined calls and blocks made of scalar `let`s and a value of the same
  kinds.

All other expressions keep short-circuit evaluation. These include calls,
writes, division (which checks for zero), signed arithmetic and narrowing casts
(which check overflow and range in debug builds), and float-to-integer casts.
Loads through an optional reference also keep it, since the left operand may
narrow that reference (§3.8). So do fields of variable-size structs and any
index that BCE did not prove safe in this way. The left operand often supplies
the bound, as in `i < n && a[i] == c`.

### 6.12 Restated loops

Four loop shapes are emitted as an equivalent loop clang optimizes better
(the marks of §4's loop shaping and §5.10's `lenbound`). Each restated loop
starts with a comment naming its shape (`/* loop shape: ... */`), which the
test runner reads in `test/optimizer/loop_shapes.goose`'s C.

**Blocks of iterations** (`GenBlocked`). This applies to a `for` marked
`stripk` or `sumred` with an iteration count fixed at entry. Eligible
sequences are ranges, counts, fixed arrays, and arrays or slices with
`fixedlen`. Arrays and slices must be walked in place. BCE must not have
bounded the loop to fewer than two blocks. Such a loop
runs `while (at least K left) { K iterations }`, and then runs the rest one
iteration at a time in a loop of its own; each of the two loops runs a copy
of the body. Neither copy re-reads a length that the body cannot change,
so the iterations, their order and every value are the same as in the
original loop. "At least K left" is computed without overflow at the
counter's type (`AtLeastLeft`). The copies share one Goose loop scope
(`OpenLoop`), as a loop run one iteration at a time has one. A `break` in
either copy leaves both, and a `continue` reaches the end of the copy it is
in, whose step advances the counter. Both copies divide by the magic number
of a divisor fixed for the loop (§6.10's divisors), and both take `&&` and
`||` in the unconditional form where the body can be straight-line code
(§6.11).

Measured with clang-cl 22 at `/O2` over a million elements, a
strip-mined loop adding `x / d` into 4 lanes ran 4.4 times as fast with the
magic number as with the division. An in-order sum of
`if x > lo && x < hi { x } else { 0.0 }` ran 8 times as fast in the
unconditional form. clang's choice between a select and a branch for such a
term is fickle, though: a sum of byte compares that ran 2.9 times as fast
on its own ran 1.5 times as slow when inlined into each copy of a
strip-mined loop around it, where clang branched on each compare again.

A
body is emitted twice only where nothing binds to its locals once per
function (`Dupable`). That is, the body declares no data-stack local, no
captured local, no named result built at the destination, no nested
function and no function value, and builds no variable-size value. Each
copy names its locals afresh (`NameScope`), and the element binding of the
remainder copy gets a name of its own, since an aggregate's declaration
moves to the top of the function (`HoistAggregateDecls`).

* Strip-mined (`stripk`, `K` from 2 to 16): the block is
  `for (l = 0; l < K; l++, i++)`, and every `lanemods` node is emitted as
  `l` (`substs`, consulted by `GenX`). clang unrolls the block, and a
  `T[K]` indexed by the constant positions stays in registers; a SHA-256
  message schedule's 8 lanes of `u32` become two SSE vectors.
* An in-order sum (`sumred`, `K` = 8): the block computes its 8 terms into a
  local array, then adds them to `s` one after another. The term loop
  vectorizes; the additions are the loop's own, in its order.

**A bound for the trip count**: an array or slice loop whose `lenbound` is
at most 8, with a body of at most 48 nodes, runs `for (i = 0; i < bound;
i++)`, testing `i >= len` at the top of each iteration: clang unrolls it
whole into straight-line code with predictable exits. The length is still
read at every test, so growth during the walk is seen as before. A count
over a cell's `(Cell&<u32>)[..8]` neighbors is the case that needs it. A
loop whose length is known to equal its bound (`lenexact`) is left alone:
clang mostly knows that length too (a fixed array's length, or one that an
`assert` compared), and the constant only changes how clang vectorizes the
loop, which measured slower for the asserted neighbor count.

**Countdowns** (`countdown`): `while (v > 0) { v--; body }` becomes `n =
v; for (j = 0; j < n; j++) { v = n - 1 - j; body }`. At every statement of
the body, `v` holds the value the decrement would have left. After the
loop, `v` holds `0`, or its value at entry when that was not positive. An
up-counting induction variable is the form clang compiles best.

### 6.13 `simd` functions

A `simd fn` (spec §7.12) is emitted by `EmitSimdVersions` from the finished
body text of each of its specializations, including its element-run twin.
It emits one copy per level above the baseline, named `<cname>_simd<level>`
and marked `GS_SIMD_TARGET<level>`, and then the baseline under the
function's own name. The baseline starts by switching on `gs_simd_level()`
to the highest version that was built and is supported, forwarding its
parameters (the names of `SigParams`' declarations). Every caller,
prototype, function value, export wrapper and thread thunk names the
baseline, so nothing else changes. The versions are under
`#if GS_SIMD >= <level>` and the switch is under `#if GS_SIMD >= 1`. The
runtime (`runtime.h`) sets `GS_SIMD` to 2 under clang on x86-64 and to 0
everywhere else, where the C compiler sees one ordinary function, so MSVC,
TinyCC and other targets get no overhead from it. A build can lower it with
`-DGS_SIMD=1` (no AVX-512 version) or `0`.

* **Levels.** 1 is AVX2 with FMA, BMI1, BMI2, LZCNT and POPCNT; 2 adds
  AVX-512 F, BW, CD, DQ and VL (x86-64-v4). Clang inlines a
  baseline callee into a version (a callee whose target features are a
  subset of the caller's), so a small helper the loop calls is vectorized
  with it; it never inlines a version into the baseline.
* **Detection** (`gs_simd_detect`) is the compiler's own: `cpuid` for the
  feature bits and `xgetbv` for the register state the operating system
  saves (XCR0's YMM bits for level 1, its opmask and ZMM bits for level 2),
  in inline assembly. An operating system that enables a register state
  only at its first use (macOS does so for AVX-512) shows it clear and
  gets the lower level: the check can miss a version, never pick one the
  machine cannot run. Clang's `target_clones` would write the dispatch
  itself, but it needs compiler-rt's `__cpu_model` and
  `__cpu_indicator_init`, which a Windows link does not have. The level is
  detected at the first call and kept in a static that every thread reads
  and may write the same value into, through relaxed atomics, so there is
  no initialization order to get right and no race.
* **Exactness.** Clang's default `-ffp-contract=on` fuses a multiply and an
  add within one C expression wherever FMA exists, which AVX-512 implies
  and arm64 always has; GCC's default fuses across statements as well.
  That would make a version's float results differ from the baseline's, and
  a program's on arm64 from its results elsewhere
  (`test/codegen/simd_versions.goose` prints other digits without the
  guard), so `runtime.h` turns contraction off for the whole translation
  unit: `#pragma clang fp contract(off)`, GCC's `optimize("fp-contract=off")`
  and MSVC's `fp_contract(off)`. The same contraction measured 2x slower
  on a matrix multiply under AVX-512, because an FMA's latency lengthens
  the dot product's dependency chain.
* **`mul_add`.** `math`'s `mul_add` is the program saying where to fuse,
  and the one result a version may give differently from its baseline
  (spec §7.12). It is an extern bound to `gs_mul_add` (`gs_mul_addf`).
  `runtime_ext.h` defines that name twice: as a function, and then as a
  macro of the same name, which every call in the generated code names. The
  macro pastes `GS_MUL_ADD_MODE` into `gs_mul_add_fused` (`__builtin_fma`,
  always inlined so that it is built with its caller's FMA) or
  `gs_mul_add_separate` (`x * y + z`, two roundings under the contraction
  pragma), so a call gets the mode in force where it is emitted.
  `runtime.h` sets the mode to `GS_MUL_ADD_BASE`: fused where the whole
  translation unit has FMA (`__FMA__`, arm64), and separate elsewhere,
  under TinyCC and under MSVC, whose `fma()` is a call into the C library
  even with `/arch:AVX2` (a matrix multiply ran 2.7x slower fused). `EmitSimdVersions` sets it to fused
  before each version past the baseline, whose target attribute includes
  FMA, and back to the base after, so the baseline's copy of the body keeps
  the target's mode.
  * **Function values** are resolved at compile time (spec §7.6): a call through
    one names the macro like a direct call, so a `simd fn` given `mul_add`
    as a function value fuses in its versions, at `-O0` too. The function
    definition, in the base mode, is only for C that names it without the
    macro; the generated code never does.
  * **Helpers.** A callee stays in the mode it was emitted in, even where
    clang inlines it into a version later. Only the version's own body
    fuses, together with the functions Goose inlined into it. So a
    `mul_add` moved into a plain helper fuses in a `simd` caller at `-O2`,
    where Goose inlines a small helper, and not at `-O0`. An `inline fn`
    helper fuses at every level except `-O0`, where Goose inlines nothing
    (spec §7.13). The stdlib documentation says so.
  * **Tests.** `test/codegen/simd_mul_add.goose` checks each kind of call
    (plain, and in a `simd fn` direct, through an `inline fn` helper,
    through a plain helper and through a function value), generated at Goose
    `-O2` and `-O0` and built by clang at each level (`clang_mul_add` in the
    runner). `stdlib_math.goose` checks a cancellation and an overflow case
    accept either rounding and nothing else.
  * **Effect.** Making `gemm_block`'s multiply-add in goose-linalg a
    `mul_add` made its matrix multiply 1.3x faster on an i5-12600K, at 68
    GFLOP/s at n = 400.
* **Annotated, not automatic.** Building a suite of 50 benchmark programs,
  as written, for AVX2 or for AVX-512 without contraction measured 1-4%
  faster in total. That is inside the run-to-run noise, and no program
  gained in every run: their loops are scalar, or already vectorize at
  SSE2. With clang's default contraction, AVX-512 measured 15% slower.
  Versions of every function with a loop would take two to three times the
  code size for that. Whether a loop gains depends on its shape (an
  arithmetic byte mapping vectorizes, a table lookup becomes gathers that
  are no faster, a memory-bound kernel gains from AVX2 but not from
  AVX-512). The compiler does not know this, and the programmer measures
  it, so the programmer says which functions get versions. The cost of
  `simd` is the body once per level and one predicted branch on a load per
  call.

### 6.14 Elementwise float arithmetic as vectors

Elementwise arithmetic (spec §6.1) is emitted member by member
(`GenElemwiseInto`, `GenElemwiseNegInto`), and clang's SLP vectorizer then
has to find the vectors again, which it only partly does. In goose-fft's
radix-8 pass, whose butterflies are arithmetic on structs of four `f64`, the
AVX2 version had 208 two-lane operations next to 175 four-lane ones, and
spilled. So where the C compiler has GCC's vector extensions, a `+`, `-`,
`*`, `/` or negation of a value whose leaves are all floats of one width,
2, 4, 8 or 16 of them in 16, 32 or 64 bytes with nothing else in it, is one
operation on a vector type of the same size (`gs_vf64_4` and the others in
`runtime.h`). `VecShape` decides which values qualify: structs, nested
structs and fixed arrays alike. It rejects a type over 64 bytes before
looking at its leaves, and stops at the seventeenth, so a large nested array
costs nothing to check.

* **Same results.** The vector type's lanes are the members in order, and
  each lane rounds as the member's own operation would: IEEE operations
  don't depend on how many happen at once, and contraction is off (§6.13).
  A scalar operand of `*` or `/` goes to every lane through a vector
  literal.
* **Same layout.** The value stays its struct. Both operands are first
  copied to temps of the struct's own type, so a destination that is one of
  them, or whose member a scalar operand reads, works as it does member by
  member. The temps are read and the destination written through the vector
  type (`GS_VLD`, `GS_VST`), which is `aligned(1)`, since a struct of
  floats may sit anywhere (structs are packed), and `may_alias`, since it
  reads memory written as floats.
* **Only in expressions.** The vector code uses the vector types only
  inside expressions, never for a local (the generated code gathers
  declarations at the top of a function, outside the `#if`) and never as a
  parameter or result. A first version loaded and stored through inline
  functions: inside a `simd fn`'s AVX2 version that passed a 32-byte vector
  between code built for AVX and code that wasn't, which the x86-64 ABI
  forbids, and clang rejected it for Linux and macOS (Windows accepted it).
* **Where.** The vector form is under `#if GS_VEC`, temps included, with
  the member-by-member form under `#else`, so TinyCC pays nothing for it.
  `runtime.h` sets `GS_VEC` under clang and gcc, not MSVC or TinyCC, which
  have no vector extensions; `-DGS_VEC=0` turns it off.
* **Effect.** In goose-fft the radix-8 pass's AVX2 version came to 12
  two-lane operations next to 261 four-lane ones, a third fewer spills and
  no shuffles, and transforms of 16 to 16,384 points ran 1.25-1.6x faster
  (memory-bound lengths about even). Code that computes a whole struct and
  then takes it apart member by member can pay instead: computing a vector
  and extracting every lane costs more than the scalars did. goose-fft had
  one such loop, which was 10% slower until its multiplies were written per
  lane.
* **Tests.** `test/codegen/vec_elemwise.goose` covers each vector type, in
  plain functions and in a `simd fn`, and shapes that don't qualify. The
  runner's `clang_vec` builds it with `GS_VEC` on and off. The `simd fn`
  cases are the ones that failed on x86-64 Linux and macOS (above), whose CI
  runs that task.

---

## 7. The runtime

The runtime is a small amount of C99 code covering data stacks, integer
semantics, varints, aborts, text forms, workers and queues, slice pool
freelists, byte search, and the C behind `stdlib/os.goose`. It comes in two
halves. The first half is what a program's own translation unit needs.
`src/runtime/runtime.h` holds configuration, macros, the helpers that must
inline (integer operations, checks, varints), the data stack state the
emitted code reads (`gs_stks`, `gs_gl`) with the few functions that touch
it, and declarations of everything else. `runtime_ext.h` is spliced in after
the generated types, because it is written against them. It adds the extern
support an `--include` header may use (`gs_bld_append`), declares the
`gs_os_*` functions and defines math's `sqrt` (`gs_sqrt`, `gs_sqrtf`). C's
`sqrt` sets errno for a negative argument, so clang and gcc guard each call
with a test and a library call, and will not vectorize a loop around it.
Where the C compiler has a square root without errno
(`__builtin_elementwise_sqrt`, detected with `__has_builtin`), the runtime's
square root is that builtin: the same correctly rounded root, with no test
or call around it. Elsewhere it is C's. The other half, `runtime_impl.h`,
`runtime_threads.h` and `runtime_os.h`, defines what the first half
declares, and is the only part that includes the platform's headers.

How the halves meet is the driver's choice (`main.cpp`). The C that `-o`
writes defines `GS_SEPARATE_RUNTIME` and holds only the first half, with the
functions it declares left extern. `goose --emit-runtime` writes the second
half as a C file of its own (`GS_RUNTIME_OBJECT`), with the first half
included for its declarations. This file is compiled once and linked with
every program. The resulting object serves every program the compiler
writes, whatever its configuration. It always supports workers
(`GS_NEED_THREADS`, which otherwise only the program's own state follows),
it holds the failure paths of both debug and release builds, and it takes
the data stack sizes and counts from the program as the program starts
(`gs_rt_start`, called by the program's `gs_rt_init`) rather than from
macros. A program and its runtime object do have to agree on the runtime
itself. Both define `GS_RUNTIME_VERSION`, a hash of the runtime's text,
which is appended to the name of `gs_rt_start`, so a program written by
another version of the compiler fails to link. `--standalone` writes
everything into one unit, with everything the runtime defines made static. A
JIT run compiles this form, because TinyCC builds one unit in memory and
could not place the runtime object's thread-local storage there. Keeping the
platform's headers (`windows.h` above all) and the runtime's own functions
out of the program's unit makes MSVC compile a test program about 60 ms
faster. That is a third of a small program's compile time, not counting the
link, which takes about 95 ms either way. The programs run as fast as they
do with the runtime in their own unit. The runtime object holds the failure
paths, printing, the OS layer and region reservation, none of which is on a
hot path. Every function a failing check calls is declared not to return,
which is what the C compiler needs to keep a check's path short.

**Data stacks.** Each stack is one reserved region of `GS_STACK_RESERVE`
bytes (default 2 GB, capped at 2^48 by §10.4) plus an unmapped tail of
`GS_STACK_GAP` bytes. The program's configuration sets both sizes and hands
them to the runtime as the program starts. The runtime rounds the
reservation down to whole pages: the gap has to start on a page boundary to
be protected, and on Windows a stack would otherwise grow unnoticed through
the rest of a partial page committed at its end.

On Windows, pages are
committed on fault by a vectored handler that tells a commit from an
overrun. When the system refuses a commit (because its commit limit is
reached), the handler reports it as out of memory. macOS and Linux map the
usable region read/write and protect only the gap; the kernel supplies
physical pages on demand, without calling the runtime's signal handler.
To support Windows, runtime C writes to a stack only in user mode (`memcpy`,
`gs_bld_append`) and never through a system call, because a system call
would fail on an uncommitted page instead of faulting.

If the platform refuses a region (because its address
space, an address-space limit or strict overcommit accounting is exhausted),
the runtime retries at half the size, in whole pages, down to 1 MB, and
every region reserved after that starts out that much smaller
(`gs_reserve_region`). A smaller region is always safe: the checks the
compiler leaves out assume no more than `GS_STACK_RESERVE` bytes in a stack,
and the guard gap behind a smaller region only aborts growth sooner. Each
region therefore carries its own sizes for the fault handler. An overrun
ends the program with a message that names the thread program, the region's
size and the flag that raises that size. The message is written without
stdio, since a signal handler may not call stdio.

A thread program's stacks
are kept in a block reached through the thread-local `gs_stks`. Since the
compiler knows how many there are, all of them are reserved as the thread
program starts (`gs_stack_block`, called from `gs_rt_init` for main and from
its entry thunk for a worker). A worker's stacks are released when it exits.
Every region owned by the current thread program is registered
thread-locally, so that the fault handler never touches another worker's
state. The registry is sized to the program's region count; the runtime
allocates it as the thread program starts and frees it together with the
thread program's regions.

**Trimming.** `os.trim_memory` (`gs_trim_memory`) walks the calling thread
program's registry and gives back each region's complete pages above its
stack's top. Windows decommits them, stopping at the highest page the fault
handler has committed (`gs_region::committed`). Linux drops them with
`MADV_DONTNEED`. macOS replaces the unused suffix of the reservation with
a fresh read/write anonymous mapping using `MAP_FIXED`. XNU replaces the
mapping atomically, so another thread cannot claim the addresses during
the trim. The live partial page and guard gap remain outside that range.
Trimming covers the full unused reservation on macOS; ordinary allocation
and regrowth need neither a high-water mark nor a runtime-handled fault.
Each region records its stack's descriptor, and the descriptor's top in
memory must be current at the call. The extern is therefore marked
`SFunction::readsalltops`, so
that a call to it writes back every cached top. `CodeGen::CollectSpecs`
marks every function that may reach the extern, and the callers of those
functions also write back all their cached tops around the call
(`SyncReach`).

**Stack counts and the thread cap.** Every function's stacks are `gs_sp + k`
for constants below its own count. A callee's stacks start above the ones
the caller has in use at the call, and no call into a recursive cycle is
made with a stack in use (§7.8, `NoStackAcrossCycleCall`). The number of
stacks a program can have in use at once is therefore a compile-time
constant, which the compiler computes over its call graph (`BoundStacks`,
`codegen_stacks.h`). A function's count is the larger of its own count and,
over its calls, the index handed on plus the callee's count, and the members
of a cycle share one count. A count above `GS_MAX_STACKS` (set by the
program's configuration; 1024 by default) is a compile error that names the
program (`CheckStackLimit`); the runtime never checks it. The generated
`gs_program_init` hands `gs_rt_init` the main program's count, and the most
regions the main program and any one worker hold (the count plus the
dedicated stacks of the globals and of the worker's arguments). From these,
the runtime caps `hardware_threads()` at the number of workers that
`GS_STACK_BUDGET` (32 TB by default) has room for beside the main program,
but at least one. A pool sized by `hardware_threads()` therefore never runs
the program out of address space. The budget plans for this; it is not
enforced. Workers that hold no regions leave `hardware_threads()` uncapped.
`--stacks` prints the counts, the cap and each function's share. A program
built with `-DGS_STACK_STATS=1` reports each thread program's stacks and
regions on stderr as that thread program ends.

**Native stacks.** Recursion consumes only the native call stack (spec
§7.8), whose size is set when its thread starts. Running out of it ends the
program with `goose runtime error: native call stack overflow` and exit
status 1, as a data stack overrun does. The thread of every thread program
prepares for this as it starts (`gs_native_stack_init`, called from
`gs_rt_start` and `gs_thread_main`). On Windows the vectored handler handles
`EXCEPTION_STACK_OVERFLOW`, and `SetThreadStackGuarantee` keeps 64 KB of
each such stack for the handler: without that guarantee, the stack an
overflow leaves only just holds the report and `ExitProcess`. A program
built by TinyCC looks the function up at run time, because TinyCC's
`kernel32.def` lacks it. On POSIX the handler runs on an alternate signal
stack. The runtime allocates one for each such thread unless the thread
already has one (ASan gives every thread its own), and releases it together
with a worker's data stacks. A fault from 64 KB below the thread's stack up
to its top counts as that stack overflowing. The bounds of the stack are
read as the thread starts (`pthread_getattr_np` on Linux,
`pthread_get_stackaddr_np` and `pthread_get_stacksize_np` on macOS).
Elsewhere there is no way to read them, and the overflow is not reported.
For a fault of neither kind, the runtime reinstates the handler it replaced
and lets the fault recur under that handler, which gives the default action
or a sanitizer's report. A JIT program runs on the compiler's main thread
(§1), and installs all this there.

**Contained workers** (spec §11.2). A worker started with
`thread_spawn_contained` runs its thread program under `gs_run_contained`,
which points the thread-local `gs_contain` at a `gs_contained` on its stack
(a `jmp_buf` and the message). Every failed check and `abort(msg)` ends in
`gs_fail`, which formats the message. In a contained worker, `gs_fail`
stores the message in a `malloc`ed buffer and jumps; anywhere else it prints
the message and calls `gs_end`. After the jump, the worker releases what a
return would: its globals instance and stack block (`gs_thread_release`),
its copied arguments, and its regions. Its id is recorded in a bit set with
one bit per id, which `thread_wait` reads to answer `false`. Its message
goes into `gs_aborted` and stays there until the first wait that asks for it
takes it out, so that list holds only the messages no wait has taken. The
bit is never cleared. `gs_panic`, `gs_exit`, and the fault handlers still
end the process.

**Integer semantics** (§6.2): every operation runs at its type through
`gs_add_i32`-style helpers. Under `GS_DEBUG` these are functions that check
the wide result; otherwise they are macros equal to the release expression
(when they were functions, they measured 13--37% of runtime under a
non-inlining backend). Division and modulo are always functions, check for
zero, and use Euclidean `%`. One table (`CodeGen::ArithX`) writes out the
scalar operators in C wherever one is applied: a binary operator, a compound
assignment (its plain operator), `++` and `--`, and each member of an
elementwise operation. `as` goes through `GS_RANGE`/`GS_F2I`/... macros that
check in debug builds and cast in release builds. Two kinds of `as` are
plain C casts in every build: a cast to a float, and a cast from an integer
type whose every value the target type holds. `as!` and release float-to-int
casts use the defined wrap of `gs_f2iwrap`. It tests the value itself
against the i64 range (a value lies in that range exactly when its
truncation does), and inside the range converts with the C cast, a single
hardware instruction that truncates by itself. NaN and the values outside
the range go to the out-of-line `gs_f2iwrap_slow`, which is the only place
that calls libm's `trunc` and `fmod` (these are calls on baseline x86-64,
which has no rounding instruction). The add, sub, mul and neg helpers of a
signed type, and every checked cast, also take the file and line of the
operation. A debug check that fails prints them together with the operands
(or the value) and the type. The release macros drop these arguments without
evaluating them, so release builds compile to the same code as they would
without them. The arguments add about 1% to the generated C, and no
measurable TinyCC compile time. Bounds checks are one unsigned compare
(`GS_IDX`).

**Slice pools** (§5.4): the `gs_spans_*` functions (`runtime_impl.h`) keep a
`reusable[]` pool's freelist on a stack of its own. They are handed the
freelist's base, node count and stack top (the address of a cached top works
as well as the memory form). The free spans are stored in the leaves of a B+
tree ordered by index. Its nodes are `GS_SPAN_NODE` (512) bytes each and are
numbered from the base. Node 0 is a header (root, height, the chain of
emptied nodes), a leaf holds up to 31 spans, and an inner node holds up to
20 children, each with the first index and the largest count under it.

Placing a run (`gs_spans_alloc`, used by `alloc_slice` and when
`realloc_slice` moves a slice) follows those largest counts down to the
first span that can hold the run. If no span can, it falls back to the last
span, and the run starts there if that span reaches the end of the array.
Freeing (`gs_spans_free`) descends by index to the last span before the run.
The span after the run is that span's neighbor in the leaf, or the first
span of the next leaf. The first index of the next leaf is the key of the
entry that leads to it, so a path to that leaf is only made when its first
span touches the run. Growing a slice that does not end the array
(`gs_spans_regrow`) takes the same descent from the slice's start. The span
after the slice is the one it grows into. If it cannot grow into that span,
the span before and the span after are what the slice's elements merge with
before the slice is placed again. Each of these operations takes one descent
(two for a move), and on the way back up, the entries above a changed leaf
are brought up to date until one of them is unchanged. A largest count is
updated from the one count under it that changed. A count that grew can only
raise it. The node is rescanned only when the count that was the largest
shrank, and then only until another count as large turns up. On its way
down, placing a run notes at each level the largest count before the first
fit (all of them smaller than the run). Then, once it has taken from the
span, only the entries after the path need scanning. Nodes are searched
front to back, which on nodes of this size measured faster than a binary
search, with or without branches (a binary search's steps wait on each
other's loads). Entries are moved by loops rather than by `memmove`, because
the calls cost more than moving a few entries.

A full node splits in halves. An emptied node is removed from its parent,
and a root left with one child passes the root down to that child, so a
freelist that empties becomes a single empty leaf again. Nodes are never
returned to the stack: emptied nodes are chained for reuse, which keeps node
numbers valid, and with them a worker's copy of the freelist. The emitted
code itself keeps the element region's count and top, and fills in the
default values.

std's `slice_pool` (docs/stdlib.md) keeps the same tree in its `nodes` field,
with the header in its `tree` field and node 0 left zero, and calls a copy of
these functions: its externs `gs_slice_pool_*` and their `gs_spc_*` helpers,
in `runtime_os.h`, where `gs_rref` is defined. Those fields are the
program's to write, so the copy checks
what it reads before it uses it: a node number must be one of the nodes past
node 0, an entry count must fit the node it is read from as a leaf or inner
node, the height must fit a path, and an entry moved into or out of a node
must be one the node has room for or holds. A node reached at two levels of
a path, or one the chain hands out while it is still in the tree, keeps
every access inside the nodes: each count the copy writes fits a leaf, and
the counts that index an inner node's children are checked where they are
read. A check that fails aborts with `GS_E_SPANTREE` at the `gs_loc` the
extern was passed (`SFunction::takesloc`), the program's call into
`slice_pool`, which `gs_spcpath` carries. A wrong tree that passes them hands
out wrong spans, which `slice_pool`'s own check against its length catches
where they start past it. Sums of stored indices and counts wrap rather than
overflow, and new nodes past the last are zeroed, as a pushed `node { .. }`
would be. A `reusable[]` pool's freelist is out of the program's reach, so
it keeps the unchecked functions, which measured 3-5% faster on fragmented
and resize workloads under TinyCC and the JIT; the copy goes when
`reusable[]` does.

**The native layers** behind `gfx`, `audio`, `ui` and `physics` are not part
of this runtime. They are packages (`docs/design/packages.md`) in
repositories of their own, which `src/blessed.txt` names. The layers are
native C over SDL3 and Nuklear in the gfx package, and over Box3D in the
physics package. That C is compiled once into static libraries, which a
program links when it is built from the generated C, and which are compiled
into the compiler itself for JIT runs when the compiler is configured with
them. The generated C only declares the `gs_gfx_*`, `gs_audio_*`, `gs_ui_*`
and `gs_phys_*` functions it calls. A JIT run hands their addresses to
TinyCC with `tcc_add_symbol`. It takes them from the symbol table compiled
in with the package (`builtin.h`), or, for a package that is not compiled
in, from the library the compiler builds for it (`native.h`). The packages'
own `docs/gfx.md`, `docs/audio.md`, `docs/ui.md` and `docs/physics.md`
describe the layers.

**Byte search** (std's `find_any` and `find_pair`, `docs/stdlib.md`) is done
by `gs_scan_any` and `gs_scan_pair` in `runtime_impl.h`. std's `extern`
declarations reach them through the slice adapters `gs_find_any` and
`gs_find_pair` in `runtime_ext.h`. A set is std's `ByteSet`, whose layout
`gs_byteset` repeats; `byte_set` has already worked out how to test it. On
x86-64 the search tests 16 bytes at a time:

* with SSE2 compares for a set of up to three bytes or one range, or the
  complement of either, with a separate loop for each such kind and each
  pair of kinds (force-inlined loops that take the kinds as constants, since
  a switch inside the loop compiled to two indirect jumps per block);
* with SSSE3 `pshufb` nibble tables for any other set, when the CPUID check
  that `gs_rt_start` makes finds SSSE3.

Everything else, TinyCC included, tests one byte at a time, and searches for
a single byte with `memchr`. The last part of a range is tested by loading
the range's last whole block again and shifting the positions already tested
out of the result. That way no load reads past the range, which may end
where a data stack's unmapped gap begins.

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
  a[i]; i++; }`, and the downward `while d > 0 { d--; a[d]; }`. The loop
  variable's `>= 0` and `<= len` invariants are inferred for a local counter
  whose every write is either an assignment of a provable value or a
  `++`/`--`/`+= c`/`-= c` that cannot wrap;
* the same loops over an array that the body *grows*: a `push` never lowers a
  bound (`grow_during_loop` in `test/optimizer/bce.goose`);
* a second index that a counted `for` loop steps by at most one per
  iteration (`w += if keep { 1 } else { 0 }` or `if keep { w += 1; }`) and
  that starts at or below the loop's index, such as the swap index of std's
  branchless partition and a compaction's `v[w] = x`
  (`test/optimizer/bce_step_counters.goose`);
* an array filled by a counted loop and then indexed by the same count:
  `for i in n { a.push(...) }` states `a.len == n` afterwards, when no other
  statement in that loop touches `a` and no `break`/`continue` skips an
  iteration (including a `continue` inside a `block { }`);
* `s[s.len - 1]` after `guard s.len > 0`, `s[k]` after `assert(k >= 0);
  assert(k < s.len);` or an early-out `if k < 0 || k >= s.len { return }`;
* reductions: `a[k % a.len]` once `a.len > 0` is known (a `guard`, or the
  loop that filled `a`), `a[k % -4]` against a length above 3, and
  `a[(h & 63) as! i64]` into 64 elements. A mask bounds its result when the
  mask is provably non-negative, so `let mask = a.len - 1` also needs
  `a.len >= 1` to be known. A symbolic negation as divisor (`k % (0 - n)`)
  is not expressible. A `u64` hash reduced with `%` or `&` and cast to `i64`
  carries its range through the cast;
* halving indices: `a[i / 2]` and `a[i >> 1]` for an index `i` already in
  range, a parent `a[(i - 1) / 2]` once `i > 0`, and a sift-up loop
  `var i = a.len - 1; while i > 0 { let p = (i - 1) / 2; ...; a[i] = a[p];
  i = p; }`, whose index only ever moves down from a proven start (std's
  `heap_push`; `test/optimizer/bce_halving_index.goose`);
* multiples and quotients of the index against those of the length:
  `r[2 * i + 1]` for `i < n` and `r.len == 2 * n`, `out[i * 4 + b]` for
  `b` in `0..4` and `out.len == xs.len * 4`, a bit array's `flags[j >> 5]`
  for `j < n` and `flags.len == (n + 31) >> 5`, and the chunk
  `let d = buf[4 * i..4 * i + 4]`, four elements long, so `d[3]` needs no
  check (`test/optimizer/bce_scaled_indices.goose`);
* a stride loop starting above zero, `var j = 1; while j + 4 <= a.len`, and
  an open-addressing probe `i = (i + 1) & mask` with `mask == a.len - 1`
  once `a.len != 0` is known (`test/optimizer/bce_invariant_offsets.goose`);
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
  length facts survive;
* conditions on a sum whose variable is bounded, so the sum cannot have
  wrapped: the lookahead `if i + 1 < s.len && s[i + 1] == '"'` inside
  `while i < s.len`, `while i + 3 <= s.len { s[i + 2]; i += 3; }`, the range
  `for j in i + 1..n` and the inclusive form `for j in a..e + 1`;
* a byte (or any value of a type narrower than 64 bits) read out of an
  array, a field or a call, used to index a table large enough for its
  type's range: `tab[s[i]]` into 256 entries, `tab[s[i] >> 2]` into 64;
* an index or slice bound that an earlier check on the same path already
  passed: `xs[k]` after `xs[k]`, `xs[i]` for `i in lo..hi` after
  `xs[lo..hi]`.

* an index of an index type into its array (`nodes[w]` for `w: u32 in
  nodes`, spec §3.11), and into another array once their lengths are
  related (`dist[w]` after `assert(dist.len >= nodes.len)`, with nothing
  growing `nodes` since): `test/optimizer/bce_index_types.goose`;
* facts about arrays of other types, after a growth or shrink through a
  reference to an array of one type: the facts survive it
  (`test/optimizer/bce_typed_kills.goose`).

**Kept** (the check stays, and is usually a well-predicted branch):

* an index loaded from a data structure (`dist[q[i]]`, `pool[slots[k].node]`,
  `out[cursor[s]]`): the analysis tracks no array contents. Storing an index
  type there moves the check to where the value is converted, and a
  `checkfree` function (spec §7.14) proves none is left in a loop;
* a `u64`-typed index variable (a plain `u64` is never a base), an index
  read out of a field or element (`n.count`: only variables and lengths are
  bases, though a narrow type's range still counts), and any value that
  reaches the index through a cast that the facts cannot prove in range;
* an index whose relation to the length crosses a `pop`, `pop_n`, `resize`,
  `clear`, whole assignment, or a call the summary says may resize that array (or a
  call with no summary at all: a thread spawn, a call through an opaque
  site);
* a comparison against a value computed by a call that mutates tracked state
  inside the condition itself;
* an index that is `var + c` at a width narrower than `i64`, and a condition
  whose side is `var + c` where nothing bounds the variable (the addition
  may then have wrapped before the compare);
* a loop exit condition that is a disjunction (`while i < n && ok`), whose
  negation the domain cannot state;
* a counter stepped twice in one iteration, inside a nested loop, or in a
  `while` loop, whose own counter can move by any amount: only a `for`
  loop's index carries a step counter's bound;
* a child index carried around a loop by doubling (`c = 2 * c + 1`, or a
  hole `i = c` moving down a heap): the domain cannot double a bound, so
  neither the child's lower bound nor the hole's upper bound survives the
  loop head (std's `heap_pop` keeps these checks; they have no measurable
  cost).

To help the compiler remove bounds checks:

* State facts with `assert` when the compiler cannot see them. For example, use
  `assert(a.len == n)` at a function's entry when its callers are opaque.

* Keep indices in `i64` locals derived from counters and lengths, rather than in
  `u64` locals or values loaded from storage.

* Write image and matrix kernels using row slices, or use `y * W + x` with a
  length stated by the caller.

* Use `--bce-lines` to find lines that retain checks.

On MSVC, a retained check in a hot loop prevents vectorization entirely: `blur`
is 1.75x slower with its nine checks. On clang, the checks cost little; the
loop-view hoist described below matters more.

### 9.2 Arrays behind references

An array reached through a reference or slice keeps its base and length in
memory, and the C compiler must reload them after every byte store. Two
things remove the reloads:

* **View hoisting**: in a loop that only reads and writes *elements* of the
  array behind a reference variable (no `push`, `pop`, `pop_n`, `resize`,
  `clear`, whole assignment, rebind, or call that can reach it), the view is
  read once before the loop. This gives a 4x speedup on `blur` under clang.
  A loop that grows the array does not get this; move growth out of the read
  loop, or split the loop.
* **Stack-top caching**: pushes through a fat reference parameter run with
  the top and the count in registers when the function qualifies (§6.10). It
  qualifies when every fat reference in the body is a parameter, the
  parameters are provably distinct arrays at every call site (exact roots),
  and the function returns nothing nonfixed, has no `return from` channel,
  captures no resizable, and names no global that could be one of the
  parameters' pointees. For this purpose, an inlined helper's reference
  parameter, or a `let` bound to a variable, counts as that variable. A
  function that also reads a fat reference out of a field, or holds one in a
  local bound some other way, keeps the memory form for every stack, except
  for a literal's fields while the literal is built. For helpers that push
  frequently, pass distinct pools as parameters to enable this optimization.

A function caches the stack tops of the arrays it owns where it grows them,
even when it also has reference parameters. A nested function does the same
for the arrays it captures, and so does a function that builds its result by
pushes, for its destination. When growth occurs only inside a loop, the
cache is confined to that loop, so a loop that only updates elements needs
no register for a cached top. A call to a helper that is handed no array
needs no synchronization. A call handed a reference to a named array syncs
only that array's stack, and a call handed a struct holding a reference, or
an array of them, syncs every cached stack.

### 9.3 Construction and copies

* A nonfixed value is built at its destination (§4.3): `let x = f()`,
  `v.push(f())`, `v.append(f())`, `g(f())`, `x = f()` and a struct field
  initializer all hand the callee their stack. A `return` of a named local
  makes no copy when no return hands back another local (`DetectNrvo`),
  and this still holds after the optimizer inlines the callee. A `return []`
  or `return str(...)` beside `return result` builds its own value behind
  `result` and moves it down, which costs a move of that value's size.
  Returning different locals on different paths copies all but one of them,
  and so does a return whose value reads the local (`return result[0..n]`).
  A multi-name receive (`let a, b = f(); return a;`) copies.
* `v.append(f())` for a `T[]`-returning `f` compiles a second copy of `f` in
  element-run form; a builtin or dispatch result there costs one `memmove`
  of the elements over the prefix. A `T[..]` result is built on a temporary
  and costs one copy of its elements.
* A fixed-size element pushed into an array is evaluated first and stored
  after, so `v.push(f(v))` may grow `v` inside `f`. A variable-size element,
  or an element holding relative references, is built in its slot, and `f`
  may then not grow `v`. Nor may a callee grow the array that a
  `v.append(f())` or a whole assignment `v = f()` is building into (§4.2).
  Either is a compile error, and the callee's growths of its parameters,
  globals and captures count.
  The elements of an appended literal, `v.append([f(v), x])`, follow the
  same rule as pushed ones. A whole assignment's `f` may not use `v` at all
  (§4.4). New contents computed from the old ones are built in a local of
  their own and assigned as its `copy()`, which is one copy.
* An array or slice of another kind that meets a `T[..k]` (as a local, an
  argument, a field, an assignment or a return) is copied into the C value
  in O(length) after a capacity check, whatever the source's
  representation. Into a local or an assignment's target, it is copied
  directly into the target's slots.
* `==` on a `u8[..k]` key (any static-capacity limited array of integer,
  bool or reference elements) up to 64 bytes is a few masked word compares
  with no loop; slices of up to 16 bytes compare inline as well.
* `copy(x)` is a real O(size) copy, and so is any assignment of a non-fixed
  lvalue; the checker requires the `copy` to be written out, so the cost is
  visible.
* A function returning several values is never inlined. At `-O1` and above,
  a single use or a small body qualifies a function for inlining (thresholds
  of 8 nodes at `-O1`, 16 at `-O2`), subject to the eligibility rules and
  nesting limit in section 4.
* `str(...)` and `format(out, ...)` write directly at the destination's stack
  top; `print` renders into a temporary builder first. Structural rendering
  of aggregates calls a generated walker per type.
* A fixed-size struct literal is a C temporary copied to its destination,
  except a literal holding relative references, which is built in place.

### 9.4 Integer types

* Arithmetic runs at the operands' width (§6.2): a sum of `u8` taps wraps at
  8 bits; widen each operand (`as u16`) before summing.
* `varint` needs a decode on every read; use it for values with a wide or
  unpredictable range, and a sized integer for values whose range is known
  (`calc` was 11% slower with `varint` numbers that fit in a `u8`). Length
  prefixes decode with a one-byte fast path; value fields do not.
* A `u64` local drops out of every bounds proof; compare `u64` hashes with
  `.len`-derived values directly (§6.1 allows it) and reduce with `%` or `&`
  before binding the result to an `i64`.
* `let` bindings of `.len`, `.cap` or literals keep the non-negativity that
  the `u64` comparison rule needs; `var` bindings lose it.
* A literal argument to a generic or untyped parameter is a literal parameter
  adapting to every use in the body; negative literals can adapt to narrower
  types too, provided they fit every recorded use.

### 9.5 Relative references and pools

* Self-relative links (`T&<u32>`) are no measurably slower than indices on
  structures that are built once and then walked. On a structure that
  *relinks* (an LRU list), each store subtracts the field's own address and
  each load adds it back before the next load can issue, which measured
  1.5x slower than indices.
* `in pool` links (`T&<u32 in pool>`) store as a subtraction from a base held
  in a register and load as `base + off`. They let other arrays hold 4-byte
  links into the pool, and they can be copied freely. They are the form to
  use for relink-heavy structures in a global pool. clang schedules the
  base-plus-offset load better than MSVC.
* An optional link needs a null test on every load; a sentinel-terminated
  structure with non-optional links initialized through `self` loads with a
  plain add (`lru_nonopt`: 1.10x under MSVC).
* The store-time range check is omitted for `u32` links at the default
  reservation and for any width the reservation cannot exceed; `u16` links
  keep it.
* A structure whose links stay within one root array has no rebasing cost
  when moved or serialized; `bytes_of` is a zero-copy view of it.
* A plain reference parameter outside a recursive cycle has its own root
  class and cannot be stored as a relative link into the pool it came from;
  inside a `recursive` cycle the pool-class analysis identifies it. Helpers
  that link nodes should be in the cycle, take the pool itself, or use
  `in pool` links (whose root is the named global whatever the parameter's
  class).

### 9.6 Recursion and dispatch

* At `-O1` and above, a `recursive fn` has its base case inlined at every
  self-call, in either call syntax, when its body starts with the base case
  `if c { return e; }`, or has every self-call inside one `if c { ... }`
  that compares parameters it never assigns. This halves the calls of a
  complete tree walk. Mutual recursion disables it, and a self-call whose
  array result is taken whole by a slice destination stays a call.
* A self-recursive integer function whose tail returns fold with one
  associative operator becomes a loop; in `1 + f(l) + f(r)` only the calls
  down the right spine become the loop. Floats, `%`, returns inside nested
  loops, and callees that can `return from` the function disable it.
* Cycle members are never inlined and nothing is inlined into them.
* Case-function dispatch is one `switch` with a direct call per arm, and the
  arms can be inlined like any call; `match` is the same `switch`, written
  in place.
* `return ... from` costs a thread-local write at the return, a load and
  never-taken branch after every call on a propagation path, and nothing on
  an ordinary return.

### 9.7 Scratch, shrinking, and lifetimes

* A grow-only array can be shrunk (`clear`, `pop`, `pop_n`, `resize`) wherever
  the checker can see that no reference or slice into it is live afterwards
  (§3.10): use a view for the last time, then shrink; a shrink is a
  statement of its own, not part of a larger expression.
* Scope exit releases storage by restoring a stack watermark. A scratch
  buffer declared inside a loop is reset this way at the end of each
  iteration, without per-element cleanup. In a recursive function, a
  scratch buffer declared in a block that ends before the recursive call
  reuses the same data stack at every level (§7.8).
* `reusable` pools cost nothing per operation beyond the freelist push and
  pop; `free(i)` bounds-checks its index.
* A `reusable[]` pool's `alloc_slice` and `free_slice`, and a
  `realloc_slice` that does not just extend a slice at the end of the array,
  each walk the freelist's tree of free spans from the root. Their cost
  therefore grows with the logarithm of the number of free spans (a few
  levels for millions of them). Merging keeps the number of free spans to at
  most one more than the number of allocated runs between them. A slice
  handed back costs a range test unless the checker placed it in the pool,
  and new elements cost one `memset` unless the element type has field
  defaults.
* A `realloc_slice` that cannot grow in place places the run again and
  copies the slice. The copy is placed at the front of the span it takes, so
  the rest of that span is room to grow into. When no room is left, a slice
  grown one element at a time is copied at every growth; growing by a factor
  keeps the copies amortized.
* A thread program copies every global it uses at spawn; keep a worker's
  globals small or pass what it needs as arguments.

### 9.8 Backends

The generated C is the same for every backend; the differences measured in
`bench/notes.md` come from the backends themselves. clang vectorizes checked
loops and schedules base-plus-offset loads; MSVC does neither, and is better
at recursion into a bump allocator. `bench/results.md` reports every row
under both, and the JIT backend (TinyCC) is for running without a
toolchain, not for speed.

### 9.9 Scanning bytes

A loop that tests one byte at a time runs at about a byte per cycle, and no
C compiler vectorizes such a loop while it can exit early. There are two
faster alternatives. First, std's `find_any` and `find_pair`
(`docs/stdlib.md`) search for a `ByteSet` 16 bytes at a time, with the set
prepared once outside the loop. Second, a test over a block of fixed size
that counts hits rather than exiting vectorizes where its `&&` and `||`
qualify for unconditional evaluation (section 6.11). For example,
`for j in 32 { if p1[j] == a && p2[j] == b { n += 1; } }` over slices of 32
bytes, whose indices the loop bounds, takes the vector form, and a block
with a hit is then searched again one byte at a time. The same test in a
`while` loop that stops at the first hit stays scalar.

### 9.10 Loop shapes

Some loops, written in the natural way, compile to code as fast as
hand-tuned versions, because codegen restates them (§6.12):

* State in a few lanes picked by the counter, `for x, i in data { lanes[i
  % 8] ... }`, runs in blocks in which every lane index is a constant, so a
  `T[8]` of lanes is kept in registers; there is no need to write the block
  loop by hand.
* A float sum `for x in xs { s += term(x); }` keeps its exact order of
  additions, and its terms are still computed eight at a time where `term`
  is a pure expression after inlining.
* A loop over a small limited array, `for n in cell.neighbors` or
  `count(cell.neighbors) { ... }` over a `T[..8]`, is bounded by the
  capacity and unrolls completely; an `assert` of the exact length is not
  needed for that, though it still helps where the lists are full.
* `while i > 0 { i--; ... }` runs as a counted loop, as fast as counting up
  and deriving `i`.

The block forms need the iteration count to be fixed when the loop starts,
so a loop whose body may change the length of the array it walks (§6.5)
runs one iteration at a time.

### 9.11 Wider instruction sets

The C is built for the target's baseline (SSE2 on x86-64), where many byte
loops cannot vectorize profitably; for example, a stride-3 deinterleave
needs SSSE3's `pshufb`. `simd fn` (§6.13) gives a function AVX2 and AVX-512
versions under clang, chosen at run time. This helps a loop when:

* clang can vectorize it: no early exit (accumulate a validity flag, `seen
  |= a | b | c | d`, instead of breaking), every check elided (§9.1), and
  nothing in it that syncs a cached stack top (§6.10);
* the per-element work is arithmetic. A table lookup becomes a gather,
  which measured no faster than scalar loads on Zen 5, while ranges tested
  with a wrapping subtraction (`if c - 'a' < 26 { v = c - 71; }`) become
  compares and blends;
* the function runs the loop, rather than being called from inside one.

`stdlib/base64.goose` is written to these rules: its arithmetic codec
encodes and decodes a test input in 0.58/0.55 s at SSE2 (a table codec
0.64/0.57), 0.15/0.20 with AVX2 and 0.10/0.09 with AVX-512. For floats,
which clang may not reassociate, a loop has to be vectorizable as written.
A dot product's running sum is not, while the same sums computed as row
updates (`c_row[j] += aik * b_row[j]` for k in order) are, and they round
identically. A matrix multiply written that way goes from 2.67 s to 0.91 at
SSE2 and 0.49 with AVX2. AVX-512 does not improve on that, because the
kernel is bound by its reads of `b`.

---

## 10. Where the compiler is more conservative than the specification

This section lists the rules the current implementation applies beyond, or
in place of, what the specification allows, and the shapes the C backend
refuses outright:

* A long-distance return from a named function may carry only references
  rooted at globals or static data (TODO 0d); a long-distance return out of
  a function value's body is mapped through the calls it leaves (§3.12).
* Inside a recursive cycle, a reference rooted at a caller's fixed-size local
  is pass-down only unless the parameter it came through is threaded (§3.11).
  The spec's cycle store rule is stated the same way and is marked for
  refinement (TODO 5). Beyond the spec's rule, a merged value or a rebound
  variable that may be rooted at a threaded class is pass-down only; the
  spec stores such a value where each of its roots could be.
* When the entry call gave a reference, slice or holder parameter static
  data and a back edge passes storage of its own to that parameter, the
  back edge gets a holder result that may only be passed down. The body was
  checked with that parameter as static data, which no record can map to
  what the back edge passes (§3.11).
* A plain reference parameter's root class is identified with a global pool
  only inside a recursive cycle or through the `in pool` form (§9.5 above,
  `bench/notes.md` item 1), so `index_of`, a relative store and an exact
  read-back through such a parameter need a pool that some `T&<w in pool>`
  type names. Recording in the specialization key which global every
  exactly rooted reference argument points into, rather than only those,
  would lift that restriction, at the cost of one specialization per
  distinct global passed.
* Bounds-check elimination tracks no array contents and no `u64` variables
  other than those of index types (§5.12).
* A slice loaded through a reference-to-slice parameter is a class of its
  own only where the body reaches the argument's slot through references
  alone (§3.4). In a `recursive fn`, the slice is only bounded by the
  caller's slot (`SlotView`). The same holds when the parameter is given a
  field or an element (as a `format` overload's parameter is, given a part
  of what print, str or format renders); a slice variable the callee can
  name (a global's, or one that a nested function or a function value handed
  to the callee sees); or a caller's parameter class without such a class of
  its own. What the callee returns of such a slice may then point into
  anything the slot outlives. So a nested cursor helper's token
  (`take(cur, 5)` with `fn take(p: u8[:]&, n: i64) -> u8[:]` declared where
  `cur` is) cannot be returned past the function owning `cur`. And a callee
  that shrinks an array at the slot's depth or outside it while such a slice
  is still used is an error in the callee, whatever its call sites pass.
  Where the slot is a slice variable's own, what the callee stores of that
  slice is bounded by the variable too, not traced to its binding (§3.5).
  Where the slice is a class of its own, a store that may write the slot
  through another class or a bound joins it, whatever slot the call sites
  give that class, and a store in a nested body leaves it bounded by the
  slot there.
* A shrink of a grow-only array treats a slice slot that a reference names
  as holding a view of every array at the slot's depth or outside it (§3.10,
  item 4), whether a variable holds the reference or a holder does. The slot
  is a slice variable, or the caller's slot that a parameter's class stands
  for; for a reference variable, the latter excludes a class that has a
  class of its own for its slice. So `R { r: s }` still used after a clear
  of an array declared beside `s` is an error even where `s` views another
  array. Two changes would narrow this: judging a slice variable by its
  bindings, as the §5.2 scan does (a store through a reference to the
  variable rebinds it too, §3.5), and judging a holder's class that has a
  class for its slice as a reference variable's class is judged, with the
  pairs its callers judge (`NoteLiveViews`).
* When a grow-only array is shrunk, two things are left to each call to
  judge, by its record of the argument's storage (§3.10, **Parameters'
  views**): what the caller's array that a slice parameter views the
  elements of holds, and what the holder that a reference parameter names
  holds. This applies whether the callee reads that storage through the
  parameter, through a holder it stored the parameter into, or through a
  by-value holder parameter whose contents are one array. The following are
  also left to each call (**Class reads**): a view read out of that storage
  before the shrink; a holder of the activation's that keeps such a view or
  was copied out of that storage; a merge of such copies; a literal holding
  one; a call's result that is one (each of these also where a variable
  holds it); and a view read out of such a holder before the shrink, in a
  loop too. In judging them, every store on record there counts, including
  one the callee makes after its shrink or into another element, and a slice
  of a call's result, whose elements nothing records, is taken to hold
  anything. Such a view is still taken to point anywhere at the parameter's
  class depth or outside it in these cases: where a `var` holds it; where a
  merge or a rebind joins it with anything else rooted at the class; where
  it was read out of a view itself read out of the storage (`wss[0][0]`);
  and where a call returns one it read out of what an argument views that is
  no class of the caller's. The same holds for a holder copied out of the
  storage where it may lie in several storages
  (`for h in if c { a } else { b }`), in a view read out of the storage
  (`for h in hss[0]`) or in what a by-value holder parameter views
  (`p.hs[0]`). It also holds for a merge, a literal or a call's result that
  joins such a copy with anything else rooted at the class
  (`Q { h: ns[1], at: ns[0] }`), and for a view read out of such a holder
  where anything else was stored there, and where a `for` loop binds it
  (`for w in ws`, `ws` filled with views out of the storage). It holds, too,
  for what a `var` parameter's elements hold while the parameter is still
  used, and for what a by-value holder parameter's class holds where its
  contents are not one array exactly (`RootArg::heldexact`, never set in a
  `recursive fn`; `emit(buf, P { words: ["x", "y"] })` views a literal, a
  temporary no argument names exactly). Finally, it holds for what a slot
  without a class of its own for its slice holds:
  `fn emit(out: u8[>..]&, hss: (H[:])[:])` running
  `for h in hss[0] { out.clear(); out.append(h.s); }` is an error in the
  callee wherever a call site passes `hss` from `out`'s scope or a deeper
  one, while the same loop over the holders that `hs: H[:]` views is judged
  at each call.
* A parameter is a slot read (§3.10) only where its argument has a root that
  holds a grow-shrink array (§3.4), or, for a holder, where the holder's
  contents have such a root. The key records the bit only there, so that a
  function given no such argument is never specialized twice for it.
  Elsewhere the class holds no such array for the store rule to find. But a
  holder parameter's contents only bound what it holds, and a grow-shrink
  shrink in the body still counts a holder parameter passed on in the same
  statement where that bound is as deep as the array. A call's result takes
  the bit from a return through a parameter's class only where the argument
  for that class has the bit too (`RetAltVal`). So a holder read out of a
  parameter's array and returned, `fn f(rs: Row[>..<]&) -> Row { rs[0] }`,
  is not a slot read at the call.
* A holder loaded whole through a reference (`DecayRef`) has its contents
  bounded by the reference's roots, which may name the grow-shrink array
  whose element the reference points at. When stored, the holder fails the
  store rule as a reference into that array would, even though what it holds
  lies in the element's fields. A field of the holder read through the
  reference, or a match binder's copy of its payload, is a slot read.
* When an argument's root only bounds it, and the argument may point into,
  or holds, a grow-shrink array in what that root leads to, the argument
  gives its class a grow-shrink array that is not its root's own
  (`RootArg::gsvia`), and that array is taken to hold anything. So a whole
  grow-shrink array that a call returned (`keep(arr_of(t))`) may not be
  stored by the callee as a reference to the whole array, though the caller
  may store it, while one loaded out of a slot (`keep(t.a)`) may be stored
  by the callee. A value that merges a byte view no slot held with a root
  that only bounds it counts every grow-shrink array that a byte view can
  cover in what that root leads to (§3.5), because `Prov::freshview` belongs
  to the value, not to each root.
* A shrink of an array in what an inexactly rooted reference points at
  (`c.s.arr.pop()`, with `c.s` read out of `c`) counts as a shrink of every
  array of that array's type that the root bounds. It does not count only
  the arrays in storage that can hold what the reference points at, as a
  store through the reference does (§3.5). This is because the pairs of
  roots the shrink leaves its callers (`LiveShrink`) and its bound summaries
  (`BoundShrink`) carry only the array's type, so narrowing the shrink by
  what its path reached would only move the rejection to the call.
* A shrink of a global array counts another global as holding a reference
  into it where the store record says it may (§3.10). The record is coarse
  in two places. First, static data that a back edge returns is an inexact
  root (`RetAltVal`), which may be any array its pointee fits, so a global
  given such a result counts wherever it could. Second, a global holder
  passed to a by-value holder parameter gives it class 0, with no call-site
  facts for the class, so what the callee stores of it also counts wherever
  it could.
* A local holder's `contents` are one set for the whole holder (§3.6): a
  view stored in one of its fields or elements is among what a read out of
  any other field or element may point into, whatever their types. A holder
  copied out of a field or an element has only that container as a bound on
  what it holds, so a read out of the copy takes every candidate, even
  though the container's own `contents` may name one array.
* A store through a reference to a slice that may name several slots binds
  every slice variable the reference may name (§3.5), each under §9.2's
  depth rule. Such a reference is one read out of storage, a merge, a
  parameter given such an argument, or the class of a nested function's
  parameter beside the variables of the frames around it. A view of an array
  at a depth other than the one where one of these variables is bound is an
  error for that variable, even though the store may never land in it. Every
  global slice variable of the slot's type is among them, because a function
  checked later may make a reference to it; a local is among them only once
  a reference to it has been made. A class of its own for each captured
  variable that a nested function's parameter names exactly, in the
  specialization key, would spare the variables beside it.
* What a callee stores through a reference-to-slice parameter binds the
  caller's variable to what the callee stored, as bounded by its argument,
  unless the callee loaded the slice through another such parameter whose
  slice's elements hold no references (§3.5). So a slice read out of a slice
  of slices behind a parameter binds the variable to anything the argument
  outlives, which may then break the depth rule.
* A reference to a slice has one writability for both the slot it names and
  the slice the slot holds (§3.8). So a reference to a slice variable
  holding a read-only slice cannot re-point the variable either:
  `skip(&cur)` advancing a cursor `var cur = text` over a parameter given a
  literal is rejected, as `skip(cur)` is. Nor can a reference to a slot that
  is not written (a by-value binder, or a field of a `const` value or of a
  value reached through a `const T&`) write the elements of the slice there,
  though a copy of the slice can: `poke(c.f)` writing `p[0]` is rejected, as
  `poke(&c.f)` is, and `let t = c.f; t[0] = 65;` is not. A `format` overload
  taking a slice by reference is given a read-only temporary for a slice
  that is not in storage (**Format overloads**). So it cannot write the
  elements of `g[1..]` either, nor of `if c { s } else { return; }`,
  although it can write those of a variable holding that slice, `s` itself
  included. A view already keys its slice's writability separately (§3.4).
  Two changes would lift these restrictions. A writable reference beside a
  read-only view, with a store through the reference allowed to put a
  read-only slice where the view is read-only, would lift the first where
  the parameter has a view. A view as writable as the slice, whatever the
  reference, would lift the second.
* The growth-during-construction rule (§3.10) assumes that a parameter class
  may be any global or captured local a callee grows, that two classes of
  one activation are one array unless every call site keeps both concrete
  and exact, and that a callee still being checked grows whatever its text
  names.
* While print, str or format runs a `format` overload, the views an argument
  reads through (an optional reference, a slice, a non-fixed array's
  elements, a holder's references) stay held even where the overload takes
  the whole argument and nothing of it is rendered around the call (§3.10,
  **Format overloads**).
* An element read out of an array of references (or slices) and stored back
  into it through a reference parameter (`xs[0] .= xs[n]`) leaves the array
  treated as holding a reference into itself, so shrinking it is an error
  when a later use follows (std's `heap_pop` sifts before it pops for this
  reason). A callee that stores a view read out of one parameter's storage
  into another parameter's storage is checked in its body by the depths of
  their classes, which bound the storage and not the views it holds. So
  `copy_into(xs, tmp)` for a local `tmp` of views copied out of the
  parameter `xs` is an error, while the same call with slices that may each
  be `xs` or `tmp` is judged at the call by what each holds (std's
  `stable_sort` merges its last pass into `xs` instead of copying back for
  this reason).
* The C backend rejects:
  * binding, copying or dispatching a *resizable* ADT payload;
  * a reference to a resizable nested in a variable-size prefix or an ADT
    payload;
  * copying a resizable value with a variable-size prefix;
  * `==` on resizable structs;
  * `self` in a frame object literal;
  * a `format` overload by reference on a resizable without its own header;
  * `str`/`to_bytes`/`from_bytes` into a fixed-size destination.
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

8. **Float text was not always shortest.** `gs_fmt_f64` printed
   `1.000000000000001` as `1.0000000000000011`, even though the shorter
   text reads back to the same value. It kept the first correctly rounded
   `printf` output that `strtod` read back, trying 15 and then 17
   significant digits, and `gs_fmt_f32` did the same from 6 to 9 digits.
   Trying 16 digits as well still missed powers of two. Below a power of
   two, the gap to the next float is half the gap above it, so the shortest
   text can lie above the value while the correctly rounded text of that
   length, below the value, reads back as another float. 2^89 printed as
   `6.1897001964269014e+26` rather than `6.189700196426902e+26`, and 45
   other doubles and three `f32`s (2^-96, 2^87 and 2^90) were also printed
   with one digit too many. And under TinyCC on Windows, msvcrt's `printf`
   rounds a tie away from zero, so `562949953421312.25`, halfway between
   two 16-digit forms that both read back, printed as `562949953421312.3`
   there and as `562949953421312.2` elsewhere.
   **Resolved:** the runtime finds the shortest digits, and the nearest of
   those, exactly, in integers (section 6.9), and `CatOne` takes them from
   `std::to_chars`. `test/runtime/float_text_digits.goose` prints the
   powers of two and ties.

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

---

## 12. The language server

`goose lsp` (`lsp.h`) serves an editor over the Language Server Protocol
on stdin and stdout, as `docs/design/language_server.md` describes. The
compiler is the server, and a whole program is analyzed again each time
one of its files changes. `goose lsp --probe` prints the protocol version
it serves and exits; a client runs it to tell a compiler that has the verb
from one that does not. `--stdio`, which clients such as
`vscode-languageclient` pass, names the only transport there is.

**Transport** (`lsp_rpc.h`). Messages are JSON-RPC framed by
`Content-Length`, on streams put in binary mode (`SetBinaryStdio`,
`main.cpp`). A reader thread parses each message as it arrives
(`Transport::ReadAll`) and puts it in a queue, which the worker (the main
thread) drains (`Transport::Pop`). A `$/cancelRequest` is noted rather than
queued, and the request it names gets the protocol's cancelled error when
its turn comes. A message with no method is the client's reply to a request
from the server, and needs no handling. A change to a document (`didOpen`,
`didChange`, `didClose`) also sets `Transport::cancel`, which the parser
polls at each top-level declaration and the checker at each body and
statement (`Ast::CheckCancel`). When it is set, they throw `Cancelled`,
which is not a `CompileError`, so neither a tentative parse nor a driver's
catch mistakes it for one. `JsonOut` writes the replies, and `JsonQuote`
(`json.h`) writes their strings. The protocol's positions are UTF-16 columns
on 0-based lines; the compiler's are byte columns on 1-based lines.
`TextIndex` converts between these positions and byte offsets over a file's
text, and `Utf16Col` and `ByteCol` convert within a line. `PathOfUri` and
`UriOfPath` convert between paths and `file:` URIs as VS Code writes them
(`file:///c%3A/dir/a.goose`). `PathKey` is the form that two ways of writing
one file's path share; it is lower case on Windows.

**Workspace** (`lsp_workspace.h`). The open documents' texts are served to
the compiler through `ParseProgram`'s `SourceProvider`, ahead of the files
on disk. A root is a program that is analyzed: either an entry file the
editor configured (`initializationOptions.entryFiles`, or the `goose`
section of its settings), or an open file that no root reaches. A change to
a file marks every root that reaches it dirty; each such root is due after
a debounce (`DEBOUNCE_MS`). An open file that no root reaches becomes
a root of its own, and stops being one once another root reaches it or the
file closes. This holds whatever takes an open file's root away: if the
program reaching it closes, stops importing it or stops being configured,
the file becomes a root of its own again (`RootOpenFiles`, run after every
change to the roots).

The worker analyzes a due root whenever no message is waiting
(`Workspace::Analyze`, run on the compile thread's stack through
`RunOnCompilerStack`). Like `goose check`, it parses, resolves, checks with
`nomain`, optimizes and runs bounds-check elimination. It sets
`package_nofetch` so that nothing is fetched, and `Ast::exemplars` so that
the generic functions nothing instantiated are checked as exemplars (§3.1).
Every diagnostic is collected: the warnings and the errors that the parser
and the checker continued past, through `Ast::sink` (§2), and the error that
ended the attempt, from its `CompileError`. The diagnostics are published
for every file the attempt reached (an empty list for a file with none, so
the editor drops the previous attempt's diagnostics). Related lines that
have a place are sent as related information, and the rest are appended to
the message. A root keeps what its latest attempt gave each file
(`Root::published`, as the protocol's JSON). A file is published as the
union of what every root reaching it gave (`Workspace::PublishDiagnostics`),
with an error that two roots report alike (the same range, severity and
message; the instantiation chains differ) included once. So a file that two
programs import keeps the errors one of them finds in it when the other is
dropped or stops importing it, and a root that goes away removes only its
own diagnostics. A program with errors is not optimized or bounds-check
analyzed. An attempt that got past resolution replaces the analysis the
root's queries read, with whatever its check annotated before each error. An
attempt that failed earlier keeps the previous analysis, so queries go on
answering from it. A cancelled attempt leaves the root dirty.

The index of the names as written is built once resolution is done, before
the check, because the check rewrites a global's initializer: it checks the
initializer in place rather than a clone of it (`Analysis::IndexSource`,
below). The rest of the indexes are built as soon as the check is done,
before the optimizer rewrites the bodies. The optimizer inlines calls and
drops constants, and with them the clones the features read; those clones
stay allocated (the `Ast` owns every node) but are no longer in the tree.

Each time an analysis replaces the previous one, the client is sent the
refreshes it accepts (`workspace/codeLens/refresh`,
`workspace/semanticTokens/refresh`, `workspace/inlayHint/refresh`, as its
`capabilities.workspace` declares), through `Workspace::Request`. An editor
asks for lenses, semantic tokens and inlay hints as a file opens or changes.
A request that the worker takes before the analysis that would answer it
(one that came during another root's analysis, or with other messages
waiting) gets a stale answer or none, which the editor would keep until the
next edit. Every other request about the documents (and these three, when
the client accepts no refresh) first runs the analyses that are due, without
waiting out the debounce (`Workspace::Settle`,
`Server::AsksAboutDocuments`). A hover or a definition requested right after
a change is about the text after the change, whose positions the earlier
analysis does not know, and one requested right after a `didOpen` would
otherwise find no analysis at all. These analyses cannot be cancelled
(`Analyze`'s `cancellable`), since a change after the request is about a
text the request does not ask about.

The analysis that an attempt replaces, and the analysis of a removed root,
are freed on a thread of the workspace's own (`Workspace::Reap`). Freeing a
large program's tree takes a tenth of the time its check took, and the
requests after an analysis, starting with the editor's refreshes, would
otherwise wait for it. The `Ast` owns everything the thread frees, so the
next analysis can run meanwhile; the thread is joined when the server exits.

**Analysis** (`lsp_analysis.h`). An analysis is the `Ast` as the check left
it, together with the indexes the features read. `FileIndex::occs` holds
every occurrence of a name in a file, by start offset, found by walking
every declaration and body once. Occurrences have two categories. Uses are
`IDENT`, `DOT` for a member, `TYPE`
for a type name, or `INIT` for a field named in a struct literal. Each keeps
its source node or type expression. Declarations and declaration parts use
`FN`, `STRUCT`, `ENUM`, `ALIAS`, `GLOBAL`, `LOCAL`, `FIELD`, `VARIANT`,
`PARAM`, or `GENERIC`. `BINDER` covers variables bound by loops, match arms,
arms generated by `const for`, and block parameters. `FileIndex::calls` holds
every call's extent, and
`FileIndex::spans` every node's extent, including the top-level
declarations'. A node's extent runs from the earliest token in it (a
binary's left operand lies before the operator the node is anchored at) to
its end. These indexes describe the program as written (`IndexSource`), and
the walk that builds them runs before the check. The check checks a global's
initializer in place: it puts the defaults of the parameters a call leaves
out among the call's arguments, and the defaults of the fields a struct
literal leaves out among the literal's initializers (as clones of the
declaration's defaults, which are indexed where they are written); it orders
the literal's initializers by field, under the field's name; and it expands
a `const for` into clones of its fragments. None of this is what the source
says there.

`Analysis::clones` maps each source node to the clones the checker made of
it, one per specialization (`Node::origin`). The clones carry `exprtype` and
the resolutions (`Ident::vdef`, `Dot::fieldidx`, `Call::spec`). `Find`
returns the first clone a predicate accepts, which is how a feature reads a
type or a target, and `TypesOf` returns every distinct type the clones gave
a node, for hover in a generic body. The exemplars' clones
(`Ast::exemplarspecs`, §3.1) come after the real ones. They are the clones
of an exemplar's own function and of the functions nested in it, but not
those of a callee's specialization made for an exemplar, whose types are the
caller's opaque ones. `exemplarclones` says which clones they are. The same
walk of the clones records what the source alone does not say:

* `vardecls` and `defplaces`: the `let` or `var` of each checked variable
  (`VarDef`) and the place of its name. For a loop's, an arm's or a block
  parameter's binding, that place is where the source writes the name,
  rather than where the checker placed the variable (the loop, the match,
  the call). It is read from the clone's own names (an arm a `const for`
  generated has no counterpart among the arms of the source's match).
* `patternvariants`: the variant each match pattern's name resolved to.

A binding with no place is one the compiler synthesized, such as a block's
implicit `it`.

**Features** (`lsp_features.h`). Each request locates its file's analysis
and the occurrence at its position (`Locate`, `FileIndex::At`). Hover
renders a declaration header (`FnHeader`, `VariantHeader`), the type a clone
gave the node, the specializations a function has (`SpecsText`) and the
documentation comment (`DocText`, with the `//` stripped). For a name no
instantiation reaches, hover says so. For a type that an exemplar's clone
gave, it says that the type is an exemplar's (`NoteExemplar`: `a: T`, "in an
exemplar instantiation"). For a name past the error that ended an exemplar,
it says that the exemplar stopped before that name (`NotChecked`). The
binding of an arm a `const for` generates shows the type each arm gave it.
A type name that names nothing gets a placeholder struct from resolution
(`SStruct::unknown`); hover shows it as an unknown type, it has no
definition or semantic token, and rename refuses it. Go to definition
follows the resolution to the declaration's name (`DefinitionOf`), and on an
`import` line opens the file (`Ast::imports`). Document symbols are the
file's top-level declarations with their fields, variants and nested
functions; workspace symbols are those of every root. Semantic tokens
classify the occurrences by their resolution (`SemanticTokenOf`, the legend
in `SEMANTIC_TYPES`). Signature help finds the innermost call whose
parentheses hold the position (`FileIndex::CallAt`), and counts the commas
before the position to find the active parameter, leaving out commas in
string and character literals and in comments. It lists the overloads of the
callee's name, with the resolved one active; for a UFCS call these are the
ones the receiver's type selects (`MethodsOf`, from its clone's type), which
is also where hover and definition look for a call the check left
unresolved. Completion (`Completion`) looks
at what precedes the name being typed:

* After a `.`, it offers the members of the value before the `.`
  (`MemberCompletions`). The innermost node ending at the `.` is the object
  (the incomplete `Dot` the parser keeps, §2, is what makes the checker type
  it). The object's type from a clone gives the fields of a struct or a
  variant's payload, the functions whose first parameter takes the type
  (`FirstParamTakes`: untyped, generic, the type or a reference to it) as
  UFCS calls, and the builtin members of its kind (`RecvKindOf`). An enum's
  name before the `.` gives its variants, and a value no instantiation typed
  gives every field and function.
* After `::`, it offers the namespace's declarations.
* Otherwise it offers the names in scope (`ScopeCompletions`), taken from
  the parse tree alone so that a generic body has them too. In order of
  nearness (`sortText`), these are the bindings of the enclosing blocks
  declared before the position, the enclosing functions' parameters, type
  parameters and nested functions, every namespace's declarations (qualified
  where the namespace is not the file's own), the builtins and the keywords.

References and rename share one notion of identity: the place of the
declaration's name, as `DefinitionOf` finds it for any occurrence. Two
occurrences refer to the same thing when their definitions coincide
(`ReferencesTo`, which looks at every occurrence in every file of the
analysis, through the clones). The search runs in every program that reaches
the declaration's file (the analysis of each root that has the file with the
request's text), not only in the one answering for the file. So a module two
programs import is renamed in both, and a use that only another program
resolves (in a generic function only that program instantiates) is found
too. A place that several analyses find counts once. A file is read only in
the analyses that have the same text for it as the first analysis that
reaches it, so that their places agree. References lists the places,
including the declaration's own when the request asks for it. Rename
replaces the leaf of each occurrence (a `ns::name` keeps its qualifier) and
the declaration's name with the new name, as a `WorkspaceEdit` keyed by URI.
It answers with an error instead of an edit for a keyword, a builtin, a name
with no declaration, or a name the compiler synthesized (its declaration
does not contain the name). The call hierarchy uses the same identity.
`textDocument/prepareCallHierarchy` answers with the function whose
declaration the occurrence at the position resolves to, as an item whose
selection range is the function's name; the `callHierarchy/incomingCalls`
and `outgoingCalls` requests that follow find the function again by that
range. Incoming calls are the function's references that are a call's
callee, searched in every program reaching it, as for references. They are
grouped by the innermost function declared around each call (`EnclosingFn`,
by the declarations' extents). A call in a global's initializer is in no
function and is left out, and a function that two analyses both have forms
one group, identified by its file and place. Outgoing calls are the callees
in the function's body that resolve to a function of the program, grouped by
callee; a nested function's calls are left to the nested function, and calls
to builtins are left out. Each group's ranges are the callee's name at each
call.

Inlay hints give two things. The first is the type of each `let` or `var`
that has no type written, shown after its name, where the clones' `VarDef`s
agree on it (there is no hint where the specializations differ, or where an
exemplar knows the type only as `?`). The second is the parameter's name
before each argument written in a call resolved to a specialization (not
before the defaults that a global's initializer, checked in place, holds). A
UFCS call's object stands for the first parameter, and an argument that is
the parameter's own name gets no hint. Folding ranges are every
brace-delimited body (a block, a match, a struct literal) and declaration
that spans more than one line, as `region`, and every run of `//` comment
lines, as `comment`. A body's or declaration's range ends one line early
where text follows the body on its last line, since an editor folds whole
lines. Selection ranges give, for each position, the chain of extents that
hold it: first the name at the position, then the node extents of
`FileIndex::spans` outward to the declaration, each holding the one before.

Code lenses (`CodeLenses`) sit on the names of the file's top-level
functions. Above a generic function (one with type parameters or an untyped
parameter, `IsGeneric`), a lens shows the number of its specializations,
with their `name(argument types)` lines as the lens's data (`SpecLines`,
which hover shows too; specializations the types do not tell apart share one
line). Above `main`, a lens shows "Run" with the file's URI. The protocol
has no way to ask which commands a client runs itself, so a lens names a
command only where the client listed it in its initialization options'
`commands` (`Workspace::clientcommands`), as the VS Code extension does. The
commands are `goose.showInstantiations`, with the function's name and the
lines as arguments, and `goose.run`. Otherwise a count is shown as a title
alone, and there is no "Run".

**Testing.** `test/lsp_protocol.py` (`docs/testing.md`) drives the server
over its own protocol on the fixtures in `test/lsp/`, whose `// lsp:` lines
state the queries and their answers, and the lines the errors are at. For
instance, `recovery.goose` has an error in each of two functions, both
published, with the queries answered from what was checked, and
`exemplar.goose` tests hover, definition, signature help and completion
inside generic functions nothing calls. In
`errors_tc/recovery/exemplar_unchecked.goose`, whose generic functions no
binding could make right, the server gives no diagnostic, as `goose check`
gives none for them. The script's client declares the VS Code extension's
commands and the three refreshes, checks that each refresh comes after an
analysis, and answers the server's requests.
`test/lsp_sweep.py` is the robustness and latency sweep over the
repository's programs (`docs/design/language_server.md` §12), run by
hand. The editors' configuration is in `docs/editors.md`.
