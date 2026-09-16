# Goose for Visual Studio Code

An extension for the [Goose language](https://github.com/aardappel/goose). No
language server, runtime dependencies, or compiler changes are required.

## Features

- Syntax highlighting for `.goose` files, including nested block comments,
  escaped strings and characters, decimal/hex floats, namespaces and builtins.
- Bracket matching, auto-closing, indentation, comment toggling, folding and
  snippets (`main`, `fn`, `struct`, `enum`, `for`, `guard`, `match`, and more).
- Compiler checks on open/save, error and warning underlines, and Problems
  entries. Use **F8 / Shift+F8** to jump between problems. Lexer errors use the
  compiler's caret; type errors highlight the source line. Instantiation chains
  appear as related locations in Problems.
- **Goose: Check Program**, **Goose: Run Program**, **Goose: Generate C**, and
  **Goose: Show Compiler Output** in the Command Palette. The editor's play
  button runs the program in a task terminal, with stdin and clickable errors.
- Top-level declarations in Outline, breadcrumbs and Go to Symbol (**Ctrl+Shift+O**).
- Clickable imports, with root-relative, importing-file-relative (`import .foo;`),
  and standard-library resolution matching the compiler.
- Check, Run and Generate C tasks, plus the `$goose` problem matcher for custom
  tasks with absolute paths or paths relative to the workspace folder.

These are lexical and compiler-command features. Semantic completion, rename,
cross-file symbol definitions, formatting, debugging, and an LSP are deferred.

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
Use `--skip-install` to reuse existing `node_modules`. The stable VSIX filename
is intentionally allowed in Git; commit the rebuilt file with extension updates
so people can install without building locally. The extension version still
comes from `package.json`.

Open this `vscode/` directory in VS Code and press **F5** to launch an Extension
Development Host with the parent Goose repository open. No build is needed.
Packaging is local; the package is not published to the Marketplace.
`UNLICENSED` deliberately does not introduce a license for the repository.

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

**Run Program** uses `--jit` and needs a Goose build with TinyCC enabled.
**Generate C** writes a `.c` file alongside the entry file, replacing an existing
file of that name. Compiler execution is disabled in untrusted workspaces;
highlighting, snippets and the outline remain available.

## Tasks

**Tasks: Run Task** offers tasks for the current Goose file (or configured entry).
For a persistent task, add this to your project's `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "type": "goose",
      "label": "Check my Goose program",
      "action": "check",
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
edit/save lifecycle. Build the compiler first.

The implementation uses VS Code's [TextMate grammar support](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide),
[task provider API](https://code.visualstudio.com/api/extension-guides/task-provider),
and [workspace trust API](https://code.visualstudio.com/api/extension-guides/workspace-trust).
