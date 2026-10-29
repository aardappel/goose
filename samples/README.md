# Goose samples

> **Deprecated language features:** `reusable` and `reusable[]`. Use
> [`slot_pool<T>`](../docs/stdlib.md#slot_pool) for slots and
> [`slice_pool<T>`](../docs/stdlib.md#slice_pool) for contiguous runs.
> Builtin behavior and historical examples below remain relevant to existing
> code; removal is planned separately, after the remaining migration gaps.

If you have not written Goose before, read [`docs/tutorial.md`](../docs/tutorial.md)
first: it introduces the language by example and says which of these to read
when. [`docs/reference.md`](../docs/reference.md) then covers the whole
language, topic by topic.

These short, self-contained programs demonstrate common programming tasks
using Goose's memory model: flat data, references into growing arrays,
relative links, and variable-size enums, without heap allocation. Each file
starts with a comment explaining what it demonstrates. The files are numbered
in reading order. The later samples assume familiarity with the first six.

Build any sample into an executable beside it with the C compiler the Goose
compiler finds on the machine, or run it straight away inside the compiler's
process:

    goose build 01_tour.goose && ./01_tour
    goose 01_tour.goose

You can also build the C that the compiler writes by hand: write it with
`goose -o tour.c 01_tour.goose` and link it with the runtime that
`goose --emit-runtime` writes, or use `--standalone` for a C file that holds
the runtime itself. `goose build --dry-run` prints those commands.

`run_samples.py` compiles and runs them all and compares their output with
`expected/`, both as executables and in-process where the in-process backend
is available. `test/run_tests.py` calls it, so the samples are compiled and
run as part of the test suite.

## Foundations

| Sample | What it shows |
|---|---|
| [01_tour](01_tour.goose) | The core language in one program: values and widths, every array kind, slices, structs, control flow as expressions, several results, optionals, references bound with `.=` and compared with `.==`. |
| [02_memory](02_memory.goose) | The memory model, made visible: references that survive growth, scope exit as the only free, scratch buffers cleared through a helper, flat nested containers, `copy`, grow-shrink stacks, limited arrays, `reusable` pools. |
| [03_strings](03_strings.goose) | Strings as `u8` arrays: builders, `str`/`format`, slices as safe views, split/trim/join/find, parsing, sorting slices, UTF-8, inline small strings, a `format` overload. |
| [04_errors](04_errors.goose) | The three error idioms: a trailing `bool`, an optional narrowed by `if`/`guard`, and `return ... from` for deep failures; `assert`, `abort`, `exit`. |
| [05_shapes](05_shapes.goose) | Algebraic data types in fixed and variable mode, `match` by value and by reference, case functions as the virtual-call idiom; variable-mode values with their text inline, stored in one flat array and reached in any order through an index of 4-byte `in pool` links. |
| [06_functions](06_functions.goose) | Generics without declaring them, overloading, UFCS, blocks that compile to loops, `return` through a HOF, nested functions with free variables. |

## Classic algorithms

| Sample | What it shows |
|---|---|
| [07_sieve](07_sieve.goose) | Eratosthenes: a tight loop at declared widths whose bounds checks the compiler proves unnecessary and removes; Euclidean `%`; wrapping unsigned hashes. |
| [08_bignum](08_bignum.goose) | Arbitrary precision on `u32` limbs with `u64` carries: 100!, fib(500), 2^1000, decimal printing, a `format` overload; grow-only numbers popped through references. |
| [09_sorting](09_sorting.goose) | Insertion sort, a recursive merge sort with caller-owned scratch, binary search, all over slices; the library's `sort` and `stable_sort`; timings on a million ints. |
| [10_sudoku](10_sudoku.goose) | Backtracking with `u16` bitmask candidates, filling the cell with the fewest candidates first; the whole state is one struct passed by reference. |
| [11_maze](11_maze.goose) | BFS and Dijkstra over a byte grid: a grow-only queue with a read head, a grow-shrink heap through `heap_push`/`heap_pop`, paths drawn back onto the maze. |
| [12_huffman](12_huffman.goose) | Frequencies, a priority queue, a code tree with 2-byte relative links, bit packing, and a round trip. |

## Data structures

| Sample | What it shows |
|---|---|
| [13_linked_list](13_linked_list.goose) | A doubly-linked list in a `reusable` pool with `in pool` links and a `self` sentinel found by `.==`: O(1) insert/remove through references, slot reuse, move-to-front. |
| [14_bst](14_bst.goose) | A binary search tree in a grow-only pool with self-relative links: insertion by retargeting one reference, recursive walks with caller-owned output. |
| [15_word_freq](15_word_freq.goose) | Word counts of a text file, using a dictionary keyed by slices into the text, so no words are copied; the results are sorted and tabulated. |
| [16_records](16_records.goose) | Orders with inline strings and item lists kept as one flat array of variable-size records; per-customer totals through a dictionary. |

## Parsers and interpreters

| Sample | What it shows |
|---|---|
| [17_calc](17_calc.goose) | An interactive calculator: tokens as fixed-mode enum values; recursive descent that computes values directly, through functions nested in `evaluate` that share its locals; variables in a dictionary; every error reported by one `return ... from`. Run with `calc < data/calc.stdin`. |
| [18_json](18_json.goose) | A JSON parser and printer: variable-mode nodes in one pool with 4-byte relative links, the parser state as locals of `parse` shared by the nested recursive functions, deep errors with positions, pretty and compact rendering, key and index lookups. |
| [19_vm](19_vm.goose) | A stack bytecode VM: a fixed-mode enum per instruction, a `match` dispatch loop, a disassembler, two hand-assembled programs. |

## Graphics, simulation, threads

| Sample | What it shows |
|---|---|
| [20_life](20_life.goose) | Conway's Life on a torus: two byte grids that swap roles, Euclidean `%` wraparound, ASCII frames. |
| [21_raytrace](21_raytrace.goose) | Spheres, a floor, shadows and reflections with `float3` math; writes `raytrace.ppm` and prints an ASCII preview. |
| [22_image_filters](22_image_filters.goose) | Box blur and Sobel over row slices, bounds-check free; writes `.pgm` files. |
| [23_mandelbrot_threads](23_mandelbrot_threads.goose) | A worker pool over typed queues: flat jobs go in and flat rows come back, reassembled in order; serial vs parallel timing on stderr. |

## Interop

| Sample | What it shows |
|---|---|
| [24_call_c](24_call_c.goose) | `extern fn` to libm and to a small C header (`call_c.h`): scalars, a slice, a struct through a reference, a string builder C appends to. Build with `goose --include call_c.h ...`. |

## Serialization

| Sample | What it shows |
|---|---|
| [25_serialize](25_serialize.goose) | A word index saved and loaded as raw bytes: `bytes_of` views the element region without copying it, `to_bytes` builds the framed image, `from_bytes` verifies an untrusted one before it becomes a value. Fixed nodes with 4-byte links and a compact variable-size form of the same tree; tampered, truncated and extended files are rejected. |

## Pools of slices

| Sample | What it shows |
|---|---|
| [26_file_tree](26_file_tree.goose) | A directory tree: the nodes are kept in a stdlib `slot_pool` and passed around as references. Each directory's entries are one run of 4-byte `in nodes.items` links in a stdlib `slice_pool`, and a run is grown in place or moved by `realloc_slice`. `rm -r` returns slots and runs to the pools, `mv` relinks, and the pools' counts show the reuse. |

## The GPU

| Sample | What it shows |
|---|---|
| [27_gfx_cube](27_gfx_cube.goose) | A window and a spinning, lit cube with the `gfx` module: GLSL shaders written in the program as `"""` strings and compiled in by `embed_shader`, vertex and index buffers from `bytes_of`, a uniform block as a packed struct, a depth buffer, the arrow keys; saves its last frame as `gfx_cube.png`. Needs a compiler built with the gfx package (`GOOSE_BUILTIN_LIBS`); the test runner draws it off screen. |
| [28_physics_boxes](28_physics_boxes.goose) | Boxes fall into a walled arena containing two pyramids and a rotating arm. The `physics` module simulates them with Box3D on up to eight worker threads. Each frame retrieves all transforms in one call and writes them into an instance buffer. Each box is drawn as a unit cube with sunlight and shadows. Shaders are `"""` strings in the program; the two vertex shaders share their inputs through one global part. Space triggers an explosion, the mouse controls the view, and Up and Down adjust the rate at which boxes fall. Once the arena contains `--boxes` boxes, the oldest are returned to the top. Needs a compiler built with the gfx and physics packages (`GOOSE_BUILTIN_LIBS`); the test runner draws 120 frames off screen. |

## Windows and widgets

| Sample | What it shows |
|---|---|
| [29_ui_todo](29_ui_todo.goose) | A to-do list and a color mixer with the `ui` module: Nuklear's immediate-mode widgets drawn through `gfx`. Each frame lists the windows and widgets and reads back what the user did to them: a text field committed with Enter, buttons, check boxes, options, a menu bar, a tooltip, a progress bar, a color picker and properties. The check marks are recolored through Nuklear's color table, text is drawn in Roboto from the Nuklear submodule, and the whole ui is scaled up and down, with its font baked again at each scale. Saves its last frame as `ui_todo.png`. Needs a compiler built with the gfx package (`GOOSE_BUILTIN_LIBS`); the test runner draws 30 frames off screen. |

## A small game

| Sample | What it shows |
|---|---|
| [30_mini_minecraft](30_mini_minecraft.goose) | A playable voxel world using only `gfx`: procedural hills, trees, caves, coal, water, pixel textures, sky and a bitmap font. Exposed-face chunk meshes with corner shading, captured mouse look, fixed-step walking and swimming, a grid ray for mining and placement, inventory, four crafting recipes, lamps, mining fragments, a day/night cycle, and validated atomic saves. Starts directly in an 80 × 40 × 80 world. It uses no asset files and does not depend on physics or UI. The test runner checks gameplay and renders 30 frames off screen. |

Run `goose samples/30_mini_minecraft.goose`. WASD walks, Space jumps or swims,
Shift runs, Ctrl sneaks slowly and stops at ledges, and the mouse or arrow
keys look around. Jumping or releasing Ctrl lets you step off a ledge; sneaking
does not change the player's height. Hold the left mouse button to mine;
right-click or E places a block (hold to build repeatedly).
Select a slot with 1–8 or the wheel, middle-click to select the aimed-at
material, and press C to craft. Two wood blocks make eight planks; five planks
make a wooden pick. Three mined stone and two planks upgrade it to a stone
pick. One coal and one plank make four lamps. Hold Shift while clicking a
recipe or pressing its number to craft all affordable batches, up to the
inventory limit. Tools equip automatically and are only crafted once.

Crafting and pause release the cursor. Losing focus also pauses; click or
press Escape to resume. A full day lasts ten minutes, with a moving sun,
moon, stars, and peaceful nights that make lamps useful around a home.

H toggles help, Escape pauses, and F12 saves `mini_minecraft.png`. F5 explicitly
saves `mini_minecraft.sav` in the working directory; F9 loads it. Starting a
fresh world never overwrites a save. Saves include the time of day; older
saves without a clock load in the morning. Close the window to quit. `-- --seed N`
chooses terrain, `-- --frames N` renders a repeatable screenshot, and
`-- --test` runs the gameplay checks without opening a window. The finite
world deliberately omits mobs, hunger, damage, tool wear, and flowing water.

## A small Doom-like game

| Sample | What it shows |
|---|---|
| [31_mini_doom](31_mini_doom.goose) | A playable Freedoom E1M1 using `gfx` and `audio`: checked little-endian WAD parsing, palette and patch decoding, a texture atlas, BSP-clipped floor polygons, textured walls, billboard sprites, sliding collision, stairs, falling, doors, lifts, pickups, combat, and one update/draw loop. Includes pistol, shotgun, chaingun and fists, four enemy types, imp fireballs, exploding barrels, damaging floors, secrets, the blue key and the exit. Uses the supplied `data/freedoom1-e1m1.wad`. |

Run `goose samples/31_mini_doom.goose` from the repository root, or pass the
source's path from any working directory. It starts directly in the level.
Its default WAD path is `data/freedoom1-e1m1.wad` under `resource_dir()`:
beside the entry source for JIT, or beside a compiled executable for AOT.
When distributing the executable, include that `data` directory, or use
`--wad <path>` to select an external WAD. The sample runner supplies `--wad`
because it builds executables outside the sample's asset directory.
The recursive BSP walk uses caller-owned growable scratch, restoring its
length on return; clipping has no fixed polygon-corner limit.
WASD walks, Shift runs, mouse or Left/Right turns, E or Space uses doors
and switches, and left mouse or Ctrl fires. Select fists, pistol, shotgun,
or chaingun with 1–4 (the latter two must be collected). Tab toggles the map;
H toggles help. Escape pauses and releases the cursor; click or Escape
resumes. Losing focus pauses. R restarts after death or completing the level.
F12 saves `mini_doom.png` in the working directory.

The sample uses medium-difficulty things. Find the blue key and use the exit
switch. Lifts lower when used or crossed, wait, and carry you back up. Some
walls are secret doors. The automap shows the whole map and does not pause
play. Blue lines mark locked doors; yellow lines mark triggers.

`-- --frames 30` runs for thirty frames with a fixed timestep and saves a PNG.
The sample runner uses this mode for its rendering smoke check.
`-- --silent` disables audio playback; if the audio device fails, the game also plays silently.
`-- --wad PATH` overrides the default WAD; relative paths use the working directory.

This is an independent, approximate game, not a compatible Doom engine.
It omits music, saves, multiplayer, jumping, vertical aiming, monster
pathfinding, directional monster art and the larger weapon set. Unimplemented
weapon/ammo pickups provide bullets; a chainsaw pickup does the same. The
WAD's own sprites, font and sounds keep the sample self-contained beyond
that one asset file.
