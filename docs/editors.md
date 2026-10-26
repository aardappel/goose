# Goose in other editors

The compiler is its own language server: `goose lsp` speaks the Language
Server Protocol on stdin and stdout, so any editor with an LSP client can
use it. VS Code has an [extension](../vscode/README.md) that starts it for
you. For Neovim, Helix and Emacs, the configuration below is all that is
needed.

Every snippet starts `goose lsp` assuming the compiler is on PATH; if it
is not, replace `goose` with the executable's full path. The server finds
the standard library the way `goose check` does (`GOOSE_STDLIB`, or the
`stdlib/` directory near the executable). For a compiler that is not in
its source checkout, add `--stdlib` and the directory to the command:
`goose lsp --stdlib /path/to/goose/stdlib`.

Each open file is analyzed as a program of its own, with the files it
imports. To have a module checked as part of the program that imports it
(and the errors of that program shown in it), name the program's entry
file in the client's initialization options, as an absolute path:
`{ "entryFiles": ["/path/to/project/main.goose"] }`. The snippets show
where these options go.

The server's semantic tokens color names by what they resolve to
(functions, types, fields, variants, parameters, variables). They do not
cover keywords, strings or comments, and the TextMate grammar is only part
of the VS Code extension.

## Neovim

Neovim 0.11 or later, in `init.lua`:

```lua
vim.filetype.add({ extension = { goose = 'goose' } })
vim.lsp.config('goose', {
  cmd = { 'goose', 'lsp' },
  filetypes = { 'goose' },
  root_markers = { '.git' },
  -- init_options = { entryFiles = { '/path/to/project/main.goose' } },
})
vim.lsp.enable('goose')
```

On Neovim 0.10, start it from the filetype instead:

```lua
vim.filetype.add({ extension = { goose = 'goose' } })
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'goose',
  callback = function(ev)
    vim.lsp.start({ name = 'goose', cmd = { 'goose', 'lsp' }, root_dir = vim.fs.root(ev.buf, { '.git' }) })
  end,
})
```

By default, Neovim shows diagnostics, hover (`K`), go to definition
(`CTRL-]`), references (`grr`), rename (`grn`), document symbols (`gO`),
signature help (`CTRL-S` in Insert mode), completion (`CTRL-X CTRL-O`, or
as you type with `vim.lsp.completion.enable`) and semantic highlighting.
Inlay hints need `vim.lsp.inlay_hint.enable()`, folding needs
`foldmethod=expr` and `foldexpr=v:lua.vim.lsp.foldexpr()`, and code lenses
need `vim.lsp.codelens.refresh()` (called on `BufEnter` and `InsertLeave`,
for example). The lenses above generic functions count their
instantiations. To list the instantiations with `vim.lsp.codelens.run()`,
give Neovim the command and tell the server that the client has it:

```lua
vim.lsp.commands['goose.showInstantiations'] = function(command)
  vim.ui.select(command.arguments[2], { prompt = 'Instantiations of ' .. command.arguments[1] }, function() end)
end
-- and in vim.lsp.config('goose', ...):
--   init_options = { commands = { 'goose.showInstantiations' } },
```

## Helix

In `languages.toml` (in Helix's configuration directory, or `.helix/` in
the project):

```toml
[language-server.goose]
command = "goose"
args = ["lsp"]
# config = { entryFiles = ["/path/to/project/main.goose"] }

[[language]]
name = "goose"
scope = "source.goose"
file-types = ["goose"]
roots = [".git"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }
language-servers = ["goose"]
```

(`config` is what Helix sends as the initialization options.)

Helix shows diagnostics, hover (`space k`), go to definition (`gd`),
references (`gr`), rename (`space r`), document and workspace symbols
(`space s`, `space S`), signature help and completion as you type, and
inlay hints with `display-inlay-hints = true` under `[editor.lsp]` in
`config.toml`. Helix highlights with tree-sitter grammars, and Goose has
none. Helix also has no semantic tokens, folding or code lenses, so Goose
files are shown without colors.

## Emacs

With Eglot (built in since Emacs 29), in `init.el`:

```elisp
(define-derived-mode goose-mode prog-mode "Goose"
  (setq-local comment-start "// "))
(add-to-list 'auto-mode-alist '("\\.goose\\'" . goose-mode))
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs '(goose-mode "goose" "lsp")))
(add-hook 'goose-mode-hook #'eglot-ensure)
```

For entry files, the server program takes the options after the command:
`'(goose-mode "goose" "lsp" :initializationOptions (:entryFiles ["/path/to/project/main.goose"]))`.

Eglot shows diagnostics through Flymake, hover and signature help in
ElDoc, go to definition (`M-.`), references (`M-?`), rename
(`eglot-rename`), document symbols in Imenu, workspace symbols
(`xref-find-apropos`), completion (`completion-at-point`) and inlay hints;
the newest Eglot, from GNU ELPA, highlights with semantic tokens too
(`eglot-semantic-tokens-mode`). Eglot has no folding or code lenses.

## Formatting

`goose format` rewrites files in the standard layout: indentation, the
spaces between tokens, blank lines and trailing commas, with the line
breaks left where they are. As with gofmt, the types and `=`s of a run of
declarations (consecutive `const`s, a struct's fields) line up in columns. It takes files and directories (a directory
stands for the `.goose` files under it), and with none, or `-`, formats
standard input to standard output. A file that does not parse is left as
it is and its errors printed, with exit status 2; `goose format --check`
writes nothing and lists the files that are not formatted, with exit
status 1, for a CI step or a commit hook.

The language server formats too (`textDocument/formatting`), the editor's
text rather than the file on disk, with edits to the lines that change
only, so the cursor and marks on the others stay. A document that does not
parse gets no edits; its diagnostics say why.

In Neovim, `vim.lsp.buf.format()` formats; to format on save, in the
configuration above:

```lua
vim.api.nvim_create_autocmd('BufWritePre', {
  pattern = '*.goose',
  callback = function() vim.lsp.buf.format() end,
})
```

In Helix, `:format` formats, and `auto-format = true` in the `[[language]]`
entry above formats on save.

In Emacs, `eglot-format-buffer` formats; to format on save:

```elisp
(add-hook 'goose-mode-hook
          (lambda () (add-hook 'before-save-hook #'eglot-format-buffer nil t)))
```

Without the language server, an editor can run `goose format -` on the
buffer instead: with [conform.nvim](https://github.com/stevearc/conform.nvim)
in Neovim, say,

```lua
require('conform').setup({
  formatters = { goose = { command = 'goose', args = { 'format', '-' }, stdin = true } },
  formatters_by_ft = { goose = { 'goose' } },
  format_on_save = { timeout_ms = 2000 },
})
```

or `formatter = { command = "goose", args = ["format", "-"] }` in Helix's
`[[language]]` entry.
