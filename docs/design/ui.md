# The ui module (`ui`)

This document describes the optional ui module's implementation, test
coverage, and remaining work. See `docs/stdlib.md` for the API reference and
`stdlib/ui.goose` for the module source.

## What it is

`import ui;` gives a Goose program Nuklear, the single-header immediate-mode
GUI library: windows, rows and layout spaces, groups, trees and list views;
labels, buttons, check boxes, options, selectables, sliders, knobs, progress
bars, color pickers, properties, text fields and editors, charts; popups,
combo boxes, contextual menus, tooltips and menu bars; styles, fonts baked
from TrueType files, and drawing straight onto a window. It is built the way
`gfx` and `physics` are (`docs/design/gfx.md`), with Nuklear in the place of
SDL3:

    Goose program --extern fn--> gs_ui_* (goose_ui, native C) --> Nuklear (compiled in)

and draws through gfx: `stdlib/ui.goose` turns a frame into triangles and
draws them with a gfx pipeline, and feeds a context the input gfx collected.

The layer, `goose_ui` (`src/ui/`), is native C compiled once into a static
library: `goose` links it for JIT runs and hands its functions to TinyCC with
`tcc_add_symbol` (`AddUiSymbols`, `src/jit.h`), and a program built from the
generated C links the same archive. Neither the generated C nor TinyCC sees
Nuklear's header.

The API stays Nuklear's under Goose names -- `nk_button_label` is
`ui::button_label` -- and covers nearly all of `nuklear.h` a program calls:
361 functions. An immediate-mode program says each frame which windows and
widgets there are, and learns what the user did from the same calls; the
program's data stays its own, passed in by reference where a widget edits
it.

## How it is built

* **Nuklear** comes from the submodule `third_party/nuklear` (shallow),
  pinned to a commit of its master branch 36 commits after v4.13.3, which
  adds the link widget, tooltips shown after a delay and a property's
  precision; the next release should replace the pin. `cmake/ui.cmake`
  compiles its implementation once, in `src/ui/ui_nuklear.c`, with the
  configuration every file of the layer includes it with
  (`src/ui/ui_internal.h`): fixed-size types, the default allocator, font
  baking with its built-in font, vertex buffer output with 32-bit indices,
  256 bytes of typed text per frame, and `strtod` for its number parsing.
  The layer's own files build with warnings on, Nuklear's without.
* **Link inputs for AOT programs**: `build/ui/<config>/link-cc.rsp` and
  `link-msvc.rsp` on Windows name the archive, and on other systems `-lm`.
  `goose --ui-link msvc|cc` prints the path, looked for as `--gfx-link` does:
  `GOOSE_UI_LINK`, then a `ui/` beside the binary, then the build tree
  (`NativeLinkFile`, `src/utils.h`). A program that renders its ui through
  gfx passes the gfx file too.

**Opting out**: `-DGOOSE_UI=OFF`, or a checkout without the submodule. The
compiler then builds and behaves as before: ui programs typecheck and
generate the same C, and only running one in-process fails, with "this
compiler was built without Nuklear; check out third_party/nuklear and
reconfigure". `--ui-link` fails the same way.

## The compiler's side

* **Calls into the layer** are recognized by their C symbol's `gs_ui_`
  prefix. Codegen notes which native layers a program calls in one
  `NativeLayers` (`src/utils.h`), which a JIT run registers the symbols of
  (`RunJit`), and a compiler without the ui layer refuses the run.
* **A `thread_fn` never reaches the layer**: its handle tables and error
  state are the process's, and gfx, which it draws through, is main-thread
  only.

## The layer (`src/ui/`)

`ui_api.h` lists every function once (`GS_UI_API`) and every constant
(`GS_UI_CONSTANTS`); `test/api_check.py` checks `stdlib/ui.goose` declares
exactly those functions with the same C shapes, the same struct fields in
the same order, and the same constant values, as it does for gfx and
physics. The constants have Nuklear's values, a static assert in
`ui_nuklear.c` holding each to it.

* **Strings are slices**, never NUL-terminated: where Nuklear has a `_label`
  function taking a C string and a `_text` one taking a length, the layer
  has the `_label` name and passes the length on.
* **Memory Nuklear keeps pointing at is the layer's**: fonts' TrueType data
  and glyph ranges, a row's ratios (kept to the end of the frame), cursors,
  the scroll offsets a group writes back when it ends, the text a paste
  inserts. The program never lends memory beyond a call; a text field edits
  a copy, written back after.
* **Handles**: contexts, font atlases, fonts and text editors are a slot
  index with a generation, so a destroyed one is an error, not a crash.
* **No callbacks**: filters are picked from Nuklear's set (`FILTER_*`), the
  clipboard is text the program hands over and takes back, and what Nuklear
  draws comes back as arrays -- its command list flattened into one struct
  per command, or vertices, indices and draw calls from its converter.
* **Style** is one Goose-shaped struct read and written whole, where Nuklear
  hands out pointers into its `nk_style`; `push_style` and `pop_style` keep
  whole styles.
* **Scopes are checked.** Nuklear trusts its caller to pair every begin with
  its end, to put widgets only in an open window with a row laid out, and to
  begin no window inside another; otherwise it writes past its buffers or
  divides by zero. The layer keeps a stack of what is begun (windows,
  groups, popups, combo boxes, menus, trees, charts, rows, disabled runs)
  and checks every call against it, and each frame's input against the
  frame: a window begun during input is a misuse.
* **Errors** follow gfx: a failure outside the program's control returns
  false or a zero handle with the reason in `error()`; a misuse is skipped,
  printed as it happens (the first eight), counted, and the Goose side
  aborts with it at the next `input_begin`, `convert`, `render`,
  `destroy(context)` or `check()`. Nuklear's own asserts are routed into the
  same path.

* **Scale** is the layer's own: a context has one, the pixels to each
  unit of the ui's layout. Nuklear lays out and draws in its units as ever;
  `convert` multiplies the vertices and clip rectangles it produces, and
  the input functions divide positions, keeping the fractions Nuklear's
  whole-number input functions would lose. Text is sharp when its glyphs
  were baked at the scale: baking an atlas at a scale multiplies each
  font's size before Nuklear rasterizes it and sets the font's height back
  after, and Nuklear, which scales a glyph by that height over the baked
  one, then measures at the size and draws from the bigger glyphs. A
  context `create` made bakes its own font again when its scale changes.
  A destroyed atlas's texture is listed for the renderer that made it
  (`released_textures`), which `render` releases.

Where Nuklear misbehaves on its own, the layer steps around it:

* A character typed into a full text field moves the cursor as if it went
  in; `edit_string`'s filter turns away what would not fit, counting a
  selection or the character replaced as free.
* `nk_textedit_paste` takes the text's bytes for its characters, reading
  past a text with longer ones; the layer pastes what typing would, as far
  as it fits, as one undo step.
* `nk_edit_buffer` reads the window's editing mode but never writes it; the
  layer writes it back, as `nk_edit_string` does, so an editor's mode keys
  last beyond their frame.
* `nk_to_lower` subtracts where it should add, which leaves the fuzzy match
  pairing capitals only with capitals: both sides go in lower case.
* A progress bar is sized by its value before Nuklear clamps it; sliders,
  knobs and a plot of equal values divide by an empty range. The layer
  clamps the one and refuses or widens the others.
* A combo box's items string is read by count, past its end when it holds
  fewer; the layer counts them first.

Not exposed, because each needs a callback into Goose code, would lend
Nuklear the program's memory, or has a Goose form already: text filters of
the program's own, combo boxes filled by a callback and custom drawing
(`nk_push_custom`); custom allocators, fixed memory and compressed fonts;
Nuklear's string, buffer and draw list APIs; pushing single style values
(the style goes whole); and the printf-style labels and tooltips, which
`str()` makes.

## The Goose module (`stdlib/ui.goose`)

Namespace `ui`: constants, handles, value and style structs, the `extern`
declarations, and thin wrappers -- fresh arrays for commands, vertices,
text and lists; two results as two values (`widget`, `list_view_begin`,
`copied`); `edit_string` over a limited array (`u8[..k]`); combo boxes over
an array of strings; style item, image and rectangle helpers. Names avoid
Goose keywords and the globals a namespaced declaration would shadow inside
the module.

Drawing through gfx is `render(context)`, over the screen or a render
target, with a conversion's settings. What it keeps between frames -- a
pipeline, a sampler, vertex and index buffers grown to powers of two -- is
one global, made on first use and made again when `gfx::open_count()` says
gfx was closed and opened since, which also forgets the atlases' textures
so they are uploaded again. `input_from_gfx(context)` turns gfx's events of
the last frame, in order, into Nuklear's input: the mouse, editing keys by
their scancode names, shortcuts by the character their key types with Ctrl
or Command, typed text (turning gfx's text input on), the frame's duration,
and the clipboard both ways.

gfx gained what that needs, each tested on its own in
`test/gfx/gfx_events.goose`: `events()` (the last frame's input in order,
with key modifiers and typed characters), `text_input`, `inject_text`,
`scancode` and `key_name`, `clipboard` and `set_clipboard` (gfx's own when
headless, which keeps tests off the system clipboard), `open_count`, and the
X1 and X2 mouse buttons.

## Testing

* **`test/ui/`** in the regular suite, run AOT at -O0 and -O2 and through
  TinyCC where the compiler has the layer, and only typechecked and turned
  into C where it does not. Most drive a context with input of their own and
  check what comes back and what was drawn, headless and without gfx:
  `ui_fonts` (atlases, baking at 1 and 2, TrueType files, ranges, merging,
  metrics, released textures),
  `ui_windows` (window state and geometry, every kind of row, groups, trees,
  list views), `ui_widgets`, `ui_popups` (popups, combo boxes, contextual
  menus, tooltips, menus), `ui_edit` (filters, a text editor's operations,
  fields over buffers and limited arrays, flags, full fields, an editor's
  modes, keys and clipboard), `ui_style` (the style whole, pushed, reset and
  from a table, fonts, cursors, colors, helpers), `ui_draw` (every canvas
  command, conversion, the input queries, the ui at a scale) and
  `ui_charts`. `ui_render` draws through headless gfx and drives the
  context from injected gfx input: clicks, typing, editing keys, shortcuts
  and the clipboard; it reads back the screen and a render target, draws
  and clicks at twice the scale, and runs again after gfx is closed and
  opened. The tests call 361 of the layer's 361 functions. `ui_misuse`
  checks a widget outside any window aborts the program at the next frame
  with the reason, `ui_misuse_messages` shows what the layer says about 58
  misuses, each skipped without harm, and `ui_err_thread` that a
  `thread_fn` reaching ui is a compile error.
* **`samples/29_ui_todo.goose`**, a to-do list and a color mixer, runs
  headless in the samples runner for 30 frames, JIT and AOT.

Nuklear's layout and drawing are plain float arithmetic, the same on every
platform, so the tests print coordinates and colors as they come.

### Platforms tested

| | Windows 11 | Linux (Ubuntu 24.04 under WSL2) | macOS |
|---|---|---|---|
| Build | MSVC | clang++ for the compiler, gcc for the C | not run |
| Suite, incl. `test/ui/` | MSVC, TinyCC | gcc, TinyCC; sanitizer profile | not run |
| Sample | JIT, cl; in a window and headless | JIT, gcc | not run |

Under the sanitizer profile the fixtures that draw through gfx, like gfx's
own, stop on leaks LeakSanitizer finds in SDL's X11 setup and in Mesa's
lavapipe; the rest pass, the layer's allocations included.

## Follow-up work

* Replace the pinned commit with Nuklear's next release.
* Undo in `edit_string` fields: Nuklear clears the editor it shares between
  them on every call, so only `edit_buffer`'s text editors keep history.
* Custom text filters, and fonts measured by the program, which need a way
  for C to call a Goose function.
* The display's content scale from gfx (SDL has it), for a program that
  wants its ui sized as the system's own.
* Mouse wheel and double-click injection in gfx, for tests of the rest of
  `input_from_gfx`.
