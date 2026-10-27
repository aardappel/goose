# kostya/benchmarks in Goose

Goose versions of the five tests of
[kostya/benchmarks](https://github.com/kostya/benchmarks) (Brainfuck,
Base64, Json, Matmul, Primes), laid out the way that repository lays out a
language's tests. `compare.py` builds them and measures them alongside the
suite's own C, C++ and Rust tests, in the same way the suite measures them.

## The rules, and how the Goose tests follow them

The suite asks for code "as the average software developer would write it":
algorithms as the public sources describe them and as the other languages'
tests implement them, standard containers and libraries used as their
documentation shows, idiomatic data structures, and release builds. Within
those rules, each test here is written as idiomatic Goose and uses what
Goose's memory model and library provide:

| test | file | written as | Goose in it |
|---|---|---|---|
| Brainfuck | `brainfuck/bf.goose` | `bf.rs`: an op tree, a tape that starts at one cell and grows, run recursively | the ops of every loop body are one run of a single op pool; the parser keeps its unfinished bodies on one shared stack, since a recursive function holds no growable array of its own |
| Base64 | `base64/test.goose` | `base64.rs`: the library codec, a new buffer per call | `std`'s `base64`, whose loops the C compiler vectorizes; each buffer is a data-stack bump, freed at the closing brace of the loop body |
| Json | `json/test.goose` | `json_struct.rs` (serde, typed) | `json::Reader` maps the text onto `Coordinate`s as serde's derive does, without building a tree |
| Json | `json/test_dom.goose` | `json_value.rs` (serde `Value`), RapidJSON's `Document` | `json::parse`'s tree: fixed-size nodes in one array, linked by 4-byte self-relative offsets, strings as views of the text |
| Matmul | `matmul/matmul.goose` | `matmul.jl` without threads: `c[i][j] += a[i][k] * b[k][j]` with j innermost, no transpose | a matrix is an array of row slices, and the rows are runs of one `reusable[]` pool; one `assert` per row removes the bounds checks from every index in the inner loop |
| Primes | `primes/primes.goose` | `primes.rs`: sieve of Atkin, a trie of the primes' digits, a breadth-first walk | trie nodes in one array; each node's map is a run of (digit, node) pairs in a `reusable[]` pool; queue entries hold their prefix inline in a `u8[..16]` |

The Goose tests depart from the other languages' tests in two ways. Both
are forced by the memory model, which puts them in the same category as
"V and Python don't provide [unordered hashmaps] out of box" in the suite's
own notes:

* Primes: a `dictionary` grows, and a growable value cannot be an element of
  an array. Every trie node is an array element, so a node's map from digit
  to child is an unordered run of at most ten pairs, looked up by scanning
  it, as Zig's `array_hash_map` does below eight entries.
* Brainfuck, Matmul, Primes: Goose has no heap, so what the C++ and Rust
  allocate one at a time (an op vector per loop body, a row, a trie node)
  is stored in pools that the caller owns.

`common/notify.h` implements the suite's measurement protocol (a TCP message
to `localhost:9001` as the measured work starts and another as it ends) for
Goose, which has no sockets. It is a few lines of C over the suite's
`common/libnotify`, reached through `extern fn` and `--include`, and it
names each test `Goose/<C compiler>`, as the C tests name theirs.

## Results

Linux (WSL 2 on Windows 11), Ryzen 9 9950X3D, clang 20.1.8, Rust 1.99.0,
kostya/benchmarks at 5bc68b3, Goose at this commit. Every implementation is
built with the suite's flags (`-O3 -march=native -flto=auto`, plus
`-std=c23` for C, Goose's C included, and `-C target-cpu=native` for Rust)
and timed between its two notify messages. Times are the median of 7
interleaved rounds. Memory is the suite's base + increase of the resident
set.

### Brainfuck bench.b

| test | time, s | min | memory, MiB | its time / Goose's |
|---|---:|---:|---:|---:|
| goose | 0.749 | 0.740 | 1.29 + 0.00 | 1.00 |
| c | 0.803 | 0.792 | 1.05 + 0.00 | 1.07 |
| c++ | 0.754 | 0.752 | 2.00 + 0.00 | 1.01 |
| rust | 0.715 | 0.712 | 1.12 + 0.00 | 0.95 |

### Brainfuck mandel.b

| test | time, s | min | memory, MiB | its time / Goose's |
|---|---:|---:|---:|---:|
| goose | 8.716 | 8.590 | 2.30 + 0.00 | 1.00 |
| c | 9.201 | 9.149 | 1.05 + 0.77 | 1.06 |
| c++ | 9.192 | 9.164 | 3.68 + 0.52 | 1.05 |
| rust | 9.880 | 9.844 | 1.14 + 1.04 | 1.13 |

### Base64

| test | time, s | min | memory, MiB | its time / Goose's |
|---|---:|---:|---:|---:|
| goose | 0.088 | 0.088 | 2.55 + 0.00 | 1.00 |
| c | 0.606 | 0.604 | 2.11 + 0.00 | 6.90 |
| c aklomp | 0.044 | 0.043 | 2.16 + 0.00 | 0.50 |
| rust | 0.467 | 0.464 | 2.44 + 0.00 | 5.31 |

### Json

| test | time, s | min | memory, MiB | its time / Goose's |
|---|---:|---:|---:|---:|
| goose | 0.062 | 0.059 | 113.11 + 12.28 | 1.00 |
| goose dom | 0.082 | 0.081 | 113.19 + 183.07 | 1.33 |
| rust serde typed | 0.081 | 0.079 | 111.81 + 10.85 | 1.32 |
| rust serde untyped | 0.441 | 0.381 | 112.12 + 839.50 | 7.13 |
| rust serde custom | 0.079 | 0.079 | 111.54 + 0.00 | 1.28 |
| c++ rapidjson | 0.131 | 0.128 | 114.93 + 129.34 | 2.12 |
| c++ rapidjson precise | 0.174 | 0.170 | 114.20 + 129.68 | 2.80 |
| c++ simdjson | 0.040 | 0.028 | 113.90 + 60.26 | 0.65 |
| c++ simdjson dom | 0.088 | 0.045 | 113.75 + 177.33 | 1.42 |
| c yyjson | 0.074 | 0.057 | 112.00 + 229.96 | 1.20 |

### Matmul (1500)

| test | time, s | min | memory, MiB | its time / Goose's |
|---|---:|---:|---:|---:|
| goose | 0.234 | 0.227 | 2.71 + 52.32 | 1.00 |
| c | 2.475 | 2.416 | 2.80 + 67.80 | 10.57 |
| rust | 1.341 | 1.263 | 2.52 + 68.32 | 5.73 |

### Primes

| test | time, s | min | memory, MiB | its time / Goose's |
|---|---:|---:|---:|---:|
| goose | 0.033 | 0.032 | 2.32 + 23.13 | 1.00 |
| c++ | 0.037 | 0.036 | 4.00 + 71.62 | 1.12 |
| rust | 0.085 | 0.083 | 2.38 + 72.68 | 2.60 |

## Where Goose is behind, and why

Goose is the fastest of the four languages on Brainfuck mandel.b, Matmul and
Primes. On Base64 and Json it is the fastest of the implementations that are
not SIMD libraries. It is behind in three places:

* **Brainfuck bench.b, 5% behind Rust** (level with C++, ahead of C). The
  gap is not caused by the bounds checks that remain on the tape's indexing
  (Rust's test uses `get_unchecked`): taking them out of the generated C
  changed the time by 0-4%, which is within the spread that code placement
  alone causes (builds of the same source differ by up to 10%).
  `for op in program` walks a pointer to the end of the ops, as Rust's loop
  does. Iterating with the slice's data, its length and an index would take
  three registers around the recursive call, two of them spilled and
  reloaded per op, which cost bench.b 2-3%. Two other ways of writing the
  interpreter were slower overall: a reference to the current cell, which in
  Goose stays valid as the tape grows (13% faster on bench.b, 14-30% slower
  on mandel.b), and passing the head's position into and out of each `run`
  (10% slower on bench.b).
* **Base64, 2x behind aklomp/base64**, a C library of hand-written SSSE3,
  AVX2 and AVX-512 intrinsics (the suite lists it separately from plain C,
  as "C (aklomp)"). Goose is 5.3x ahead of Rust's base64 crate and 6.9x
  ahead of plain C. Goose's codec is plain Goose code that clang vectorizes:
  the regrouping of the bytes vectorizes well, but the translation of 6-bit
  values to characters and back compiles to compares and selects, where
  aklomp uses table shuffles. About 10% of Goose's time goes to the zero
  fill that `resize` does on the output before the codec overwrites it.
* **Json, 1.5-2x behind simdjson** On-Demand (median 0.040 s, min 0.028 s,
  against 0.062 s and 0.059 s), which finds the document's structure 64
  bytes at a time with SIMD. Goose is 1.3x ahead of serde typed and 2.1x
  ahead of RapidJSON, and the tree version is 5x ahead of serde's `Value`
  while using under a quarter of its memory. Half of the numbers
  (`"x": -1.34e-30`) lie outside the exact fast path (19 digits and a power
  of ten up to 22). `std`'s `json` reads those with the Eisel-Lemire
  algorithm, as Go, Rust's core and fast_float do, and uses its big-integer
  rounding only for the rare number whose rounding Eisel-Lemire leaves open.
  Using the big integers for all of them took 0.133 s. Goose's means match
  Python's to the last digit; serde's default parser is one ulp off in z.

Matmul follows the loop order of the suite's Julia test (`matmul.jl`,
whose `c[m, n] += a[m, k] * b[k, n]` runs down columns because Julia's
arrays are column-major) rather than the transposed dot products of plb that
the C and Rust tests use. The suite allows this: "variants are acceptable if
the reference implementation exists". Every cell still sums the same
products in the same order, starting from zero, so the result is bit for bit
the same as Rust's at every size tried (101, 333, 1000, 1500). But the inner
loop runs along rows of b and c, which vectorizes and needs no transposed
copy (a quarter less memory), whereas each dot product is a single
dependency chain of adds. Written as plb's version, Goose took 1.218 s,
level with Rust. In that version C is 2x behind Rust because clang fuses the
multiply and add of `t += p[k] * r[k]` into one FMA by default under
`-march=native`: on Zen 5 that step takes 4 cycles, where a separate add
takes 2. Goose, like Rust, rounds every operation separately.

Memory: Primes uses a third of the memory that the C++ and Rust use, because
its trie is one array of 17-byte nodes and one array of 9-byte map entries,
whereas each C++ and Rust node is a hash map with allocations of its own.
Matmul uses 52 MiB against 68, since it has no transposed copy, and the Json
tree uses 183 MiB against serde `Value`'s 840. Elsewhere Goose's numbers are
within half a MiB of the others'.

## Running it

`compare.py` reads the suite's checkout from `$KOSTYA`, or from a
`kostya-benchmarks/` or `benchmarks/` directory next to this repository's
checkout, and builds under `bench/kostya2/build` (or `--build-dir`). It
fetches the C libraries the suite's Makefiles fetch (RapidJSON, simdjson,
yyjson, aklomp/base64) and needs clang, cargo and git. If `/tmp/1.json`, the
Json test's input, is missing, it is written by `json/generate_json.py`, a
seeded Python copy of the suite's `generate_json.rb`.

```
python3 bench/kostya2/compare.py build                  # Goose, C, C++ and Rust
python3 bench/kostya2/compare.py check                  # every output, once
python3 bench/kostya2/compare.py run --reps 5           # the tables above
python3 bench/kostya2/compare.py run --only json --langs goose,rust
python3 bench/kostya2/compare.py ab bench.b old/bf new/bf --reps 10
```

`ab` runs several builds of one test in turn, round after round. On this
machine, code placement alone changes the Brainfuck tests' times by up to
10% between builds of the same source, so a change to one of them is only
worth believing if it holds across several builds of each side (vary
`-falign-functions` to get them).

Measure on Linux. On Windows `compare.py` builds the C and C++ tests with
the shims in `common/win`, but every implementation, C included, ran
Brainfuck 6x and Matmul 2.7x slower there than under WSL on the same
machine, so Windows numbers are not comparable with the suite's and say
little about the code.

## In the suite itself

The `add-goose` branch of the suite adds the tests in the places where the
suite keeps each language's tests (`brainfuck/bf.goose`, `base64/test.goose`,
`json/test.goose`, `json/test_dom.goose`, `matmul/matmul.goose`,
`primes/primes.goose`), a POSIX-only `common/goose/notify.h` over
`common/libnotify`, and the following:

* `common/commands.mk`, where `-w` keeps warnings about the generated C's
  unused runtime helpers and temporaries out of the build log:
  ```make
  GOOSE_BUILD =		goose -O2 --standalone --include notify.h -o $@.c $^ && clang $(CLANG_FLAGS) -w -std=c23 -I../common/goose -o $@ $@.c $(LIBNOTIFY_FLAGS) -lm -pthread
  ```
* in each test's `Makefile`, its binary in `executables` and a rule, for
  example in `brainfuck/Makefile`:
  ```make
  target/bin_goose: bf.goose | target libnotify
  	$(GOOSE_BUILD)
  ```
  (`target/base64_goose`, `target/json_goose` and `target/json_dom_goose`,
  `target/matmul_goose`, `target/primes_goose` likewise).
* `docker/Dockerfile`, which builds Goose from source at a commit on GitHub:
  ```dockerfile
  # https://github.com/aardappel/goose
  ARG GOOSE=<a goose commit>
  RUN git clone -q https://github.com/aardappel/goose \
      && git -C goose checkout -q $GOOSE \
      && CXX=clang++ cmake -S goose -B goose/build -DCMAKE_BUILD_TYPE=Release -DGOOSE_LIBTCC=OFF \
      && cmake --build goose/build --target goose -j
  ENV PATH="/root/bin/goose/build/:${PATH}"
  ```
* `docker/versions.rb`:
  ```ruby
  'Goose' => -> { `git -C /root/bin/goose log -1 --format='%h %cs'` },
  ```
