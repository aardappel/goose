# Debugging Goose programs

<details>
<summary>Table of contents</summary>

- [0. Summary](#0-summary)
- [1. What a debugger sees today](#1-what-a-debugger-sees-today)
- [2. What is wanted](#2-what-is-wanted)
- [3. Option A: native debug information, existing debuggers](#3-option-a-native-debug-information-existing-debuggers)
  - [3.1 Source lines: `#line`](#31-source-lines-line)
  - [3.2 Keeping the C close to the source](#32-keeping-the-c-close-to-the-source)
  - [3.3 Showing Goose values](#33-showing-goose-values)
  - [3.4 Editors](#34-editors)
  - [3.5 Testing](#35-testing)
  - [3.6 What it cannot do](#36-what-it-cannot-do)
- [4. The JIT](#4-the-jit)
- [5. Option B: `goose dap`, a debugger in the compiler](#5-option-b-goose-dap-a-debugger-in-the-compiler)
  - [5.1 Shape](#51-shape)
  - [5.2 Stopping: statement sites](#52-stopping-statement-sites)
  - [5.3 Frames and values](#53-frames-and-values)
  - [5.4 Expressions in Goose](#54-expressions-in-goose)
  - [5.5 Built programs and threads](#55-built-programs-and-threads)
  - [5.6 What it cannot do](#56-what-it-cannot-do)
  - [5.7 What it costs to keep](#57-what-it-costs-to-keep)
- [6. A and B compared](#6-a-and-b-compared)
- [7. Recommendation and order of work](#7-recommendation-and-order-of-work)
- [8. Open questions](#8-open-questions)
- [References](#references)

</details>

Research note, November 2026. Nothing here is implemented. The question
was how a Goose program should be debugged in VS Code and in other
editors, given that the compiler emits C: by default a debugger shows that
C, with C names and C types. Two options are compared. Option A gives the C
compilers' debug information Goose's source lines and names, and leaves
the debugging to the native debuggers that already speak the Debug Adapter
Protocol. Option B builds a debugger into the compiler, `goose dap`, which
runs the program itself and shows it in Goose terms. Section 3 is A,
section 5 is B, section 6 compares them, section 7 gives the
recommendation and the order to build it in.

## 0. Summary

**Option A** is recommended. `#line` directives map the generated C back to
Goose lines in every C compiler's debug information (PDB or DWARF). Under
`--debug` the generated C is shaped so that a C debugger reads it well
(typed resizable headers, named structs, compiler temporaries out of the
way), and the compiler writes a description of how each Goose type is
represented. Visualizers built from that description show Goose values:
a natvis file generated per program for Microsoft's debugger, and one
static LLDB module for every other editor and platform. The VS Code
extension builds with `--debug` and hands the session to an existing
debug adapter. Nobody writes a debugger.

**Option B**, a DAP server in the compiler driving the program through
hooks the code generator inserts, is the only way to debug under the JIT
and the only way to get Goose names, exact scopes and Goose-syntax watch
expressions everywhere. It costs a debugger's worth of code (stopping,
stepping, frames, a value printer, an expression compiler, a protocol
server), it cannot see into C, and it cannot debug programs with threads
under the JIT. It is the option to revisit if debugging without a C
toolchain turns out to matter to many users.

**The JIT** gets no interactive debugger under A. What it can get cheaply
is a stack trace in Goose lines when a program crashes, from TinyCC's own
backtracer (section 4).

## 1. What a debugger sees today

`goose build --debug` compiles the C with `/Od /Zi` or `-O0 -g`
(`src/build.h`) and turns on the overflow, range and tag checks. That is
all the support there is. The VS Code extension contributes a `goose`
debug type, but it only runs the program with the JIT in a task
(`noDebug`) and refuses `attach`.

The C that `goose -o shapes.c samples/05_shapes.goose` writes shows what a
C debugger has to work with:

* **No line mapping.** There is no `#line`, so breakpoints and steps are
  in `shapes.c`, and a `.goose` file cannot take a breakpoint.
* **The Goose inliner still runs.** `--debug` leaves the Goose compiler at
  its default `-O2` (`src/main.cpp`), so a call stack can miss functions
  that were inlined into their callers before the C was written.
* **Every local is declared at the top of its function.** `main_g` starts
  with at least 90 declarations, 70 of them compiler temporaries `t0`,
  `t1`, ... A Goose variable keeps its name with a suffix (`total_g`), and
  shadowed ones become `s_g`, `s_g_2`, `s_g_3`, all visible at once.
  Functions are named likewise (`area_g_2` is one of the `area` case
  functions).
* **Fixed ADTs and slices read reasonably.** A tag and a union of the
  variants' payloads, and a typed pointer with a length:

  ```c
  struct Shape_g {
      uint8_t tag;
      union {
          Shape_g_Circle_p v_Circle_g;
          Shape_g_Rect_p v_Rect_g;
          Shape_g_Tri_p v_Tri_g;
      } u;
  };
  typedef struct { uint8_t *data; int64_t len; } sl_u8;
  ```

  The debugger shows the tag as a number and every union member, and a
  slice as a pointer to its first element.
* **Resizables have lost their element type.** Every resizable array's
  header is the same `gs_rhdr { uint8_t *base; int64_t len; }`, so nothing
  in the debug information says that `shapes_g` holds `Shape_g` values. No
  visualizer can show its elements. This is the one representation that
  blocks Option A, and it is fixed in the C (section 3.2), not in a
  visualizer.
* **The rest is bytes.** Variable-mode ADT values and arrays of them
  (`r_Shape_g_vm` is a `uint8_t *`), varints, self-relative and
  pool-relative references (integers), fat references (`gs_rref`, a header
  pointer and a stack) and root structs (a descriptor of field references)
  all need code to decode.

## 2. What is wanted

From the user's side:

1. Breakpoints and stepping in `.goose` files, in the standard library's
   files too.
2. A call stack of Goose functions.
3. Locals by their Goose names, only those in scope.
4. Values as Goose values: an array's elements, the active variant of an
   ADT, text as text, a reference followed to what it refers to.
5. Watch and console expressions.
6. VS Code first, then the editors `docs/editors.md` covers (Neovim,
   Helix, Emacs), on Windows, Linux and macOS.
7. A stop at a runtime error (a failed check, `abort`) or a crash, with the
   frames intact.

From the project's side: few moving parts, and little to keep in step as
the language and its representations change. Each part should be tested
by the suite, so that a codegen change that breaks debugging fails a test.

## 3. Option A: native debug information, existing debuggers

The Debug Adapter Protocol is how an editor talks to a debugger; the
debugger still has to exist. Native debuggers for C already speak it:
Microsoft's (`cppvsdbg`, through the C/C++ extension), LLDB's `lldb-dap`
(shipped with LLVM) and CodeLLDB, and GDB, which has had a DAP interpreter
since version 14. They already do breakpoints, stepping, threads, attach,
core dumps and data breakpoints, for every C compiler Goose builds with.
Option A makes the C they see carry Goose's lines, names and types, and
writes nothing that speaks DAP.

### 3.1 Source lines: `#line`

The code generator writes `#line N "path.goose"` before the C of each Goose
statement, with the statement's `Line` (which already carries the file and
line, `src/ast.h`). MSVC puts it in the PDB's line table and gcc and clang
in DWARF's, so every native debugger then sets breakpoints in, steps
through, and shows the call stack in, `.goose` files. TinyCC's line
information follows `#line` too, which section 4 relies on.

* A statement that becomes several C statements is one step, since the
  debugger steps by line.
* The column is lost: two Goose statements on one line are one step.
* Every C line after the first `#line` belongs to some Goose line. Helpers
  generated per type (`gs_size_Shape_g_vm`) take the line of the type's
  declaration, so stepping into one lands on the enum; everything with no
  Goose line goes before the first `#line`, where it keeps its C lines.
* Paths are absolute, so the debugger needs no source map.
* Standard library functions get their own files' lines. Stepping into
  them is what a user asks for with "step in", and it shows the library's
  source rather than C.

Runtime functions (`gs_*`) have no Goose source. Debuggers can be told to
step over them: LLDB's `target.process.thread.step-avoid-regexp`, and
Visual Studio's Just My Code rules for native code (`.natjmc`), whose
effect under `cppvsdbg` is still to be checked.

`--debug` should also imply the Goose compiler's `-O0`, so that the stack
the debugger shows is the one the source describes. `-O1` and `-O2` with
`--debug` stay available for debugging what the inliner did.

### 3.2 Keeping the C close to the source

Under `--debug` only, the code generator changes the shape of what it
writes, never what it does:

* **Typed resizable headers.** One header type per element type,
  `struct gs_rhdr_Shape_g { Shape_g *base; int64_t len; }`, laid out like
  `gs_rhdr`, so the runtime functions that take a `gs_rhdr *` take it by a
  cast. With this, a resizable shows its elements the way a slice does.
* **Every struct has a tag.** `typedef struct sl_u8 { ... } sl_u8;`, so
  that natvis, GDB and LLDB all have the same name to match.
* **Temporaries out of the way.** The compiler's temporaries become fields
  of one local struct, `gs_tmp`, which a Locals view shows as one
  collapsed entry. At `-O0` this costs nothing: every local lives in memory
  anyway.
* **Locals in C blocks** where the Goose scopes allow it, so a shadowed
  variable leaves the Locals view when its Goose scope ends. Whether the
  code generator can declare locals where their scopes begin, rather than
  at the top, is an open question (section 8).
* Possibly **no `_g` suffix** on locals whose names cannot collide with a
  C keyword or a runtime name, so that a C watch expression of a plain
  variable reads like Goose.

These are code generator changes, and the suite tests them: a debug build
of every fixture must still produce the expected output.

### 3.3 Showing Goose values

The code generator knows how every Goose type is represented, because it
chose the representation. Under `--debug` it writes that down, beside the
executable, as a description: per C type, the Goose type it represents and
which kind of representation it is, with the kind's facts (a slice's or
resizable's element type, an ADT's variant names by tag and the union
member each one's payload is in, a relative reference's width, target type
and pool, a variable-mode value's layout, a root struct's fields). The
visualizers interpret that description. They contain one piece of code per
*kind* of representation, and none per type.

**Microsoft's debugger** reads natvis, which matches C type names exactly
(its wildcards are for C++ template arguments), so the compiler generates
the natvis for each program from the description. For an ADT:

```xml
<Type Name="Shape_g">
  <DisplayString Condition="tag == 0">Circle {u.v_Circle_g}</DisplayString>
  <DisplayString Condition="tag == 1">Rect {u.v_Rect_g}</DisplayString>
  <DisplayString Condition="tag == 2">Tri {u.v_Tri_g}</DisplayString>
  <DisplayString Condition="tag == 3">Dot</DisplayString>
  <Expand>
    <ExpandedItem Condition="tag == 0">u.v_Circle_g</ExpandedItem>
    <ExpandedItem Condition="tag == 1">u.v_Rect_g</ExpandedItem>
    <ExpandedItem Condition="tag == 2">u.v_Tri_g</ExpandedItem>
  </Expand>
</Type>
```

Slices and resizables are `ArrayItems` over the data pointer and length,
text is a length-limited UTF-8 string, and a relative reference is pointer
arithmetic and a cast (a pool's base is a global, which natvis expressions
can name). Walking an array of variable-mode values needs natvis's
`CustomListItems` loop, which can do it, awkwardly. The linker's
`/NATVIS:` option embeds the file in the PDB, so Visual Studio and VS
Code's `cppvsdbg` find it with no configuration.

**LLDB** gets one Python module, `goose_lldb.py`, kept in the repository
and the same for every program. LLDB matches formatters by regular
expression on type names (`type summary add -x`, `type synthetic add -x`),
so the module registers one formatter per kind and looks the type up in
the program's description. Python makes the awkward cases easy: a varint
is a loop, an array of variable-mode values is a walk with a limit.
LLDB reads DWARF on all three platforms and PDBs on Windows, so this one
module serves every editor on every platform; the natvis adds Microsoft's
debugger where that is used.

**GDB** has its own Python API. The decoding can be shared with the LLDB
module behind a thin layer per debugger; GDB's front end can wait until
someone asks for it, since `lldb-dap` debugs gcc's output as well. GDB
loads a script named in an object's `.debug_gdb_scripts` section, as
Rust's standard library does, so it would need no configuration. LLDB has
no equivalent outside macOS dSYM bundles; the launch configuration imports
the module.

When the language changes, a type built from existing kinds needs nothing.
A new kind of representation needs a description kind, a natvis template
and a Python class: three small pieces in three places, generated from or
driven by one description that the code generator writes.

### 3.4 Editors

In VS Code the extension changes in two places:

* `"breakpoints": [{ "language": "goose" }]` in `package.json`'s
  `contributes`. Without it VS Code does not let anyone set a breakpoint in
  a `.goose` file (short of `debug.allowBreakpointsEverywhere`).
* The `goose` debug type (`vscode/src/extension.js`, where it resolves a
  launch) keeps running the JIT task for `noDebug`. For a debug launch it
  runs `goose build --debug` as a task, so build errors land in the
  Problems view, then starts the native session with
  `vscode.debug.startDebugging` and ends the `goose` one. The native
  configuration is `cppvsdbg` for an MSVC or clang-cl build when the C/C++
  extension is installed, and otherwise `lldb-dap` or CodeLLDB with
  `command script import goose_lldb.py` in its initial commands. A launch
  with neither installed says which extension to install.

Microsoft's debugger is licensed for Microsoft's own editors only (VS Code
as Microsoft builds it, and Visual Studio), so VSCodium, Cursor and every
other editor use LLDB on Windows too. Other editors need no Goose code:
nvim-dap, Emacs's dape, Helix's DAP support and Zed all launch `lldb-dap`
with a program and initial commands, and `docs/editors.md` gets a snippet
for each, the way it has one for the language server. Visual Studio itself
opens the executable with its PDB, natvis included, with no extension at
all.

### 3.5 Testing

A fixture per kind of representation, built with `--debug`, and a script
that runs the debugger in batch mode, sets a breakpoint by Goose line,
prints the locals and the stack, and compares them with expected text:

```bash
lldb -b -o "command script import goose_lldb.py" -o "b shapes.goose:120" \
     -o run -o "frame variable" -o bt ./shapes
```

On Windows, `cdb` does the same for Microsoft's debugger, with `.nvload`
and `dx`. The tests run where a debugger is installed and are skipped
elsewhere, as the native-package fixtures are. A representation change that
breaks a visualizer then fails a test instead of a user's session.

### 3.6 What it cannot do

* **Watch expressions are C.** The debugger evaluates
  `s_g.u.v_Circle_g.r_g`, not `s.r`, and cannot call a Goose function by
  its Goose name. Visualizers change how values look, not what can be
  typed.
* **No debugging under the JIT** (section 4), and a debug session needs a
  C compiler and a debugger installed. On Windows, `goose run` needs
  neither.
* **Scopes are approximate.** Grouping temporaries and declaring locals in
  blocks hides most of the C; what remains shows.
* **Values look different in different debuggers**, because natvis and
  Python are two implementations of the same description.

## 4. The JIT

Under Option A the JIT gets no interactive debugging. A native debugger
would attach to the `goose` process, and the program's code in it has no
debug information any debugger loads:

* Windows debuggers read PDBs, which TinyCC cannot write.
* GDB and LLDB have a JIT interface: the JIT builds an object file in
  memory, with its debug information, and registers it through
  `__jit_debug_register_code`. TinyCC does not implement it, and it would
  need an ELF object with DWARF for every run. Neither Windows debugger
  would load one.

So a debug launch builds an executable, and "run" stays the JIT. What the
JIT can get is a Goose stack trace when a program crashes. TinyCC has a
backtracer of its own (`tccrun.c`, and `tcc_set_backtrace_func` in
`libtcc.h`) that maps the faulting address and its callers to file and
line through the in-memory line information, which with `#line` names
`.goose` lines. `cmake/libtcc.cmake` turns it off with bounds checking,
because the support objects behind them need the full Windows SDK headers
([jit_backend.md](jit_backend.md#backtraces-and-bounds-checking)). Reading
TinyCC's sources, that applies to `bt-exe.c` (an executable's backtracer,
which includes `tccrun.c` and so `windows.h`) and `bcheck.c`. An in-memory
program adds only `bt-log.c`, which includes `stdarg.h`, `stdio.h` and
`string.h`, and the backtracer itself is in `tccrun.c`, which the host
compiler builds with the SDK at hand. Backtraces alone may therefore be
possible on Windows too. That is unverified, and the runtime's own fault
handling (it commits stack pages on a fault on Windows) has to keep
running in front of TinyCC's.

## 5. Option B: `goose dap`, a debugger in the compiler

### 5.1 Shape

`goose dap` would be a command beside `goose lsp`, speaking DAP on stdin
and stdout. DAP's base protocol is the same Content-Length framing as
LSP's, so the language server's transport (`src/lsp_rpc.h`: the reader
thread and the JSON writer) carries over. The requests are fewer than the
language server's: launch, setBreakpoints, configurationDone, threads,
stackTrace, scopes, variables, continue, next, stepIn, stepOut, pause,
evaluate, setVariable, disconnect, and the stopped, output and exited
events.

The program runs in the compiler's process through the JIT, as `goose run`
does. The compile already has a thread of its own and the program the main
thread; the protocol server would be a third. Every editor with a DAP
client gets the same debugger, and nothing outside `goose` is needed.

### 5.2 Stopping: statement sites

The code generator numbers every place a stop can happen (each statement,
and each loop's back edge) and emits a check there:

```c
GS_DBG_SITE(17);    // if (gs_dbg_armed) gs_dbg_stop(17, &gs_f);
total_g = 0.0;
```

A site table records each site's file, line, column, function and the
locals in scope there. `gs_dbg_armed` is set whenever any breakpoint,
step or pause is pending, and `gs_dbg_stop` checks the site against them
and, to stop, blocks the program's thread until the protocol server says
to go on. A breakpoint marks the sites on its line. Stepping compares the
depth of the frame stack (5.3): over stops at the next site at the same
depth or shallower, in at the next site, out at the next shallower one. A
pause sets `gs_dbg_armed` from the server's thread. Since a site knows its
column, stepping can go statement by statement within a line, which
Option A cannot.

When nothing is set, a site costs a load and a branch not taken. Under
TinyCC, which inlines nothing and keeps locals in memory, that adds less,
relatively, than it would to optimized code. It has not been measured.

### 5.3 Frames and values

Each function links a frame record on entry and unlinks it on every exit:
its function's descriptor, the caller's record, the current site, and the
addresses of its locals. `return … from` propagates through ordinary
returns (spec appendix C.3), so ordinary exits are all there are; an abort
ends the program. Under TinyCC, whose locals live in memory anyway, taking
their addresses costs nothing.

Because the program shares the process, the debugger reads its memory
directly, and the value printer is C++ in the compiler working from the
compiler's own types: a resizable, a variable-mode array, a relative
reference, a root struct or a pool is decoded by code that sits next to the
code that chose its representation. To keep the C compiler the authority on
layout (packed structs, frame objects, root descriptors), the code
generator emits `offsetof` and `sizeof` tables for the types the debugger
shows rather than the printer working the layout out again. Locals come
from the site table, so the debugger shows exactly the Goose variables in
scope, by their Goose names, and never a temporary.

A failed check or an `abort` calls into the debugger before the runtime
reports it, so the program stops on the failing line with its frames and
locals intact. A crash in generated code reaches the runtime's fault
handler, which can stop the same way.

### 5.4 Expressions in Goose

An `evaluate` request's text is parsed and type-checked in the scope of
the site the program stopped at (the language server already resolves
names at a position), compiled to C as a function that takes the frame
record, compiled by TinyCC into the running process, and called on the
stopped thread. Watch expressions are then Goose, function calls included,
and they mean what the same expression means in the program. The
evaluation has to contain what the expression does: a check that fails in
a watch must stop the evaluation, not the program, and the data stacks'
tops must be restored afterwards.

### 5.5 Built programs and threads

A built executable runs in its own process, where the compiler is not.
Option B either stays JIT-only, or the debug runtime linked into a
`--debug` executable listens on a pipe and serves a small protocol of its
own (read memory, mark sites, resume, list frames) that `goose dap` drives:
a `gdbserver` written for Goose. The printer stays in the compiler. The
expression compiler cannot load code into another process, so expressions
in a built program would be limited to what can be interpreted: names,
fields, indexing.

The JIT refuses programs that start threads (TinyCC cannot place
thread-local storage; [jit_backend.md](jit_backend.md#threads)), so
JIT-only B cannot debug any program with a worker until that is solved.
With threads, each worker needs its own frame stack, and "pause" has to
stop every thread at its next site; a worker blocked in C (a queue, a
read) reaches no site, and the debugger has to report it as running.

### 5.6 What it cannot do

* **C is invisible.** Extern functions, packages' C and the runtime have
  no sites and no frame records: no stepping into them, and a crash in one
  shows only the Goose frames above it.
* **No crash dumps, no attaching** to a process not started by `goose dap`,
  **no optimized builds** (the sites and frame records keep the program's
  shape), and **no data breakpoints** except by checking at every site.
* **The program shares the debugger's process.** Goose code cannot write
  outside its data, but C code can, and a stray write from it can corrupt
  the debugger and the compiler's state, which the printer then reads.
* Compiler work still needs a native debugger on the generated C, so
  Option A's `#line` and debug builds are needed anyway.

### 5.7 What it costs to keep

* Site placement and scope tables for every construct that adds control
  flow: `match` arms, `guard`, `defer`, `return … from`, loops, inlined
  function values, and the coroutines proposed in
  [coroutines.md](coroutines.md), whose frames are values that outlive the
  calls that resume them and so would need frame records of a new kind.
* A printer for every representation. It is one implementation, against
  the compiler's own model, where A has a description and two
  interpreters of it; but it is code that runs against memory that may be
  wrong, and has to be written defensively.
* The stop, step and pause machinery, and, if B covers built programs,
  the pipe protocol and stopping threads.
* The expression compiler's containment of aborts and stack tops.
* The protocol across clients. DAP clients differ less than LSP clients,
  but each has its quirks, as `docs/editors.md` records for the language
  server.

B tests well: like the language server, the suite can drive `goose dap`
from Python (`test/lsp_protocol.py`'s client, with DAP messages) and
compare stops, stacks and values without any debugger installed.

## 6. A and B compared

| | A: native debuggers | B: `goose dap` |
|---|---|---|
| Needs a C compiler and a debugger | yes | no, under the JIT |
| Programs with threads | yes | not under the JIT until it has thread-local storage; built programs only with B's own stop-the-world |
| Editors | every DAP client, plus Visual Studio; Microsoft's debugger only in VS Code and Visual Studio | every DAP client |
| Breakpoints and steps | by line | by line and column |
| Locals | C names, all of a function's, temporaries grouped | Goose names, exactly those in scope |
| Values | visualizers interpreting a compiler-written description, natvis and LLDB | the compiler's own types |
| Watch expressions | C | Goose, calls included |
| Runtime errors | a function breakpoint on the runtime's failure path | a stop on the failing line |
| C and extern frames | yes | invisible |
| Crash dumps, attach, optimized builds | yes | no |
| Cost in the program | none beyond a debug build | a check per statement, a frame link per call |
| When a representation changes | a description kind, a natvis template, a Python class | printer code |
| When control flow changes | nothing (the C compiler places lines) | site and scope placement |
| What there is to own | `#line` and debug shapes in the code generator, a natvis generator, one Python module, extension glue | a debugger: protocol server, stop and step machinery, printer, expression compiler, frame records; more for built programs and threads |
| First useful result | `#line` and the VS Code launch: breakpoints, steps and stacks in Goose source | most of the debugger has to exist before one breakpoint works |

Rough sizes, for scale only: A is low thousands of lines, mostly
data-driven; B is several thousand for the JIT alone, and more for built
programs and threads.

## 7. Recommendation and order of work

Option A, in steps that are each useful on their own:

1. **Source lines.** `#line` in the generated C, `--debug` implying the
   Goose compiler's `-O0`, the `breakpoints` contribution and the debug
   launch in the VS Code extension. After this, VS Code, Visual Studio and
   `lldb-dap` step through Goose source with Goose call stacks, and
   scalars, structs, fixed arrays and fixed ADTs are readable.
2. **Debug shapes** in the code generator (3.2), typed resizable headers
   first.
3. **The description and `goose_lldb.py`**, with the batch-mode tests.
   This covers every editor on every platform.
4. **Generated natvis**, embedded in the PDB.
5. **JIT backtraces** (section 4), independent of the rest.
6. Editor snippets in `docs/editors.md`; GDB's front end when asked for.

Option B is worth revisiting if a large share of users debug without a C
toolchain, if Goose-syntax evaluation turns out to matter more than C
watch expressions suggest, or if the visualizers cannot show
variable-mode, relative or root data acceptably. Much of A carries over:
the line and scope information the code generator collects for `#line`,
the description of representations (which B's printer could consume rather
than walking the compiler's model), the test fixtures, and the `goose`
debug type in the extension, which would become a real adapter.

## 8. Open questions

* Can locals be declared where their scopes begin, or does the code
  generator's structure (temporaries live across statements, jumps for
  `defer` and `return … from`) need them at the top?
* Should `#line` be written in release builds too? Profilers and crash
  dumps would then name Goose lines; generated C read by people, and the
  corpus diffs (`test/emit_order.py`, the differential harness) would see
  more churn.
* Is LLDB's PDB support good enough for C on Windows (locals, unions,
  bit-exact packed structs), so that one Python module really serves every
  non-Microsoft editor there?
* Where does the description live: a file beside the executable, or a
  section in it? A file is simplest; a section cannot be separated from
  the executable it describes.
* What does TinyCC's own debug information (`-g` to a file) give LLDB on
  Windows? A TinyCC debug build would let a machine with no other C
  compiler debug with LLDB alone.
* How do coroutine frames appear in a call stack under each option?

## References

* Debug Adapter Protocol: https://microsoft.github.io/debug-adapter-protocol/
* VS Code, debugger extensions: https://code.visualstudio.com/api/extension-guides/debugger-extension
* Natvis: https://learn.microsoft.com/en-us/visualstudio/debugger/create-custom-views-of-native-objects
* LLDB variable formatting: https://lldb.llvm.org/use/variable.html
* lldb-dap: https://github.com/llvm/llvm-project/tree/main/lldb/tools/lldb-dap
* GDB's DAP support: https://sourceware.org/gdb/current/onlinedocs/gdb.html/Debugger-Adapter-Protocol.html
* GDB's JIT interface: https://sourceware.org/gdb/current/onlinedocs/gdb.html/JIT-Interface.html
* GDB's `.debug_gdb_scripts` section: https://sourceware.org/gdb/current/onlinedocs/gdb.html/dotdebug_005fgdb_005fscripts-section.html
* Rust's embedded debugger visualizers: https://doc.rust-lang.org/reference/attributes/debugger.html
* The licence of Microsoft's debugger: https://github.com/OmniSharp/omnisharp-vscode/wiki/Microsoft-.NET-Core-Debugger-licensing-and-Microsoft-Visual-Studio-Code
