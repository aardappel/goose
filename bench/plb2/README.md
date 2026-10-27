# plb2 in Goose

A Goose implementation of the four tasks of
[plb2](https://github.com/attractivechaos/plb2), Programming Language
Benchmark v2, and a script that runs it against plb2's own C and Rust.

plb2 times one algorithm per task, implemented the same way in every
language, with the bottleneck in the language's own code rather than in a
library:

| task | what it does | what it exercises |
|---|---|---|
| `nqueen` | counts the solutions of the 15-queens problem | nested loops, integer bit operations |
| `matmul` | multiplies two 1500x1500 `f64` matrices | the inner loop of an i-k-j product |
| `sudoku` | solves 20 hard sudokus 200 times (kudoku) | small fixed arrays, indices loaded from tables |
| `bedcov` | finds the overlaps of 1,000,000 intervals with 1,000,000 others (implicit interval tree) | array access in the pattern of a binary search |

## Layout

`goose/` is laid out as a language directory of plb2 (`src/goose` there):
one program per task, and a `Makefile` in the style of the other languages'.
`compare.py` builds and times Goose, C and Rust here, reading C and Rust
from a plb2 checkout next to this repository's checkout (or `$PLB2`) and
writing everything under `bench/plb2/build/`:

```
python bench/plb2/compare.py build                   # Goose, C and Rust
python bench/plb2/compare.py run                     # timings and a ratio table
python bench/plb2/compare.py build c --tag c-native --cflags=-march=native
python bench/plb2/compare.py run --only matmul --langs goose,c,c-native
```

Each language is built as plb2's Makefiles build it: C with clang `-O3
-fwrapv -flto`, Rust with `rustc -C opt-level=3`, and the C that Goose
generates with the same clang at `-O3`. A run first checks every program's
output against C's, then times each one with its output going to the null
device, as plb2's hyperfine runs do.

## The ports

Each port follows plb2's C implementation function by function: the same
algorithm on the same data, printing the same bytes. Where Goose offers a
feature of its own, the port uses it:

* **`nqueen`** keeps its four per-row arrays as `i32[31]` and `u32[31]`
  locals and runs at `i32`, the width of the C's `int`s.
* **`matmul`** keeps each matrix as one flat `f64[>..]` and takes the rows
  each loop works on as slices. An `assert` on each row slice states that it
  is `n` long, which lets the compiler drop every bounds check from the inner
  loop. The product is a `simd fn`: it is compiled for the baseline
  instruction set and again for AVX2 and AVX-512, and each call runs the
  widest version the CPU supports.
* **`sudoku`** holds the sparse 729x324 matrix in one 11 KB struct of fixed
  arrays, passed by `const` reference, and each solve's state in fixed arrays
  on the native stack. The solver binds the matrix's rows in place
  (`let rs .= aux.r[c]`) rather than copying them. Goose's stdout is
  unbuffered, so the solutions are collected in a `u8[>..]` that is written
  out in 64 KB blocks, as C's stdio buffer writes them.
* **`bedcov`** builds both interval sets with `push` directly into
  grow-only arrays, sorts with the C's in-place radix sort (a `recursive fn`
  over slices, as the C recurses over pointer ranges), and collects each
  query's overlaps in one grow-only array that every query clears and
  refills.

## Results

Measured on an AMD Ryzen 9 9950X3D under Windows 11, pinned to the core
complex without the stacked cache (CPUs 16-31), with clang 22.1.3 and rustc
1.98.0. Each time is the best of 15 runs (five rounds over three renamed
copies of each executable), in seconds. A ratio above 1 means Goose is
faster.

| task | Goose | C | Rust | C / Goose | Rust / Goose |
|---|---:|---:|---:|---:|---:|
| `nqueen` | 1.758 | 1.707 | 1.834 | 0.97 | 1.04 |
| `matmul` | 0.196 | 0.394 | 0.475 | 2.01 | 2.42 |
| `sudoku` | 1.054 | 1.030 | 1.198 | 0.98 | 1.14 |
| `bedcov` | 0.520 | 0.506 | 0.587 | 0.97 | 1.13 |

plb2 builds C and Rust for the baseline x86-64 instruction set, and that is
what gives Goose's `simd` its lead on `matmul`. Built for this CPU
(`-march=native`, `-C target-cpu=native`), C runs `matmul` in 0.211 s, level
with Goose, and Rust in 0.324 s. Goose built with `matmul`'s `simd fn` as a
plain `fn` (`compare.py build --no-simd`) runs it in 0.392 s, level with
plb2's C build. On the other three tasks the native builds are no faster:
they are within 3% either way, except on `bedcov`, which they run 7% (C)
and 14% (Rust) slower.

Under Linux (WSL2, Ubuntu 24.04, the same CPUs), with the C that Goose
generated here compiled by clang 20.1.8 and gcc 13.3 at `-O3`, and plb2's C
by the same at its own flags, best of 10, in seconds:

| task | Goose, clang | C, clang | C / Goose | Goose, gcc | C, gcc | C / Goose |
|---|---:|---:|---:|---:|---:|---:|
| `nqueen` | 1.809 | 1.686 | 0.93 | 1.906 | 1.812 | 0.95 |
| `matmul` | 0.220 | 0.388 | 1.76 | 0.370 | 0.362 | 0.98 |
| `sudoku` | 1.050 | 1.066 | 1.02 | 1.302 | 1.243 | 0.95 |
| `bedcov` | 0.533 | 0.517 | 0.97 | 0.552 | 0.523 | 0.95 |

Goose's C backend builds `simd` versions only under clang on x86-64, so
under gcc `matmul` runs the baseline loop, as fast as C's. Under gcc the
bounds checks slow the code down, while under clang they have no
measurable cost: with the checks removed from the generated C, `sudoku`
runs in 1.242 s under gcc, level with C, and `bedcov` in 0.552 s (from
0.561 in that run).

## Where Goose is behind, and why

**`nqueen` trails C by 3% here and by 7% under Linux clang 20, and the time
is not in extra work.** The generated C of the solver matches plb2's C
statement for statement. The bounds checks on the row index remain,
because the analysis cannot carry `k < n` around the backtracking loop. But
clang folds them into one compare on the path that advances a row, and
removing them makes no measurable difference. What remains is a difference
in how clang compiles two ways of writing the same loop. Hand edits to the
generated C that change only how its expressions are written (temporaries,
casts, the mask of a shift count) move the time by as much as the gap, in
both directions. So does the placement of the code: with every loop aligned
to 32 or 64 bytes, the Linux gap is 3-5%.

The search for the next free column is written as plb2's C writes it, a
`while` with a `break`:

```goose
while i < n {
    if y & (1 << i) == 0 { break; }
    i++;
}
```

`while i < n && y & (1 << i) != 0 { i++; }` runs in the same time: a `&&`
that decides a branch compiles to the same chain of branches a C compiler
would produce for it (`docs/implementation.md` §6.11). The `&&` used to
compile to a `uint8_t` flag that the right operand set inside an `if`. In
that form it made this loop 3% slower, and rewriting plb2's C into the same
shape made the C 4-6% slower, under clang 22 and clang 20 alike.

**`sudoku` is level with C, after one change that made it 13% faster.** The
solver first copied the matrix rows it walks into locals (`let rs =
aux.r[c]`, an 18-byte `u16[9]`), as is natural for a fixed-size value.
Clang keeps such a copy as a real one on the stack, once per constraint
visited; binding the row in place (`let rs .= aux.r[c]`) took the solver
from 1.21 to 1.07 s. The bounds checks left in the solver are all on
indices loaded from the matrix (`sc[aux.c[r][j]]`), which no analysis of the
program's own arithmetic can bound; removing them from the generated C makes
no measurable difference.

**`matmul` matches C at each instruction set, and the `simd` version is what
makes it 2x faster than plb2's build of the C.** Without `simd`, Goose runs
the baseline (SSE2) build of the loop as fast as C does. With `simd`, it
runs the AVX2 build as fast as C compiled with `-march=native`, and the
executable stays portable. The asserts are needed because the analysis
derives a slice's length from its bounds only when they differ by a
constant (`a[lo..lo + 4]`), and here the row length `n` comes from the
command line.

**`bedcov` is level with C under clang**, within the 3% by which its runs
vary. It is bound by memory: almost all its time is spent in the million
queries, each of which walks down a 32 MB array. Its remaining bounds checks
have no measurable cost. Neither does the zero fill of the traversal stack,
which the port does once per query and the C does not: a limited array
(`StackCell[..64]`, pushed and popped, never filled) runs in the same time.

plb2's Rust programs differ from its C programs in ways that affect the results.
`matmul` multiplies vectors of vectors through iterator zips. `bedcov` generates
one interval fewer per set (`1..n`) and uses the standard library's stable sort
instead of a radix sort. Its output therefore differs from the other two
languages'.

## Compiler follow-ups

* A `let` copy of a fixed-size value that is only read while the source stays
  unchanged could refer to the source instead, which would make `sudoku` as
  first written 13% faster.
* Track a slice's length when its bounds differ by a variable
  (`a[lo..lo + n]`), so that the inner loop of `matmul` needs no asserts.
