**Goose runs at about 2.7x the speed of idiomatic C++ and about 1.15x the speed
of hand-optimised C++, and the backend barely matters.** The two toolchains
agree to within 4% on the idiomatic figure (2.79x under v145, 2.68x under
clang). The expert figure differs a little more (1.13x and 1.21x), but that
spread is one row: clang is poor at `vector::push_back`, which inflates `push`
from 1.18x to 1.95x. No conclusion in this suite changes with the compiler.

**The expert column is the interesting one.** A 2.7x win over `unique_ptr` and
`unordered_map` mostly measures the allocator, and any arena would collect part
of it. The claim worth making is the other one: the *idiomatic* Goose program,
written the way the spec's own examples are written, is ahead of the C++ a
programmer only reaches by hand-building an arena with `uint32` indices, or by
threading `string_view`s through their own open-addressed table. It wins nine
of the ten under each toolchain -- `graph` is the exception under v145 (0.98x)
and `particles` under clang (0.96x) -- and the two largest margins, `records`
at 1.38x and `interp` at 1.37x, are exactly the benchmarks whose data C++
cannot represent as compactly at any level of effort.

**Memory is the least ambiguous column: 2.2x smaller than idiomatic C++, 1.3x
smaller than expert.** Where the expert row has literally the same layout Goose
does -- `sum`, `push`, `tree`, `graph`, `particles` -- the two land within 1%,
which is the right answer and a good check on the measurement. The gap opens
exactly where Goose can express something C++ cannot: variable-mode enum
payloads (`records` 3.8x, `interp` 2.4x, `sexp` 2.0x) and inline variable-size
strings (`strlist` 3.2x against `vector<string>`).

**Three ratios need a caveat.** `graph`'s idiomatic column compares Goose's CSR
against C++'s `vector<vector>`, so it is a data-structure comparison as much as
a language one -- both languages want CSR, and there the two are level.
`push`'s expert figure is 1.18x under v145 and 1.95x under clang for backend
reasons, so read the v145 one. And `sum`, the control, sits at 1.08x on
identical memory, which is what a benchmark with no Goose advantage in it
should do.

**What is not in these numbers.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their
time generating data (`sexp`, `words`, `graph`) have their ratios pulled toward
1.0; the gap on the phase under test is larger than the table shows.
