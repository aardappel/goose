# Goose formatter and language server

A formatter and a stdio language server, both written in Goose.

| File                | Role                                                  |
| ------------------- | ----------------------------------------------------- |
| `main.goose`        | CLI and `--lsp` entry points                          |
| `formatter.goose`   | Formatter, shared by the CLI and the server           |
| `scan.goose`        | Tokenizer that keeps comments and literal spellings   |
| `lsp.goose`         | Language server requests                              |
| `protocol.goose`    | Open documents and JSON-RPC output helpers            |
| `diagnostics.goose` | Syntax and compiler diagnostics, and their publishing |
| `check.goose`       | Runs `goose --check` and reads its messages           |
| `workspace.goose`   | Finds the files a document imports                    |
| `hover.goose`       | Declaration signatures and doc comments for hover     |
| `rpc.goose`         | JSON-RPC message storage (uses `stdlib/json.goose`)   |
| `config.goose`      | `gls.json` lookup and parsing                         |

## Usage

From the repository root:

```sh
cmake -S . -B build
cmake --build build --target goose-tools --config Release
build/goose-tools --write file.goose
build/goose-tools --check file.goose
build/goose-tools < file.goose
build/goose-tools --lsp
```

Multi-configuration builds put the executable in `build/Release/`, and Windows
adds the `.exe` suffix. To run the sources directly through the JIT:

```sh
build/goose --jit tools/gls/main.goose -- --lsp
```

`--write` formats files in place and replaces each changed file atomically.
`--check` exits 1 if a file needs formatting. Read errors, write errors and
incomplete source exit 2. With no option, one file or stdin is formatted to
stdout.

The formatter keeps comments, literal spellings, raw-string margins and LF or
CRLF line endings. It checks that the token stream is unchanged before it
returns any output.

## Configuration

Put `gls.json` (or `.gls`, also JSON) in the project directory:

```json
{
  "formatter": {
    "singleLineExpressions": true,
    "maxBlankLinesBetweenFunctions": 1,
    "maxLineWidth": 100,
    "maxCompactStatements": 1
  }
}
```

The CLI and the server look in the source file's directory and then in each
parent, and use the closest file. If a directory has both names, `gls.json`
wins. Stdin and unsaved editor documents start from the working directory.
Settings are read again on every format request. An invalid file stops
formatting with an error and leaves sources untouched.

| Option                          | Default | Meaning                                                     |
| ------------------------------- | ------- | ----------------------------------------------------------- |
| `singleLineExpressions`         | `true`  | Keep inline one-expression blocks on one line                |
| `maxCompactStatements`          | `1`     | Most top-level statements in a one-line body (1 to 20)      |
| `maxLineWidth`                  | `100`   | Longest allowed one-line body, in columns (20 to 1000)      |
| `maxBlankLinesBetweenFunctions` | `1`     | Most blank lines kept between one-line functions (0 to 100) |
| `indentWidth`                   | `4`     | Spaces per indent level (1 to 16)                           |
| `insertSpaces`                  | `true`  | Indent with spaces instead of tabs                          |

Unknown option names are errors. Without `indentWidth` and `insertSpaces`, the
CLI uses four spaces and the server uses the editor's formatting options.

### Single-line expressions

A single-expression function body stays on one line by default, for example
`fn twice(x: i64) -> i64 { x * 2 }`. Inside a multiline function, a one-line
block that contains one expression, such as `if c < 128 { return 1; }`, also
stays on one line. The formatter preserves that inline block shape; it does
not pull a block onto one line when the source already spreads it across lines.
Set `singleLineExpressions` to `false` when you want those inline blocks and
single-expression functions expanded.

A final expression or a block such as `if` counts as one statement. Statements
inside a block are not counted. The line, including indentation, must also fit
in `maxLineWidth` columns, with a tab counting as `indentWidth`. A body over
either limit is expanded.

Comments, declarations, `let`, `var`, `const`, loops and `match` always keep a
body multiline. Empty bodies stay `{}`.

Neighbouring functions are separated by at most `maxBlankLinesBetweenFunctions`
blank lines when both are one line. If either is multiline, exactly one blank
line separates them. Leading doc comments stay with the function below them.

## Language server

The server supports UTF-16 positions, full document sync, formatting,
`willSaveWaitUntil`, symbols, completion, hover, definitions and
diagnostics. Point any LSP client at `goose-tools --lsp` and enable format on
save. The built executable needs nothing else installed, except the Goose
compiler for the compiler diagnostics below.

### Diagnostics

Syntax errors (unclosed delimiters, unterminated literals) are found by the
server itself and update as you type.

Type and semantic errors come from the compiler. When a file is opened or saved,
the server runs `goose --check` on the file on disk, so the compiler never sees
unsaved text. Errors and warnings are published on the file they belong to, so
an error in an imported file appears there, with the call chain as related
information. A result is hidden as soon as its document changes, and comes back
at the next save. The server is busy while the compiler runs, which is
usually well under a second. Runs are stopped after `timeoutMs`.

A file with no `fn main()` is checked as a module, so library files do not
report a missing `main`. Set `entry` to check a file together with the program
that imports it.

The compiler is the first of these that exists:

1. `check.compiler` in `gls.json`. A bare name is looked up on `PATH`, and a
   relative path is relative to the `gls.json`.
2. The `GOOSE_COMPILER` environment variable.
3. `goose` on `PATH`.

If it cannot be started, the server shows one warning and reports syntax errors
only. Clients can turn compiler diagnostics off with the initialization option
`{"semanticDiagnostics": false}`, as the VS Code extension does because it runs
the compiler itself.

```json
{
  "check": {
    "enabled": true,
    "compiler": "goose",
    "stdlib": "stdlib",
    "entry": "src/main.goose",
    "timeoutMs": 15000
  }
}
```

All `check` options are optional. `timeoutMs` is 1000 to 300000.

### Navigation and hover

Navigation and completion are lexical. They cover top-level declarations of the
document and of the files it imports, directly or through other imports.
Locals, members and overload resolution are not implemented, so every overload
of a name is listed.

Imports are resolved like the compiler does. `import .a.b;` is `a/b.goose` beside
the importing file. `import a.b;` is relative to the program's root file, then
the standard library. The server does not know the root file, so it tries the
document's directory and its parents up to the one holding `gls.json` (three
levels without one), then the `check.entry` directory. The standard library is
`check.stdlib`, `GOOSE_STDLIB`, and whatever `goose --print-stdlib` reports for
the configured compiler, so the search order lives only in the compiler. The
server asks once and reuses the answer until the settings change. A name in
a namespaced file is found as `namespace::name` only. Imported files are read
again on every request, from the open editor text where a file is open.

Hover shows the declaration header without its body, then any leading `//`,
`///`, `/* */` or `/** */` comments. Markdown in comments is kept and comment
markers are removed. A blank line detaches a comment from the declaration, and a
trailing comment belongs to the code before it. Overloads with the same name are
listed together. Clients get Markdown or plain text depending on what they
support. Types come from the declaration only, with no inference.

The [VS Code extension](../../vscode/README.md) uses this server. It finds a
built `goose-tools` and does not ship these sources.
