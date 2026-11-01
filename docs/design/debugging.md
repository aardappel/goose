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
- [6. Option C: `goose dap` in front of lldb-dap](#6-option-c-goose-dap-in-front-of-lldb-dap)
  - [6.1 Shape](#61-shape)
  - [6.2 The debug map](#62-the-debug-map)
  - [6.3 What the adapter shows](#63-what-the-adapter-shows)
  - [6.4 What it depends on](#64-what-it-depends-on)
  - [6.5 What it cannot do](#65-what-it-cannot-do)
  - [6.6 What to tighten](#66-what-to-tighten)
- [7. The three compared](#7-the-three-compared)
- [8. Recommendation and order of work](#8-recommendation-and-order-of-work)
- [9. Open questions](#9-open-questions)
- [References](#references)

</details>

Research note, November 2026. Nothing here is on master. The question was
how a Goose program should be debugged in VS Code and in other editors,
given that the compiler emits C: by default a debugger shows that C, with
C names and C types. Three options are compared. Option A gives the C
compilers' debug information Goose's source lines and names, and leaves
the debugging to the native debuggers that already speak the Debug Adapter
Protocol, with visualizers inside them for Goose values. Option B builds a
debugger into the compiler, `goose dap`, which runs the program itself and
shows it in Goose terms. Option C, which a prototype on a fork implements,
is also a `goose dap`, but one that stands in front of LLDB's adapter: LLDB
runs the program, and the compiler's adapter turns what LLDB reports into
Goose. Section 3 is A, section 5 is B, section 6 is C, section 7 compares
them, section 8 gives the recommendation and the order to build it in.

## 0. Summary

**Option C** is recommended. `#line` directives map the generated C back to
Goose lines in every C compiler's debug information (PDB or DWARF), which
every option needs. `goose build --debug` writes a map of each function's
locals and each type's representation, and `goose dap` speaks DAP to the
editor while driving `lldb-dap` underneath: LLDB does breakpoints,
stepping, threads and unwinding, and the adapter reads values out of
memory by their Goose types, names frames and locals as the source does,
steps past the compiler's code and stops on failed checks. The Goose
knowledge is in one place, in the compiler, changing with the code
generator and tested by the suite, and every editor with a DAP client runs
the same command. It depends on how `lldb-dap` behaves, not only on the
protocol, and leaves out Microsoft's debugger.

**Option A** puts the Goose knowledge into the debuggers instead: a natvis
file generated per program for Microsoft's debugger and an LLDB module for
the rest, interpreting the same kind of description, with the generated C
reshaped under `--debug` so that a C debugger reads it well. It keeps
Visual Studio and `cppvsdbg`, but it has two interpreters of the
description, shows C names and C expressions, and needs configuration in
each editor. Its natvis half stays worth having if Visual Studio users ask.

**Option B**, a DAP server in the compiler driving the program through
hooks the code generator inserts, is the only way to debug under the JIT
and the only way to get Goose-syntax watch expressions with calls. It
costs a debugger's worth of code (stopping, stepping, frames, a value
printer, an expression compiler, a protocol server), it cannot see into C,
and it cannot debug programs with threads under the JIT. It is the option
to revisit if debugging without a C toolchain turns out to matter to many
users, and C's map, value printer and tests carry over to it.

**The JIT** gets no interactive debugger under A or C. What it can get
cheaply is a stack trace in Goose lines when a program crashes, from
TinyCC's own backtracer (section 4).

## 1. What a debugger sees today

`goose build --debug` compiles the C with `/Od /Zi` or `-O0 -g`
(`src/build.h`) and turns on the overflow, range and tag checks. On master
that is all the support there is. The VS Code extension contributes a `goose`
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
  blocks Option A's visualizers; A fixes it in the C (section 3.2), and C
  by naming the element type in its map (section 6.2).
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
  at the top, is an open question (section 9).
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

Under A and C the JIT gets no interactive debugging. A native debugger
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
column, stepping can go statement by statement within a line, which A and
C cannot.

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
  the `#line` and debug builds of A and C are needed anyway.

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

## 6. Option C: `goose dap` in front of lldb-dap

A prototype on a fork implements this option (`pdoane/goose`, branch
`dap-prototype`, commits `190a9e6a` and `4f2c161e`, November 2026). Its
`#line` half comes from the source-debugging part of goose#169. This
section describes that prototype as its code reads; it has not been built
or run for this note.

### 6.1 Shape

`goose dap` speaks DAP on stdin and stdout, like `goose lsp`, and starts
`lldb-dap` as a child process that it speaks DAP to in turn (`src/dap.h`,
about 1,400 lines). A launch whose program is a `.goose` file runs
`goose build --debug -O0` on it and launches the executable under
`lldb-dap`. LLDB places the breakpoints (through the `#line` information),
runs, steps, stops and unwinds the program, and reads its memory. The
adapter answers `stackTrace`, `scopes`, `variables`, `threads`, `evaluate`
and `exceptionInfo` itself, adds to `setFunctionBreakpoints`, and looks at
every stop before the editor sees it; every other request and event passes
through unchanged. It finds `lldb-dap` through `--lldb-dap`,
`GOOSE_LLDB_DAP`, `xcrun` on macOS, or `PATH`.

Compared with A, the debugger underneath is the same, but the Goose
knowledge sits in front of it rather than inside it, and the editor talks
to the compiler. Compared with B, the protocol server and the value printer
are the same idea, but nothing in the generated C stops the program: LLDB
does.

### 6.2 The debug map

`goose build --debug` writes `<program>.debug.json` into the build
directory (`src/codegen_debugmap.h`):

* For each type: its kind, and its size when it is fixed. The kinds are
  integer (with its signedness, or varint), float, bool, struct (its fields
  and pads), ADT (its tag size and variants), slice, array (fixed, limited,
  variable or resizable) and reference (optional, fat, self-relative or
  pool-relative).
* For each C function: the Goose function's name, and its locals: the
  Goose name, the C lvalue, the declaration line, the line its block ends
  on, the type, whether it arrives as a captured pointer, and a root
  struct's components. Named constants come with their values.
* The program's globals, its typed queues, and whether it uses
  `return … from`.

The map calls itself a private contract between the compiler and its own
adapter, so that both change together. It is the description of section
3.3, with one interpreter of it instead of two, and that interpreter in the
compiler. Under `--debug` the optimizer also stops propagating constants
into their uses, so that a `let` stays a variable a debugger can read.

### 6.3 What the adapter shows

* **Call stacks** of Goose functions by their Goose names. Frames of
  functions the map does not know (the runtime, C) are left out.
* **Locals** by their Goose names: the parameters, and the locals declared
  before the stopped line whose block has not yet ended. An inner one
  shadows an outer by declaration line, and temporaries never appear.
  Globals and queues are scopes of their own.
* **Values** read by type, from memory the adapter asks `lldb-dap` for in
  4 KB pages, cached for the stop: text as text, ADTs as
  `Note.Count {n: 300}`, variable-mode arrays walked, varints decoded,
  self-relative references followed, pool-relative ones shown as their
  offset. A value's one-line form is read 4 levels deep and 64 elements
  long, and the rest when the editor expands it, which also stops a cycle
  of references.
* **Threads** named by the Goose function they run, their line, and what
  they wait on (`qget`, `thread_wait`).
* **Stepping** that lands outside the `.goose` files in the program's
  directory, in generated C or in the standard library, steps on. A
  breakpoint that lands in a function's prologue steps into its first
  line. While a `return … from` is unwinding (the runtime's in-flight flag
  is set), a step carries on until it lands.
* **Failed checks** stop as an exception, through a function breakpoint on
  the runtime's `gs_fail` that the adapter always sets, with the check's
  message and the failing Goose line.
* **The console** evaluates a small Goose expression language over memory:
  names in scope, fields, indexing, `.len`, arithmetic (with Goose's
  Euclidean `%`), comparisons, `&&` and `||`. It cannot call functions.

`test/dap_protocol.py` drives the adapter from the suite, with fixtures
under `test/dap/`, wherever `lldb-dap` is installed. It checks breakpoints,
names and values, stepping past generated code, threads, `return … from`
and failed checks.

### 6.4 What it depends on

Much of what the adapter relies on is how `lldb-dap` behaves rather than
what the protocol says:

* It parses `lldb-dap`'s text for values: a `uint8_t` rendered as a
  character literal, the `0x` in a pointer's rendering, the quoted bytes of
  a failure message.
* It asks for the threads twice after a stop, since `lldb-dap`'s first
  answer can hold only the stopped thread.
* It replaces `lldb-dap`'s scope references, which are the same for every
  frame.
* It finds each local's address by having LLDB evaluate `&x_g`, a C
  expression, once per local per stop, and a frame object's size by
  evaluating `sizeof`.

Each of these depends on a version of `lldb-dap`, where A depends on the
debuggers' documented formatter interfaces. Some layout facts are also in
the adapter rather than in the map: a resizable's length 8 bytes after its
base, a runtime-sized limited array's two `u32` counts, a frame object's
tail 16 bytes before the end of its C struct, a queue node's 16-byte
header. A code generator change to any of them breaks the adapter, and
only a fixture that covers that shape notices.

LLDB is the only debugger underneath. Microsoft's debugger is licensed only
for use with Microsoft's editors (section 3.4), which rules it out behind
`goose dap` in other editors and makes it doubtful in VS Code too, so
Visual Studio and `cppvsdbg` get none of the Goose view. On Windows the
adapter is `lldb-dap` reading an MSVC build's PDB or a clang build's debug
information; `lldb-dap.exe` ships with Visual Studio's LLVM component. The
prototype's paths assume `/`, and its suite test runs only on macOS or
where `lldb-dap` is on `PATH`, so it does not look to have been run on
Windows yet.

### 6.5 What it cannot do

* **No debugging under the JIT**, as with A.
* **Console expressions are a subset**, without calls, and their parser is
  the adapter's own, not the compiler's, so it does not follow the
  language as the language changes.
* **Scopes are line ranges.** Two declarations on one line, and the copies
  of a `const for` body (which all show, since a stop does not say which
  copy runs), are approximate.
* **C frames are hidden** from the call stack, so a stop in extern code
  shows only the Goose frames above it, though LLDB has the rest.
* **Every value is a round trip** to `lldb-dap`, cached by page for the
  stop, and every local an evaluation. How that feels on large frames or
  with many threads has not been measured.
* **No Microsoft debugger** (6.4).

### 6.6 What to tighten

* **`#line` only where the line changes, and only for debug builds.** The
  prototype writes `#if GS_DEBUG`, `#line` and `#endif` before every line
  of generated C in every build, four lines for each, in release output and
  in the C the JIT compiles too. For code the compiler writes itself it
  writes `#line 0`, outside the range C allows for `#line` (1 to
  2147483647).
* **The layout in the map**, every offset the adapter needs, from the C
  compiler rather than worked out a second time: an `offsetof` and
  `sizeof` table compiled into the debug build, which the adapter reads
  once at launch.
* **Addresses from the protocol.** A DAP variable can carry a
  `memoryReference`. Where `lldb-dap` gives a variable's address that way,
  it replaces evaluating `&x_g` and parsing the result; whether it does for
  every kind of local is to be checked.
* **The console on the compiler's front end.** The text is parsed and
  checked as a Goose expression in the scope of the stopped line (the
  language server already resolves names at a position), and the checked
  tree is evaluated over memory, so the console understands what the
  compiler does.
* **`--debug` alone implies `-O0` and kept variables**, for every command,
  rather than the adapter passing `-O0` and the optimizer keeping
  variables only for `build`.
* **Windows**: separators in paths, and a run of the adapter's tests with
  `lldb-dap` from Visual Studio's LLVM, against an MSVC build and a clang
  build.

## 7. The three compared

| | A: native debuggers, visualizers | C: `goose dap` in front of `lldb-dap` | B: `goose dap` alone |
|---|---|---|---|
| Runs the program: breakpoints, steps, threads | the native debugger | `lldb-dap` | the compiler, through hooks in the generated C |
| Needs a C compiler and a debugger | yes | yes, and `lldb-dap` | no, under the JIT |
| Programs with threads | yes | yes | not under the JIT until it has thread-local storage; built programs only with B's own stop-the-world |
| Editors | every DAP client, configured per editor, plus Visual Studio | every DAP client, running one command | every DAP client, running one command |
| Microsoft's debugger | yes, in VS Code and Visual Studio | no | not involved |
| Breakpoints and steps | by line | by line, past the compiler's code and through `return … from` | by line and column |
| Call stack | C function names | Goose names, C frames left out | Goose names |
| Locals | C names, all of a function's | Goose names, by line range | Goose names, exactly those in scope |
| Values | natvis and an LLDB module interpreting a description | the compiler's adapter interpreting its map | the compiler's own types |
| Console | C | a Goose subset over memory, no calls | Goose, calls included |
| Runtime errors | a function breakpoint on the runtime's failure path | an exception stop on the failing line | a stop on the failing line |
| C and extern frames | yes | in LLDB, hidden from the stack | invisible |
| Crash dumps and attach | yes | LLDB can; the adapter does not ask it to yet | no |
| Optimized builds | as well as C debugging of them goes | no (the map is for debug builds) | no |
| Cost in the program | none beyond a debug build | none beyond a debug build | a check per statement, a frame link per call |
| Depends on | the debuggers' formatter interfaces | `lldb-dap`'s behaviour | nothing outside `goose` |
| When a representation changes | a description kind, a natvis template, a Python class | the map and the adapter's reader | printer code |
| When control flow changes | nothing | nothing, unless stepping needs a new rule | site and scope placement |
| What there is to own | `#line`, debug shapes, a natvis generator, an LLDB module, extension glue | `#line`, the map, the adapter | a debugger: protocol server, stop and step machinery, printer, expression compiler, frame records |

Sizes: C's prototype is about 1,400 lines of adapter, 240 of map and some
50 of code generator and optimizer changes, plus its tests. A would be of
the same order, most of it the natvis generator and the LLDB module. B
would be several thousand lines for the JIT alone. The sizes of A and B
are estimates.

## 8. Recommendation and order of work

Option C, in steps that are each useful on their own:

1. **Source lines.** `#line` in debug builds where the Goose line changes,
   and `--debug` implying `-O0` and kept variables (6.6). This alone gives
   every native debugger Goose lines, and every later step needs it.
2. **`goose dap` in front of `lldb-dap`**, from the prototype, with the
   layout in the map and run on all three platforms.
3. **VS Code**: the `breakpoints` contribution (3.4), and the `goose` debug
   type's debug launch served by `goose dap` as its adapter executable,
   while `noDebug` keeps running the JIT task. Other editors get a snippet
   each in `docs/editors.md`, as they have for the language server.
4. **The console on the compiler's front end** (6.6).
5. **Generated natvis**, from the same map, for Visual Studio and
   `cppvsdbg`, if their users ask: the one part of A still worth having,
   and cheap once the map exists.
6. **JIT backtraces** (section 4), independent of the rest.

Option A's debug shapes in the code generator (3.2) and its LLDB module are
not needed under C: the adapter hides the temporaries, and the map gives a
resizable's element type.

Option B is worth revisiting if a large share of users debug without a C
toolchain, or if Goose watch expressions with calls turn out to matter. C
carries over to it: the protocol server, the map, the value reader (which
reads through one memory-reading function, which under B would read the
process's own memory), the console, the tests and the editor
configuration. C is a step toward B rather than away from it.

## 9. Open questions

* Is LLDB's PDB support good enough for C on Windows (locals, unions,
  packed structs), so that one adapter over `lldb-dap` serves every editor
  there?
* Which versions of `lldb-dap` to support, and how the suite pins and
  tests them, given section 6.4.
* Should `#line` be written in release builds too? Profilers and crash
  dumps would then name Goose lines; generated C read by people, and the
  corpus diffs (`test/emit_order.py`, the differential harness), would see
  more churn.
* Are line ranges good enough for scopes, or should the map carry exact
  statement ranges?
* Should the adapter show C frames, collapsed, when a program stops in
  extern code?
* Where does the map live: a file in the build directory, as in the
  prototype, or a section in the executable? A file is simplest; a section
  cannot be separated from the executable it describes.
* For A's debug shapes: can locals be declared where their scopes begin,
  or does the code generator's structure (temporaries live across
  statements, jumps for `defer` and `return … from`) need them at the
  top?
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
* The Option C prototype: https://github.com/pdoane/goose/tree/dap-prototype
* goose#169, whose `#line` mapping the prototype uses: https://github.com/aardappel/goose/pull/169
