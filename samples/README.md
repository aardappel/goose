# Goose samples

Short, self-contained programs that show what writing Goose is like: the
things every language has to do, written the way Goose wants them written,
with the language's own strengths -- flat data, references into growing
arrays, relative links, variable-size enums, zero-allocation everything --
doing the work. Each file is one program with a comment at the top saying
what it demonstrates. The files are numbered in reading order, which is
the order below; the first six are the ones the rest assume.

Build and run any of them from this directory (the C compiler can be `cl`,
`clang` or `gcc`):

    goose -o tour.c 01_tour.goose && cl tour.c && tour

`run_samples.py` compiles and runs them all and compares their output with
`expected/`; `test/run_tests.py` calls it, so they are compiled and run as
part of the test suite.

## Foundations

| Sample | What it shows |
|---|---|
| [01_tour](01_tour.goose) | The core language in one program: values and widths, every array kind, slices, structs, control flow as expressions, several results, optionals, references bound with `.=` and compared with `.==`. |
| [02_memory](02_memory.goose) | The memory model, made visible: references that survive growth, scope exit as the only free, scratch buffers cleared through a helper, flat nested containers, `copy`, grow-shrink stacks, limited arrays, `reusable` pools. |
| [03_strings](03_strings.goose) | Strings as `u8` arrays: builders, `str`/`format`, slices as safe views, split/trim/join/find, parsing, sorting slices, UTF-8, inline small strings, a `format` overload. |
| [04_errors](04_errors.goose) | The three error idioms: a trailing `bool`, an optional narrowed by `if`/`guard`, and `return ... from` for deep failures; `assert`, `abort`, `exit`. |
| [05_shapes](05_shapes.goose) | Algebraic data types in fixed and variable mode, `match` by value and by reference, case functions as the virtual-call idiom. |
| [06_functions](06_functions.goose) | Generics without declaring them, overloading, UFCS, blocks that compile to loops, `return` through a HOF, nested functions with free variables. |

## Classic algorithms

| Sample | What it shows |
|---|---|
| [07_sieve](07_sieve.goose) | Eratosthenes: a tight loop at declared widths with the bounds checks proved away; Euclidean `%`; wrapping unsigned hashes. |
| [08_bignum](08_bignum.goose) | Arbitrary precision on `u32` limbs with `u64` carries: 100!, fib(500), 2^1000, decimal printing, a `format` overload; grow-only numbers popped through references. |
| [09_sorting](09_sorting.goose) | Insertion sort, a recursive merge sort with caller-owned scratch, binary search, all over slices; the library's `sort` and `stable_sort`; timings on a million ints. |
| [10_sudoku](10_sudoku.goose) | Backtracking with `u16` bitmask candidates and fewest-candidates-first, the whole state one struct passed by reference. |
| [11_maze](11_maze.goose) | BFS and Dijkstra over a byte grid: a grow-only queue with a read head, a grow-shrink heap through `heap_push`/`heap_pop`, paths drawn back onto the maze. |
| [12_huffman](12_huffman.goose) | Frequencies, a priority queue, a code tree with 2-byte relative links, bit packing, and a round trip. |

## Data structures the Goose way

| Sample | What it shows |
|---|---|
| [13_linked_list](13_linked_list.goose) | A doubly-linked list in a `reusable` pool with `in pool` links and a `self` sentinel found by `.==`: O(1) insert/remove through references, slot reuse, move-to-front. |
| [14_bst](14_bst.goose) | A binary search tree in a grow-only pool with self-relative links: insertion by retargeting one reference, recursive walks with caller-owned output. |
| [15_word_freq](15_word_freq.goose) | Word counts of a text file with a dictionary keyed by slices into the text: nothing copied, results sorted and tabulated. |
| [16_records](16_records.goose) | Orders with inline strings and item lists kept as one flat array of variable-size records; per-customer totals through a dictionary. |

## Parsers and interpreters

| Sample | What it shows |
|---|---|
| [17_calc](17_calc.goose) | An interactive calculator: tokens as fixed-mode enum values, recursive descent straight to values by functions nested in `evaluate` that share its locals, variables in a dictionary, every error one `return ... from`. Run with `calc < data/calc.stdin`. |
| [18_json](18_json.goose) | A JSON parser and printer: variable-mode nodes in one pool with 4-byte relative links, the parser state as locals of `parse` shared by the nested recursive functions, deep errors with positions, pretty and compact rendering, key and index lookups. |
| [19_vm](19_vm.goose) | A stack bytecode VM: a fixed-mode enum per instruction, a `match` dispatch loop, a disassembler, two hand-assembled programs. |

## Graphics, simulation, threads

| Sample | What it shows |
|---|---|
| [20_life](20_life.goose) | Conway's Life on a torus: two byte grids that swap roles, Euclidean `%` wraparound, ASCII frames. |
| [21_raytrace](21_raytrace.goose) | Spheres, a floor, shadows and reflections with `float3` math; writes `raytrace.ppm` and prints an ASCII preview. |
| [22_image_filters](22_image_filters.goose) | Box blur and Sobel over row slices, bounds-check free; writes `.pgm` files. |
| [23_mandelbrot_threads](23_mandelbrot_threads.goose) | A worker pool over typed queues: flat jobs in, flat rows out, reassembled in order; serial vs parallel timing on stderr. |

## Interop

| Sample | What it shows |
|---|---|
| [24_call_c](24_call_c.goose) | `extern fn` to libm and to a small C header (`call_c.h`): scalars, a slice, a struct through a reference, a string builder C appends to. Build with `goose --include call_c.h ...`. |
