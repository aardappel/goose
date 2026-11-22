# Debugging Goose programs

A Goose program is C by the time it runs, so the C debuggers debug it:
LLDB, gdb, and Visual Studio or cdb on Windows. `goose build -g` builds the
program so that they show and step the Goose source rather than the
generated C. This page describes what `-g` does, then how to debug on the
command line and in an editor.

## 1. What `-g` does

`-g` (or `--debug-info`) is `--debug`, with its overflow, range and tag
checks and C debug information, and in addition:

- **Goose lines.** Every line of a function's C is preceded by a `#line`
  naming the Goose line it came from, with the file's absolute path. The
  debugger's source view, breakpoints, stepping and call stack are in the
  `.goose` files. After each function a `#line` hands the C back to the
  `.c` file, so the runtime and helpers keep their own lines.
- **One stop per line.** A debugger puts a breakpoint location in every
  lexical block a line's code passes through. So every local of the C,
  loop counters included, is declared at the function's top, and a Goose
  line stops once, and once each time round a loop.
- **A frame per function.** No inlining unless an `-O` is given
  (`-O0`), so every Goose function keeps its frame and arguments.
- **Live arrays.** A resizable array's count is written each time it
  changes, rather than cached in a local until the array's extent ends,
  so the debugger reads the array as it is from any frame.
- **A debug map.** Beside the C, `<stem>.debug.json` lists each function
  instance's C name and Goose name, its locals with their Goose types and
  the lines they are in scope, the globals, and the fields and variants of
  the program's structs and enums. The C debug information has none of
  these; Goose support in a debugger reads them from the map. Its format
  is at the top of `src/codegen_debugmap.h`.

The map is written with the C and changes none of it. Without `-g`
nothing above happens: the C, `--debug`'s included, is what it was.
`goose run -g` gives the same C to the JIT, which has no debugger.

## 2. On the command line

The C debug information alone is enough to step the Goose source. Take
`area.goose`:

```goose
fn area(w: i64, h: i64) -> i64 {
    let a = w * h;
    return a;
}

fn main() {
    var total: i64 = 0;
    for i in 0..3 {
        total = total + area(i, 2);
    }
    print(total);
}
```

```
$ goose build -g area.goose
$ lldb ./area
(lldb) b area.goose:2
Breakpoint 1: where = area`area_g + 20 at area.goose:2:31
(lldb) run
* thread #1, stop reason = breakpoint 1.1
    frame #0: area`area_g(w_g=0, h_g=2) at area.goose:2:31
-> 2   	    let a = w * h;
(lldb) bt
  * frame #0: area`area_g(w_g=0, h_g=2) at area.goose:2:31
    frame #1: area`main_g(gs_sp=0) at area.goose:9:14
    frame #2: area`main(argc=1, argv=...) at area.c:1773:5
(lldb) next
-> 3   	    return a;
(lldb) frame variable a_g
(gs_i64) a_g = 0
```

`next`, `step` and `finish` go one Goose line or call at a time. gdb
(`break area.goose:2`) and cdb (``bp `area.goose:2` ``) work the same
way.

What the debugger shows is still the C's: a Goose name with a suffix
(`area_g`, `a_g`), every C local in the function, and Goose values as
C structs. The goose-debugger repository
([github.com/SorinS/goose-debugger](https://github.com/SorinS/goose-debugger))
has Goose support built on the debug map: a module for LLDB and one for
gdb (formatters for Goose values, and Goose names and scopes), natvis
files for Visual Studio and cdb, and `goose-dap` for editors (§3). With
the LLDB module the session above continues:

```
(lldb) command script import goose_lldb.py
(lldb) goose-bt
* #0: area`area at area.goose:3
  #1: area`main at area.goose:9
(lldb) goose-locals
h: i64 = 2
w: i64 = 0
a: i64 = 0
(lldb) finish
-> 9   	        total = total + area(i, 2);
(lldb) goose-locals
total: i64 = 0
i: i64 = 0
```

`goose-catch` stops on a Goose runtime error (a failed assert, a bounds or
overflow check) at the Goose line that failed, instead of letting the
program exit.

## 3. In an editor

Editors debug through the Debug Adapter Protocol (DAP). LLVM's adapter,
`lldb-dap`, works on a `-g` build as it is, with the C names. `goose-dap`,
from the goose-debugger repository, is an adapter that runs `lldb-dap` and
turns its view into Goose: Goose function names in the call stack, the
Goose locals in scope under their own names, Goose names in watches and
breakpoint conditions, and a stop on a runtime error. Its launch
configuration takes `"build": true` and `"source": "main.goose"` to run
`goose build -g` before each session. It needs Python 3 and an `lldb-dap`
(on macOS the Command Line Tools have one).

**IntelliJ** (IDEA, GoLand, CLion 2026.2): the Goose plugin
([github.com/SorinS/goose-idea](https://github.com/SorinS/goose-idea))
debugs through the IDE's own DAP client. Set the compiler and, if it is
not found, the adapter in Settings | Languages & Frameworks | Goose, set a
breakpoint in the gutter of a `.goose` file, and choose Debug on a Goose
run configuration. The plugin builds with `-g`, starts goose-dap (or
lldb-dap), and the IDE's debugger shows the call stack, variables and
watches; F8 steps over and F7 steps into.

**VS Code** (and VSCodium): install LLVM's *LLDB DAP* extension and set
its `lldb-dap.executable-path` setting to goose-dap's full path (the
extension takes a file, not a command on PATH). A launch configuration,
which builds the program and starts it:

```json
{
    "type": "lldb-dap",
    "request": "launch",
    "name": "Debug area",
    "program": "${workspaceFolder}/area",
    "build": true,
    "source": "${workspaceFolder}/area.goose"
}
```

Breakpoints in `.goose` files need `"debug.allowBreakpointsEverywhere":
true`, as VS Code only allows them in languages a debugger has declared.

**Neovim** (nvim-dap) is to come.

## 4. Limits

- A debugger evaluates C, not Goose: an expression names C variables
  (`total_g + 1`). goose-dap turns the Goose names in a watch or a
  condition into the C ones; it does not call Goose functions.
- `goose run` (the JIT) cannot be debugged; debug a `goose build -g`
  executable.
- Stepping into a call to the runtime (checked arithmetic, a push) steps
  into C; goose-dap tells the debugger to step over them.
