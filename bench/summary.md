**Goose runs at about 2.75x the speed of idiomatic C++, about 1.18x the speed of
hand-optimised C++, and about 1.16x the speed of idiomatic Rust, on 1.34x less
memory than either of the last two.** The backend matters a little more than it
used to: the two C/C++ toolchains are 8% apart on the idiomatic figure (2.86x
under v145, 2.64x under clang) where they were within 5%, because this round's
codegen work helped v145 more than clang almost everywhere and hurt clang on
`particles`. No conclusion in this suite changes with the compiler.

**Rust lands exactly where expert C++ lands.** 1.18x and 1.15x against Rust,
1.17x and 1.19x against hand-built C++ -- the same number, reached from a very
different place. That is Rust's own claim holding up, and it is why it gets one
target here rather than two tiers: it reaches hand-optimised-C++ performance
from code a competent programmer writes without hand-building anything. Six of
the ten are inside 11% either way; Goose wins `records` by 2.2x, `interp` by
1.3-1.4x, and `push` and `strlist` by 11-13%. Rust is ahead on one benchmark
only, `particles`, by 2% against the v145 build and 5% against the clang one --
and that clang number is a regression this round rather than a standing result
(see `notes.md`).

**How Rust gets there is the result worth reporting.** In four of the ten --
`push`, `tree`, `interp` and `sexp` -- the Rust row that comes closest to Goose
is linked by integer indices where the Goose row is linked by typed references,
because safe Rust cannot hold a reference into a container it is still growing.
`push` isolates this exactly: it keeps a pointer to every 64th element of an
array it is still appending to. Goose writes that with real `Item&` references
that stay valid forever; in Rust it does not compile at all, and no std
container is both pointer-stable and O(1)-append. They are 11% apart on time
(150 vs 168 ms) and identical on memory (500 vs 501 MB). What differs is that
the Goose links are typed, nullable and checked, and the Rust ones are integers
that name no type, are not tied to the container they index, and go quietly
stale if that container is reused. Where Rust does keep real references -- `Box`
nodes in `tree`, `interp` and `sexp` -- it is 2.8x to 5.9x slower than its own
arena row. So Goose is not merely level with Rust on these: it is ahead of the
shape Rust falls back to when the borrow checker says no, while keeping the
typed one.

`graph` is the honest exception: CSR wins there in every language and CSR is
index-based in every language, so nothing separates them at the winning row. The
difference only shows in the one-pass linked build, which Goose writes with real
`Edge&` references into a growing pool and Rust can only write with indices --
and which is 4.5-5.7x slower than CSR in both, so it is not the row anyone
should ship.

**Memory is the least ambiguous column: 1.34x smaller than the best Rust row,
and 2.2x smaller than idiomatic C++.** Nothing in this round's compiler work
touched layout, so these are the same numbers as last time, which is itself a
check on the measurement. Where Rust and Goose have literally the same layout --
`sum`, `push`, `tree`, `graph`, `particles` -- the two land within 0.2%. The gap
opens exactly where Goose can express something a fixed enum cannot:
variable-mode payloads (`records` 4.1x, `interp` 2.2x, `sexp` 1.9x) and inline
variable-size strings (`strlist` 2.2x against `Vec<String>`).

**Three places where Rust is ahead of C++, which narrow the comparison.**
`collect()` over a `TrustedLen` iterator preallocates exactly, so the `sum`
control's "did you remember to `reserve`" win does not exist against Rust.
Borrowing is the default, so `Vec<&str>` and `HashMap<&str, _>` are the first
thing written rather than something a reviewer has to ask for. And Rust enums
beat `std::variant` on both time and memory -- outright in `interp`, and in
`records` against the `std::string` payload, which is the like-for-like row --
because the tag is a byte, `match` is a jump table, and niche optimisation hides
the tag inside a payload pointer. The idiomatic-versus-expert gulf that makes the
C++ comparison interesting is much narrower in Rust.

**The expert column is the interesting one for C++.** A 2.75x win over
`unique_ptr` and `unordered_map` mostly measures the allocator, and any arena
would collect part of it. The claim worth making is the other one: the
*idiomatic* Goose program, written the way the spec's own examples are written,
is ahead of the C++ a programmer only reaches by hand-building an arena with
`uint32` indices, or by threading `string_view`s through their own
open-addressed table. It wins all ten under v145 and nine of ten under clang
(`particles`, 0.91x), and the two largest margins -- `records` at 1.42x and
`interp` at 1.38x -- are exactly the benchmarks whose data C++ cannot represent
as compactly at any level of effort.

**Four ratios need a caveat.** `graph`'s idiomatic column compares Goose's CSR
against C++'s `vector<vector>`, so it is a data-structure comparison as much as
a language one -- all three languages want CSR, and there they are level -- and
it is also the noisiest benchmark here, its linked rows moving by up to 23%
between two full runs, in every language at once. `push`'s expert figure is 1.23x under
v145 and 2.03x under clang for backend reasons, so read the v145 one. `words`
compares against a hand-rolled table in both other languages because std's
default hasher is SipHash, which is a policy choice rather than a speed one. And
`sum`, the control, is no longer quite the dead heat it was: identical memory
still, but the data-stack-top work put Goose 5% ahead of Rust under v145 while
leaving the two level under clang.

**What is not in these numbers.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their time
generating data (`sexp`, `words`, `graph`) have their ratios pulled toward 1.0;
the gap on the phase under test is larger than the table shows.
