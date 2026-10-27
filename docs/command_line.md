# The goose command line

<details>
<summary>Table of contents</summary>

- [1. Commands](#1-commands)
- [2. Running a program](#2-running-a-program)
- [3. Building an executable](#3-building-an-executable)
- [4. Checking a program](#4-checking-a-program)
- [5. Options for run, build and check](#5-options-for-run-build-and-check)
- [6. What goose prints](#6-what-goose-prints)
- [7. Packages](#7-packages)
- [8. Writing the C yourself](#8-writing-the-c-yourself)
- [9. Formatting](#9-formatting)
- [10. The language server](#10-the-language-server)
- [11. toolchain, version, help and completion](#11-toolchain-version-help-and-completion)
- [12. Environment and files](#12-environment-and-files)
- [13. Exit status](#13-exit-status)
- [14. Options for working on the compiler](#14-options-for-working-on-the-compiler)

</details>

One executable, `goose`, is the compiler, the in-process runner, the build
tool, the package fetcher, the formatter and the language server. This page
describes all of it. `goose help` prints a summary, and `goose help
<command>` the options of one command. How a program uses libraries, and how
to make one, is in [packages.md](packages.md).

## 1. Commands

```
goose <command> file.goose [options] [-- program args...]
goose file.goose [options] [-- program args...]
```

| Command | What it does |
|---|---|
| `goose run file.goose` | Compile the program and run it in this process ([§2](#2-running-a-program)). `goose file.goose` is the same. |
| `goose build file.goose` | Build an executable with a C compiler on this machine ([§3](#3-building-an-executable)). |
| `goose check file.goose` | Parse and typecheck the program, and write and run nothing ([§4](#4-checking-a-program)). |
| `goose fetch file.goose` | Fetch the packages the program declares ([§7](#7-packages)). |
| `goose format file.goose\|dir...` | Rewrite files in the standard layout ([§9](#9-formatting)). |
| `goose toolchain` | Report the C compiler, cmake and git found, and `GOOSE_HOME` ([§11](#11-toolchain-version-help-and-completion)). |
| `goose version` | Print the compiler's version and its runtime's. |
| `goose lsp` | Serve an editor over the Language Server Protocol ([§10](#10-the-language-server)). |
| `goose completion <shell>` | Print a completion script for bash, zsh, fish or PowerShell. |
| `goose help [command]` | Print the help, on everything or on one command. |

The command comes first. A first argument that is not a command is read as
an option or a file, so `goose file.goose` runs the program. A first word
with no dot or slash in it, which is neither a command nor an existing
file, is taken as a mistyped command: `goose biuld x.goose` suggests
`build` and does nothing.

Options may come before or after the file, in any order. Each command takes
one program, its *root file*: the file that holds `main` and declares the
program's packages. Everything after `--` belongs to the program being run.
`--help` or `-h` anywhere before a `--` prints the help on the command given,
and `goose --version` is `goose version`.

## 2. Running a program

```bash
goose hello.goose
goose run hello.goose -- input.txt --verbose
```

`goose run` compiles the program to C, compiles that C in memory with the
TinyCC compiler built into `goose`, and runs it in the compiler's own
process. There is no build step and no file is written. On Windows nothing
else is needed. On Linux the C library's headers are needed (`libc6-dev` or
`glibc-devel`), and on macOS the SDK of the Xcode command line tools.

The arguments after `--` are the program's. The `os` module gives them to
the program, with argument 0 being the root file's path as it was given:

<!-- check-only -->
```goose
import os;

fn main() {
    let a = args();                      // a[0] is the program
    guard a.len > 1 else { print("usage: hello file..."); exit(2); }
    for i in 1..a.len { print(i, ": ", a[i]); }
}
```

`goose run` exits with the program's exit code: 0 when `main` returns,
the code given to `exit(code)`, and 1 when the program fails a check or
calls `abort` (`goose runtime error: ...` on stderr). A program that does
not compile also exits with 1, after its errors.

What a run cannot do, a build can:

* A program that starts threads must be built. TinyCC cannot place the
  thread-local storage that each worker's data stacks are kept in, so
  `goose run` refuses it.
* TinyCC does not optimize, so a run is for trying things out, and
  timing is done on a build.
* A run keeps its data relative to the root file: `resource_dir()`
  returns the root file's directory, where a built program's is the
  executable's ([stdlib.md](stdlib.md#os)). Relative paths are relative to
  the working directory either way.

A program whose packages have C of their own still runs this way. Packages
compiled into `goose` (`goose toolchain` lists them) need nothing more. The
C of any other package, and the program's own C named in its sidecar
([packages.md](packages.md#3-a-program-with-c-of-its-own)), is built into
a shared library with a C compiler on the machine, once, and loaded into
the run. `--cc` picks that compiler as it does for a build, and `-v` prints
the commands that build it.

`goose run` takes the options in [§5](#5-options-for-run-build-and-check).
It takes no `-o`: the flag form `goose -o out.c` writes C instead
([§8](#8-writing-the-c-yourself)).

## 3. Building an executable

```bash
goose build hello.goose && ./hello
goose build -o out/hello hello.goose     # the executable elsewhere
goose build --debug hello.goose          # checked, with debug information
goose build --dry-run hello.goose        # print the commands and run none
```

`goose build` writes the program's C, compiles it with a C compiler on the
machine, and links it with the runtime and the native parts of the packages
the program uses. The executable goes beside the root file, named after it
(`hello`, or `hello.exe` on Windows), unless `-o` or the sidecar's
`output = "name";` names it. The build prints `built <executable>` when it
is done.

The C compiler is the first of these that names one:

1. `--cc name|path`
2. the `GOOSE_CC` environment variable
3. `cc = "clang";` in `GOOSE_HOME/config` (`~/.goose/config`)
4. the first compiler found on the machine.

A name is one of `msvc` (or `cl`), `clang-cl`, `clang`, `gcc` and `cc`, and
a path names the compiler's executable. Without a request, Windows looks for
`cl` on `PATH` (as in a Developer Command Prompt), then `clang-cl` and
`clang` in Visual Studio's LLVM or on `PATH`, then MSVC through Visual
Studio's `vcvars64.bat`, then `gcc` and `cc` on `PATH`. Other systems look
for `cc`, `gcc` and `clang` on `PATH`. `goose toolchain` shows which one a
build would use, and why. Setting up Visual Studio's environment takes a
second or two, so `goose` keeps the result in `GOOSE_HOME/toolchain` and
`goose toolchain --refresh` sets it up again.

The C is always compiled optimized (`/O2` or `-O2`), whatever `-O` level
the Goose compiler used, except under `--debug`. The options of a build,
beyond those in [§5](#5-options-for-run-build-and-check):

| Option | What it does |
|---|---|
| `-o exe` | Where the executable goes. Its directory is made if needed. |
| `--debug` | Turn on the overflow, range and tag checks (`GS_DEBUG=1`), and build the C without optimization and with debug information (`/Od /Zi`, or `-O0 -g`), for a debugger. |
| `--cc name\|path` | The C compiler, as above. |
| `--standalone` | Compile the runtime into the program, rather than linking the runtime object built once per C compiler. |
| `-v` | Print each command the build runs, and what it prints. |
| `--dry-run` | Print the commands the build would run, with their paths, and run none of them. A Makefile or CMake project can take them from here. |

Everything a build makes along the way goes under `GOOSE_HOME`. The program's
C, objects and logs go in `build/prog/<name>-<hash>/`, one directory per
root file. The runtime object is compiled the first time a C compiler is
used, once per compiler and per release or debug configuration, into
`build/runtime/`. When a compile or link fails, the error shows the command
and what it printed, and says where the log is.

## 4. Checking a program

```bash
goose check hello.goose
```

`goose check` parses and typechecks the program and everything it imports,
including the lifetime and bounds checks the compiler makes, and writes and
runs nothing. It prints nothing when the program is correct, and its errors
and exit code 1 when it is not. It takes the options in
[§5](#5-options-for-run-build-and-check). `--diagnostics json` gives an
editor or another tool the errors as data ([§6](#6-what-goose-prints)), and
with `--stacks` a check also reports the program's data stacks.

## 5. Options for run, build and check

These options have the same meaning for `goose run`, `goose build`,
`goose check` and the flag form `goose -o out.c`.

| Option | What it does |
|---|---|
| `-O0`, `-O1`, `-O2` | How much the Goose compiler inlines: nothing, a little, or the most (the default). Constant folding and propagation run at every level, and a build's C compiler optimizes at every level. |
| `--debug` | Turn on the overflow checks, the range checks of `as` casts and the tag checks (`GS_DEBUG=1`). Each failure names its source line, and an overflow or a cast names the values involved. A build also gets debug information ([§3](#3-building-an-executable)). |
| `-D NAME[=VALUE]` | Define a C macro in the generated program, as `-DNAME[=VALUE]` does too. The define is written into the C itself, so a run and a build see the same source. The value is 1 when none is given. |
| `--stack-reserve size` | The address space reserved for each data stack, from 64K up to 256T with a `K`, `M`, `G` or `T` suffix. The default is 2G. The address space is committed as it is used, so a large reservation costs nothing until it is used. |
| `--stacks` | Report how many data stacks the program and each of its workers use, what that adds up to, and the cap it puts on `hardware_threads()`. |
| `--bce-lines` | Report, for each source line, how many bounds checks the compiler removed and how many it kept. |
| `--no-bce` | Keep every bounds check. `checkfree` functions are then not verified, and the compiler warns about that. |
| `--specs` | Print every specialization of each function, after optimization, as source. |
| `--stats` | Print a few lines about what the compile did: the declarations parsed, the specializations and optimizations, the bounds checks removed, and the files written. |
| `--color auto\|always\|never` | Whether errors and warnings are in color ([§6](#6-what-goose-prints)). |
| `--diagnostics text\|json` | Print errors and warnings as text on stderr (the default), or as JSON on stdout ([§6](#6-what-goose-prints)). |
| `--offline` | Fetch nothing: a package that is not fetched yet is an error at its import ([§7](#7-packages)). |
| `--update` | Fetch the packages declared at a tag or branch again ([§7](#7-packages)). |
| `--stdlib dir` | Where the standard library is ([§12](#12-environment-and-files)). |
| `--cc name\|path` | The C compiler for anything that needs one ([§3](#3-building-an-executable)). |
| `-v` | Print each command that builds C, and what it prints. |

The `-D` macros that the runtime reads, which configure a program:

| Macro | Default | What it sets |
|---|---|---|
| `GS_DEBUG` | 0 | 1 is what `--debug` sets: the overflow, range and tag checks. |
| `GS_STACK_STATS` | 0 | 1 makes each thread program report its data stack use on stderr as it ends. |
| `GS_STACK_RESERVE` | 2 GB | The address space reserved per data stack, which `--stack-reserve` sets. |
| `GS_STACK_GAP` | 1 MB | The unmapped gap after each data stack, which makes runaway growth abort. |
| `GS_STACK_BUDGET` | 32 TB | The address space all thread programs together are meant to take; `hardware_threads()` is capped at what it has room for. |
| `GS_MAX_STACKS` | 1024 | The most data stacks one thread program may use. A program that needs more fails to compile. |
| `GS_SIMD` | 2 | The highest CPU level for `simd` functions' versions under clang on x86-64: 0 or 1 lowers it. |

A program that runs out of a data stack ends with a message that names
`--stack-reserve`. `--stacks` and `-DGS_STACK_STATS=1` show how many stacks
a program uses, and the [implementation
notes](implementation.md#7-the-runtime) explain how they are reserved.

## 6. What goose prints

A run prints only what the program prints. A check that succeeds prints
nothing, and a build prints only the `built` line. Errors and warnings go to
stderr, each with the line of source it is about. `--stats` adds a few lines
about the compile, on stdout, or on stderr for a run, whose stdout is the
program's.

Errors and warnings are in color on a terminal. `--color never`, or a
non-empty `NO_COLOR`, turns color off. `--color always`, or a
`CLICOLOR_FORCE` other than 0, turns it on wherever the output goes.

Work that takes a while, such as fetching a package, building a package's C
or compiling the runtime, prints one line on stderr as it starts. On a
terminal, a live line below it shows what is running, for how long, and the
last line the tool printed, and it is erased when the work is done. When
stderr goes anywhere else (a file, a pipe, a CI log, an agent running the
command), or `TERM` is `dumb`, there is no live line, only the one-line
notes. `GOOSE_PROGRESS=plain` or `GOOSE_PROGRESS=live` overrides that
choice, and `-v` and `--dry-run` always use plain output.

`--diagnostics json` prints the errors and warnings on stdout as a JSON
array, after anything a run's program printed. What `goose` itself would
have printed on stdout, the `--stats` lines and a build's `built` line,
goes to stderr instead. The exit code stays what it would be:

```json
[
  {"severity": "error", "message": "expected a value of type i32, got const u8[:]",
   "file": "bad.goose", "line": 1, "col": 26, "endLine": 1, "endCol": 29,
   "related": [{"message": "in fn main() instantiated from bad.goose:1",
                "file": "bad.goose", "line": 1, "col": 1}],
   "text": "bad.goose:1: error: expected a value of type i32, got const u8[:]\n..."}
]
```

Each object has a `severity` (`error` or `warning`) and a `message`. Where
the place is known, it also has the `file` as the compiler names it, a
1-based `line` and byte `col`, and `endLine` and `endCol` one past the last
byte. `related` holds the lines that follow the message, in the same form,
and `text` holds the whole message as the terminal would have shown it.
An error that is not about the program, such as a missing C compiler, has a
message and text only.

## 7. Packages

A program that imports a package that has not been fetched yet fetches it
with git into `GOOSE_HOME/pkg` the first time it is run, built or checked,
and builds the package's C there once per C compiler.
[packages.md](packages.md) describes how a program declares its packages.

```bash
goose fetch app.goose               # fetch what app.goose declares, and compile nothing
goose fetch --update app.goose      # fetch packages at a tag or a branch again
goose run --offline app.goose       # fetch nothing; a package not fetched is an error
```

`goose fetch` makes a later session work without a network. A package
pinned to a commit is never fetched again. One pinned to a tag or a branch
is fetched again only with `--update`, which every command that compiles
also takes. The language server never fetches anything.

When a tool that is needed is missing, the error names it: git to fetch,
cmake for a package built with CMake, and a C compiler for any C code. A
program that has no C of its own and uses no package with C needs none of
them. Deleting `GOOSE_HOME` loses nothing that a build cannot recreate
([packages.md](packages.md#5-the-cache) shows what is in it).

## 8. Writing the C yourself

`goose build` covers most needs, and `goose build --dry-run` shows the
commands it runs. To build the C in a build system of your own, or to link
a Goose program into a C or C++ host, give the compiler an output file
instead of a command:

```bash
goose -o tour.c tour.goose                    # the program's C
goose --emit-runtime goose_runtime.c          # the runtime's C
cc -O2 -c goose_runtime.c                     # once, with the programs' compiler and flags
cc -O2 tour.c goose_runtime.o -o tour -lm -pthread
```

On Windows the last two are `cl /O2 /c goose_runtime.c` and
`cl /O2 tour.c goose_runtime.obj`. On Linux, add `-ldl` before glibc 2.34.

`-o out.c` writes the program's C and runs nothing; `--jit` also runs the
program in-process as `goose run` would. The C holds the program and the part
of the runtime that is inlined into it. The rest of the runtime (the OS
layer, the reservation of data stacks, threads and printing) is the C that
`--emit-runtime` writes, which is compiled once and linked with every
program. It takes no `-D` of its own, since a program hands the runtime its
configuration as it starts. Build it with the same C compiler and flags as
the programs. A program from another version of the compiler fails to link
against it (`goose version` prints the runtime's hash, which must match).

| Option | What it does |
|---|---|
| `-o out.c` | Write the program's C, and do not run it. |
| `--standalone` | Write C that holds the whole runtime, and builds on its own: `cc -O2 tour.c -o tour -lm -pthread`. |
| `--header out.h` | Also write a header for a C host, declaring the program's `export fn`s, of which there must be one at least. The C then has no `main`: the host calls `goose_init(argc, argv)` once, which runs the program's `main`, and then calls the exported functions. |
| `--include header.h` | Have the generated C include this header, for small shims written against the generated types (spec §7.10). May be repeated. |
| `--jit` | Also run the program in this process. |
| `--emit-runtime out.c` | Write the runtime's C. |
| `--<package>-link msvc\|cc` | Print the path of the response file that links the native part of a package compiled into `goose`, for the `msvc` or `cc` style of command line. |

The [tutorial](tutorial.md#19-calling-goose-from-c) and the
[reference](reference.md#export-fn) show a C host. All the options in
[§5](#5-options-for-run-build-and-check) apply too, `-O` and `-D` among
them.

A program that uses `audio`, `gfx`, `physics` or `ui` also links that
package's native part. When the package is compiled into `goose`, its
response file is part of the compiler's installation:

```bash
cc -O2 cube.c goose_runtime.o -o cube @$(goose --gfx-link cc) -lm -pthread
```

A `ui` program puts the `ui` response file before the `gfx` one it draws
through. For packages that are not compiled in, and for a program's own C,
`goose build --dry-run` prints the commands that build each native part and
the response file it makes.

## 9. Formatting

```bash
goose format src/                  # rewrite every .goose file under src/
goose format --check .             # list the files that are not formatted
goose format < in.goose > out.goose
```

`goose format` rewrites each file in place in the standard layout: the
indentation, the spaces between tokens, blank lines and trailing commas.
Line breaks stay where they are. A directory stands for the `.goose` files
under it, except those in directories whose names start with a dot and in
build trees (directories holding a `CMakeCache.txt`). With no file, or `-`,
it formats standard input to standard output. A file that does not parse is
left as it is, and its errors are printed.

`--check` writes nothing and lists the files that are not formatted. The
exit status is 0 when every file is formatted (or now is), 1 when `--check`
found one that is not, and 2 when a file does not parse or cannot be read or
written, or an option is wrong. [editors.md](editors.md#formatting) shows how
to format on save.

## 10. The language server

```bash
goose lsp
```

`goose lsp` serves an editor over the Language Server Protocol on stdin
and stdout: diagnostics as files change, hover, go to definition,
references and rename, symbols, semantic highlighting, signature help,
completion, inlay hints, folding, code lenses and formatting. It analyzes
the programs whose root files the editor names in its
`initializationOptions` (`entryFiles`), and every open file that none of
them imports. It never fetches a package: one that is not fetched yet is an
error at its import, which `goose fetch` resolves.

| Option | What it does |
|---|---|
| `--stdio` | Serve on stdin and stdout, as it does anyway. Clients pass it. |
| `--stdlib dir` | Where the standard library is. |
| `--probe` | Print the protocol version (`goose lsp 3.17`) and exit. |

The [VS Code extension](../vscode/README.md) starts the server itself;
[editors.md](editors.md) has the configuration for Neovim, Helix and
Emacs, and the [design notes](design/language_server.md) the details.

## 11. toolchain, version, help and completion

`goose toolchain` reports the C compiler a build would use (its name,
version, path, the style of its command line and how it was found), its C++
driver, cmake, git, the packages compiled into `goose`, and `GOOSE_HOME`.
It exits with 1 when there is no C compiler. It takes `--cc` to look for a
particular compiler, `--json` for a report as JSON, and `--refresh` to set
up Visual Studio's environment again rather than take the one kept from
last time.

`goose version`, or `goose --version`, prints the compiler's version and
its runtime's: a hash of the runtime's text, which the runtime that
`--emit-runtime` writes must match.

`goose help` lists the commands and the common options; `goose help
<command>` (or `goose <command> --help`) the options of one. `goose help c`
covers writing the C yourself, and `goose help internals` the options in
[§14](#14-options-for-working-on-the-compiler). `goose help --man` prints
all of it as a man page, which a release ships as `man/goose.1`.

`goose completion bash` (or `zsh`, `fish`, `powershell`) prints a script
that completes the commands, their options and `.goose` files. To have it in
every shell:

```bash
goose completion bash > ~/.local/share/bash-completion/completions/goose
goose completion zsh > "${fpath[1]}/_goose"
goose completion fish > ~/.config/fish/completions/goose.fish
goose completion powershell >> $PROFILE
```

or, for the shell at hand, `source <(goose completion bash)`.

## 12. Environment and files

| Variable | What it does |
|---|---|
| `GOOSE_HOME` | Where fetched packages, build intermediates and settings go. The default is `~/.goose` (`%USERPROFILE%\.goose` on Windows). |
| `GOOSE_CC` | The C compiler, where `--cc` is not given. |
| `GOOSE_STDLIB` | The standard library's directory, where `--stdlib` is not given. |
| `GOOSE_PROGRESS` | `plain` or `live`: how progress shows on stderr. |
| `NO_COLOR` | Set and not empty: no color, unless `--color` says otherwise. |
| `CLICOLOR_FORCE` | Set to anything but 0: color, even when stderr is not a terminal. |
| `GOOSE_TCCLIB` | Where TinyCC's headers and support library are, for a compiler moved away from both its installation and its build tree. |

Without `--stdlib` or `GOOSE_STDLIB`, the compiler looks for the standard
library in a `stdlib` directory beside its executable, then in the parent
directories up to three levels above it, which finds a release's and a
source tree's, and last in `./stdlib`.

`GOOSE_HOME/config` holds settings that apply to every build. It is written
like a sidecar, and its one setting is the C compiler:

<!-- fragment -->
```goose
// ~/.goose/config
cc = "clang";
```

A program's *sidecar*, `foo.build` beside `foo.goose`, names the program's
own C, the libraries it links and the executable's name
([packages.md](packages.md#3-a-program-with-c-of-its-own)). The cache under
`GOOSE_HOME` is described in [packages.md](packages.md#5-the-cache).

## 13. Exit status

| Command | Exit status |
|---|---|
| `run` | The program's: 0 when `main` returns, the code it passes to `exit`, and 1 when it aborts. 1 when it does not compile. |
| `build`, `check`, `fetch`, `-o` | 0 on success, 1 on an error in the program, a failed C compile or a failed fetch. |
| `format` | 0 when every file is formatted, 1 when `--check` found one that is not, 2 on a file that does not parse or a wrong option. |
| `toolchain` | 1 when there is no C compiler. |
| any | 1 on a wrong option, a mistyped command, or a missing file. |

## 14. Options for working on the compiler

These look into what the compiler does, and serve its test suite
([testing.md](testing.md)). `goose help internals` lists them.

| Option | What it does |
|---|---|
| `--tokens` | Print the file's tokens. |
| `--parse` | Stop after parsing. |
| `--check` | Stop after typechecking, as `goose check` does. |
| `--dump` | Print the parsed program back as source. |
| `--dump-docs` | Print the documentation comments of the program's declarations, fields and variants. |
| `--dump-file out` | Write that dump to a file, and go on. |
| `--roundtrip` | Check that the dump parses and dumps back to itself. |
| `--multi-test file...` | Compile each file as a run of its own would, and end what each printed with a line on stdout and stderr: `==== goose --multi-test: exit <code> <file>`. A `%` in `-o` stands for each file's name. |
| `--bce-test` | Verify the `// bce:elide` and `// bce:keep` annotations in the source. |
| `--unsafe-no-rf-check` | Leave out the checks after calls that a `return ... from` can pass through. This is unsound, and is only for measuring what they cost. |
| `--gen-runtime-header` | Regenerate `src/runtime_inline.h` from `src/runtime/` (`GOOSE_RUNTIME` names that directory if it is not found). |
| `--compile-shader file` | Print what a shader compiles to, without a program around it; `--shader-source msl` or `hlsl` prints that source as well. |

`goose format --verify file.goose|dir...` formats each file in memory and
writes nothing. It checks the result as a format does, and that formatting
it again changes nothing, and it skips a file that does not parse. Setting
`GOOSE_ROUNDS` in the environment makes the typechecker trace what keeps a
recursive cycle's rounds going.
