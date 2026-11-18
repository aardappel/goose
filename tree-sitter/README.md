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
| `queries/helix/highlights.scm` | Generated highlight query with Helix capture names. |
| `queries/tags.scm` | Shared tree-sitter tags query. |
| `test/corpus/` | `tree-sitter test` cases. |
| `tree-sitter.json` | Grammar metadata: file types, scope, query paths. |
| `build.py` | Generates the parser and Helix highlights, and checks them. |

The parser itself (`src/parser.c`, `src/grammar.json`, `src/node-types.json`,
`src/tree_sitter/`) is generated from `grammar.js`. Of those, only
`src/parser.c` and `src/tree_sitter/parser.h` are checked in, because Zed
needs them: it fetches a grammar with git at the commit an extension names,
and compiles `src/parser.c` and `src/scanner.c` there with `src/` on the
include path, without running the tree-sitter CLI. Zed does this on the
user's machine when they install a dev extension. For an extension published
to Zed's registry, the registry's CI does it once, and users download the
compiled grammar. Run
`build.py` before pointing an editor at this directory.

## Development

```sh
python tree-sitter/build.py           # generate the parser and Helix highlights
python tree-sitter/build.py --check   # also test the corpus, queries and .goose files
```

The tree-sitter CLI is the version `package.json` pins. `build.py` installs it
into `node_modules/` on first use, so Node.js is the only requirement.

Edit `queries/highlights.scm`, not the generated Helix highlight query.
`build.py` maps its capture names to Helix scopes without changing pattern
order. `--check` compiles every Helix query; `tree-sitter test` also compiles
the shared highlight and tags queries registered in `tree-sitter.json`.
This does not exercise Helix's rendering or indentation algorithm.

It parses every `.goose` file in the repository. Each must parse, except
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

**Helix**: see the [editor setup](../docs/editors.md#helix) for the language
server, grammar and query installation. Use `queries/helix/*.scm`, rather than
the shared Neovim-style highlight query: Helix uses different capture names.
These queries provide highlighting (Helix 25.07+), indentation and text objects.
Rainbow brackets and symbols (using the shared `queries/tags.scm`) require
Helix built from master; see the editor setup for installation details.
