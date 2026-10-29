# Coroutines: stackless, value-typed, scheduled by libraries

<details>
<summary>Table of contents</summary>

- [0. Summary](#0-summary)
- [1. What the feature is for](#1-what-the-feature-is-for)
  - [1.1 The server workload](#11-the-server-workload)
  - [1.2 The constraints](#12-the-constraints)
- [2. What Goose already decides](#2-what-goose-already-decides)
  - [2.1 A suspended function can keep neither of its stacks](#21-a-suspended-function-can-keep-neither-of-its-stacks)
  - [2.2 The recursion rule is the coroutine rule](#22-the-recursion-rule-is-the-coroutine-rule)
  - [2.3 Stackful coroutines are out](#23-stackful-coroutines-are-out)
  - [2.4 Function values are inlined, so HOFs cost nothing to suspend through](#24-function-values-are-inlined-so-hofs-cost-nothing-to-suspend-through)
  - [2.5 Threads share nothing, so thread-per-core is the shape](#25-threads-share-nothing-so-thread-per-core-is-the-shape)
  - [2.6 Pools own what a coroutine cannot](#26-pools-own-what-a-coroutine-cannot)
  - [2.7 No function pointers, so the scheduler protocol is data](#27-no-function-pointers-so-the-scheduler-protocol-is-data)
- [3. What other languages did](#3-what-other-languages-did)
  - [3.1 The taxonomy](#31-the-taxonomy)
  - [3.2 Rust](#32-rust)
  - [3.3 C++](#33-c)
  - [3.4 Kotlin](#34-kotlin)
  - [3.5 Go](#35-go)
  - [3.6 Zig, twice](#36-zig-twice)
  - [3.7 C#, Swift, Python, Lua, Java, OCaml](#37-c-swift-python-lua-java-ocaml)
  - [3.8 Coroutines in C without a compiler](#38-coroutines-in-c-without-a-compiler)
  - [3.9 The fastest servers](#39-the-fastest-servers)
  - [3.10 What to take](#310-what-to-take)
- [4. The design](#4-the-design)
  - [4.1 Declaration: a function that is also a type](#41-declaration-a-function-that-is-also-a-type)
  - [4.2 Starting: a literal, and nothing runs](#42-starting-a-literal-and-nothing-runs)
  - [4.3 `yield` and `resume`](#43-yield-and-resume)
  - [4.4 Calling a coroutine from a coroutine](#44-calling-a-coroutine-from-a-coroutine)
  - [4.5 Rules at a suspension point](#45-rules-at-a-suspension-point)
  - [4.6 Blocks, HOFs and nested functions](#46-blocks-hofs-and-nested-functions)
  - [4.7 `return … from`, `defer` and the other exits](#47-return--from-defer-and-the-other-exits)
  - [4.8 Lifetimes: the frame is a holder](#48-lifetimes-the-frame-is-a-holder)
  - [4.9 Copying, flatness and threads](#49-copying-flatness-and-threads)
  - [4.10 Finishing, re-entry, abandonment](#410-finishing-re-entry-abandonment)
  - [4.11 Generics and checking order](#411-generics-and-checking-order)
  - [4.12 Lowering to C, and what it costs](#412-lowering-to-c-and-what-it-costs)
  - [4.13 What stays out of the language](#413-what-stays-out-of-the-language)
- [5. A server on top](#5-a-server-on-top)
- [6. Options, with pros and cons](#6-options-with-pros-and-cons)
- [7. The community `suspend_fn` sketch](#7-the-community-suspend_fn-sketch)
- [8. Steps](#8-steps)
- [9. Open questions](#9-open-questions)
- [References](#references)

</details>

Research note, October 2026. Nothing here is implemented, and every piece
of syntax is a placeholder. The question was whether Goose should have a
coroutine or async feature, what the smallest useful form of one is that
fits the language's stack model, and what the languages that already have
one learned. The answer proposed here is a *stackless* coroutine whose
frame is an ordinary fixed-size Goose value: started by a struct literal,
driven by one builtin, and scheduled by whatever library the program
chooses. Section 4 is the design, section 6 the alternatives considered
for each part, section 8 the order to build it in.

## 0. Summary

* **Stackless, by necessity.** A Goose function's locals live on the native
  stack or on data stacks, and a suspended function can keep neither (2.1).
  Stackful fibers would need a block of multi-gigabyte data-stack
  reservations per fiber (2.3). So a coroutine is compiled into a state
  machine, as Rust, C++, C# and Kotlin compile theirs, and its state is a
  fixed-size value.
* **The rule already exists.** §7.8 forbids a recursive function from
  calling back into its cycle while a nonfixed local is in scope. The same
  rule, with the suspension point in the role of the recursive call, is
  what makes a suspended coroutine hold no data stack (2.2). Growable data
  is owned outside, in pools, as it already is for recursion and for
  threads (2.6).
* **A coroutine is a function that is also a type.** `coro fn serve(c:
  Conn&) …` declares a function whose body may `yield`, and a fixed-size
  struct type `serve` that holds its parameters, the locals that live
  across a yield, and where it is suspended (4.1). `serve { c }` is a
  frame that has not run yet (4.2). Calling `serve(c)` from another
  coroutine runs it to completion inside the caller's state machine (4.4).
* **One builtin drives it.** `resume(f, x)` runs a frame to its next
  `yield` or to its end, and returns the yielded value and whether it
  yielded; `result(f)` reads a finished frame's result (4.3). Yield and
  resume values are flat data, not callbacks: a coroutine *tells* its
  resumer what it waits for, and the resumer, not a waker, decides what to
  do about it (2.7).
* **Everything else is a library**: event loops, timers, channels,
  cancellation, structured concurrency, `select`, `run_blocking`,
  generators-as-iterators (4.13, 5). The language supplies the state
  machine and a value to put it in; nothing in it allocates, locks, or
  calls through a pointer.
* **Zero cost otherwise.** No runtime state, no thread-local, no change to
  how an ordinary function is compiled or called. A program without a
  `coro fn` compiles to the same C it does today.
* **Thread-per-core is the server architecture**, and Goose already has it:
  workers share nothing and own their globals, so a reactor and its pools
  written as globals are per-worker by construction (2.5). Go-style
  migration of tasks between threads is neither possible nor needed; a
  flat frame can still be sent through a queue (4.9).

## 1. What the feature is for

### 1.1 The server workload

The feature is expected to be used first for server infrastructure, and
that workload fixes what matters:

* **Many more waits than threads.** A server holds 10⁴ to 10⁶ connections,
  each mostly waiting: for the next request bytes, for a backend, for a
  timer, for the socket to accept more output. Each connection has a small
  amount of state (where it is in the protocol, what it has parsed so far,
  which buffers are its) and that state must survive while it waits.
* **The kernel tells you what is ready.** `epoll`, `kqueue`, IOCP and
  `io_uring` hand the program a batch of events; the program finds the
  connection each event belongs to and runs it until it has to wait again.
  Every modern fast server, in any language, has this loop at its center.
  A coroutine feature is useful exactly to the extent that it lets the
  per-connection logic be written as straight-line code while the loop
  stays the loop.
* **Per-event cost is the whole game.** The work between two waits is
  often a few hundred instructions (parse a header, copy a response). A
  feature that adds an allocation, a lock, an atomic, or an indirect call
  per wait is measurable. One that adds a switch and a few loads is not.
* **Memory per connection decides scale.** A frame of tens of bytes beside
  a pooled buffer lets a million connections fit; a stack per connection
  does not.
* **Cores are used by running one loop per core**, with the kernel
  spreading connections across them (`SO_REUSEPORT`, NIC queues), not by
  moving connections between cores. This is the Seastar, glommio and
  monoio design (3.9), and it is the one that benchmarks fastest.

The same mechanism serves game and UI logic that runs across frames (the
patrol example of section 7), and generators.

### 1.2 The constraints

The brief for this note, restated as tests that the design has to pass:

1. **Useful**: writing the state machine by hand must be clearly worse. A
   hand-written machine (`samples/31_mini_doom.goose`'s `Actor` keeps
   `timer`, `pain`, `death` and `attacking` for this) spreads one
   sequential procedure over an enum and a `match`; the compiler can keep
   the procedure and build the enum.
2. **Thin**: the fewest new rules, each one derivable from a rule the
   language has. Nothing opaque: a reader of the generated C should
   recognize the struct and the switch.
3. **Cheapest representation**: a frame is parameters plus the locals that
   cross a yield plus a tag, no more, with nothing per resume beyond a call
   and a switch.
4. **Zero cost to code that does not use it.**
5. **Libraries build the rest.** Scheduling, I/O integration, timers,
   cancellation policy and structured concurrency are all choices that a
   server, a game and an embedded program make differently; the language
   fixes none of them.
6. **It uses what already makes Goose fast**: pools, relative references,
   flat values through queues, share-nothing workers, in-place
   construction, whole-program specialization.

## 2. What Goose already decides

Most of the design follows from the spec; this section collects the rules
that force it, with the sections they come from.

### 2.1 A suspended function can keep neither of its stacks

A Goose activation keeps its fixed-size locals on the native stack or in
registers, and its nonfixed locals on data stacks, each on the stack index
the compiler assigned it (§1.2, §10.3). A coroutine suspends in the middle
of its body and returns control to whoever resumed it; that resumer goes on
to run other code, and later resumes the coroutine again, possibly from a
different place in a different function.

* The native stack is gone: the resumer's callees reuse it. So every local
  that is live across a suspension must be stored somewhere else, and
  reloaded on resume. Every stackless coroutine design (Rust, C++, C#,
  Kotlin, Python generators) does exactly this.
* A data stack cannot be held either. Stack indices are assigned statically
  per specialization; if a suspended coroutine held index *k* for a
  `u8[>..]` local, a second instance of the same coroutine, resumed by the
  same loop, would need index *k* as well, and so would the resumer's own
  callees. The invariant that lifetimes on one stack are strictly nested
  (§1.3) is exactly what a suspended frame would break.

The only thing a suspended coroutine can own is therefore **a fixed-size
value**: the saved locals. This is the same conclusion the spec draws for
recursion, and the same one threads reach from the other direction (a
`thread_fn` gets its own block of stacks because it is a separate program,
§11.2).

### 2.2 The recursion rule is the coroutine rule

§7.8: "no function in the cycle may call into the cycle while one of its
nonfixed locals is in scope", with the temporaries rule (a) to (d) for the
unnamed nonfixed values of the calling statement. The reason is that each
activation would otherwise need a data stack of its own. Replace "call into
the cycle" with "suspension point" and the reason holds word for word: each
*suspended frame* would otherwise need a data stack of its own.

The checker already implements this rule (`CycleCallTemps`,
`CheckCycleInit`, `Node::nftemp`, implementation.md §3.11), and codegen
asserts it (`NoStackAcrossCycleCall`). A coroutine needs the same checks at
a `yield`, and the same answers for the programmer: declare the scratch
buffer in a block that ends before the yield, or own the growable data
outside and pass it in by reference or as a pool index. §7.8's advice for
cycles ("Growable data that must outlive a recursive call is owned outside
the cycle and passed in: references, slices, reusable pools, or one struct
of references to several tables, A.6") is the advice for coroutines.

### 2.3 Stackful coroutines are out

A *stackful* coroutine (a fiber, a goroutine, a Lua coroutine, Boost
Context) keeps a native stack of its own and switches to it; suspension is
a register save and a stack-pointer swap, and the body needs no transform.
It is the more convenient model: anything can suspend, at any call depth,
with no coloring. For Goose it is ruled out, not just unattractive:

* **A fiber would need data stacks of its own.** A Goose activation's
  nonfixed locals live on data stacks, so a fiber that may own a `u8[>..]`
  anywhere in its call tree needs a block of N′ stacks, as a thread program
  does (§11.2). Each stack is a reservation of `GS_STACK_RESERVE` bytes
  (2 GB by default) plus a guard gap, and N′ is the program's count
  (implementation.md §7). The address-space budget (32 TB, §10.4) that
  already caps `hardware_threads()` would cap fibers at a few thousand at
  best, and at tens for a program with many stacks. A server wants a
  hundred thousand.
* **Stacks cannot move.** Go keeps goroutine stacks small by growing them
  through copying, which needs a precise map of every pointer into the
  stack to rewrite ([Go's contiguous stacks design][go-stacks]). Goose
  references are raw addresses into data stacks whose growth "never moves
  memory" (§1.2); nothing in the language or runtime can relocate a value.
* **Switching native stacks needs assembly** per platform and ABI, which
  the in-process TinyCC backend supports only on some targets and wasm
  cannot do at all (the [wasm stack-switching proposal][wasm-switch] is
  still a proposal).
  Stackless coroutines compile to plain C and run everywhere Goose runs.

Rust reached the same place: its 2014 runtime had green threads and
[RFC 230][rfc230] removed them, for the cost of a stack per task and the
mismatch with native code; `async`/`await` came five years later as a
stackless design. The convenient model stays reachable *within* a bounded
budget, as an opt-in "task with its own stacks" (§11.3's dynamic stacks),
which is listed as future work in section 9, not as the feature.

### 2.4 Function values are inlined, so HOFs cost nothing to suspend through

A function value is a compile-time entity, spliced inline into the function
it is passed to, which is specialized per function value (§7.6,
implementation.md §6.6). So a `fn each<F>(xs: i64[:]) { for x in xs {
F(x) } }` called with a block that yields is, in that specialization, code
that yields: its locals (here, the loop index) cross the yield like the
coroutine's own, and nothing about `each` needs to be declared for it.
Kotlin needs `inline fun` to get this; Goose has it already, and it is what
keeps the standard library's HOFs usable from coroutines without a second,
colored copy (3.4, 3.10).

### 2.5 Threads share nothing, so thread-per-core is the shape

Goose workers never share mutable memory, each thread program is an
instance with its own globals, and only flat values cross through typed
queues (§11.2). This decides how coroutines and threads combine:

* A coroutine runs on the thread that resumes it and shares that thread's
  memory freely, since only one thing runs on a thread at a time and
  switches happen only at `yield`. No data race is possible, and no rule
  about what a coroutine may touch is needed beyond the ones every function
  obeys.
* A per-thread event loop, written in Goose with its pools of connections
  and frames as globals, is per-worker automatically: a worker's globals
  are its own copies. A server spawns one worker per core, each running the
  same loop on its own listening socket. That *is* the thread-per-core,
  share-nothing architecture (3.9); Goose gets it from §11.2 rather than
  from a runtime.
* Go's model, in which a scheduler moves goroutines between OS threads, is
  not available (a frame holding references cannot cross) and is not
  wanted (moving a connection's frame away from the thread whose pools hold
  its buffers would be a copy of everything). What *is* available is
  explicit distribution: a frame whose fields are all flat is a flat value
  and can be `qput` to another worker, which resumes it there (4.9).
  Blocking work (file I/O, DNS, a CPU-heavy job) goes to workers through
  the queues, and the coroutine waits for the reply as it waits for a
  socket.

So the answer to "do Goose's threads make one way of combining the two a
shoe-in" is yes: one loop per worker, frames and buffers in that worker's
pools, queues for anything that crosses. The language should enable it and
fix nothing more; the loop is library code (section 5).

### 2.6 Pools own what a coroutine cannot

A coroutine cannot own a `u8[>..]` across a yield (2.1), and a server has
a buffer per connection. In Goose this is already answered for every other
"one growable thing per item" problem: `reusable` pools of fixed-size
elements, `reusable[]` slice pools, and relative references (§5.4, §3.9;
`design/owning_dynamic_data.md`). A connection is an element of a pool, its
receive buffer a `u8[..4096]` limited array inside that element or a run in
a slice pool, and its frame holds a 4-byte `in pool` link to it. The frame
stays flat and small, the buffer stays where the kernel can write into it
while the coroutine is suspended (which is what completion-based I/O such
as `io_uring` and IOCP needs, and what Rust's borrowed-buffer futures make
hard, 3.2), and freeing a connection is `free` on two pools.

### 2.7 No function pointers, so the scheduler protocol is data

Rust's `Future::poll` receives a `Waker`, a (data pointer, vtable) pair that
the leaf future stores and calls when the task should be polled again. C++
hands `await_suspend` a `coroutine_handle`, resumed through a function
pointer in the frame. Kotlin's `Continuation` has a virtual `resumeWith`.
Goose has no runtime function values (§7.6), so a coroutine cannot hand
anyone a callback. It does not need to: the entity that resumed it is the
one that will resume it again, and it already knows which frame it holds.
What the coroutine must communicate is *what it is waiting for*, and that
is a value: `yield Wait.Readable { fd: fd }`. The loop that resumed it
stores the frame's index against that fd and resumes the frame when the fd
is ready, passing back what happened as the resume value. This is cheaper
than a waker (no clone, no refcount, no indirect call), it is flat, and it
is why the yield carries a typed value in this design (4.3).

## 3. What other languages did

### 3.1 The taxonomy

De Moura and Ierusalimschy's ["Revisiting Coroutines"][revisiting] (TOPLAS
2009) is the standard reference. A coroutine design chooses on three axes:
*stackful* or *stackless* (can it suspend from inside a nested ordinary
call); *symmetric* (any coroutine transfers to any other) or *asymmetric*
(a coroutine yields to whatever resumed it); and whether coroutines are
*first-class* values. The paper shows that asymmetric stackful first-class
coroutines have the expressive power of one-shot continuations, which is
what Lua ships. For Goose, 2.3 fixes stackless; 2.7 and simplicity fix
asymmetric; and the frame being an ordinary value gives first-class without
any further mechanism.

### 3.2 Rust

[`async`/`await`][rfc2394] (stable since 1.39, 2019) compiles an `async fn`
into an anonymous state machine implementing [`Future`][rfc2592], polled
with a `Context` carrying a `Waker`. The parts that are universally judged
right and that this design keeps:

* **Lazy start, inline nesting, static size.** Nothing runs until the first
  poll; an awaited inner future is stored *inside* the outer future's
  state, so a task is one allocation (or none: [embassy][embassy] runs
  tasks from statically allocated pools on microcontrollers). The compiler
  overlaps locals that are never live together, which fixed an exponential
  size growth ([Mandry, "Optimizing await"][mandry], rust-lang/rust#59897
  and its follow-ups). The frame layout of 4.12 is this layout.
* **Leaving the executor to libraries** kept the language small, and
  thread-per-core runtimes (glommio, monoio) could be built beside
  work-stealing ones (tokio).

What caused the famous disagreements, and what this design does about each:

* **`Pin`.** A future may borrow its own locals across an `.await`, so once
  polled it must not move; [RFC 2349][rfc2349] added `Pin` to express
  that, and it is the single most-cited pain ([boats, "Pin"][boats-pin];
  [fasterthanlime, "Pin and suffering"][pin-suffering]). The 2025 project
  goals are still working on pin ergonomics ([2025H1 async goal][rust-goals];
  [July 2025 update][rust-july]). Goose avoids the problem by rule, not
  machinery: no reference into a coroutine's own locals may be live across
  a yield (4.5, R4). The frame is then an ordinary value, copyable and
  movable, and the thing that moved into Rust's type system never arises.
  The price is that `let s = buf[..n]; yield; use(s)` over a *local* `buf`
  is an error; the buffer is owned outside, which the server shape wants
  anyway (2.6).
* **The waker is dynamic dispatch**, and its ownership rules
  (`Send + 'static` tasks for work-stealing executors) drove the
  thread-per-core debate ([boats, "Thread-per-core"][boats-tpc];
  [glommio][glommio]). Goose's protocol is a value (2.7), and threads
  already share nothing.
* **Cancellation is dropping the future**, with no async `Drop`
  (experiments only, [2025 goals][rust-goals]); a future dropped between
  polls loses its partial progress, hence tokio's documented
  ["cancellation safety"][tokio-cancel] hazards around `select!`. Goose
  has no destructors and a frame owns no growable data, so dropping a
  frame loses nothing but the frame; the resources it names are in pools
  the loop frees. Cooperative cancellation is a resume value (4.10).
* **Two ecosystems and coloring.** [Nystrom's "What Color is Your
  Function?"][colors] names the split; sync and async libraries duplicate
  each other; async in traits took until 1.75 ([AFIT][afit], 2023) and
  `dyn` async traits are still open ([2025 goals][rust-goals]); [boats,
  "Let futures be futures"][boats-futures] and the [async vision
  document][async-vision] record the runtime fragmentation. Goose has
  coloring too (only a `coro fn` yields), but less of it bites: HOFs are
  suspend-transparent (2.4), no traits exist to color, and any ordinary
  function can drive a frame with `resume` (4.3). The library advice that
  came out of Python, [sans-I/O][sansio] (protocol code that works on
  bytes and never does I/O), is the right default for Goose libraries:
  only the waiting is colored.
* **Readiness versus completion.** Rust futures borrow their buffers, so
  `io_uring` and IOCP, which need the kernel to own the buffer while the
  operation is in flight, required new APIs (tokio-uring, monoio). Goose
  buffers live in pools, so a yield can hand the kernel a slice that stays
  valid while the frame is suspended (5).
* Later additions worth knowing: async closures (stable in 1.85, 2025);
  return-type notation and `#[coroutine]` generators with resume
  arguments, both still unstable. The generators are the closest thing in
  Rust to 4.3's typed yield.

### 3.3 C++

C++20 coroutines ([cppreference][cpp-coro]) are a deliberately low-level
building block: a function containing `co_await`, `co_yield` or `co_return`
is transformed, and everything about it (the frame's allocation, when it
first suspends, what `co_await` does, what the caller gets back) is
customized through a `promise_type` and *awaiter* objects. The lesson is
mostly negative, as the brief anticipated:

* **Heap frames.** The frame is allocated with `operator new` unless the
  compiler can prove the lifetime and elide it ([HALO, P0981][halo]),
  which it can only do across inlining and never for a frame handed to an
  executor. `coroutine_handle::resume()` is an indirect call. Both are what
  this design refuses: the frame is a value whose size and type are known
  (4.1), and `resume` is a direct call to a known function.
* **Protocol soup.** Three awaiter methods, six promise customization
  points, symmetric transfer as a special return type ([Baker, "Understanding
  Symmetric Transfer"][baker-sym]; ["Understanding operator
  co_await"][baker-await]). The standard shipped no task type until
  C++26; `std::generator` came in C++23 ([P2502][p2502]), and
  `std::execution::task` for C++26 was approved at Sofia in 2025 over
  objections ([P3552R3][p3552]; [P3801][p3801]). The positive lesson is the
  one the brief named: keep the mechanism low-level, but keep the
  *protocol* to one `yield`, one `resume`, and one value each way.
* **Senders/receivers** ([P2300, std::execution][p2300]) are a lazy,
  allocation-free composition model that fits a language like Goose better
  than tokio-style futures do: work is described, connected to a receiver,
  and started, with no heap and no type erasure. It is prior art for the
  *library* layer of section 5, not for the language.

### 3.4 Kotlin

Kotlin's language feature is tiny: the `suspend` modifier, the
`Continuation` interface, and one intrinsic ([KEEP: Coroutines][keep]).
The compiler turns a suspend function into a state machine with a `label`
and fields for the locals live across suspension points, and a suspend call
passes a continuation and returns either a value or `COROUTINE_SUSPENDED`.
Everything else (`launch`, `async`, `Deferred`, dispatchers, channels,
`Flow`, cancellation, structured concurrency) is the `kotlinx.coroutines`
library, and the language has stayed stable since 1.3 (2018) while the
library evolved. That split is what the brief asks for, and it worked.

What does not carry over: the state machine object is heap-allocated per
suspending invocation (the KEEP says it is created "when suspending
function is invoked"), resumption is a virtual `resumeWith`, and
`suspendCoroutine` allocates once more per suspension, which is why
generators needed intrinsics to be affordable. These are acceptable on the
JVM and impossible here. What does carry over: cooperative cancellation
delivered *at suspension points*, structured concurrency as a library
discipline ([Elizarov, "Structured concurrency"][elizarov]; [Smith, "Notes
on structured concurrency"][njs]), and `inline` HOFs that suspend
transparently (2.4 gives Goose this for every HOF).

### 3.5 Go

Goroutines are stackful (a few kilobytes of stack, grown by copying,
[contiguous stacks][go-stacks]), scheduled M:N by a work-stealing runtime
([Vyukov's scheduler design][vyukov]) whose network poller intercepts
blocking calls so that ordinary code never notices it is asynchronous.
People like it for exactly that: no coloring, no executor to choose, a
server is `go handle(conn)`. The costs are the runtime (a GC with precise
stack maps to move stacks, a scheduler, a few hundred nanoseconds per
switch), shared-memory races, goroutine leaks and the absence of structured
concurrency (`errgroup` and `context` are libraries). None of the mechanism
is available to Goose (2.3), and the architecture it gives (migration
between threads) is not the fastest one for servers (3.9). What Goose can
offer in its place is that a per-worker loop needs no scheduler at all.

### 3.6 Zig, twice

Zig's first design ([0.10 docs: async functions][zig010]) is the closest
prior art to section 4: `@Frame(f)` was a *type*, the frame of `f`, sized
by the compiler; the *caller* placed it (on its stack, in an array, in an
allocator) with `async f(args)`; `suspend`/`resume`/`await` drove it; and
whether a function was async was *inferred* from whether it suspended. The
inference was its problem: async-ness was viral and invisible, frames'
sizes depended on whole call trees, and the feature did not survive the
move to the self-hosted compiler in 0.11 (2023; [release
notes][zig011]). Lesson: mark it explicitly (`coro fn`), and make a
coroutine's frame depend on its own body, with called coroutines inlined by
a rule rather than by inference (4.4).

Zig's second design (2025; [Cro, "Zig's new async I/O"][zig-io]; [Kelley's
devlog][zig-devlog]) passes an `std.Io` *interface value* into every
function that does I/O, as Zig already passes allocators. `io.async(f)` and
`.await` are methods on it, and the chosen implementation decides whether
they block, use a thread pool, or run green threads on an event loop; a
stackless implementation is planned. It is a vtable, with the cost admitted
("only guaranteed to be removed when a single Io implementation is used"),
and the green-thread implementation needs stack switching, which 2.3 rules
out here. The idea that does transfer is the scheduler as an explicit
parameter: in Goose that is a static function value or a module, resolved
at compile time, so the vtable cost never arises.

### 3.7 C#, Swift, Python, Lua, Java, OCaml

* **C#** invented the `async`/`await` syntax (C# 5, 2012, after F#'s
  async workflows). Its compiler builds a *struct* state machine that lives
  on the stack until the first real suspension and is boxed only then;
  `ValueTask` and pooled task sources remove the remaining allocations
  ([Toub, "How Async/Await Really Works"][toub]). It is the best write-up
  of a struct-shaped state machine and of what it costs to keep it off the
  heap.
* **Swift** ([SE-0296][se0296]) compiles async functions to run off the
  thread's native stack, with suspension only at explicit points, and puts
  structured concurrency ([SE-0304][se0304]) and actors in the language.
  Its justification for explicit `await` applies here: code between
  suspension points can be read as atomic.
* **Python** generators ([PEP 342][pep342]) have `send` (a resume value),
  `throw` and `close` (cancellation delivered at the yield as an
  exception), and [PEP 380][pep380]'s `yield from` is the desugaring of
  "call another coroutine and forward its yields" that 4.4 uses. Frames
  are heap objects.
* **Lua** coroutines are stackful, asymmetric and first-class, and the
  cleanest small API: `create`, `resume`, `yield`, `status`
  ([manual][lua]). The resume/yield value passing of 4.3 is Lua's.
* **Java** virtual threads ([JEP 444][jep444]) and **OCaml 5** effect
  handlers ([Sivaramakrishnan et al.][ocaml-effects]) are stackful designs
  that copy or relocate stack chunks to the heap; both depend on a GC that
  can find every pointer into a stack. Not available here (2.3).

### 3.8 Coroutines in C without a compiler

[Duff's device][duff] (1983), Tatham's ["Coroutines in C"][tatham] (2000)
and Dunkels' [Protothreads][protothreads] (2006) implement stackless
coroutines in plain C: a `switch` on a saved line number jumps into the
middle of the function, and any state that must survive is kept in a
struct the programmer declares. Protothreads run sensor-network nodes in
two bytes of state per thread. They show what the lowering of 4.12 is,
and exactly what a compiler adds over them: it hoists the locals that
cross a yield into the struct and reloads them, and it checks that nothing
else was needed. That is the entire "convenience" of the feature, and it
is enough to make the hand-written version clearly worse (1.2).

### 3.9 The fastest servers

[Seastar][seastar] (C++, ScyllaDB), [glommio][glommio] (Rust, io_uring),
[monoio][monoio] (Rust, io_uring) and [embassy][embassy] (Rust, embedded)
share the shape of 2.5: one event loop per core, no sharing between cores,
explicit message passing, tasks that are plain values, and completion- or
readiness-based I/O driven directly by the loop. [io_uring][iouring] is the
Linux API they target; its completion model wants buffers that outlive the
submitting task's suspension, which pools give (2.6). Goose's `thread_fn`
plus typed queues is this architecture's substrate already; the coroutine
is the missing per-connection piece.

### 3.10 What to take

| Lesson | From | In this design |
|---|---|---|
| State machine, lazy, inline-nested, statically sized | Rust, C#, C++ | 4.1, 4.4, 4.12 |
| Overlap non-simultaneous locals in the frame | Rust | 4.12 |
| Frame placed by the caller, type named by the function | Zig 0.10 | 4.1, 4.2 |
| Mark coroutines explicitly, never infer | Zig 0.11's removal | 4.1 |
| Keep the language surface tiny, the library rich | Kotlin | 4.13 |
| Cancellation delivered at suspension points, as a value | Kotlin, Python | 4.10 |
| No self-borrows across suspension, hence no `Pin` | Rust's pain | 4.5 R4 |
| No callback-shaped waker; the resumer knows the frame | Rust's `Waker`, C++ handles | 2.7, 4.3 |
| One `yield`, one `resume`, one value each way | C++'s protocol soup | 4.3 |
| Protocol logic sans-I/O; only waiting is colored | Python, Rust ecosystems | 4.13, 5 |
| Thread-per-core, share-nothing, pools own buffers | Seastar, glommio, monoio | 2.5, 2.6, 5 |
| HOFs suspend transparently | Kotlin `inline` | 2.4, 4.6 |
| Frames as values can be copied, sent, stored | Protothreads, embassy | 4.9 |

## 4. The design

Syntax is a placeholder throughout; section 6.9 lists the alternatives.

### 4.1 Declaration: a function that is also a type

```goose
coro fn serve(c: Conn&<u32 in conns>) -> bool resume Event {
    …                         // a body that may yield
}
```

`coro fn` declares two things under one name:

* **A function** whose body may contain `yield`, and which may be *called*
  only from another coroutine's body (4.4) or *started* as a value (4.2).
  It is declared like any function: fully typed parameters (as `thread_fn`
  and cycle functions require, §7.8, §11.2; the frame's layout depends on
  them, 4.11), optional result types after `->`, generics, defaults,
  overloads, namespaces, UFCS.
* **A struct type** of the same name, the *frame*: the parameters as
  fields, the locals that are live across any yield as hidden fields, the
  result (if any), and a hidden tag saying where the body is suspended.
  It is fixed-size, and it has the size class, flatness, root bindings and
  copyability that a struct with those fields has (4.8, 4.9). A lowercase
  type name is already ordinary (`float3`, `int2` in `vec`).

Two more clauses can follow the result types:

* `yield Y`: the type each `yield` hands to the resumer. It may be omitted
  and inferred from the first `yield` checked, as a result type is inferred
  from the first `return` (§7.1). A body whose yields carry nothing has no
  yield type.
* `resume X`: the type each resume supplies, and the value of a `yield`
  expression. Without it, `yield` is a statement and `resume` takes no
  value. It cannot be inferred, because the body is checked before any
  `resume` of it is (4.11).

`Y`, `X` and the results are fixed-size. A coroutine that produces
growable data appends it to a buffer it is given, as a cycle function does
(§7.8); it cannot be a `[>..]` result built at the resumer's destination,
since the resume that completes it is not statically known from the ones
that do not (6.8). A `coro fn` is not `extern`, `export`, `thread_fn` or
`recursive`; `checkfree` and `simd` apply as to any function; `inline`
means nothing, since what gets inlined is the resume.

### 4.2 Starting: a literal, and nothing runs

```goose
var task = serve { c };                   // a frame, suspended before its first statement
tasks.push(serve { c: conns.alloc_ref(Conn { fd: fd, .. }) });
```

A frame is constructed by the struct literal of its type: the parameters
positionally or by name, defaults as field defaults, built in place like
any struct (§4.2, §4.3). Nothing of the body runs. This is Rust's and
Python's laziness, chosen because it is also the thinnest: starting is a
store of the arguments and a zero tag, and no code can run that the owner
of the frame did not ask for. The parameter fields are readable from
outside (`task.c`), and writable only if declared `var`, as `let` fields
are (§3.2).

### 4.3 `yield` and `resume`

```goose
let w, suspended = resume(task, Event.Ready);   // runs to the next yield or to the end
```

* `resume(f)` or `resume(f, x)` runs the frame `f` (an lvalue of a frame
  type, or a reference to one) from where it is suspended to its next
  `yield` or to the end of its body. It returns the yielded value and
  `true`, or the yield type's zero value (as a missed `qpoll` does, §11.2)
  and `false` once the body has returned. A coroutine without a yield type
  returns the `bool` alone. `x` is required exactly when the coroutine
  declares `resume X`.
* `yield e` inside the body stores the live locals, hands `e` to the
  resumer, and returns from `resume`. The next `resume(f, x)` continues
  after it, and `x` is the value of the `yield` expression. Without a
  `resume X` clause, `yield e;` is a statement.
* `result(f)` reads a finished frame's results (`let a, b = result(f);`
  for several). It aborts on a frame that has not finished.

That is the whole runtime surface: one way in, one way out, a value each
way. Every protocol of section 5 is spelled with these. The names are
builtins so that `resume` can be specialized per frame type without a
declaration; by UFCS, `task.resume(ev)` works too (§7.1).

### 4.4 Calling a coroutine from a coroutine

```goose
coro fn read_line(c: Conn&, out: u8[..4096]&) -> bool resume Event { … }

coro fn serve(c: Conn&) resume Event {
    while read_line(c, c.line) { handle(c.line[..]); }
}
```

A call of a `coro fn` from a coroutine body runs the callee to completion
*inside the caller's state machine*: each of the callee's yields is a yield
of the caller, each resume of the caller continues the callee, and the
call's value is the callee's result. This is Rust's `.await` and Python's
`yield from` without a keyword, and Kotlin's suspend call. The callee's
yield type must be the caller's (or the caller's is inferred from it), and
its resume type must be the caller's or absent (it ignores the value).

The callee's locals that cross its yields are part of the caller's frame,
so the frame of a coroutine is sized by its whole call tree, statically.
That is why a coroutine cannot be recursive: there is no bound. A
coroutine *can* call ordinary recursive functions between yields, and an
ordinary function can call a coroutine only by starting it as a value and
resuming it (4.3). Whether the callee's code is spliced into the caller's
segments (one tag, one switch) or kept as a separate resume function with a
tag of its own (one extra switch per level per resume) is an implementation
choice per call site, like inlining; the community sketch measured the
difference at 45% on a patrol benchmark (section 7), so the spliced form is
the default and the nested form is for large callees with many callers.

### 4.5 Rules at a suspension point

A *suspension point* is a `yield`, and every call of a `coro fn` from a
coroutine, in the body or in a block it passes to a HOF (4.6).

* **R1. No nonfixed local in scope.** §7.8's rule, verbatim: no variable-
  or resizable-class `let`, `var`, by-value parameter or by-value `match`
  payload may be in scope at a suspension point, nor may one whose
  initializer contains it. Declare scratch in a block that ends before
  the yield; own growable data outside and pass it in.
* **R2. No nonfixed temporary live**: §7.8's cases (a) to (d), with the
  yield in the role of the call into the cycle. So `print(str(n), yield w)`
  is an error and `let s = str(n).len; yield w;` is not. Nor may a
  nonfixed *result* be pending: its destination stack is in use from the
  call until the value arrives (§10.3), and the arrival would be in a
  later resume, possibly from another resumer with other stacks in use. A
  `coro fn` has fixed results (4.1), so this only concerns a block that
  yields inside a function with a nonfixed result: `each_rev(xs) { yield
  w; }` is fine, `map(xs) { yield w; … }` and `exception::try { yield w; }`
  (whose `handler` returns an `E[]`) are errors.
* **R3. What crosses is stored.** A local declared before a yield and
  named after it is live across it and becomes a hidden field of the
  frame. The test is syntactic, as the shrink rules' liveness test is
  (§5.1): any mention after the yield in the local's scope counts, loops
  count their whole body, and a block or nested function that names the
  variable counts. "Named after" is decided per yield, so a local that
  dies before a later yield does not widen that yield's state.
* **R4. No reference into the coroutine's own locals crosses.** A yield
  ends an activation: the locals that are live after it are reloaded from
  the frame, in a different place. So a reference or slice rooted at a
  coroutine's local or parameter copy, or at a temporary of the yield's
  statement, may not be live across the yield. This is the outlives rule
  (§9.1) with the yield as a scope exit, and it is checked as a return of
  such a reference is. References rooted *outside* the frame (a pool, a
  global, a reference parameter's pointee, static data) cross freely: they
  are fixed values. This rule is what makes the frame a plain value (no
  `Pin`, 3.2) and an ordinary holder (4.8). Section 6.4 discusses relaxing
  it.
* **R5. No `defer` in scope** at a suspension point, in v1: a `defer` body
  runs at its block's exit, which would now be in a later resume, from a
  different resumer. Section 9 keeps the question.
* **R6. Narrowing survives, flow state is the body's.** An optional
  narrowed before a yield is narrowed after it: it is a `let` or `var` of
  the body, saved and restored, and nothing but the body writes it. (A
  `var` parameter is writable by the frame's owner between resumes, so it
  is not narrowed across a yield.) Definite assignment is checked on the
  body as a whole, as for any function.
* **R7. No yield in a global initializer, a default, or a `defer` body**,
  which have no frame; and none in a function of a recursive cycle, in a
  block passed to one, or in a nested `fn` that is not itself a `coro fn`
  (4.6).
* **R8. A yield is a statement's own.** It may stand anywhere an expression
  may (`let ev = yield w;`, `guard (yield w) == Event.Ready else { return;
  }`), subject to R2, and it is evaluated where it stands in left-to-right
  order (§2): the operands before it are evaluated before the suspension
  and kept, since they are fixed by R2.

Everything else a function may do, a coroutine may do between two yields:
grow pools and buffers through references, declare and use scratch
buffers in inner blocks, call anything, `return … from` within the
coroutine (4.7), spawn workers, block on `qget` (a logic hazard, not an
error).

### 4.6 Blocks, HOFs and nested functions

A block written in a coroutine body may yield. The function it is passed to
is specialized for that block (§7.6), and that specialization becomes part
of the coroutine: R1 to R4 apply to the HOF's own locals at the block's
yields, as they would to the coroutine's, and its locals that cross join
the frame. So `each_rev(xs) { yield Wait.Any; }` works with std's ordinary
`each_rev`, whose only local across the yield is its loop index. The HOF
itself stays an ordinary `fn`: it contains no `yield`, it only runs a
block that does. A function that yields *in its own text* is a `coro fn`,
so the sketch's `wait_until` is written `coro fn wait_until<F>() { while
!F() { yield Wait.Any; } }` and called as `wait_until { ready(c) }`; a
`coro fn` with a function-value parameter can be called but not started,
since a literal has nothing to bind `F` to. A block that yields can only be
passed to a function whose own locals and results obey the rules at that
point (R1, R2: fixed results, no nonfixed local in scope), which the
specialization checks; a block passed into a recursive cycle cannot yield
(R7). `return` inside such a block returns from the lexically
enclosing coroutine, as it returns from the enclosing named function today
(§7.6).

A nested `fn` declared in a coroutine body may not yield; a nested
`coro fn` may, and is called as any coroutine is (4.4). Function values
that are *named* coroutines (`each_rev(xs, step)` where `step` is a
`coro fn`) follow the same rule as blocks.

### 4.7 `return … from`, `defer` and the other exits

* `return` from a coroutine body finishes the frame: the results are stored
  in it, the tag marks it finished, and `resume` returns `false`.
* `return E from f` where `f` is the coroutine itself, a coroutine it
  called (4.4), or a function inlined into the coroutine by a block (4.6)
  works as today: these frames are on the native stack whenever the body
  runs, since a called coroutine's code only ever runs through its
  caller's resume. The thread-local discriminant (§C.3) is set only
  between the throw and the catch inside one `resume`. A target *outside*
  the coroutine (the function that constructed the frame, or the one
  resuming it) is an error: it is not on the compile-time path from the
  `resume` to the body, which is the condition §7.9 already checks.
  `exception::try` as written cannot take a body that yields, because its
  `handler` returns a nonfixed `E[]` (R2); a coroutine-friendly `try` with
  a fixed-size error slot (an ADT with a "none" variant) would be a small
  library addition.
* `defer` may not be live across a yield (R5). `guard`, `block`, `break`,
  `continue` and loops are unaffected: a `yield` inside a `for` over a
  slice keeps the index; a `for` over a fixed array local of the coroutine
  iterates by index over the hoisted array, as a `for` over a resizable
  already does (§6.5).

### 4.8 Lifetimes: the frame is a holder

A frame type is a struct with reference fields wherever its parameters or
hoisted locals are references, slices, or values holding them. So it is
"implicitly generic over those fields' roots" (§9.2) and checked as any
such struct: storing a frame into a pool, a local or a global is a store of
what it holds, under the store rule; a frame in a global pool holds only
references that outlive globals (pools, other globals, static data), which
is the server's shape (5); a frame built from a reference to a local of
`main`'s loop can live in that loop's locals and nowhere deeper. The
read-back rules (§9.5) give a frame read out of a pool the roots of the
pool's candidates, and `in pool` links stay exact wherever the frame is
(§3.9), which is why the server example links its connection that way.
A frame rooted at a grow-shrink array (its `c` points into a `[>..<]`) is
held by variables only (§5.2), like any such value.

Nothing here is new: it is what a struct `{ c: Conn&, n: i64, state: u8 }`
would already be subject to. The one coroutine-specific addition is R4,
which keeps the frame's own storage out of the roots of what it holds.

### 4.9 Copying, flatness and threads

Because of R4 a frame holds no reference into itself, so it is a plain
fixed-size value:

* It copies as a struct does (assignment, a by-value argument, `pop`, a
  `for` copy). A copy of a suspended frame is a second, independent
  coroutine continuing from the same point: a fork, cheap and occasionally
  useful (a generator replayed from a checkpoint), and a hazard only in the
  way any copy is.
* It is **flat** when its parameters and hoisted locals are flat (§1.1): a
  frame of integers, enums, limited arrays and index-typed slots. A flat
  frame is a `thread_spawn` argument or a queue message like any other: a
  suspended task can be handed to another worker, which resumes it there.
  This is the explicit form of work distribution (2.5); a frame holding
  references, like any such value, stays on its thread.
* It serializes: `to_bytes` of a pool of flat frames is a snapshot of
  suspended tasks, verifiable back in by `from_bytes` as a struct image
  whose tag is range-checked (§12). A consequence, not a goal, and tied to
  the compiler version that laid the frame out, as every image is.
* `==` compares the parameters, the tag and the hoisted locals as fields;
  `print` renders the parameters and the tag; `default<serve>()` exists
  when every parameter has a default, and is the unstarted frame.

### 4.10 Finishing, re-entry, abandonment

* **Finished.** After the body returns, `resume` returns `false` and
  `result(f)` reads the results. Resuming a finished frame aborts
  ("resumed a finished coroutine"), as `pop` on an empty array does; the
  check is the tag's own `match`, so it costs nothing extra.
* **Re-entry.** `resume` on a frame whose body is running (through an
  alias, from inside its own body) is a logic error, not a safety error:
  the inner resume reruns the segment after the last yield with the saved
  locals, which are valid values of their types, and the outer run
  overwrites them at its next yield. This is type-safe reuse in the sense
  of §9.4. A debug build stores a "running" tag at entry and aborts on
  re-entry; a release build does not pay the store.
* **Abandonment.** A frame that is never resumed again costs nothing to
  drop: it owns no stack, there are no destructors (§4.1), and whatever it
  names in pools is the pools' to free. This is where Goose's lack of
  destructors becomes a simplification rather than a gap: Rust's
  cancellation-safety problem (3.2) does not exist, since no partial
  progress is lost that was not already stored outside the frame.
* **Cooperative cancellation** is a resume value (`Event.Cancel`), which
  the body sees at its yield and acts on by returning; Python's `close()`
  and Kotlin's `CancellationException` are this, delivered as a value
  instead of an exception. A library can make it a convention; the
  language does not need to know.

### 4.11 Generics and checking order

A generic `coro fn step<T>(…)` is monomorphized like any function, and its
frame type is per instantiation like a generic struct's. The frame's
*layout* depends only on the parameter types and type arguments: the
hoisted locals' types follow from the body with concrete parameter types,
whatever the roots. So the layout is fixed when the type is first used
(a literal, a field, an array element), by checking the body once
standalone, as `CheckUnreached` checks a function with no caller
(implementation.md §3.1). Each `resume` site then specializes the body for
real, as a call of `fn serve_resume(f: serve&, x: Event) -> Wait, bool`
with the frame's roots, writability and `in pool` provenance, so that the
body's stores and shrinks are checked for the storage that frame actually
names. Two frame instances with different root bindings are distinct for
checking and identical in layout, as reference-holding struct instances
are (§9.2). A frame type with a function-value parameter has no literal
(4.6).

### 4.12 Lowering to C, and what it costs

The frame is a packed C struct; the body is one C function with a `switch`
over the tag that jumps to the label after the yield it was suspended at
(the Protothreads lowering, 3.8, which is legal C: a `goto` may enter a
loop body, and the generated C already declares every aggregate local at
the top of the function, implementation.md §6.7). Sketch for a body with
one yield that keeps `n` live:

```c
typedef struct serve_g { uint32_t c_g; uint8_t gs_tag; int64_t n_g; } serve_g;

static bool serve_resume(serve_g *f, Event_g ev, Wait_g *out, int64_t gs_sp) {
    int64_t n;
    switch (f->gs_tag) {
        case 0: goto L0;
        case 1: n = f->n_g; goto L1;             // reload what is live after yield 1
        default: gs_fail_finished();
    }
L0: for (;;) {
        n = net_recv(…);
        while (n == AGAIN) {
            f->n_g = n; f->gs_tag = 1;           // save what is live after the yield
            *out = (Wait_g){ .tag = READABLE, .fd = … };
            return true;                         // yield
        L1:
            if (ev.tag != READY) { f->gs_tag = DONE; return false; }
            n = net_recv(…);
        }
        …
    }
}
```

* **Copy in, copy out.** Between two yields, the locals are C locals of
  the resume function, so the C compiler allocates registers for them as it
  would in any function; a yield stores the ones live after it and a
  resume reloads them. The frame is not addressed in the hot path, which
  also keeps the C compiler's aliasing worries (implementation.md §6.2)
  out of the segment. R4 is what permits this.
* **Overlap.** Two hoisted locals never live at the same yield can share
  a slot (Rust's layout, 3.2). The frame is then parameters + tag + the
  largest live set over the yields, not the sum. v1 can lay them out
  without overlap and add it later; the C never changes shape.
* **A called coroutine** (4.4) is spliced in as a block of the caller's
  resume function with its yields numbered among the caller's (one tag),
  or compiled as its own resume function called from the caller's segment
  with its own tag in a nested frame struct. The optimizer chooses as it
  chooses inlining today (implementation.md §4).
* **Data stacks.** The resume function takes `gs_sp` like any function and
  builds its between-yield temporaries above the resumer's watermark; by
  R1 and R2 nothing of the coroutine's is on a data stack at a yield, so a
  yield restores nothing. `NoStackAcrossCycleCall`'s assertion applies to
  yields unchanged.
* **The resumer.** `resume(f, x)` is a direct call of the frame type's
  resume function, inlined into a loop that is its only caller by the
  existing policy. No thread-local, no runtime function, no table.

What it costs, per operation:

| Operation | Cost |
|---|---|
| start (`serve { c }`) | the parameters stored, a zero tag |
| `resume` | one direct call (often inlined), one switch, the loads of the locals live at that point |
| `yield` | the stores of the locals live after it, a tag store, the yield value in registers, a return |
| finish | a tag store, the result store, a return |
| a called coroutine | nothing per call when spliced; one extra switch per level per resume when nested |
| frame size | parameters + the largest set of locals live across one yield (+ result) + 1 tag byte |
| non-users | nothing: no runtime state, no hidden parameters, no change to any other function |

This is the cost of the hand-written state machine, and the community
sketch's measurement (section 7) found a hand-lowered coroutine within
10% of one when the call tree was spliced into a single tag.

### 4.13 What stays out of the language

Each of these is a library, and section 5 shows the first of them:

* **The event loop**: which fds to watch, with which kernel API, and how to
  find the frame an event belongs to.
* **Timers**, as a heap of deadlines the loop checks between events.
* **Channels** between coroutines on one thread (an array and a `Wait`
  variant), and across threads (the typed queues, §11.2).
* **`select`**: a `Wait` variant that holds several reasons, or a coroutine
  that resumes two inner frames in turn (the sketch's `await_all`, 7).
* **Structured concurrency**: a scope that owns a local array of frames and
  does not end until all are finished; the outlives rule makes a frame
  unable to outlive what it refers to (4.8), which is the half of structured
  concurrency Kotlin has to enforce by discipline.
* **Cancellation** policy: which resume value means stop, and who sends it.
* **`run_blocking`**: a loop over `resume` in an ordinary function that
  satisfies the waits by blocking (`poll` with no timeout, `qget`).
* **Generators as iteration**: `for x in gen` could be sugar over `resume`
  later; today it is `loop { let x, more = resume(g); guard more else {
  break; } … }`, or a HOF.
* **Protocol implementations** written sans-I/O (3.2): a parser consumes
  slices and says "need more" or "done"; only the function that waits is a
  `coro fn`, and the parser serves blocking and coroutine code alike.

## 5. A server on top

A sketch of the layering, with a hypothetical `net` module of `extern fn`
sockets (there is none yet; section 8 lists it as a prerequisite track).
Names are placeholders.

```goose
import std;
import net;        // hypothetical: nonblocking sockets and a readiness poll as externs

// Connections live in a pool. The buffer is a limited array inside the element,
// so the kernel can write into it while the coroutine is suspended (§5.3).
struct Conn { fd: i64, inbuf: u8[..4096], outbuf: u8[..4096] }
reusable var conns: Conn[>..] = [];

// The protocol between coroutines and the loop is two flat ADTs the library
// defines. A different loop (io_uring, IOCP, a game's frame tick) defines its own.
enum Wait  { Readable { fd: i64 }, Writable { fd: i64 }, Timer { deadline_ns: i64 }, Any }
enum Event { Ready, Timeout, Hangup, Cancel }

// Waiting is the only colored code. recv appends into the limited array and
// returns the byte count, 0 at end of stream, or net::AGAIN.
coro fn fill(c: Conn&<u32 in conns>) -> bool resume Event {
    loop {
        let n = net::recv(c.fd, c.inbuf);
        if n != net::AGAIN { return n > 0; }
        guard (yield Wait.Readable { fd: c.fd }) == Event.Ready else { return false; }
    }
}

coro fn flush(c: Conn&<u32 in conns>) -> bool resume Event { … }   // the same for Writable

// The handler is straight-line code; the parser is an ordinary function over
// slices (sans-I/O), usable from blocking code just as well.
coro fn serve(c: Conn&<u32 in conns>) resume Event {
    loop {
        guard fill(c) else { return; }
        let consumed = http::respond(c.inbuf[..], c.outbuf);   // parses, writes a reply
        guard consumed > 0 else { continue; }                   // need more bytes
        drop_front(c.inbuf, consumed);                          // keeps the unparsed tail
        guard flush(c) else { return; }
    }
}

// The loop. Frames live in a pool beside the connections; the loop keeps a
// table from fd to frame index for the events the kernel hands back.
reusable var tasks: serve[>..] = [];
var by_fd: i64[>..] = [];                                 // fd -> index into tasks, or -1
var timers: Deadline[>..] = [];                           // a heap of (deadline, task)
var ready: i64[>..<] = [];                                // tasks to resume next round

fn run(listen_fd: i64) {
    loop {
        for ev in net::poll_wait(…) {                      // (fd, readable/writable/hangup)
            let i = by_fd[ev.fd];
            let w, suspended = resume(tasks[i], if ev.hangup { Event.Hangup } else { Event.Ready });
            guard suspended else {
                conns.free(conns.index_of(tasks[i].c)); tasks.free(i); net::close(ev.fd);
                continue;
            }
            match w {
                Readable r => net::watch(r.fd, net::READ, i),
                Writable r => net::watch(r.fd, net::WRITE, i),
                Timer t    => timers.push(Deadline { at: t.deadline_ns, task: i }),
                Any        => ready.push(i),
            }
        }
        … accept new connections into conns and tasks; fire due timers …
    }
}

// Thread-per-core: each worker is a program instance with its own copies of
// conns, tasks and by_fd (§11.2), accepting on its own SO_REUSEPORT socket.
thread_fn worker(listen_fd: i64) { run(listen_fd); }

fn main() {
    for i in hardware_threads() { thread_spawn(worker, net::listen(8080, true)); }   // SO_REUSEPORT
    …
}
```

What this shows about the fit:

* The frame of `serve` is a 4-byte link, a tag and the few locals that
  cross the yields inside `fill` and `flush` (an `n`, a position): tens of
  bytes. A million connections are a million elements in two pools.
* Nothing in the loop allocates, locks or calls indirectly; `resume` is a
  switch into the frame's code. The loop's data structures are arrays.
* The buffers are the pool's, so a completion-based loop (io_uring, IOCP)
  can submit `c.inbuf`'s free slots to the kernel at the yield and hand the
  byte count back as the resume value: the yield value would carry the
  slice, and the frame need not be pinned for the kernel to write into
  memory it does not own (2.6).
* Every worker runs the same loop on its own copies of the globals. There
  is no scheduler and no cross-thread anything until the program wants it,
  and then it is a queue.
* The protocol logic (`http::respond`) is uncolored and testable without a
  socket.

## 6. Options, with pros and cons

Each part of section 4 had alternatives. The recommendation is marked.

**6.1 Stackless (recommended) / stackful / dynamic stacks.** Stackless is
forced (2.3). Stackful is ruled out by the data-stack model. *Dynamic
stacks* (§11.3, a task owning a block of data stacks, switched by setting
`gs_stks`) would lift R1 for a bounded number of heavyweight tasks and is
the community sketch's suggestion for later; it costs address space per
task and a stack switch per resume, and it should be an opt-in on the
declaration (`coro fn … with stacks`), never the default. Not in v1.

**6.2 The frame type: named by the function (recommended) / `co<f>` / a
user struct.** Naming the frame after the function (Zig's `@Frame(f)` in
spirit, 3.6) gives the type a name to put in pools and fields, a literal
to start it with, and fields for the parameters, at no new type-system
concept. `co<f>` (the sketch) needs a type expression that names a
function, which nothing else in Goose has. The Protothreads form, where
the user declares the state struct and the compiler only checks that
nothing else crosses a yield, is the thinnest of all and keeps the layout
entirely in the user's hands (packed fields, `varint`s), but it gives up
the one convenience the feature exists for (3.8); it remains available as
a style, since a coroutine whose locals never cross a yield has a frame of
just its parameters. A `--frames` listing (section 8) tells the programmer
what was hoisted.

**6.3 Typed yield and resume values (recommended) / bare `suspend()` with
fields.** With bare suspension, a coroutine communicates with its resumer
through the frame's parameter fields, which is enough for a game tick that
polls every frame but not for a server, where the loop must learn *what*
each of 10⁵ tasks waits for without asking each one (2.7). A typed yield
costs nothing (the value is returned in registers) and gives generators for
free. The resume value can be emulated through a `var` field, so it is the
more optional half; it is kept because cancellation and completion results
(a byte count from io_uring) are naturally resume values, and because a
value in registers beats a store and a reload through the frame.

**6.4 No self-references across a yield (recommended) / self-referential,
non-copyable frames.** The sketch chose the second: build in place, never
copy, and the outlives rule keeps references to the frame's own locals
valid. Goose can express "fixed but not copyable" already (a value holding
self-relative references, §3.9), and a reference into the frame's own
locals *is* self-relative in nature. That form allows `let s = local[..n];
yield; use(s)`, and it costs: the frame cannot be copied, popped, sent or
serialized, the locals must live in the frame in place (addressed through
`f->` in the hot path), and the layout cannot overlap slots whose addresses
were taken. R4 keeps the frame a plain value and the hot path in registers,
at the price of one error message that points at the fix. A later
relaxation can keep both: a local whose address crosses a yield lives in
the frame in place, and references to it are rooted at the frame's
storage (exact when the frame is a variable, the pool's when it is an
element), while the frame stays copyable because TODO 16's "whole-region
copy" is exactly what copying a frame is.

**6.5 Lazy start (recommended) / run to the first yield at construction.**
Kotlin and C++ (by default) run eagerly to the first suspension. Lazy is
thinner (a literal), gives the owner control over when work happens, and is
what Rust's experience endorses; the cost is that a coroutine that wants to
do something immediately needs one resume, which a library `start` does.

**6.6 Spliced call trees (recommended by default) / nested frames.** 4.4
and 4.12. Splicing duplicates a callee's code per call site, like
inlining, and gives one switch; nesting shares code and costs a switch per
level. Let the optimizer decide by size, as it does for calls.

**6.7 A std `Wait`/`Event` convention / fully generic coroutines.** Rust's
`Waker` lets any future run on any executor; a typed yield ties a library
coroutine to the `Wait` type it constructs. The recommendation is a small
conventional `io::Wait`/`io::Event` pair in the standard library that its
reference loop consumes, so that library coroutines and loops interoperate,
with the language itself indifferent. A loop with other needs defines its
own types and adapts. This is the explicit trade of dynamic dispatch for
flat data, and it is the same trade Goose makes everywhere else.

**6.8 Fixed-size results and yields only (recommended) / nonfixed
results.** A `[>..]` result is built at its destination by the call that
produces it (§7.3); for a coroutine, that is one of many `resume` calls,
and the destination would have to be reserved across all of them. The
spec's idiom for recursion (build into a buffer passed by reference, §7.8)
covers it. Revisit if real code wants `coro fn lines() -> u8[>..]`.

**6.9 Syntax.** `coro fn` beside `thread_fn` and `recursive fn`; the
sketch's `suspend_fn` is the other obvious spelling. `yield` is universal.
`resume`/`result` as builtins (like `thread_spawn`/`thread_wait`), reached
by UFCS as members. The `resume X` header clause is the least settled part:
alternatives are a marked parameter (`ev: resume Event`) or an attribute
list. `await` as a keyword is deliberately absent: a call is a call.

## 7. The community `suspend_fn` sketch

[bitspittle's `suspend_fn` proposal][bitspittle] (October 2026, read after
the design above was drafted, as the brief asked) arrives at the same core
from the Kotlin side: stackless; §7.8's rule at `suspend()`; a called
suspend function's state stored inside the caller's variant, "a tree of
enums with a size that is known at compile time", as Rust lays futures out;
`co_start` that runs nothing; `co_resume -> bool`; everything else (
`await`, `await_all`, `run_blocking`, a scheduler) as library code. Its
patrol example, a guard that walks, rests and walks back while the game
keeps drawing, is a good second use case beside the server, and its before
and after (four `match` arms and a flag, against four straight lines)
makes the case of 1.2(1) better than prose does.

Its measurement is the most valuable part: 10,000 hand-lowered patrols for
10,000 frames on an Apple M3 Pro cost 0.88 ns per guard per frame as a
hand-written machine, 1.28 ns as a nested lowering (a tag per level), and
0.97 ns with the levels merged into one program counter; 22, 48 and 24
bytes of state. So a coroutine costs what the machine it replaces costs,
*if* the call tree is spliced (6.6), and the nested form is 45% slower.
That number is what made 4.4 and 4.12 default to splicing. (The benchmark
lives in that fork, `bench/suspend_fn`, not in this repository.)

Where this note differs, and why:

| | `suspend_fn` sketch | This note |
|---|---|---|
| suspension carries | nothing; the resumer polls a predicate each tick | a typed value each way (4.3), so a loop with 10⁵ tasks learns what each waits for (2.7, 6.3) |
| frame | `co<f>`, built in place, never copied, not flat | a struct named after the function, copyable and flat when its fields are (4.9, 6.4) |
| references to own locals across a suspension | allowed, hence non-copyable | forbidden (R4), hence a plain value and a register-resident hot path |
| threads | a coroutine cannot cross | a flat frame can be queued to another worker (4.9) |
| blocks in HOFs that suspend | open question | allowed; the HOF's specialization joins the coroutine (4.6) |
| `return … from` | open question | within the coroutine's call tree, as today; never past `resume` (4.7) |
| generators | open question | the same machinery (4.3) |
| game-frame use | the example | works unchanged: `resume(p, Event.Tick)` per frame, with `yield Wait.Any` for "next frame" |

Its open question "is a fixed but not copyable kind of value new to the
type system" is answered by R4 (nothing non-copyable is needed) and, for
the relaxation of 6.4, by §3.9's values with self-relative references.

## 8. Steps

1. **Settle the spec text.** A new §7.15 (or a section of its own) with
   4.1 to 4.11 as rules, R1 to R8 numbered, placeholder syntax decided,
   and the `Wait`/`Event` convention noted as a library matter. Add TODO
   items for 6.1, 6.4's relaxation and 6.8.
2. **Measure the server shape by hand**, as the sketch measured the patrol:
   a hand-lowered C echo loop over 10⁵ pooled frames with a `poll`-based
   reactor, in `bench/coro/`, against the same program in Rust/tokio, Go
   and plain C with `epoll`. This fixes the target before anything is
   built and tests the claim of 1.1 that the per-event cost is a switch and
   a few loads. Replicate the patrol numbers on this machine too.
3. **Checker.** Parse `coro fn`, the `yield`/`resume` clauses and the
   `yield` expression; the frame type as a struct instance built from the
   parameters plus the hoisted locals (R3's syntactic liveness, reusing the
   shrink rules' `blockpos` scan); R1 and R2 by reusing `CycleCallTemps`,
   `CheckCycleInit` and `Node::nftemp` with yields as the marked calls; R4
   as the return check on roots at each yield; R5 to R8 as the simple
   rejections they are; the `resume`/`result` builtins specialized per
   frame type and roots (4.11); error messages that name the local and the
   yield.
4. **Codegen.** The frame struct beside the other generated types; one
   resume function per frame instantiation with the switch and labels,
   copy-in/copy-out at the segment boundaries; splicing of called
   coroutines first (it needs no second tag), the nested form after, chosen
   by the optimizer's inlining policy; `gs_sp` and the watermark
   discipline unchanged. `--frames` listing each frame's size and hoisted
   locals, and the LSP's hover showing the same.
5. **Tests.** A corpus entry per rule (each error message), the patrol and
   the echo loop as samples, a generator, a yielding block through std's
   HOFs, the R2 rejection of `exception::try` around a yield, a frame
   through a queue, a
   `from_bytes` of a frame pool, re-entry and finished-resume aborts in
   debug.
6. **Networking track** (independent of 1 to 5, and a prerequisite for the
   server): sockets, nonblocking I/O and a readiness poll in `os` or a new
   `net` module as `extern fn`s over the runtime, portable (`poll`) first,
   `epoll`/`kqueue`/IOCP next, `io_uring` when the completion-shaped
   protocol of 5 is wanted.
7. **The reference loop in std**: `io::Wait`, `io::Event`, a reactor with
   timers, `run_blocking`, and the thread-per-core skeleton of section 5
   as a sample. Then the server benchmark of step 2 in Goose, which is the
   measurement the brief's "absolute fastest" claim stands or falls on.
8. **Later**: overlapped frame slots (4.12), the 6.4 relaxation, `for x in
   gen`, dynamic-stack tasks (6.1), nonfixed results (6.8).

## 9. Open questions

* Whether the resume value should be declared in the header (`resume X`),
  marked on a parameter, or dropped in favor of `var` fields (6.3, 6.9).
* Whether parameters should be visible as frame fields at all, or the
  frame should be opaque apart from `resume` and `result`. Visible fields
  are convenient (the loop reads `tasks[i].c` to free the connection) and
  cost nothing; opaque is tidier.
* `defer` across a yield (R5): it could be kept in the frame as a flag and
  run at the block's exit in whichever resume reaches it, at one byte and
  one branch per `defer`. Wait for a use.
* Large hoisted locals: copy-in/copy-out of a `u8[1024]` local per resume
  is 2 KB of traffic. The 6.4 relaxation (in-place slots) or a size
  threshold would address it; the workaround today is to own the buffer
  outside, which is the idiom anyway.
* Whether a `thread_fn` entry may itself be a `coro fn` (no obvious use;
  no), and whether `simd coro fn` means anything (not in v1).
* Dynamic-stack tasks (6.1): what the opt-in looks like and how many a
  program may have, given the address-space budget.
* A `for x in gen` form and a `yield`-based iterator protocol for std's
  containers, once generators are in use.

## References

Spec and implementation sections referred to above:
[§1.2 memory model](../goose_spec.md#12-memory-model),
[§1.3 the stack invariant](../goose_spec.md#13-the-stack-invariant),
[§4.3 in-place construction](../goose_spec.md#43-the-in-place-construction-guarantee),
[§5.4 reusable arrays](../goose_spec.md#54-reusable-arrays-safe-allocation-escape-hatch),
[§7.6 static function values](../goose_spec.md#76-static-function-values),
[§7.8 recursion](../goose_spec.md#78-recursion),
[§7.9 `return … from`](../goose_spec.md#79-return--from-long-distance-return),
[§9.2 roots](../goose_spec.md#92-roots-and-the-depth-check),
[§10.3 stack assignment](../goose_spec.md#103-stack-assignment-deterministic-recipe),
[§11.2 concurrency](../goose_spec.md#112-concurrency),
[§11.3 dynamic stacks](../goose_spec.md#113-dynamic-stacks-future);
[implementation.md §3.11 recursion](../implementation.md#311-recursion),
[§6.2 stack assignment](../implementation.md#62-stack-assignment),
[§6.6 calls and `return from`](../implementation.md#66-calls-dispatch-function-values-return-from),
[§7 the runtime](../implementation.md#7-the-runtime);
[owning_dynamic_data.md](owning_dynamic_data.md),
[scope_exit_cleanup.md](scope_exit_cleanup.md).

[revisiting]: https://www.inf.puc-rio.br/~roberto/docs/MCC15-04.pdf "de Moura, Ierusalimschy: Revisiting Coroutines, ACM TOPLAS 2009"
[rfc230]: https://rust-lang.github.io/rfcs/0230-remove-runtime.html "Rust RFC 230: remove the green-threading runtime (2014)"
[rfc2394]: https://rust-lang.github.io/rfcs/2394-async_await.html "Rust RFC 2394: async/await"
[rfc2592]: https://rust-lang.github.io/rfcs/2592-futures.html "Rust RFC 2592: futures"
[rfc2349]: https://rust-lang.github.io/rfcs/2349-pin.html "Rust RFC 2349: Pin"
[mandry]: https://tmandry.gitlab.io/blog/posts/optimizing-await-1/ "Mandry: Optimizing await (generator layout)"
[embassy]: https://embassy.dev/ "Embassy: async Rust for embedded, statically allocated tasks"
[boats-pin]: https://without.boats/blog/pin/ "boats: Pin"
[pin-suffering]: https://fasterthanli.me/articles/pin-and-suffering "fasterthanlime: Pin and suffering"
[rust-goals]: https://rust-lang.github.io/rust-project-goals/2025h1/async.html "Rust 2025H1 goal: bring async closer to parity with sync"
[rust-july]: https://blog.rust-lang.org/2025/08/05/july-project-goals-update "Rust project goals, July 2025 update"
[boats-tpc]: https://without.boats/blog/thread-per-core/ "boats: Thread-per-core"
[glommio]: https://www.datadoghq.com/blog/engineering/introducing-glommio/ "Introducing glommio: thread-per-core on io_uring"
[tokio-cancel]: https://docs.rs/tokio/latest/tokio/macro.select.html#cancellation-safety "tokio::select! cancellation safety"
[colors]: https://journal.stuffwithstuff.com/2015/02/01/what-color-is-your-function/ "Nystrom: What Color is Your Function?"
[afit]: https://blog.rust-lang.org/2023/12/21/async-fn-rpit-in-traits.html "async fn in traits, Rust 1.75"
[boats-futures]: https://without.boats/blog/let-futures-be-futures/ "boats: Let futures be futures"
[async-vision]: https://rust-lang.github.io/wg-async/vision.html "Rust async vision document"
[sansio]: https://sans-io.readthedocs.io/ "Writing I/O-free (sans-I/O) protocol implementations"
[cpp-coro]: https://en.cppreference.com/w/cpp/language/coroutines "C++20 coroutines"
[halo]: https://wg21.link/p0981 "P0981: Halo, coroutine heap allocation elision"
[baker-sym]: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer "Baker: Understanding symmetric transfer"
[baker-await]: https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await "Baker: Understanding operator co_await"
[p2502]: https://wg21.link/p2502 "P2502: std::generator (C++23)"
[p3552]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3552r3.html "P3552R3: Add a coroutine task type (C++26)"
[p3801]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3801r0.html "P3801: Concerns about the design of std::execution::task"
[p2300]: https://wg21.link/p2300 "P2300: std::execution (C++26)"
[keep]: https://github.com/Kotlin/KEEP/blob/master/proposals/coroutines.md "Kotlin KEEP: Coroutines"
[elizarov]: https://elizarov.medium.com/structured-concurrency-722d765aa952 "Elizarov: Structured concurrency"
[njs]: https://vorpus.org/blog/notes-on-structured-concurrency-or-go-statement-considered-harmful/ "Smith: Notes on structured concurrency"
[go-stacks]: https://docs.google.com/document/d/1wAaf1rYoM4S4gtnPh0zOlGzWtrZFQ5suE8qr2sD8aWQ "Go: contiguous stacks design"
[vyukov]: https://docs.google.com/document/d/1TTj4T2JO42uD5ID9e89oa0sLKhJYD0Y_kqxDv3I3XMw "Vyukov: Scalable Go scheduler design"
[zig010]: https://ziglang.org/documentation/0.10.1/#Async-Functions "Zig 0.10.1: async functions and @Frame"
[zig011]: https://ziglang.org/download/0.11.0/release-notes.html "Zig 0.11.0 release notes"
[zig-io]: https://kristoff.it/blog/zig-new-async-io/ "Cro: Zig's new async I/O (2025)"
[zig-devlog]: https://ziglang.org/devlog/2025/ "Zig devlog 2025: std.Io"
[toub]: https://devblogs.microsoft.com/dotnet/how-async-await-really-works/ "Toub: How async/await really works in C#"
[se0296]: https://github.com/apple/swift-evolution/blob/main/proposals/0296-async-await.md "Swift SE-0296: async/await"
[se0304]: https://github.com/apple/swift-evolution/blob/main/proposals/0304-structured-concurrency.md "Swift SE-0304: structured concurrency"
[pep342]: https://peps.python.org/pep-0342/ "PEP 342: coroutines via enhanced generators"
[pep380]: https://peps.python.org/pep-0380/ "PEP 380: yield from"
[lua]: https://www.lua.org/manual/5.4/manual.html#2.6 "Lua 5.4 manual: coroutines"
[jep444]: https://openjdk.org/jeps/444 "JEP 444: virtual threads"
[ocaml-effects]: https://dl.acm.org/doi/10.1145/3453483.3454039 "Sivaramakrishnan et al.: Retrofitting effect handlers onto OCaml, PLDI 2021"
[duff]: https://en.wikipedia.org/wiki/Duff%27s_device "Duff's device"
[tatham]: https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html "Tatham: Coroutines in C"
[protothreads]: http://dunkels.com/adam/pt/ "Dunkels: Protothreads"
[seastar]: https://seastar.io/ "Seastar"
[monoio]: https://github.com/bytedance/monoio "monoio"
[iouring]: https://kernel.dk/io_uring.pdf "Axboe: Efficient IO with io_uring"
[wasm-switch]: https://github.com/WebAssembly/stack-switching "WebAssembly stack-switching proposal"
[bitspittle]: https://github.com/bitspittle/goose/blob/suspend-fn-proposal/docs/design/suspend_fn.md "bitspittle: suspend_fn proposal"
