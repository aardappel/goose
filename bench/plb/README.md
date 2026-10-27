# Programming-Language-Benchmarks in Goose

Goose programs for the problems of
[Programming-Language-Benchmarks](https://github.com/hanabi1224/Programming-Language-Benchmarks)
(PLB), one directory per problem, named as PLB names its sources: `1.goose`
is a single-threaded program, `2-m.goose` a multithreaded one. Every problem
but `http-server` is here; Goose has no networking.

PLB takes its problems and their rules from the Computer Language Benchmarks
Game: each program runs the algorithm the problem prescribes, and within
that limit it may be as fast as the language allows, with threads (`-m`)
and intrinsics (`-i`) marked in the file name. These programs use what
Goose offers for speed: pools with 4-byte relative links instead of
allocations, `simd fn` for loops that vectorize, `thread_fn` workers and
typed queues for the `-m` programs, and bounds checks in the hot loops that
the compiler proves unnecessary and removes. Where the other languages use a
library that Goose does not have (big integers, MD5), the Goose program
includes its own implementation.

## Running

`plb.py` builds, tests and times the Goose programs and PLB's own Rust, C and
C++ programs. It reads the problems, inputs, expected outputs, data files and
reference sources from a PLB checkout beside this repository's checkout (or
`$PLB`), and writes everything under `bench/plb/build/`.

```
python bench/plb/plb.py build                      # every Goose program
python bench/plb/plb.py build --langs rust,c,cpp   # the reference programs
python bench/plb/plb.py test                       # outputs against PLB's expected ones
python bench/plb/plb.py bench nbody --reps 5       # timings and peak memory, interleaved
python bench/plb/plb.py report --markdown          # the table below
```

Everything is built the way PLB builds C, with clang `-O3 -march=broadwell
-fomit-frame-pointer`. This includes the generated Goose C, so its
`simd fn`s are capped at AVX2 like the references (`--cflags=-DGS_SIMD=2`
lets them use AVX-512). Rust uses PLB's Cargo profile and dependencies (LTO,
one codegen unit, `-Ctarget-cpu=broadwell`). On Windows the C and C++
references are built with a few POSIX shims (`winshim/`); C's nbody/5.c
builds with gcc only. Runs are pinned (`--cpus`, default 16-31: on the
Ryzen 9950X3D these were measured on, those CPUs are the core complex
without the large L3).

## Results

Times are wall time in ms, best of at least 5 runs, with the program and its
peak memory under each time. They are for the whole process, so about 11 ms
of every row is Windows process start-up.
Measured October 2026 on CPUs 16-31 of a Ryzen 9950X3D under Windows 11.
Each language's fastest program is shown, single-threaded and multithreaded
separately (a reference that uses OpenMP, rayon or a multithreaded async
runtime counts as multithreaded whatever its name). "ratio" is the fastest
other language's time divided by Goose's: above 1, Goose is faster.

| problem | input | Goose | Rust | C/C++ | ratio | Goose -m | Rust -m | C/C++ -m | ratio -m |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | 15 | 25 <sub>1 9.2M</sub> | 165 <sub>5 11.5M</sub> | - | 6.56 | 16 <sub>2-m 12.6M</sub> | - | - | - |
| binarytrees | 18 | 108 <sub>1 19.7M</sub> | 1625 <sub>4 40.0M</sub> | - | 15.04 | 30 <sub>2-m 69.7M</sub> | - | - | - |
| coro-prime-sieve | 1000 | 15 <sub>1 7.8M</sub> | 61 <sub>3 9.5M</sub> | - | 4.20 | - | 21 <sub>3-m 10.6M</sub> | - | - |
| coro-prime-sieve | 4000 | 34 <sub>1 8.0M</sub> | 761 <sub>3 14.9M</sub> | - | 22.66 | - | 115 <sub>5 13.0M</sub> | - | - |
| edigits | 100000 | 25 <sub>1 9.3M</sub> | 28 <sub>1 7.9M</sub> | - | 1.11 | - | - | - | - |
| edigits | 250001 | 59 <sub>1 11.3M</sub> | 70 <sub>1 8.5M</sub> | - | 1.20 | - | - | - | - |
| fannkuch-redux | 10 | 45 <sub>1 7.7M</sub> | 78 <sub>3-i 7.7M</sub> | - | 1.74 | 16 <sub>1-m 7.8M</sub> | 18 <sub>3-im 9.0M</sub> | 22 <sub>1-im 9.1M</sub> | 1.10 |
| fannkuch-redux | 11 | 399 <sub>1 7.7M</sub> | 809 <sub>3-i 7.7M</sub> | - | 2.03 | 59 <sub>1-m 7.8M</sub> | 68 <sub>3-im 9.0M</sub> | 87 <sub>1-im 9.1M</sub> | 1.15 |
| fasta | 250000 | 18 <sub>1 8.8M</sub> | 20 <sub>6 7.7M</sub> | - | 1.13 | 18 <sub>1-m 9.3M</sub> | 31 <sub>5c-m 17.6M</sub> | - | 1.76 |
| fasta | 2500000 | 65 <sub>1 8.8M</sub> | 76 <sub>6 7.7M</sub> | - | 1.18 | 60 <sub>1-m 13.2M</sub> | 189 <sub>5-m 17.6M</sub> | - | 3.16 |
| helloworld | QwQ | 12 <sub>1 7.7M</sub> | 12 <sub>1 7.7M</sub> | 12 <sub>1 7.7M</sub> | 0.99 | - | - | - | - |
| json-serde | sample 5000 | 25 <sub>1 12.7M</sub> | 28 <sub>4-i 14.5M</sub> | - | 1.12 | - | - | - | - |
| json-serde | canada 15 | 113 <sub>1 55.7M</sub> | 116 <sub>5-i 62.1M</sub> | - | 1.02 | - | - | - | - |
| knucleotide | 250000_in | 31 <sub>1 16.2M</sub> | 38 <sub>8 16.2M</sub> | - | 1.23 | 22 <sub>1-m 23.7M</sub> | 25 <sub>8-m 21.5M</sub> | 35 <sub>1 20.7M</sub> | 1.12 |
| knucleotide | 2500000_in | 144 <sub>1 38.0M</sub> | 182 <sub>8 26.9M</sub> | - | 1.27 | 69 <sub>1-m 71.0M</sub> | 70 <sub>8-m 32.6M</sub> | 182 <sub>1 31.0M</sub> | 1.02 |
| lru | 100 500000 | 18 <sub>2 7.7M</sub> | 24 <sub>0-unsafe 7.7M</sub> | - | 1.31 | - | - | - | - |
| lru | 1000 1000000 | 27 <sub>2 7.8M</sub> | 29 <sub>0-unsafe 7.7M</sub> | - | 1.10 | - | - | - | - |
| lru | 1000 3000000 | 56 <sub>2 7.8M</sub> | 85 <sub>0-unsafe 7.8M</sub> | - | 1.52 | - | - | - | - |
| mandelbrot | 1000 | 18 <sub>1 7.9M</sub> | 19 <sub>8 7.8M</sub> | - | 1.05 | 14 <sub>1-m 8.4M</sub> | - | - | - |
| mandelbrot | 5000 | 143 <sub>1 10.8M</sub> | 173 <sub>8 10.5M</sub> | - | 1.21 | 29 <sub>1-m 12.4M</sub> | - | - | - |
| merkletrees | 15 | 38 <sub>1 12.0M</sub> | 179 <sub>1 15.5M</sub> | - | 4.76 | - | - | - | - |
| merkletrees | 17 | 127 <sub>1 24.7M</sub> | 770 <sub>1 39.6M</sub> | - | 6.04 | - | - | - | - |
| nbody | 500000 | 25 <sub>1 7.7M</sub> | 22 <sub>4-i 7.7M</sub> | 22 <sub>0-i 7.7M</sub> | 0.89 | - | - | - | - |
| nbody | 5000000 | 132 <sub>1 7.7M</sub> | 103 <sub>4-i 7.7M</sub> | 108 <sub>0-i 7.7M</sub> | 0.78 | - | - | - | - |
| nsieve | 10 | 39 <sub>1 17.5M</sub> | 42 <sub>1 17.2M</sub> | 39 <sub>1 17.2M</sub> | 0.99 | - | - | - | - |
| nsieve | 12 | 135 <sub>2 12.6M</sub> | 168 <sub>2 12.3M</sub> | 137 <sub>1 12.4M</sub> | 1.02 | - | - | - | - |
| pidigits | 8000 | 150 <sub>1 7.9M</sub> | 880 <sub>2 8.8M</sub> | - | 5.85 | - | - | - | - |
| pidigits | 4000 | 45 <sub>1 7.8M</sub> | 218 <sub>2 8.2M</sub> | - | 4.88 | - | - | - | - |
| regex-redux | 250000_in | 65 <sub>1 17.8M</sub> | - | - | - | 32 <sub>2-m 25.1M</sub> | 38 <sub>7 19.6M</sub> | - | 1.16 |
| regex-redux | 2500000_in | 490 <sub>1 105.5M</sub> | - | - | - | 179 <sub>2-m 177.6M</sub> | 239 <sub>7 106.2M</sub> | - | 1.33 |
| secp256k1 | 500 | 31 <sub>1 7.7M</sub> | 29 <sub>0 7.8M</sub> | - | 0.94 | - | - | - | - |
| secp256k1 | 2000 | 76 <sub>1 7.7M</sub> | 70 <sub>0 7.8M</sub> | - | 0.93 | - | - | - | - |
| spectral-norm | 2000 | 32 <sub>2 7.8M</sub> | 63 <sub>7 7.8M</sub> | - | 1.95 | 17 <sub>2-m 8.7M</sub> | 21 <sub>7-m 9.2M</sub> | 19 <sub>8 9.0M</sub> | 1.13 |
| spectral-norm | 4000 | 91 <sub>2 7.8M</sub> | 213 <sub>7 7.7M</sub> | - | 2.35 | 25 <sub>2-m 9.0M</sub> | 40 <sub>7-m 9.4M</sub> | 31 <sub>8 9.1M</sub> | 1.22 |
| spectral-norm | 8000 | 322 <sub>2 7.9M</sub> | 812 <sub>7 7.8M</sub> | - | 2.52 | 52 <sub>2-m 9.7M</sub> | 114 <sub>7-m 9.6M</sub> | 77 <sub>8 9.1M</sub> | 1.47 |

The allocation-heavy problems (binarytrees, merkletrees) say as much about
Windows' heap as about Rust: Rust's `Box` allocations go through
`HeapAlloc`, which is slower than the glibc malloc that PLB's own runs use.

With AVX-512 allowed, the `simd fn` programs are faster still: nbody/2 takes
79.7 ms at 5000000 (against 103 for Rust's fastest), spectral-norm/2 126 ms
and 2-m 39 ms at 8000, and mandelbrot/1-m 21 ms at 5000.

## Notes on the programs

* **binarytrees** builds every tree in a pool local to the function that
  makes it, with 8-byte nodes and 4-byte self-relative links. The pool is
  freed by one pointer reset when the function returns, which the rules
  allow (the C and C++ programs use memory pools). `2-m` runs each depth's
  iterations on workers.
* **merkletrees** forbids arenas ("every tree node needs to be either
  separately freed or GCed"), so its nodes come one at a time from a
  `reusable` pool with a freelist, and each tree is freed node by node.
* **coro-prime-sieve**: Goose has no coroutines, and its queues are one per
  element type, so the chain of thousands of filters cannot be a chain of
  threads either. The program builds the same chain as the generator
  programs (C#, D, Python): one lazy filter stage per prime, each pulling
  from the one before it, where a pull is a call and the value is handed
  back as its return value. It is not the channel-and-coroutine structure
  of the Rust programs, so its ratio says little about coroutines.
* **nbody/1** is the plain scalar algorithm in a form clang unrolls
  completely; **nbody/2** keeps the five bodies in the lanes of 8-wide arrays
  and handles the ten pairs as two rotations, which is faster than nbody/1
  with AVX-512 and slower with AVX2.
* **spectral-norm/1** computes the products exactly as a row-by-row loop
  does, eight rows per vector; **/2** also sums four columns' fractions over
  the product of their denominators, one division per four columns, as C's
  `6-im` does.
* **fannkuch-redux** keeps a permutation in one u64, a nibble per element,
  and flips two permutations at once for the instruction-level parallelism.
* **fasta** uses the prescribed linear congruential generator as written,
  and integer thresholds for the cumulative probabilities.
* **knucleotide/1** counts in a hash table of its own, as C's program does;
  **/2** counts with the stdlib `dictionary`.
* **lru/1** uses the stdlib `dictionary`; **/2** a table made for the cache.
* **json-serde** reads into typed records with the stdlib `json` reader and
  writes with its writer, numbers included, except that integral values are
  written as integers, as the Rust programs' `MyF64` writes them.
* **pidigits**, **edigits** and **secp256k1** include their own big-integer
  arithmetic on 64-bit limbs. In the pidigits spigot, the products and sums
  are fused into one pass each (GMP's addmul kind). edigits uses Toom-3 and
  Karatsuba multiplication and divide-and-conquer division, as the Rust
  crates implement them. secp256k1 uses fixed 4x64-bit field elements and
  follows the algorithm of the hand-written `1.rs`.

## Where Goose is behind, and why

* **nbody** (0.78 against intrinsics, 0.90 against plain C++): the `-i`
  programs avoid the divider by using a float reciprocal square root
  estimate and Newton steps, which Goose cannot express (it has no
  intrinsics and no float bit casts). Against the plain C++ program (132
  against 119 ms), the generated C is the same loop, and the speed of
  clang's code for it varies by 10-20% with unrelated changes around it.
* **secp256k1** (0.93): Rust's `0.rs` uses the k256 crate, whose scalar
  multiplication uses precomputed tables of signed windows; the Goose program
  follows the hand-written algorithm (`1.rs`, which it beats by 18x). The
  Goose program's field arithmetic is faster than k256's.
* **regex-redux/1**: the stdlib regex engine is a DFA that takes one table
  lookup per byte, and its prefilter rates byte pairs by English letter
  frequencies, which does not help on DNA. Rust's regex crate finds these
  alternations of short literals with a SIMD multi-literal search (Teddy).
  Rust's `7.rs` runs on two threads; forced to one thread, it takes about
  255 ms against Goose's 490.
* **knucleotide/1-m's memory** (71 MB against Rust's 33 at 2500000, at the
  same speed): workers share no memory, so each gets a copy of the sequence,
  and `thread_spawn` copies a growable argument twice (on the spawner's data
  stack, then into the new thread).

## Limitations found

In the compiler, runtime and library, while writing these programs:

* **No 64x64->128-bit multiply.** The 32-bit-halves idiom compiles to one
  `mulx` only while clang sees it as a separate function. Once it is
  inlined, clang rewrites the halves of an operand it knows something about,
  and the pattern is lost. The bignum programs declare the function
  `recursive` to keep it out of line.
* **A loop-invariant divisor** gets its reciprocal precomputed before the
  loop (`gs_divu_gen`, a 128/64-bit division) even where the loop runs about
  once per entry, and a u32 divisor gets a 64-bit reciprocal. In
  coro-prime-sieve this took 138 ms instead of 31.
* **Bounds checks kept** in an inner loop over a slice made inside the outer
  loop (`let cols = src[1..];` there, `cols[j]` and `cols[j + 3]` under
  `while j + 4 <= cols.len`). When the slice is made before the outer loop,
  the checks are removed.
* **A lifetime false positive**: a slice returned by a function that reads a
  field of a struct passed by reference is tied to an unrelated local that
  is cleared later.

  ```goose
  struct Rd { text: const u8[:] }
  fn read(r: Rd&) -> const u8[:] { r.text[0..1] }
  fn main() {
      var text: u8[>..] = []; text.append("xyz");
      var r = Rd { text: text };
      var kinds: (const u8[:])[>..] = [];
      var res: u8[>..] = [];
      kinds.push(read(r));
      res.clear();   // error: cannot clear res while kinds is still used
      print(kinds.len, res);
  }
  ```
* **Definite assignment** does not recognize a fixed array that is filled
  element by element (`var a: f64[8]; for l in 8 { a[l] = 1.0; } return a;`).
* **No `bool` to integer cast**, and `x as! i64 & mask` parses `i64 &` as a
  reference type, with an error that does not say so.
* **regex** has no multi-literal search, and its prefilter's byte
  frequencies suit English text only.
* **dictionary** grows at 2/3 load with 16-byte slots, which slows down
  miss-heavy workloads (lru/1 122 against lru/2 54 ms); std's `hash(u64)`
  spreads small dense keys in a way that causes probe collisions
  (knucleotide/2 336 against 220 ms with a simpler hash).
* **os** has no streaming file read, so knucleotide and regex-redux hold the
  whole input file in memory.
