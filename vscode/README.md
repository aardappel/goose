# Goose for Visual Studio Code

An extension for the [Goose language](https://github.com/aardappel/goose). No
language server, runtime dependencies, or compiler changes are required.

## Features

- Syntax highlighting for `.goose` files, including nested block comments,
  escaped strings and characters, raw `"""` strings across lines, decimal/hex
  floats, namespaces and builtins.
- Bracket matching, auto-closing, indentation, comment toggling, folding and
  snippets (`main`, `fn`, `struct`, `enum`, `for`, `guard`, `match`, and more).
- Compiler checks on open/save, error and warning underlines, and Problems
  entries. Use **F8 / Shift+F8** to jump between problems. Lexer errors use the
  compiler's caret; type errors highlight the source line. Instantiation chains
  appear as related locations in Problems.
- **Goose: Check Program**, **Goose: Run with JIT**, **Goose: Generate C**, and
  **Goose: Show Compiler Output** in the Command Palette. The editor's play
  button and **▶ Goose** status-bar button run the program in a task terminal,
  with stdin and clickable errors.
- **Ctrl+Shift+B**, **F5**, and **Ctrl+F5** run with JIT while editing Goose
  (**Cmd+Shift+B** for build on macOS). **Goose: Run with JIT** is also available
  in VS Code's Run configuration picker.
- Top-level declarations in Outline, breadcrumbs and Go to Symbol (**Ctrl+Shift+O**).
- Clickable imports, with root-relative, importing-file-relative (`import .foo;`),
  and standard-library resolution matching the compiler.
- Check, Run and Generate C tasks, plus the `$goose` problem matcher for custom
  tasks with absolute paths or paths relative to the workspace folder.

The extension uses lexical analysis and compiler commands. It does not yet
provide semantic completion, rename, cross-file symbol definitions, formatting,
debugging, or an LSP.

## Install the prebuilt extension

The repository includes [goose-language.vsix](goose-language.vsix). Download it
and select **Extensions: Install from VSIX...** in VS Code, or install from a
checkout with:

```sh
code --install-extension vscode/goose-language.vsix
```

No Python, Node.js, or npm is needed to install the prebuilt extension. Compiler
checks and running programs still require the Goose compiler (see below).

## Build or develop

To rebuild the VSIX, install Python 3 and Node.js with npm (Node.js 22+
recommended), then run from the repository root:

```sh
python vscode/build_vsix.py
```

The script works from any working directory. It installs the locked npm build
dependencies, runs the extension tests, and writes `vscode/goose-language.vsix`.
Use `--skip-install` to reuse existing `node_modules`. The VSIX file is tracked
in Git. Commit the rebuilt file with extension updates so people can install
without building locally. The extension version still comes from `package.json`.

Open this `vscode/` directory in VS Code and press **F5** to launch an Extension
Development Host with the parent Goose repository open. No build is needed.
Packaging is local; the package is not published to the Marketplace.
The extension is under the repository's [Apache License 2.0](../LICENSE); the
build copies that file into this directory, where `vsce` looks for it, so it
ships inside the package.

## Compiler setup

Build Goose separately. By default the extension looks in the workspace folder
for `goose`, `build/Release/goose`, `build/Debug/goose`, and `build/goose` (with
`.exe` on Windows), then uses `goose` on PATH. For another location, set
`goose.compilerPath` to the executable, without shell quotes or arguments.

For a project with imported files, set the entry file so that checks and runs
use the whole program:

```json
{
  "goose.compilerPath": "${workspaceFolder}/build/Release/goose.exe",
  "goose.entryFile": "samples/01_tour.goose",
  "goose.checkOnSave": true,
  "goose.checkTimeout": 15000,
  "goose.runArguments": []
}
```

Paths are relative to the containing workspace folder, or the source directory
when no folder is open. Settings can be configured per folder in a multi-root
workspace. `goose.stdlibPath` supplies `--stdlib`; leave it empty to use Goose's
usual lookup, including `GOOSE_STDLIB` and directories near the executable.

Checks use `goose --check` and never run the program or generate C. They check
saved files, wait while any open Goose file in the same folder has unsaved
changes, and clear stale results when sources change. Explicit commands save
open Goose files in the same folder first. The compiler currently stops at the
first error and typechecks instantiated functions; the extension preserves
those limitations. Configuring the entry file is particularly useful for libraries.

**Run with JIT** uses `--jit` and needs a Goose build with TinyCC enabled.
**Generate C** writes a `.c` file alongside the entry file, replacing an existing
file of that name. Compiler execution is disabled in untrusted workspaces;
highlighting, snippets and the outline remain available.

## Tasks

JIT Run is the extension's build task. **Ctrl+Shift+B** runs it directly while
editing a Goose file; Check and Generate C remain explicit commands under
**Tasks: Run Task** and in the Command Palette. The editor and status-bar play
buttons use the same JIT command. Check-on-save still only checks the program.

The Goose shortcuts take precedence while a Goose editor has focus. To use
your existing VS Code build/debug shortcuts instead, set
`"goose.useRunKeybindings": false`. Existing `tasks.json` group/default choices
are preserved. Buttons contributed by other extensions, such as CMake Tools,
still control their own build systems; use the **▶ Goose** button for Goose.

**Tasks: Run Task** offers tasks for the current Goose file (or configured entry,
even when no Goose editor is active).
For a persistent task, add this to your project's `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "type": "goose",
      "label": "Run Goose with JIT",
      "action": "run",
      "file": "main.goose",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": "$goose"
    },
    {
      "type": "goose",
      "label": "Generate C",
      "action": "generateC",
      "file": "main.goose",
      "output": "main.c"
    }
  ]
}
```

`file` defaults to `goose.entryFile` or the active Goose document; `${file}` and
`${workspaceFolder}` are supported. `output` defaults to the entry file with a
`.c` suffix. Tasks use process arguments directly, including paths with spaces.
Use VS Code's task save behavior to save edited files before running custom tasks.

## Run configurations

Select **Goose: Run with JIT** in the Run and Debug configuration picker to use
VS Code's native Run controls. This launches the same terminal task, using
`goose.entryFile` or the current Goose file. An optional `.vscode/launch.json`
configuration can choose a specific program:

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "goose",
    "request": "launch",
    "name": "Goose: Run with JIT",
    "program": "${workspaceFolder}/main.goose",
    "noDebug": true
  }]
}
```

Run configurations execute programs without breakpoints or stepping. Stop
an executing program with **Tasks: Terminate Task** or its terminal's trash
button. Program arguments still come from `goose.runArguments`.

## Verification

`npm test` exercises the actual TextMate/Oniguruma grammar, declaration/import
scanning, diagnostic parsing (including Windows paths and UTF-8 carets), task
arguments, and cancellation/stale-result handling. Compiler integration tests
run when a Goose build exists in the parent repository; set `GOOSE_TEST_COMPILER`
to test another executable. Missing compilers are reported as skipped tests.

For the editor integration suite, create an empty directory
`build/goose-extension-fixture` in the parent repository, then select **Test Goose
Extension** in this directory's Run and Debug menu. This suite writes fixtures
and settings only in that disposable directory. It exercises activation, real
compiler diagnostics, related error locations, imports, outline, tasks, and the
edit/save lifecycle, and real JIT execution through commands, build tasks, and
Run configurations. Build the compiler with TinyCC first.

The implementation uses VS Code's [TextMate grammar support](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide),
[task provider API](https://code.visualstudio.com/api/extension-guides/task-provider),
and [workspace trust API](https://code.visualstudio.com/api/extension-guides/workspace-trust).
