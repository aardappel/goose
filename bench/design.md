The design of Goose asks the question: what if went all-out on more efficient inline data
structure options, and more efficient memory management at the language level, is it possible
to surpass the performance of current performance leading languages like C++ and Rust,
while being "safe"?

To answer that question, we must run some benchmarks that explore the kinds of code
Goose seeks to improves, which means that Goose winning some of these benchmarks
is not surprising. While that makes for good marketing material, we also want to
find the limits of Goose, and find any limitations to improve the language.

We want to however find algorithms to test that are familiar and frequently occurring,
not anything contrived.

What are some advantages of Goose we're seeking to test the effects of?
* Super compact inline data structures.
* Super cheap in-place construction everywhere and pointer bump push back / append.
* Less copying overhead.
* No "realloc" of resizable data structures, while allowing element pointers.
* Variable size enums.
* No allocator at all: no malloc/free call, no per-allocation header, no size class
  rounding, no free list to walk, no fragmentation, and no lock or thread cache in the
  path. Deallocation is a pointer reset. `reusable` pools (spec 5.4) cover the
  mutation-heavy cases that would otherwise force an allocator back in.
* No destructors and no drop glue: releasing a large nested structure is O(1), not an
  O(n) pointer chasing traversal. C++/Rust pay for teardown of every unique_ptr/Box/
  String they built, and it is the same cache hostile walk as building it was.
* Relative references: 1-2 byte links where C++ and Rust spend 8. Note the idiomatic
  Rust workaround for pointer heavy graphs is already an index based arena
  (`Vec<Node>` plus u32 indices), so that, not `Box`, is the Rust comparison point,
  and it still costs 4 bytes plus a base+scale computation per hop.
* Compact scalars inside aggregates generally: packed structs (no padding), u8/u16/
  varint array length fields, u8 enum tags, varint payload fields. Rust and C++ round
  enums up to max-variant-plus-alignment and cannot express any of the rest without
  hand rolled bit twiddling.
* Nested containers stay flat and contiguous: `u8[][>..]` is one block, where
  `Vec<String>`/`vector<string>` is 1 + N allocations each carrying a 24 byte header
  (mitigated only for short strings, by SSO, in C++). Worth testing both short
  (SSO sized) and long strings for exactly this reason.
* Guaranteed construct-in-destination (4.3, 7.3): no construct-then-copy anywhere,
  guaranteed NRVO, callees writing their result directly into the caller's container
  (`v.push(f())`). C++ has non-guaranteed NRVO and an `emplace_back` that cannot do
  this for variable size values; Rust memcpy-moves large values around freely, which
  is a well known weakness of it.
* Tag dispatch instead of vtables (8.2): no vtable pointer per object (8 bytes on
  every polymorphic node), a jump table instead of an indirect call, and the arms stay
  inlinable.
* Exact size growth: a grown-to-n array occupies n elements, where Vec/vector carry up
  to 2x capacity slack on top of the allocator's own rounding.
* Goose data is already its own serialized form: a relative-reference structure loads
  by memcpy or mmap with no parse and no pointer fixup pass. Load time is a real and
  frequently occurring cost that this reduces to I/O.
* Cheap long distance errors (`return from`, 7.9): a discriminant check per frame,
  versus C++ exceptions (catastrophic on the throwing path) and Rust `?` chains that
  also copy the error payload up each frame.
* Thread queues (11.2) move flat contiguous values: no per-element pointer chasing, no
  serialization step, no shared ownership machinery.

We will want to test against performance conscious (but still idiomatic) C++ and Rust.
C++ for example can do all of the above if you're willing to go down to manual
pointer management and data packing, but noone uses the language like this outside of
highly specialized protocols. Goose makes it convenient and safe.

Besides speed, we will want to test memory usage, and the combination of both, by
running with at least 3 different data sizes for each algorithm, where the smallest
one focuses on pure CPU, but the 2 bigger sizes will stress cache and memory
subsystems increasingly, to see if the Goose advantage appears or widens.

We will want to test all reasonably idiomatic forms per language, e.g. for an
algorithm that works on variant/polymorphic data, for Goose we'll want to see
both fixed and variable enums if possible, for Rust just fixed enums, and for
C++ we'll want to compare subclasses (most idiomatic but slow), std::variant
(faster but unergonomic) and C-style unions (fastest, but very unsafe).

If an algorithm does a lot of resizable array pushing, we'll want to be
fair to C++ and compare both what happens if the programmer optimally
chooses `reserve`, and what if they don't bother to use it.

We'll also want to find examples of code that Goose is currently weak at,
and some that are a wash (because all languages go thru the same compiler
backend).

## Where the gains should compound

Most of the advantages above are the same advantage seen from different angles: they
shrink the bytes touched per unit of work. An algorithm's benefit should therefore be
roughly proportional to how pointer rich and allocation rich its data is, and should
widen with data size as the working set falls out of each cache level in turn. The
biggest expected wins, in rough order:

1. Anything that builds many small linked things: parse trees, ASTs, s-expressions,
   scene graphs. Allocation cost, node size (relative refs, varints, u8 tags, no
   vtable pointer) and teardown cost all stack up in the same benchmark.
2. Anything holding a collection of variable size things: lists of strings, lists of
   records, jagged arrays. One block versus 1+N allocations.
3. Anything that grows while holding pointers into what it grew. In C++/Rust this
   forces the programmer into indices, a chunked arena (deque-like, extra
   indirection), or reserving a hard upper bound. All three are worth benchmarking as
   separate rows, because it is exactly the workaround cost we are trying to delete.
4. Load/save and cross thread paths, where Goose does no work at all and the others
   serialize or pointer fixup.

If a benchmark's data is a flat array of scalars, expect no advantage. That is the
wash case, and useful precisely as the control.

## Where we should expect Goose to lose

Worth benchmarking deliberately, so the limits are ours to report rather than someone
else's to discover:

* Arithmetic runs at the operands' own width (6.2), so an i32 kernel is genuinely
  32-bit — but Goose's packed, unaligned layouts may still cost SIMD performance
  against aligned C++ data. A straightforward integer array kernel should quantify
  what remains.
* Packed, unaligned layouts (3.2) may cost real SIMD performance for the same reason:
  no padding means no alignment guarantee. Also TODO 10. A float/vector math benchmark
  (particles, n-body, image kernel) is the test, and it doubles as the exercise for
  elementwise struct math (6.1).
* No aliasing information. Rust hands LLVM `noalias` on `&mut`, C++ has `restrict`
  when someone bothers, and Goose deliberately permits aliasing and so tells the
  backend nothing. Any in-place transform over two array arguments is where this shows.
* Bounds checks on everything non-fixed, like Rust, unlike C++. Where the optimizer
  fails to elide them, C++ wins. Since the spec allows disabling them wholesale we can
  report both, but the checked number is the headline: that is the honest safe vs safe
  comparison against Rust, with C++ as the unsafe baseline.
* Copies are real. By-value semantics with no move operation (4.1, TODO 3) means idioms
  a Rust programmer would express as a cheap move are an O(size) copy in Goose. A
  benchmark that returns different locals on different paths (7.3), or reassigns large
  values around, will show it. This is the most likely place to find something worth
  fixing in the language.
* Stack discipline friction. Workloads whose lifetimes are genuinely not nested (caches,
  long lived mutable graphs, anything wanting free-then-alloc at differing sizes) must
  go through `reusable` pools, and recursive cycles cannot own growable locals at all
  (7.8). Worth one benchmark that is honestly awkward in Goose, to see what the
  workaround costs.
* Address space accounting: report committed pages, not the multi-GB reservations
  (10.4), or the memory numbers are nonsense. Conversely, Goose commits at page
  granularity per stack, so at the smallest data sizes it may legitimately look worse
  than malloc.

## Candidate algorithms

Familiar, widely written code, each chosen to hit specific axes above:

* Word frequency count over a text corpus: flat list of strings, hash map growth,
  string keys. Hits axes 2 and 3, and the map is spec A.3.
* JSON or s-expression parse, then walk/query: axis 1 in full, plus variable mode
  enums, relative references (A.2), varint fields, and `return from` on the error path.
  Probably the flagship benchmark.
* Expression or bytecode interpreter: case function dispatch vs virtual calls vs
  std::variant visit vs Rust match. Isolates dispatch and node size from allocation.
* Graph build plus BFS/Dijkstra: adjacency built incrementally (axis 3), then traversed.
  Compare Goose's nested resizables against hand built CSR in C++/Rust, which is the
  performance conscious thing to do there and is genuinely more work to write.
* Particle/n-body or image kernel: the SIMD and packed-layout questions. Expect a wash
  or a loss; that is the point of including it.
* Save/load round trip of a built structure: mmap and use, vs parse and fixup.
* Sort of a flat scalar array: the control. Should be a dead heat.
* Binary trees (the well known benchmark game one) is worth including for
  recognizability, with the caveat stated in the writeup that it is really an allocator
  benchmark, which is why we don't lean on it.

## Measurement notes

* Goose compiles to C, so build the C++ and the generated Goose C through the same
  clang version and optimization level, and note that Rust goes through LLVM too. The
  intent is to compare data structure and memory strategy, not backends.
* Measure teardown inside the timed region. Freeing the structure is a real cost that
  Goose does not pay, and excluding it silently hands the others a free win.
* Give C++/Rust a fair allocator: system malloc is the default idiom, but a mimalloc or
  jemalloc row is the strong baseline, and a hand rolled bump/arena row is the "an
  expert wrote this" tier. Same tiering idea as the `reserve` fairness point above.
* For memory, report peak RSS and the payload size the program thinks it has, so
  allocator overhead, capacity slack and per-object headers show up as the gap between
  the two. That gap is a headline result in its own right.
* Pin to a core, fix the clock where possible, run each size cold and warm, and report
  a distribution rather than a best of N.

## The suite as built

`bench/goose/` and `bench/cpp/` hold ten benchmarks against this design, from
`sum` (the control) to `sexp` (the flagship). `bench/run_bench.ps1` builds and
runs them at three sizes each, checks that every implementation of a benchmark
prints the same checksum, and writes `bench/results.md`; the commentary that
file ends with lives in `bench/notes.md`. Rust and clang rows are picked up
automatically once those toolchains and `bench/rust/` are present.
