# Goose for Visual Studio Code

An extension for the [Goose language](https://github.com/aardappel/goose). With
a compiler that has `goose lsp`, the compiler itself is the language server:
the extension starts it, and the server analyzes your programs as you type.
With an older compiler, the extension checks saved files with `goose check`
instead.
Other editors can use the same server: [Goose in other editors](../docs/editors.md)
has the configuration for Neovim, Helix and Emacs.

## Features

- With the language server:
  - errors and warnings as you type, over every file of the program (the
    entry file set in `goose.entryFile`, or the open file);
  - hover with declarations, types, instantiations and documentation
    comments;
  - go to definition (including across imports and on `import` lines);
  - find all references and rename across the program's files;
  - the outline and Go to Symbol in the file and the workspace;
  - semantic highlighting of names that resolve to a function, type,
    parameter, field or variant;
  - signature help while typing a call's arguments;
  - inlay hints for the types of `let`s and `var`s and the parameter names
    at calls;
  - folding of bodies and comment runs;
  - expand selection by syntax;
  - code lenses: above each generic function, the number of its
    instantiations, which a click lists, and **Run** above `main`, which
    runs that file with the JIT.

  **Goose: Restart Language Server** restarts the server;
  `goose.useLanguageServer` turns it off.

- Syntax highlighting for `.goose` files, including nested block comments,
  escaped strings and characters, raw `"""` strings across lines, decimal/hex
  floats, namespaces and builtins.
- Highlighting for `goose` fenced code blocks in Markdown (``` or `~~~`), in the
  editor and in the Markdown preview.
- Bracket matching, auto-closing, indentation, comment toggling, folding and
  snippets (`main`, `fn`, `struct`, `enum`, `for`, `guard`, `match`, and more).
- Without the language server: compiler checks on open/save, error and
  warning underlines, and Problems entries. Use **F8 / Shift+F8** to jump
  between problems. A compiler with `goose check --diagnostics json` reports
  the source range of each error and warning, which is what gets underlined,
  and instantiation chains appear as related locations in Problems at the
  calls that made them. With an older compiler the extension reads the text
  output instead: lexer errors use the compiler's caret, type errors highlight
  the source line, and instantiation chains link to the calling line.
  While a Goose file is open, a status bar item shows the last check: **Goose**
  with a check mark, **Goose: N errors** (opens Problems), or **Goose: Check
  Failed** when the check failed without a source location (compiler not found,
  timeout, or an unlocated message). A failure also adds a warning in Problems
  on the checked file with no text underlined, and clicking the item opens the
  Goose output with the full compiler output. With the language server the
  item counts the errors it reports. **Run** next to it runs the file with
  the JIT.
- **Goose: Check Program**, **Goose: Run with JIT**, **Goose: Build
  Executable** (`goose build`: an executable beside the source, linked with
  the packages the program uses), **Goose: Generate C**, and
  **Goose: Show Compiler Output** in the Command Palette. The editor's play
  button and **Run** status-bar button run the program in a task terminal,
  with stdin and clickable errors.
- **Ctrl+Shift+B**, **F5**, and **Ctrl+F5** run with JIT while editing Goose
  (**Cmd+Shift+B** for build on macOS). **Goose: Run with JIT** is also available
  in VS Code's Run configuration picker.
- Without the language server, a lexical scan provides top-level
  declarations in Outline, breadcrumbs and Go to Symbol (**Ctrl+Shift+O**).
  Imports are also clickable, and are resolved root-relative,
  importing-file-relative (`import .foo;`), or in the standard library, as
  the compiler resolves them.
- Check, Run, Build and Generate C tasks, plus the `$goose` problem matcher for
  custom tasks with absolute paths or paths relative to the workspace folder.

The language server also completes names in scope and members after a `.`,
finds references and calls, renames, shows inlay hints, folds, and formats:
**Format Document** (Shift+Alt+F) lays the file out as `goose format` does,
and `"editor.formatOnSave": true` does it on every save. It does not provide
debugging (`docs/design/language_server.md` has the plan). A
function body that no instantiation reaches has no types: hover says so, and
member completion there offers every field and function.

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

The script works from any working directory. It installs the locked npm
dependencies, runs the extension tests, bundles the extension and the language
client library into `dist/extension.js` with esbuild, and writes
`vscode/goose-language.vsix`. Use `--skip-install` to reuse existing
`node_modules`. The VSIX file is tracked in Git. Commit the rebuilt file with
extension updates so people can install without building locally. The
extension version still comes from `package.json`.

Open this `vscode/` directory in VS Code and press **F5** to launch an Extension
Development Host with the parent Goose repository open; the launch runs the
bundler first (`npm run bundle`, after an `npm install`).
Packaging is local; the package is not published to the Marketplace.
The extension is under the repository's [Apache License 2.0](../LICENSE); the
build copies that file into this directory, where `vsce` looks for it, so it
ships inside the package.

## Compiler setup

Build Goose separately. By default the extension looks in the workspace folder
for `goose`, `build/Release/goose`, `build/Debug/goose`, and `build/goose` (with
`.exe` on Windows), then uses `goose` on PATH. For another location, set
`goose.compilerPath` to the executable, without shell quotes or arguments.

Checking accepts a file with no `fn main()`, so editing an imported file does
not report a missing `main`. For a project with imported files, set the
entry file so that checks and runs use the whole program:

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

The language server is started once per window. It uses the compiler and
standard library of the first workspace folder (or of the open file, if
there is no folder), and analyzes every folder's `goose.entryFile` as a
program. A Goose file that no entry file imports is analyzed on its own, as
`goose check` would analyze it. The extension asks the compiler whether it
provides a language server (`goose lsp --probe`) at activation and whenever
the Goose settings, the folders or the workspace's trust change, and
otherwise falls back to the checks described below. The server's output is
in the Goose output channel. The server does not fetch packages: an
unfetched package is an error at its import, which **Goose: Check Program**
(with the language server, this runs `goose check` in a terminal) or
`goose fetch` resolves.

Without the language server, checks use `goose check --diagnostics json` and never run the program or
generate C. A compiler that rejects the flag is checked with plain `goose check`
from then on, until it is rebuilt, and its text output is parsed instead; the
Goose output channel shows the compiler's text either way. Checks run on
saved files, wait while any open Goose file in the same folder has unsaved
changes, and keep the last result, marked stale, until the next check completes.
Changing Goose settings or workspace folders, or closing the files that use an
entry file, clears its results instead.
Explicit commands save open Goose files in the same folder first. The compiler
currently stops at the first error and typechecks instantiated functions; the
extension preserves those limitations. Configuring the entry file is particularly
useful for libraries.

**Run with JIT** uses `--jit` and needs a Goose build with TinyCC enabled.
**Generate C** writes a `.c` file alongside the entry file, replacing an existing
file of that name. Compiler execution is disabled in untrusted workspaces;
highlighting, snippets and the outline remain available.

## Tasks

JIT Run is the extension's build task. **Ctrl+Shift+B** runs it directly while
editing a Goose file; Check, Build Executable and Generate C remain explicit
commands under **Tasks: Run Task** and in the Command Palette. The editor and status-bar play
buttons use the same JIT command. Check-on-save still only checks the program.

The Goose shortcuts take precedence while a Goose editor has focus. To use
your existing VS Code build/debug shortcuts instead, set
`"goose.useRunKeybindings": false`. Existing `tasks.json` group/default choices
are preserved. Buttons contributed by other extensions, such as CMake Tools,
still control their own build systems; use the **Run** button for Goose.

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

`npm test` covers the TextMate/Oniguruma grammar, declaration and import
scanning, and parsing of JSON and text diagnostics, including Windows paths and
UTF-8 byte columns. It also checks fallback from `--diagnostics json` to text,
task arguments, cancellation, and handling of stale results. Language-server
tests cover probing and setup, then use the real protocol to check diagnostics
after text changes and a hover response.

Compiler integration tests run when a Goose build exists in the parent
repository. Set `GOOSE_TEST_COMPILER` to test another executable. A missing
compiler is reported as a skip. So are JSON tests with a compiler that lacks the
flag, and server tests with one that lacks the verb.

For the editor integration suite, create an empty directory
`build/goose-extension-fixture` in the parent repository, then select **Test Goose
Extension** in this directory's Run and Debug menu. This suite writes fixtures
and settings only in that disposable directory. With the language server off,
it exercises activation, real compiler diagnostics, related error locations,
imports, outline, tasks, the edit/save lifecycle, and real JIT execution
through commands, build tasks, and Run configurations. Then, where the
compiler has a language server, it checks a hover answered by the server and
the server's code lenses: a generic function's instantiations, and **Run**
running the file it is in rather than the entry file. Build the compiler with
TinyCC first.

The implementation uses VS Code's [TextMate grammar support](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide),
[task provider API](https://code.visualstudio.com/api/extension-guides/task-provider),
and [workspace trust API](https://code.visualstudio.com/api/extension-guides/workspace-trust).
