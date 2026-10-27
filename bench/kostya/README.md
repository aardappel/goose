# LangArena in Goose

A Goose implementation of the 50 benchmarks of
[LangArena](https://github.com/kostya/LangArena), laid out as a language
directory of that repository, plus the tools to run it here against
LangArena's own Rust and C++ implementations.

## Layout

| here | in a LangArena checkout |
|---|---|
| `goose/` (`Makefile`, `run`, `test`, `src/`) | `goose/` |
| `docker/goose` | `docker/goose` |

`goose/src/main.goose` is the runner: it reads a configuration (`run.js` or
`test.js`), runs the benchmarks it lists in order and prints the same output
as every other language's runner. `bench.goose` holds the harness: the
configuration (read with the standard library's `json`) and the run loop from
the other languages' Benchmark class. Goose has no classes or traits, so each
benchmark is a struct with `prepare`, `run` and `checksum` functions, which
`main.goose` passes to the generic `execute`. `helper.goose` holds
LangArena's random number generator and checksums, and each of the other
files holds one benchmark group. JSON, regex, Base64 and CSV come from
Goose's standard library, as the other languages get them from their own
standard library or from a package.

LangArena measures how well each compiler handles the same algorithm written
the same way, so every benchmark follows the reference implementations (Rust
and Crystal) struct by struct and function by function. Where Goose's memory
model requires a different structure (it has no heap, no growable arrays
inside arrays and no shared memory between threads), the comments say what
is used in place of the original.

LangArena itself also needs these entries:

* `benchmarks.rb`, in `LANG_MASKS`:
  `'goose' => ['./goose', ['.goose'], ['target']],`
  and a run:
  ```ruby
  Run.new(
    name: "Goose",
    build_cmd: "make prod",
    binary_name: "./target/benchmark",
    run_cmd: "./target/benchmark",
    version_cmd: "git -C /opt/goose log -1 --format='goose %h %cs'",
    dir: "/src/goose",
    container: "goose",
    group: :prod,
    deps_cmd: "mkdir -p target",
  ),
  ```
* `docker-compose.yaml`, a service like the other languages':
  ```yaml
  goose:
    <<: *shared
    image: langarena:goose
    working_dir: /src/goose
    build:
      dockerfile: docker/goose
      args:
        version: "<a goose commit>"
    volumes:
      - .:/src
      - ./cache/build/goose/target:/src/goose/target
    depends_on:
      - base_clang
  ```

## Running here

`compare.py` builds and runs all three languages, reading Rust and C++ from a
LangArena checkout next to this repository's checkout (or `$LANGARENA`) and
writing everything under `bench/kostya/build/`. C++ is built with clang, the
way LangArena's `make prod` builds it, and Rust with `cargo build
--release`. The generated Goose C is compiled with clang `-O2`, as
LangArena's own C and C++ are. On Windows both C and C++ get
`-fstrict-aliasing`, which is clang's default on Linux, where LangArena
runs.

```
python bench/kostya/compare.py build                 # all three languages
python bench/kostya/compare.py test                  # test.js checksums
python bench/kostya/compare.py run --reps 3          # run.js timings and a ratio table
python bench/kostya/compare.py run --langs goose --only Sort,Json --reps 1
```

Runs are pinned to CPUs 16-31 (`--cpus`). On the Ryzen 9950X3D these results
were measured on, those CPUs are the core complex without the large L3,
which is the closer match to LangArena's Ryzen 3800X. On the other core
complex, cache-bound benchmarks run up to 2.5x faster.

`ab.py` compares several builds of the Goose suite (for example, from two
compilers). It interleaves their runs, keeps the best of N, and also runs
each build as renamed copies. Windows chooses the address an executable is
loaded at based on its file name, and code placement alone moves single
benchmarks by 10-30%, so only interleaved comparisons over several builds
in the same session are worth reading.
