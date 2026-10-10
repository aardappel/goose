# LangArena in Goose

A Goose implementation of the 50 benchmarks of
[LangArena](https://github.com/kostya/LangArena), laid out as a language
directory of that repository, plus the tools to run it here against
LangArena's own Rust and C++ implementations.

## Layout

| here | in a LangArena checkout |
|---|---|
| `goose/` (`run`, `test`, `src/`) | `goose/` |
| `docker/goose` | `docker/goose` |

`goose/src/main.goose` is the runner: it reads a configuration (`run.js` or
`test.js`), runs the benchmarks it lists in order and prints what every other
language's runner prints. `bench.goose` holds the harness (configuration,
warm-up and timed loop), `helper.goose` LangArena's random number generator
and checksums, and each other file one benchmark group. JSON, regex, Base64
and CSV come from Goose's standard library, as the other languages take them
from theirs or from a package.

LangArena itself also needs these entries:

* `benchmarks.rb`, in `LANG_MASKS`:
  `'goose' => ['./goose', ['.goose'], ['target']],`
  and a run:
  ```ruby
  Run.new(
    name: "Goose",
    build_cmd: "sh -c 'mkdir -p target && goose -O2 --standalone -o target/benchmark.c src/main.goose && clang -O3 -w target/benchmark.c -o target/benchmark -lm -pthread'",
    binary_name: "./target/benchmark",
    run_cmd: "./target/benchmark",
    version_cmd: "sh -c 'cd /opt/goose && git describe --always'",
    dir: "/src/goose",
    container: "goose",
    group: :prod,
    deps_cmd: "true",
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
        version: master
    depends_on:
      - base
  ```

## Running here

`compare.py` builds and runs all three languages, reading Rust and C++ from a
LangArena checkout next to this repository's checkout (or `$LANGARENA`) and
writing everything under `bench/kostya/build/`. C++ is built the way
LangArena's `make prod` builds it, with clang; Rust with `cargo build
--release`; the generated Goose C with clang `-O3`. On Windows both C and
C++ get `-fstrict-aliasing`, which is clang's default on Linux, where
LangArena runs.

```
python bench/kostya/compare.py build                 # all three languages
python bench/kostya/compare.py test                  # test.js checksums
python bench/kostya/compare.py run --reps 3          # run.js timings and a ratio table
python bench/kostya/compare.py run --langs goose --only Sort,Json --reps 1
```

Runs are pinned to CPUs 16-31 (`--cpus`): on the Ryzen 9950X3D these were
measured on, that is the core complex without the large L3, the closer match
to LangArena's Ryzen 3800X; on the other one, cache-bound benchmarks run up
to 2.5x faster.

`ab.py` compares several builds of the Goose suite (for example two
compilers) interleaved, best of N, each build also as renamed copies:
Windows places an executable by its file name, and code placement alone
moves single benchmarks by 10-30%, so only interleaved same-session
comparisons over several builds are worth reading.
