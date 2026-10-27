# tree-sitter-goose

A [tree-sitter](https://tree-sitter.github.io/) grammar and highlight query for
[Goose](https://github.com/aardappel/goose), for editors that highlight with
tree-sitter (Neovim, Helix, Zed, …).

The grammar follows the compiler's recursive-descent parser (`src/parser.h`)
and the spec's grammar sketch (`docs/goose_spec.md`, Appendix D). It is laxer
than the compiler in a few places to keep it small (declaration modifiers in any
order, for example), but it should never reject a program the compiler accepts.

## Layout

| Path | |
|---|---|
| `grammar.js` | The grammar. |
| `src/scanner.c` | Hand-written external scanner for nesting `/* */` comments. |
| `queries/highlights.scm` | Highlight query (nvim-style capture names). |
| `test/corpus/` | `tree-sitter test` cases. |
| `tree-sitter.json` | Grammar metadata: file types, scope, query paths. |
| `build.py` | Generates the parser, and checks it. |

The parser itself (`src/parser.c`, `src/grammar.json`, `src/node-types.json`,
`src/tree_sitter/`) is generated from `grammar.js` and not checked in. Run
`build.py` before pointing an editor at this directory.

## Development

```sh
python tree-sitter/build.py           # generate the parser
python tree-sitter/build.py --check   # also run the corpus and parse every .goose file
```

The tree-sitter CLI is the version `package.json` pins. `build.py` installs it
into `node_modules/` on first use, so Node.js is the only requirement.

`--check` parses every `.goose` file in the repository. Each must parse, except
the files that are deliberately malformed, which are listed in `MALFORMED` in
`build.py`: the error and recovery tests, and the token soup in
`test/syntax/lexer_tokens.goose`. When the compiler's syntax changes, update
`grammar.js` in the same change, and `--check` should still pass. To update
the corpus's expected trees after a grammar change, run
`npx tree-sitter test --update` here and review the diff.

## Editor setup

Each editor builds the grammar from this directory, once `build.py` has
generated the parser, and copies `queries/highlights.scm` into its own query
path.

**Neovim** (nvim-treesitter):

```lua
vim.filetype.add({ extension = { goose = 'goose' } })
require('nvim-treesitter.parsers').get_parser_configs().goose = {
  install_info = {
    url = '/path/to/goose/tree-sitter',
    files = { 'src/parser.c', 'src/scanner.c' },
  },
  filetype = 'goose',
}
```

Then run `:TSInstall goose` and copy `queries/highlights.scm` to
`~/.config/nvim/queries/goose/highlights.scm`.

**Helix** (`languages.toml`):

```toml
[[language]]
name = "goose"
scope = "source.goose"
file-types = ["goose"]
comment-token = "//"
block-comment-tokens = { start = "/*", end = "*/" }

[[grammar]]
name = "goose"
source = { path = "/path/to/goose/tree-sitter" }
```

Then run `hx --grammar build` and copy `queries/highlights.scm` to
`~/.config/helix/runtime/queries/goose/highlights.scm`.
