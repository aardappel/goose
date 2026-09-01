**Goose runs at about 2.7x the speed of idiomatic C++, about 1.15x the speed of
hand-optimised C++, and about 1.14x the speed of idiomatic Rust, on 1.34x less
memory than either of the last two.** The backend barely matters: the two C/C++
toolchains agree to within 5% on the idiomatic figure (2.76x under v145, 2.65x
under clang), and no conclusion in this suite changes with the compiler.

**Rust lands exactly where expert C++ lands.** 1.13x and 1.14x against Rust,
1.13x and 1.18x against hand-built C++ -- the same number, reached from a very
different place. That is Rust's own claim holding up, and it is why it gets one
target here rather than two tiers: it reaches hand-optimised-C++ performance
from code a competent programmer writes without hand-building anything. Eight of
the ten are inside 11% either way; Goose wins the other two, `records` by 2.2x
and `interp` by 1.3x. Rust is ever ahead on only two rows, and on each only
against one of the two Goose builds: 2% ahead of the v145 build on `particles`,
6% ahead of the clang build on `sum`.

**How Rust gets there is the result worth reporting.** In four of the ten --
`push`, `tree`, `interp` and `sexp` -- the Rust row that ties Goose is linked by
integer indices where the Goose row it ties is linked by typed references,
because safe Rust cannot hold a reference into a container it is still growing.
`push` isolates this exactly: it keeps a pointer to every 64th element of an
array it is still appending to. Goose writes that with real `Item&` references
that stay valid forever; in Rust it does not compile at all, and no std
container is both pointer-stable and O(1)-append. The two tie on time (161 vs
162 ms) and on memory (500 vs 501 MB). What differs is that the Goose links are
typed, nullable and checked, and the Rust ones are integers that name no type,
are not tied to the container they index, and go quietly stale if that container
is reused. Where Rust does keep real references -- `Box` nodes in `tree`,
`interp` and `sexp` -- it is 2.8x to 6x slower than its own arena row. So Goose
is not merely level with Rust on these: it is level with the shape Rust falls
back to when the borrow checker says no, while keeping the typed one.

`graph` is the honest exception: CSR wins there in every language and CSR is
index-based in every language, so nothing separates them at the winning row. The
difference only shows in the one-pass linked build, which Goose writes with real
`Edge&` references into a growing pool and Rust can only write with indices --
and which is 5-6x slower than CSR in both, so it is not the row anyone should
ship.

**Memory is the least ambiguous column: 1.34x smaller than the best Rust row,
and 2.2x smaller than idiomatic C++.** Where Rust and Goose have literally the
same layout -- `sum`, `push`, `tree`, `graph`, `particles` -- the two land within
0.2%, which is the right answer and a good check on the measurement. The gap
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

**The expert column is the interesting one for C++.** A 2.7x win over
`unique_ptr` and `unordered_map` mostly measures the allocator, and any arena
would collect part of it. The claim worth making is the other one: the
*idiomatic* Goose program, written the way the spec's own examples are written,
is ahead of the C++ a programmer only reaches by hand-building an arena with
`uint32` indices, or by threading `string_view`s through their own
open-addressed table. It wins all ten under v145 and nine of ten under clang
(`particles`, 0.97x), and the two largest margins -- `records` at 1.38x and
`interp` at 1.30x -- are exactly the benchmarks whose data C++ cannot represent
as compactly at any level of effort.

**Four ratios need a caveat.** `graph`'s idiomatic column compares Goose's CSR
against C++'s `vector<vector>`, so it is a data-structure comparison as much as
a language one -- all three languages want CSR, and there they are level.
`push`'s expert figure is 1.11x under v145 and 1.90x under clang for backend
reasons, so read the v145 one. `words` compares against a hand-rolled table in
both other languages because std's default hasher is SipHash, which is a policy
choice rather than a speed one. And `sum`, the control, is a dead heat on
identical memory, which is what a benchmark with no Goose advantage in it should
do.

**What is not in these numbers.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their time
generating data (`sexp`, `words`, `graph`) have their ratios pulled toward 1.0;
the gap on the phase under test is larger than the table shows.
