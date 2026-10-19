# A language server for Goose

<details>
<summary>Table of contents</summary>

- [1. What there is today](#1-what-there-is-today)
- [2. What the compiler gives a server, and what it lacks](#2-what-the-compiler-gives-a-server-and-what-it-lacks)
- [3. Decisions](#3-decisions)
- [4. The server](#4-the-server)
- [5. Compiler additions](#5-compiler-additions)
- [6. Features, and what each needs](#6-features-and-what-each-needs)
- [7. The extension](#7-the-extension)
- [8. Testing](#8-testing)
- [9. The plan](#9-the-plan)
- [10. Deferred and open](#10-deferred-and-open)
- [11. Exemplar instantiation](#11-exemplar-instantiation)

</details>

What it would take to turn the VS Code extension in `vscode/`, which runs
`goose check` on save and parses its text, into a language server: a
`goose lsp` verb in the compiler, speaking the Language Server Protocol over
stdio, with the extension as one of its clients. The note records what the
extension and the compiler do today, the measurements the design rests on,
the shape proposed, the compiler changes it needs in order of size, what
each editor feature needs from them, and a plan in phases where the first
phase already improves the extension as it is.

## 1. What there is today

The extension (`vscode/`, about 730 lines of JavaScript and 600 of tests,
no runtime dependencies, packaged with `vsce package --no-dependencies`)
has two halves.

**Client-side, from the source text alone:** a TextMate grammar (also
injected into ` ```goose ` blocks in Markdown), bracket and comment
configuration, snippets, an outline built by a small lexical scan
(`language.js`: `declarations` finds top-level `fn`/`struct`/`enum`/
`type`/`let`/`var`/`namespace` heads by brace depth) and document links on
`import` lines that resolve the module the way `StdlibDirs` does.

**Through the compiler:** on open and save of a trusted workspace's file,
`goose check <entry>` runs with a timeout (`diagnostics.js`), where the
entry is `goose.entryFile` if set, else the file itself. Its stderr is
parsed by regular expression: `file:line[:col]: error|warning: message`,
a caret line two lines down for lexer errors, and `in f instantiated from
file:line` frames as related locations. Since a typecheck error names only
a line, the client underlines the first identifier on it. The results are
kept per entry file (`CheckQueue`), marked stale on edits, dropped when no
open document checks that entry any more, and shown in Problems, a status
bar item and an output channel. Tasks run, build and generate C through the
same executable, with a problem matcher for the task terminal.

What it cannot do follows from that split: nothing knows what a name means.
There is no hover, no go to definition beyond import links, no completion
beyond snippets and the editor's word list, no references or rename, no
semantic highlighting, diagnostics only on save, only the first error of a
program, positioned at a line, and only for one program per workspace
folder at a time. Every feature on that list needs the compiler's
resolution and typecheck, which only the compiler has. The protocol is how
an editor asks a compiler those questions, and once the compiler answers
them, every editor with a client (Neovim, Emacs, Zed, Helix, JetBrains)
gets Goose support with a few lines of configuration.

## 2. What the compiler gives a server, and what it lacks

The front end is structured so that most of a server's questions have an
answer in the data that exists after `goose check`. The gaps are in
positions, in how errors come out, and in what happens after the first one.

**Positions.** `Line` (`ast.h`) is a line number and a file index; nothing
has a column or an end. `Parser::CurLine` is the one place that makes
them (22 uses, 48 node constructions through `Parser::New`), from
`Lexer::tokline`. The lexer knows the column (`attr.data() -
toklinestart`), which `FunVal::col` alone records, for the instantiation
chain. Every declaration (`SFunction`, `SStruct`, `SEnum`, `SAlias`,
`VarDef`, `VarDecl`) and every `TypeExpr` has a `Line`; a `Field`, `Param`,
`GenericParam` or `SVariant` has none. Names are `string_view`s into the
source buffers, which `Ast::sources` keeps alive, so an unqualified name's
byte offset is recoverable by pointer arithmetic today; a qualified name
(`ns::x`) is interned (`Ast::Intern`) and a synthesized one (`result#1`)
never was in the source, so positions have to be recorded, not derived.

**Diagnostics.** A compile error is `CompileError { string msg }`, thrown
with its text already rendered: `Lexer::ErrorAt` writes `file:line: error:
msg`, the source line and a caret; `TypeCheck::ErrorIn` writes the same
head, the source line without a caret, and the instantiation chain
(`InstantiationChain`, `DumpInstance`, one `in fn f<T = u8>(i64)
instantiated from file:line` per frame); warnings go to stderr as they
occur (`PrintWarning`), deduplicated by node and text; BCE's `checkfree`
report is one more rendered string. The first error ends the compile, and
the parser uses the same exception to backtrack a tentative parse (`catch
(CompileError &)` in the generic-call and function-value prefixes). The
test suite's 1,800 `errors_tc/` and 60 `errors/` fixtures each carry a
`// error: message` marker, which the runner matches against a line of
the form `file:line[:col]: error: message`; it compares neither the
echoed source line nor the caret nor the chain, and it accepts a column
already. So the head's format and the messages stay whatever a server
does, while the heads may gain columns without touching a fixture.

**Annotations.** A function body is checked once per specialization, as a
clone (`FnSpec::body`), and the clone carries everything a server wants:
`Node::exprtype`, `Ident::vdef` or `fnref`, `Dot::fieldidx`, `member`,
`variantconst` and `einst`, `Call::spec`, `dispatch`, `builtin`, `fvbody`
and `rettypes`, each `VarDef` with its concrete type, and `TypeExpr::Dump`
and `DumpInstance` render a type or a specialization's signature as the
diagnostics do. Every clone node names its source node (`Node::origin`),
and `Node::Children` walks any tree, so a map from a source node to its
clones, and from a position to a node, is a walk over `ast.fnspecs` and
`Ast::ForEachRootTree`. The template body itself is never annotated. A
generic function nothing instantiates, or a nested function nothing calls,
has no clone and so no types (`docs/implementation.md` §3.1; the server's
exemplars, §11, give the former one); a
non-generic, fully annotated top-level function nothing reaches is checked
standalone (`CheckUnreached`), and a module without `main` is accepted
under `check` (`nomain`), which is how the extension checks a module file
on its own today.

**Scope.** Lexical scope exists only while the checker runs (`frames`,
`scopes`, `vars`, `LookupVar`); nothing records where a variable's scope
begins and ends. Name completion needs that, or its own walk of the parse
tree.

**Comments.** The lexer drops them (`goto again` in `Lexer::Next`), so
there is no documentation to show on hover.

**Sources.** `ParseProgram` loads every file from disk, except the root,
which `--roundtrip` hands it as text (`rootsource`). A server has the
editor's unsaved buffers, for any file.

**The process.** The compiler is already a multi-compile process:
`--multi-test` compiles each file as a run of its own would, an `Ast` and a
compile thread each, and the typechecker has no global state (one
`thread_local`, the stack floor). A compile runs on a thread with a 64 MB
stack (`RunOnCompilerStack`), which a server keeps. Two things a compile
does would stall a server: `ParseProgram` fetches a declared package it has
not got (`FetchPackage`, git over the network, inside the parse) and the
checker compiles `embed_shader` sources (`CompileShader`, cached only in
`Ast::shaders`, so once per `Ast`).

**Cost.** The question an editor server stands or falls on is whether the
whole program can be analyzed again on every change. Measured on this
machine (Windows, Release build, October 2026), in-process figures through
`--multi-test` with the same file twenty times, process figures with
`time`:

| | files | lines | process start | parse and resolve | typecheck and optimize | with BCE |
|---|---|---|---|---|---|---|
| `samples/18_json.goose` (with `std` and `os`) | 3 | 1,780 | 34 ms | 4 ms | | 10 ms |
| `test/goose_in_goose/main.goose` | 14 | 3,318 | 34 ms | 10 ms | 82 ms | 104 ms |
| `samples/31_mini_doom.goose`, first run | | | | 15.3 s | | |
| `samples/31_mini_doom.goose`, after | | | | 53 ms | | 75 ms |

The 15 seconds are the `gfx` package being fetched by the parse. The rest
says a full re-analysis of a program the size of the self-hosted compiler
is a tenth of a second in-process, and a typical program a hundredth,
which is well inside the debounce an editor uses anyway (the extension
waits 180 ms today). Nothing incremental is needed to start, and the data
structures (an append-only `Ast` per compile, freed whole) suit replacing
the analysis rather than updating it.

## 3. Decisions

1. **The server is the compiler.** `goose lsp` is a verb of the one
   executable, so the server is always the version of the compiler it
   serves, needs no second distribution, and reuses the front end rather
   than approximating it. The alternative, a server written in TypeScript
   or Goose that reimplements resolution, is what Zig's ZLS does for
   `comptime`, with heuristics that give up past a point, and what its
   maintainers plan to replace with the compiler's own analysis. Goose's
   checker decides types per instantiation with reference roots and
   literal parameters in the key (§3.1); no reimplementation would stay
   faithful.

2. **Analyze whole programs again on every change.** A change to any file
   of a root re-parses and re-checks the root from the editor's buffers,
   after a debounce and with cancellation; the new `Ast` replaces the old
   one, and queries are answered from the latest analysis that completed.
   This is what clangd does per translation unit (with only the preamble
   cached) and it is cheap here (§2). Incrementality, if ever, is per
   root: parse results per file can be cached by content hash later
   without changing the design.

3. **Programs are roots, as the extension has them.** A root is an entry
   file: those configured (`goose.entryFile`, which becomes a list), and
   any open file no configured root imports. The import graph of the last
   analysis says which root a file belongs to, so editing `stdlib/json.goose`
   re-analyzes the programs that import it, and a module nothing imports is
   analyzed on its own as `check` does (`nomain`): its fully annotated
   functions checked, its generics not.

4. **Positions are byte offsets with lengths.** Nodes and declarations get
   a start column and an end; the server converts to line and UTF-16
   column at the protocol boundary, since VS Code's client asks for
   UTF-16. The compiler never deals in the protocol's units.

5. **Diagnostics are data first, text second.** A `CompileError` carries a
   structured diagnostic (file, range, message, severity, the chain as
   related locations) and the rendered text is one function of it, kept
   byte-identical for the CLI and the fixtures. The same structure is
   printed as JSON under a flag, which the current extension can consume
   before any server exists.

6. **Recovery comes in stages and is never silent.** First the server
   reports the first error as today while answering queries from whatever
   was annotated before it (the clone of a function is annotated up to the
   failing node). Then the parser recovers per statement and declaration,
   then the checker per root. Each stage reports more, never less, and the
   CLI's behavior under errors is unchanged until the last one, which is
   when `goose check` can print every error too.

7. **The server never waits on the network or the shader compiler.** A
   package not fetched is a diagnostic at its import and a command to fetch
   it; shader blobs are cached across analyses by source text.

8. **The extension stays usable without the server.** The grammar,
   snippets, outline, links and tasks stay client-side (a highlighting
   grammar should not depend on a server being up; Lobster's extension
   made the same choice in 2023), and the check-on-save path stays as the
   fallback for a compiler without the verb.

## 4. The server

**Where.** Headers included by `main.cpp` after the passes, like the rest
of the compiler, and a `verb == "lsp"` branch in `Main` (`goose lsp`
takes `--stdlib` and nothing else; the client passes settings through the
protocol). Four files:

| File | Holds |
|---|---|
| `lsp_rpc.h` | The transport: `Content-Length` framing on stdin/stdout (both in binary mode), a JSON writer beside `json.h`'s reader, request ids, `$/cancelRequest`, the reader thread and the message queue |
| `lsp_workspace.h` | `Overlay` (open documents: path, text, version), `Root` (an entry file, its latest `Analysis`, its import set), root selection from the configuration and the open documents, `workspace/didChangeWatchedFiles`, scheduling: the debounce, the cancel flag, running an analysis on the compiler stack |
| `lsp_analysis.h` | `Analysis`: the `Ast`, the diagnostics per file, and the indexes built once after the check: nodes per file sorted by offset, source node to clones, declarations with their positions; the queries `NodeAt`, `DeclOf`, `TypeOf`, `ReferencesTo`, `ScopeAt` |
| `lsp_features.h` | One handler per request, mapping queries to protocol structures: hover, definition, references, rename, call hierarchy, document and workspace symbols, completion, signature help, semantic tokens, inlay hints, folding and selection ranges, code lens |

**Threads.** One reader thread parses messages into a queue. One worker
thread owns everything else: it applies notifications to the overlay,
answers requests from the latest analyses, and runs analyses, each on the
compiler stack thread (`RunOnCompilerStack`), which blocks the worker for
the duration. A `didChange` that arrives while an analysis runs sets the
cancel flag, which the parser and checker poll (§5 F), and the worker
starts over once the queue is drained. Requests that arrive during an
analysis wait for it; at a tenth of a second that needs no second reader
of the `Ast`, so nothing is locked. A request about the documents that
arrives while a change to them is still waiting out the debounce, or
right behind the `didOpen` whose analysis has not run, has that analysis
run first, not cancelled by changes after it: an answer from the analysis
before the change would be about another text, or there would be none
yet. Lenses, semantic tokens and inlay hints are the exception where the
client takes their refresh, which asks again once the analysis is done.
`$/cancelRequest` drops a request still in the queue.

**An analysis.** For a root: `ParseProgram` with a source provider that
serves the overlay before the disk (§5 B), with fetching off (§5 E),
`ResolveTypeNames`, `TypeCheckProgram` with `nomain`, then the optimizer
and BCE for the `checkfree` errors (a fifth of the time; a setting can
skip them), each diagnostic collected rather than thrown or printed
(§5 C). Then the indexes. An analysis that was cancelled is discarded; one
that failed with a parse error before any annotation keeps the previous
analysis for queries and publishes the new diagnostics. Diagnostics are
published for every file of the root, open or not, as the extension does
today, and cleared for files a root no longer reaches.

**Roots.** `initialize` brings the configured entries per workspace
folder; `didOpen` of a file no analysis reaches makes it a root of its
own, dropped again when closed; a file reached from several roots belongs
to the first configured one for queries, and all of them re-analyze when
it changes. The standard library is parsed by every root that imports it,
which the measurements say is affordable (parsing is a tenth of a check).

**Positions.** The server holds the text of every file of every analysis
(the `Ast` does already). A protocol position becomes a byte offset by
walking the line, counting UTF-16 units; the reverse walk makes ranges.
The byte-order mark `LoadSource` strips is stripped from an overlay text
too, so offsets agree with the editor's, which never shows it.

## 5. Compiler additions

In order of size, smallest first; each is useful on its own and testable
without the protocol.

**A. Positions.** `Line` gains `int col` (byte column, 1-based, as
`FunVal::col` is), set in `Parser::CurLine` from `lex.attr.data() -
lex.toklinestart`; `FunVal::col` goes. `Field`, `Param`, `GenericParam`,
`SVariant` and `Import` gain a `Line`. `VarDef` has one. Then ends: the
lexer records where the previous token ended (`prevend`), `Node` gains
`int end` (a byte offset in the file, or an end line and column), and each
parse function sets it on the node it returns (38 functions; a
`Finish(node)` helper at their returns), as do the declaration parsers for
`SFunction` and the types. The instantiation chain can then name a block
by its `Line` alone, and a typecheck error can carry the node's whole
range. Twelve bytes per `Line` instead of eight, in every node and type:
nothing measurable.

**B. A source provider.** `ParseProgram`'s `rootsource` becomes a
`function<const string *(const string &path)>`, consulted for every file
before `LoadSource`. `--roundtrip` passes one that serves the dump as the
root. The server passes the overlay.

**C. Structured diagnostics.** A `Diagnostic` of file index, start and
end, severity, message and related locations (the instantiation chain's
frames, each with its rendered head as the message, as the extension
shows them today). `CompileError` carries one plus the text;
`Lexer::ErrorAt`, `TypeCheck::ErrorIn`, the import errors in
`ParseProgram`, `BCE::CheckfreeReport` and the driver's own errors fill
it, and one `Render(ast, diagnostic)` makes the text they build today,
whose heads and messages the fixtures match. Warnings stop printing themselves: `PrintWarning`
hands them to a sink the driver sets, which for the CLI prints at once as
now and for the server collects. A `--diagnostics json` flag on `check`
prints the structures as one JSON array on stdout (the writer from
`lsp_rpc.h`), for scripts and for the extension's fallback path.

**D. Recovery.** Two independent halves.

*D1, the parser.* `ParseBlockBody` and `ParseTopDecl` catch a
`CompileError` from a statement or declaration, record it, and skip to the
end of the statement (the next `;` or `}` at the block's depth, braces
balanced) or of the declaration (the matching `}` of a body, or the next
`;`). A block that lost statements is marked `broken`; a function whose
body is broken keeps its signature and gets `SFunction::broken`, so the
file's other declarations still parse, and the checker checks a broken
body up to the lost statement and reports nothing for the rest of it (a
call of a broken function is checked against its signature and its
result's roots are taken as the most permissive, so one error does not
cascade). The tentative parses are unaffected: they catch inside their
own extent and the real parse then fails again, outside, where recovery
catches it. One more kind of recovery matters for completion: a `.` with
no name after it parses as a `Dot` with an empty name, marked
incomplete, which the checker checks for its object's type and then
stops the body on, silently (§6).

*D2, the checker.* The constructor checks the roots in order: the global
initializers, `main`, each `thread_fn`, each unreached function
(`CheckUnreached`), the exports, then the whole-program fixups. Each root
is wrapped: on an error, record it, restore `frames`, `scopes`, `vars`
and the per-body state to the root's entry (they are plain vectors with
sizes to truncate to; `CheckSpecBody` pushes and pops `frames` itself
rather than by a guard, so the catch does the pop), mark every `FnSpec`
created since the root began as `failed`, and go on to the next root. A
failed specialization is never reused: `GetOrCreateSpec` skips it when
matching, and a call that would reuse it is an error of its own,
suppressed in the report since it is a consequence. The fixups
(`SettleParamRootExactness`, `VerifyLiterals`, `CheckGlobalShrinks`) run
over the specializations that completed. With this, one erroneous
function costs the program the diagnostics of the functions that reach it
and nothing else, which is what an editor user expects of a whole-program
checker. The CLI keeps stopping at the first error until this is in, then
`goose check` prints them all, in root order; the `errors_tc/` fixtures
have one error each by construction and stay as they are.

**E. No fetching.** An `Ast::nofetch` flag: `PackageRoot` reports a
package that is not in the cache as a diagnostic at the import (`goose
fetch` fetches it) instead of calling `FetchPackage`. `goose lsp` sets
it; `goose check --offline` could too.

**F. Cancellation.** A `const atomic<bool> *cancel` on the `Ast`, polled
in `ParseTopDecl`, `CheckSpecBody` and `CheckStmt`, which throw a
`Cancelled` type of their own when set: not a `CompileError`, so neither
the tentative parses nor D1 and D2 catch it.

**G. A shader cache.** `CompileShader` and `CompileShaderFile` consult a
process-wide map by stage and source text before compiling, where
`Ast::shaders` keeps the per-program map codegen reads.

**H. Documentation comments.** The lexer keeps the text of a run of `//`
lines that stand on lines of their own immediately above a token (a blank
line or any token ends the run), as `Lexer::doc`; the declaration parsers
take it into `SFunction`, `SStruct`, `SEnum`, `SAlias`, `VarDecl`, `Field`
and `SVariant` (`string_view doc` into the source). No new syntax: the
standard library's existing comments above declarations become hover text
where they sit directly above one, and a banner followed by a blank line
attaches to nothing.

**I. `goose lsp`.** The server itself (§4): roughly the size of `bce.h`
once the features of §6 are in, and little of it compiler-specific beyond
`lsp_analysis.h`.

Not needed: an incremental parser, a separate symbol database, a change
to how specializations are keyed or cloned, or a formatter (`Dump` drops
comments and prints a `guard` as the `if` it became, so it is not one).

## 6. Features, and what each needs

| Feature | Answered from | Needs |
|---|---|---|
| Diagnostics on change | the analysis of each root the file belongs to | B, C, E, F; D1 and D2 for more than the first error |
| Hover | the clone at the position: `exprtype`, a `VarDef`'s type, `DumpInstance` of a `Call::spec`, a declaration's signature and doc | A, H |
| Go to definition | `Ident::vdef` (local or global `VarDef`), `fnref` or `Call::spec->sf`, `Dot::fieldidx` into the `SStruct`, `variantconst`, a `TypeExpr`'s `SStruct`/`SEnum`/`SAlias`, an `Import`'s file | A |
| References, rename | every clone's `Ident`, `Dot`, `Call`, `TypeName` resolving to the declaration, reduced to source nodes by `Origin()`; rename edits the leaf of each spelling and the declaration | A; across the roots that reach the file |
| Call hierarchy | the function named at the position, as definition finds it; its incoming calls are the references that are a call's callee, by the function declared around each; its outgoing calls are the callees in its body, its nested functions' left to them, by the function each resolves to | A (ends, to find the function around a call) |
| Document symbols | the parsed file's declarations, nested functions and fields included, replacing the lexical scan | A (ends, for the ranges) |
| Workspace symbols | the declaration tables of every root | |
| Completion of names | the parse tree: the bindings of the blocks enclosing the position (`VarDecl`, parameters, `for` and `match` binders, nested functions), then the namespace's globals, functions, types, the builtins table and the keywords; works in generic bodies, which have no clone | A |
| Completion after `.` | the incomplete `Dot`'s object type from its clone: the instance's fields, the enum's variants, the functions whose first parameter accepts the type (UFCS), the builtins table's members by receiver kind (`BR_` masks); without a clone, every field and function name | D1 (the incomplete `Dot`) |
| Signature help | the overloads of the callee's name (`Namespace::functionmap`) and the builtins table's arity, from the parse alone; the active parameter by commas | A |
| Semantic tokens | the parse tree and the symbol tables: function, type, type parameter, variant, field, namespace, parameter, variable (`let` read-only), with no clone needed | A |
| Inlay hints | a `let` without a type: its `VarDef::type` in the clone (the first specialization's, where they differ); parameter names at calls from `Call::spec->sf` | A |
| Folding, selection ranges | node ends | A (ends) |
| Code lens | the specializations of a generic function (`sf->specs`), "run" above `main` through the existing task | |
| Formatting | not offered (§5) | |

Inside a generic function nothing instantiates, the features answer from
an exemplar instantiation (§11): the function checked with each type
parameter bound to a fresh opaque type, as far as that check gets.

## 7. The extension

`src/client.js` starts `goose lsp` with the configured `compilerPath`
through `vscode-languageclient` (LSP 3.18 is the current protocol version;
3.17 is enough for everything here), with the settings as
`initializationOptions` and `didChangeConfiguration` after, a restart
command, and the existing output channel for the server's log. The
language client brings the first runtime dependency, so the extension is
bundled with esbuild into `dist/extension.js` and `vsce package` keeps
`--no-dependencies`; `build_vsix.py` runs the bundle step before
packaging. The grammar, snippets, language configuration, tasks, the run
and build commands and keybindings stay as they are. The status bar item
derives its state from the server's published diagnostics instead of the
check queue.

Fallback: at activation the client runs `goose lsp --probe` (prints the
protocol version and exits); a compiler without the verb keeps the current
behavior, with `diagnostics.js` reading `check --diagnostics json` where
the flag exists and the text otherwise. The regular-expression outline and
import links are registered only when there is no server. This keeps one
extension serving every compiler for a while; the fallback goes once the
server has shipped in a release.

Other editors: a Neovim `lspconfig` entry and a Helix `languages.toml`
entry are a few lines each and belong in `docs/` with the extension's
README.

## 8. Testing

**Compiler side, no protocol.** The fixtures keep matching the heads and
messages, which C does not change. A `--diagnostics json` run of a handful of
`errors_tc/` fixtures checks the structures (positions, ranges, related
locations) against expected JSON. Recovery (D) gets fixtures of its own
under `test/errors_tc/recovery/`: files with several errors and an
expected list, run with `check` once the CLI reports them all, and through
the JSON flag before.

**The server.** `test/lsp_protocol.py`, beside `cli_paths.py`, starts
`goose lsp` with a pipe, initializes, opens fixtures from `test/lsp/`, and
runs the queries the fixtures ask for in comments (`// lsp: hover 12:7
"x: i64"`, `// lsp: definition 12:7 -> 3:4`, `// lsp: complete 20:9 has
push`), comparing the answers; it also edits a document to an erroneous
state and back and checks the diagnostics follow, and cancels an analysis
with a change. It is a section of `run_tests.py`, run in CI on all three
platforms. `test/lsp_sweep.py` (§12) is the sweep over every program in
the repository, run by hand.

**The extension.** The `node:test` unit tests stay; `compiler.test.js`
gains the JSON path; the extension-host test, which runs in a VS Code
instance, asks for a hover and a definition through
`vscode.executeHoverProvider` and `vscode.executeDefinitionProvider` and
checks what the server gave. CI does not run the extension's tests today;
with a server to keep in step, a job that runs `npm test` on Linux is due.

## 9. The plan

**Phase 0, the compiler alone:** A (columns, declaration part positions,
ends), B, C with `--diagnostics json`, E, H. The extension reads the JSON
and underlines exact ranges, with the warnings and the chain, which is a
visible improvement before any server exists. Each item is a change of its
own with its tests. A and C are in (`docs/implementation.md` §2,
`test/diagnostics_json.py`): a diagnostic's range runs from the earliest
token its node has on the anchor's line to the node's end, and a call is
anchored at its callee rather than its `(`. So are B (`ParseProgram`'s
`SourceProvider`), E (`--offline`, `package_nofetch`, an error at the
import) and H (`Lexer::doc` on declarations, fields and variants, printed
by `--dump-docs`). The extension's reading of the JSON is a task of its
own.

**Phase 1, a server:** F, G, `goose lsp` with the transport, workspace and
analysis of §4, and diagnostics on change, hover, go to definition,
document and workspace symbols, semantic tokens and signature help, all of
which the annotations of a first-error analysis already support. The
extension's client and fallback. A `test/lsp_protocol.py` from the start.
This is in: the server (`docs/implementation.md` §12: `lsp_rpc.h`,
`lsp_workspace.h`, `lsp_analysis.h`, `lsp_features.h` and `lsp.h`, with
`Ast::CheckCancel` and the shader cache in `CompileShader`), whose index is
one occurrence per name per file rather than a node-per-position table, and
the extension's client (`vscode/src/client.js`: a probe with `goose lsp
--probe`, a `vscode-languageclient` client bundled with esbuild, and the
check-on-save mode kept for a compiler without the verb or
`goose.useLanguageServer` off).

**Phase 2, more than one error:** D1, then D2; completion (both kinds),
references and rename, inlay hints, folding and selection ranges. `goose
check` reports every error. D1 and D2 are in (`docs/implementation.md`
§2 and §3.1): the parser recovers per statement (`Block::brokenat`,
`SFunction::broken`, `Dot::incomplete`) and per declaration, which it
drops whole, reporting each error as it goes (`Ast::Report`, kept in
`Ast::errors`); the checker checks a broken body up to its error and
finishes the specialization from the signature (`Abandoned`), so a call
of a broken function is checked against the signature alone and the
caller goes on; each root is checked by `CheckRoot`, which reports its
error and returns the checker to the root's entry (`Checkpoint`). One
refinement on the text above: only the specializations whose bodies were
being checked when the error struck are marked `failed`, since one that
completed before it is sound and keeping it reusable spares the roots
that call it (a helper `main` called before its error, say); what the
root recorded for the fixups stays, being what checked code did. The
server skips the optimizer and bounds-check elimination for a program
with errors and answers queries from what was annotated. The fixtures
are in `test/errors_tc/recovery/` and `test/lsp/recovery.goose`. So is
recovery inside a declaration and from a failed import
(`docs/implementation.md` §2 and §3.1, `test/lsp/broken_decls.goose`):
a struct, an enum or a top-level function with an error past its name
stands with what was parsed before it, marked broken (a function with
no body, which calls are checked against nothing), and what it lacks is
a consequence the checker does not report; resolution reports an
unknown name where it is written and gives the use a type standing for
nothing, a value of which is a consequence too, rather than ending the
analysis; and an import that fails is reported at the import, the
program going on without the module, whose names are then unknown
where they are used.

The features needing no recovery are in
(`docs/implementation.md` §12): references and rename, whose identity is
the place of the declaration a use resolves to, through the clones, with
rename refusing builtins, keywords and synthesized names as an error reply;
inlay hints for the types of `let`s and `var`s and the parameter names at
calls; folding ranges for bodies and comment runs; and selection ranges
from the node extents the index records (`FileIndex::spans`). The
analysis's indexes are built before the optimizer runs, which inlines calls
and drops constants and would lose the clones with them. Completion is in
as well (`docs/implementation.md` §12, `test/lsp/complete.goose`): names in
scope from the parse tree, members from the type the clone of the value
before the `.` carries, with UFCS functions and builtin members, and a
namespace's declarations after `::`. With that, phase 2 is complete.

**Phase 3:** code lens, exemplar instantiation for generics, parse caching
per file if a workspace turns out to need it, the other editors' entries.
Exemplar instantiation is in (§11; `docs/implementation.md` §3.1 and §12,
`test/lsp/exemplar.goose`): after the real roots, the server's check gives
each generic function no root specialized one specialization with opaque
bindings, whose errors end it silently and which no later pass sees, and
the features read its clone as any other, saying where a type is an
exemplar's. Across the test corpus (2,500 files) the server's diagnostics
are unchanged by it, and its analyses take about a sixth longer in total,
none of the large programs measurably.

Code lens is in (`docs/implementation.md` §12): the instantiation count
above each generic function, which the VS Code extension lists in a quick
pick (`goose.showInstantiations`), and "Run" above `main` through the
extension's run command, each naming its command only for a client whose
initialization options list it, with `workspace/codeLens/refresh` after
each analysis. The other editors' entries are `docs/editors.md`: Neovim,
Helix and Emacs (Eglot).

Phases 0 and 1 together are the bulk of the compiler-side work; the
features of phase 2 are mostly server code over data phase 0 provided.

## 10. Deferred and open

* **Exemplar instantiation.** Designed in §11 and in: what `T` supports
  is answered by giving every operator and member an opaque result, and
  the keys by checking the exemplar as an unreached function is checked,
  apart from the real specializations. Open: an exemplar ends at the first
  thing it does not support, of which the commonest are a literal where a
  `T` is expected (`var total: T = 0`), a builtin given a `T` (`print`,
  `hash` where it is no overload set), and a type parameter called as a
  function value (`F(x)`), which could be bound to a function value whose
  call gives an opaque result. A nested generic function, and a generic
  function only other exemplars call, are exemplified only as the
  exemplar's body reaches them.
* **Rename across programs.** A module reached from several roots gets
  edits from each; the server unions them by position. A name spelled
  through a namespace qualifier is renamed at its leaf. Builtins and
  keywords are not renamable, nor are synthesized names.
* **Whether the CLI shows columns for typecheck errors.** With columns
  known, `goose check` could print `file:line:col:` heads and a caret
  under the node as the lexer does. The test runner and the extension's
  parser both accept the column already (§2), so this is a choice of
  presentation for after phase 0, with the JSON form available either way.
* **Position encoding.** VS Code negotiates UTF-16; the server converts.
  Supporting `utf-8` for clients that offer it is a `positionEncoding` in
  the initialize result and no conversion, once a client wants it.
* **A browser extension.** Running the compiler in the extension host as
  WebAssembly would serve `vscode.dev`; the compile thread, the process
  runner and TinyCC make that a port, not a build flag. Not planned.
* **Memory.** One `Ast` per root, replaced whole; a workspace with many
  configured entries holds many. If that matters, roots can be analyzed
  lazily (only those with open files) and dropped when their files close,
  which the root selection of §4 already allows.

## 11. Exemplar instantiation

A generic function nothing calls has no specialization, so nothing inside
it has a type. An exemplar is the specialization the server asks for in
place of the missing call: the function checked once with each type
parameter bound to an opaque type.

**Opaque types.** Each type parameter, and each untyped parameter, is
bound to a fresh `TY_GENERIC` marked `TypeName::opaque`, which prints as
the parameter's name (`T`; `type of x` for an untyped `x`). It stands for
itself: equal only to itself (by its `TypeName`, which a `const` copy
shares), concrete as far as the checker is concerned (substitution leaves
it alone, `HasGenerics` is false of it, `ValidateType` accepts it), and a
fixed-size, flat value holding no references, which is what the size-class
and root queries already say of a kind they do not otherwise handle. It
has no fields, members or operators of its own; the check gives it these:

- A binary operator with an opaque operand gives `bool` for a comparison
  and the opaque operand's type for anything else (the left one's where
  both are), so `a + b` on two `T`s is a `T`; unary `-` and `~` give the
  operand's type, and a compound assignment to an opaque target takes any
  operand.
- A member access on an opaque value, read or written, gives a value of
  one more opaque type, `?`, which all such accesses share: the exemplar
  cannot know what the member is. So does a call of a function with an
  opaque argument (one at any depth of the argument's type) that none of
  its overloads takes, `hash(key)`, which only a real binding could
  resolve; an overload with a generic parameter takes it, and the callee
  is specialized for the opaque binding as for any other.
- An `as` from or to an opaque type gives the target type.
- Anything else is an error: a literal where an opaque type is expected,
  a builtin given an opaque value, a type parameter called as a function
  value (`F(x)`), indexing, iterating or testing an opaque value.

**The check.** The server sets `Ast::exemplars`; `goose check` never does.
After the real roots and the whole-program fixups, every top-level
function with type parameters or untyped parameters that no root
specialized gets an exemplar, checked as `CheckUnreached` checks a plain
function nothing reached (each reference, slice or holder parameter rooted
at a class of its own, writable), in a mode where an error is not
reported: a `CompileError` or an `Abandoned` ends the exemplar silently,
returning the checker to the root's entry as `CheckRoot` does, and no
warning is given. What the check annotated before the error stays, so an
exemplar is as useful as the code before the first thing the opaque types
do not support; for a body that only moves its `T`s around, compares them
and passes them on, that is all of it.

**Isolation.** Exemplars run after everything that reports, so nothing they
record reaches a fixup or a diagnostic. A call inside an exemplar does not
reuse a specialization a real root made, since a reuse merges what the
call site says into facts left out of the key (`RootArg::exact`,
`concrete`, the needed targets), which the later passes read; it makes its
own. When the exemplars are done, every specialization they made leaves
`ast.fnspecs` and its function's `specs` for `Ast::exemplarspecs`, marked
with the function whose exemplar made it (`FnSpec::exemplar`): the
optimizer, bounds-check elimination, `--specs` and the counts never see
one, and no real call can reuse one, since every real call came first.

**The server.** `Analysis::Build` indexes those of the exemplar
specializations that belong to the exemplar's function or to a function
nested in it (a callee's specialization made for it carries the caller's
opaque types, which say nothing about the callee), after the real ones,
and remembers which clones they are. Hover on `a` then shows `a: T`, with
"in an exemplar instantiation" after it; completion after a `.` reads the
object's type from the clone, as for any value (the builtin members of a
slice's element, or for an opaque value only the functions whose first
parameter is generic); signature help and definitions follow the clone's
resolutions, and references and rename see the uses; a name past the
error that ended the exemplar says that the exemplar stopped before it.

## 12. The sweep, and its baseline

`test/lsp_sweep.py` is the server's robustness and latency sweep over the
repository's Goose programs, run by hand (`--quick` is the subset a test
suite could afford; `run_tests.py` does not run it today). Every file under
`samples/`, `test/goose_in_goose/main.goose`, every stdlib module, every
positive fixture under `test/*/` (but `errors/`, `errors_tc/`, `lsp/` and
`goose_in_goose/`), and every `errors/` and `errors_tc/` fixture is opened
as a root of its own on one of eight servers; the sweep waits for its
diagnostics, asks hover, definition, completion and signature help at
every identifier start and at every position right after a `.` (found
lexically, outside comments and strings), asks semantic tokens, document
symbols, folding ranges, inlay hints and code lenses once, sends a burst
of ten random one-character edits and waits for their diagnostics, puts
the text back and waits for the first diagnostics to be published again,
and closes the file. It fails on a server exit, a reply that is an error,
a request slower than two seconds, a server that stops answering, or
output that is not a message. The report lists the files sorted by their
slowest request, the slowest request per method, every failure, and the
totals that follow.

**Baseline** (October 2026, this machine, eight servers at once):

| | |
|---|---|
| Files | 2,531 (31 samples, 1 bootstrap, 11 stdlib, 581 fixtures, 60 `errors/`, 1,847 `errors_tc/`) |
| Positions | 260,878 |
| Requests | 1,058,698 |
| Slowest request | 3.5 ms (completion, `test/ui/ui_popups.goose`) |
| Slowest per method | semantic tokens 2.8 ms; signature help 1.7 ms; hover, definition 0.3 ms; code lens, symbols 0.2 ms; inlay hints, folding 0.1 ms |
| Analysis (didOpen to its diagnostics) | median 0.6 ms, 90th percentile 4 ms, slowest 154 ms (`goose_in_goose/main.goose`) |
| Analysis after the burst of edits | median 161 ms, of which 150 ms is the debounce |
| Whole sweep | 123 s |

What the numbers mean, for the next person:

* **Every request is under 2 ms**, over a million of them; completion
  is the costliest per request (it writes every name in scope with its
  detail), then semantic tokens. The slowest request the sweep ever saw
  was 15 ms, before the second fix below.
* **First finding: a byte quoted on its own.** The lexer's "illegal
  character" and "unknown escape sequence" messages quoted one byte of
  the character, so a multibyte character (a non-breaking space in a
  comment whose `//` an edit had broken) put invalid UTF-8 in the
  diagnostic, and in the server's JSON, which a client decoding UTF-8
  strictly cannot read. The lexer now quotes the whole character with
  its code point (`Utf8CharAt`, `QuotedCharAt`;
  `errors_tc/recovery/lexer.goose` has one).
* **Second finding: the first request after an analysis waited for the
  last analysis to be freed.** Replacing a root's analysis destroyed the
  old `Ast` on the worker, after publishing: 6 ms for a ui fixture, 8 ms
  for the doom sample, 13 ms for the bootstrap, a tenth of the check's
  time, paid by whatever request came next, which in an editor is the
  refresh of the lenses, tokens and hints after every analysis. The old
  analysis, and a removed root's, is now freed on a thread of its own
  (`Workspace::Reap`), and the request after an analysis or a close is
  as fast as any.
* No request was answered with an error, no server exited or stopped
  answering, and the diagnostics after the text was put back were
  always the first ones again.
* **A root's errors may be published under another file's URI.** Eleven
  `errors_tc/` fixtures, and two `errors/` ones, show no diagnostic in
  the root file: their error is located in `std.goose`,
  `exception.goose`, `dictionary.goose` or a package file, in a function
  instantiated from the fixture, and is published for that file, with
  the instantiation chain as related information pointing back into the
  root. An editor shows it in its problems list under the stdlib file.
  Whether such an error should also be reported at the instantiating
  call in the root is open.
* **Package errors are as `--offline` reports them.** A sample or fixture
  importing a package not in `GOOSE_HOME/pkg` (the sweep points the
  servers at `build/goose-home` where it exists, else at `$GOOSE_HOME`)
  gets the "not fetched" error at its import and is otherwise analyzed;
  the gfx `_err_` fixtures report their shader errors as `goose check`
  does.
