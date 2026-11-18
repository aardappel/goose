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
  - [2.7 The scheduler protocol is the function-value arguments](#27-the-scheduler-protocol-is-the-function-value-arguments)
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
  - [4.1 A coroutine is an ordinary function](#41-a-coroutine-is-an-ordinary-function)
  - [4.2 Starting: a literal, and nothing runs](#42-starting-a-literal-and-nothing-runs)
  - [4.3 Resume and suspend](#43-resume-and-suspend)
  - [4.4 Calling: the activation is a whole call tree](#44-calling-the-activation-is-a-whole-call-tree)
  - [4.5 Rules at a suspension point](#45-rules-at-a-suspension-point)
  - [4.6 Blocks, HOFs and nested functions](#46-blocks-hofs-and-nested-functions)
  - [4.7 `return … from`, `defer` and the other exits](#47-return--from-defer-and-the-other-exits)
  - [4.8 Lifetimes: the frame is a holder](#48-lifetimes-the-frame-is-a-holder)
  - [4.9 Copying, flatness and threads](#49-copying-flatness-and-threads)
  - [4.10 Finishing, forking, abandonment](#410-finishing-forking-abandonment)
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
* **A coroutine is a higher-order function.** Nothing in a coroutine's
  declaration differs from any other function's (4.1):
  `fn serve<inherited await_non_blocking, …>(conn: Conn&)`
  is ordinary, and the function values bound to its
  parameters — its scheduler implements those — are all the ways it can
  suspend. `suspend<serve>` names the type of a suspended activation of
  it: a fixed-size struct holding the parameters, the locals of its whole
  spliced call tree that live across a suspension, and where it is parked
  (4.1). `ServeTask { conn }` is a task that has not run yet (4.2).
* **One builtin drives it.** `resume(task)` consumes a suspended task and
  runs it to its next suspension or to its end. The statement that parks it,
  `suspend { task => … }`, lives only in the scheduler's handlers:
  parking is the handler's
  own act, and the block takes ownership of the newly parked task. Nothing
  crosses a suspension as data: the coroutine's protocol with its
  scheduler is the types of its function-value parameters, and what an
  earlier draft called `Wait` — which fd, which deadline — is already an
  argument the coroutine passes and book-keeping internal to the handler
  that receives it (2.7).
* **Everything else is a library**: event loops, timers, channels,
  cancellation, structured concurrency, `select`, `run_blocking`,
  generators-as-iterators (4.13, 5). The language supplies the state
  machine and a value to put it in; nothing in it allocates, locks, or
  calls through a pointer.
* **Zero cost otherwise.** No runtime state, no thread-local, no change to
  how an ordinary function is compiled or called. A program that never
  writes `suspend<…>` compiles to the same C it does today.
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
   cross a suspension plus a tag, no more, with nothing per resume beyond a
   call and a switch.
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
of its run and returns control to whoever resumed it; that resumer goes on
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
a suspension point, and the same answers for the programmer: declare the
scratch buffer in a block that ends before the suspending call, or own the
growable data outside and pass it in by reference or as a pool index.
§7.8's advice for
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
F(x) } }` called with a block whose calls reach a `suspend { }` is, in that
specialization, code that can suspend: its locals (here, the loop index)
cross the suspension like the coroutine's own, and nothing about `each`
needs to be declared for it. Kotlin needs `inline fun` to get this; Goose
has it already, and it is what keeps the standard library's HOFs usable
from coroutines without a second, colored copy (3.4, 3.10).

This section is the load-bearing one now. The coroutine is itself a HOF
(4.1), and every way it can suspend is one of the function values bound to
its parameters — scheduler code, spliced in here, so that a `suspend { }`
in a handler is a suspension inside the coroutine's own specialization.
The frame covers the handler's live-across-suspension locals as readily as
`each`'s loop index (4.5, R3), and nothing outside the coroutine's tree
knows or cares.

### 2.5 Threads share nothing, so thread-per-core is the shape

Goose workers never share mutable memory, each thread program is an
instance with its own globals, and only flat values cross through typed
queues (§11.2). This decides how coroutines and threads combine:

* A coroutine runs on the thread that resumes it and shares that thread's
  memory freely, since only one thing runs on a thread at a time and
  switches happen only at `suspend`. No data race is possible, and no rule
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

A coroutine cannot own a `u8[>..]` across a suspension (2.1), and a server has
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

### 2.7 The scheduler protocol is the function-value arguments

Rust's `Future::poll` receives a `Waker`, a (data pointer, vtable) pair that
the leaf future stores and calls when the task should be polled again. C++
hands `await_suspend` a `coroutine_handle`, resumed through a function
pointer in the frame. Kotlin's `Continuation` has a virtual `resumeWith`.
Goose has no runtime function values (§7.6), so a coroutine cannot hand
anyone a callback at run time. It does not need to: the entity that resumed
it is the one that will resume it again, and it already knows which task it
holds.

An earlier draft of this note answered the remaining question — *what is
the coroutine waiting for* — with a typed value through `yield`, such as
`Wait.Readable { fd: fd }`, and an `Event` back through `resume`. Under
HOFs-as-coroutines (4.1) a type like that turns out to be unnecessary, for
two reasons. First, what the coroutine waits on is exactly what it already
passes to the functions it calls: the fd and flags of
`await_non_blocking(fd, flags)`, the deadline of `wait(time)`. Second, the
functions those arguments arrive in are not the far side of a protocol:
they are scheduler code running *spliced into* the coroutine (2.4),
written next to the loop's own bookkeeping. The loop learns an fd by
watching it in `await_non_blocking`'s own body, a few lines from the `suspend`
that parks its caller; a type naming the reason for a parking would
describe data that never leaves that function. So the design defines no
`Wait` and no `Event`: the protocol is the types of the coroutine's
function-value parameters, declared once by the coroutine's author and
implemented once by the scheduler (4.3).

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
  a suspension (4.5, R4). The frame is then an ordinary value, copyable and
  movable, and the thing that moved into Rust's type system never arises.
  The price is that `let s = buf[..n];` with `use(s)` still to come cannot
  cross a suspending call; the buffer is owned outside, which the server
  shape wants anyway (2.6).
* **The waker is dynamic dispatch**, and its ownership rules
  (`Send + 'static` tasks for work-stealing executors) drove the
  thread-per-core debate ([boats, "Thread-per-core"][boats-tpc];
  [glommio][glommio]). Goose's protocol is its coroutine's function-value
  arguments (2.7), and threads already share nothing.
* **Cancellation is dropping the future**, with no async `Drop`
  (experiments only, [2025 goals][rust-goals]); a future dropped between
  polls loses its partial progress, hence tokio's documented
  ["cancellation safety"][tokio-cancel] hazards around `select!`. Goose
  has no destructors and a frame owns no growable data, so dropping a
  frame loses nothing but the frame; the resources it names are in pools
  the loop frees. Cooperative cancellation is a handler's result (4.10).
* **Two ecosystems and coloring.** [Nystrom's "What Color is Your
  Function?"][colors] names the split; sync and async libraries duplicate
  each other; async in traits took until 1.75 ([AFIT][afit], 2023) and
  `dyn` async traits are still open ([2025 goals][rust-goals]); [boats,
  "Let futures be futures"][boats-futures] and the [async vision
  document][async-vision] record the runtime fragmentation. Goose colors
  even less: nothing in a coroutine's body says it suspends (4.1), HOFs are
  suspend-transparent (2.4), no traits exist to color, and any ordinary
  function can drive a task with `resume` (4.3). The library advice that
  came out of Python, [sans-I/O][sansio] (protocol code that works on
  bytes and never does I/O), is the right default for Goose libraries:
  the waiting lives in the scheduler's handlers (4.13).
* **Readiness versus completion.** Rust futures borrow their buffers, so
  `io_uring` and IOCP, which need the kernel to own the buffer while the
  operation is in flight, required new APIs (tokio-uring, monoio). Goose
  buffers live in pools, so a scheduler handler can hand the kernel a slice
  that stays valid while the frame is parked (5).
* Later additions worth knowing: async closures (stable in 1.85, 2025);
  return-type notation and `#[coroutine]` generators with resume
  arguments, both still unstable. The resume argument is the closest thing
  in Rust to Goose's handler arguments (4.1).

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
notes][zig011]). Lesson: name the coroutine where it is used (`suspend<F>`,
4.1) rather than inferring suspension from a body, and make its frame
depend on the tree that actually runs, with the bound function values
spliced in (4.4).

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
  exception), and [PEP 380][pep380]'s `yield from` desugars "call another
  coroutine and forward its yields" — 4.4 needs no equivalent, since a
  callee is spliced into the caller's machine. Frames are heap objects.
* **Lua** coroutines are stackful, asymmetric and first-class, and the
  cleanest small API: `create`, `resume`, `yield`, `status`
  ([manual][lua]). 4.3's shape — resumed from outside, parked from inside
  the body — is Lua's.
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
cross a suspension into the struct and reloads them, and it checks that
nothing else was needed. That is the entire "convenience" of the feature, and it
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
| One `resume` in, one `suspend` out, no values between | C++'s protocol soup | 4.3 |
| Protocol logic sans-I/O; only waiting is colored | Python, Rust ecosystems | 4.13, 5 |
| Thread-per-core, share-nothing, pools own buffers | Seastar, glommio, monoio | 2.5, 2.6, 5 |
| HOFs suspend transparently | Kotlin `inline` | 2.4, 4.6 |
| Frames as values can be copied, sent, stored | Protothreads, embassy | 4.9 |

## 4. The design

Syntax is a placeholder throughout; section 6.9 lists the alternatives.

### 4.1 A coroutine is an ordinary function

```goose
fn serve<
    inherited await_non_blocking: fn(fd: i64, flags: i32) -> bool,
    inherited wait: fn(time: i64),
    inherited close: fn(conn: Conn&),
>(conn: Conn&) {
    defer close(conn);
    …                            // an ordinary body; nothing in it names a scheduler construct
}
```

Coroutines are not declared. The coroutine is an **ordinary function**
whose function-value parameters happen to be implemented by code that
suspends (4.3). It is declared like any function: fully typed parameters
(as `thread_fn` and cycle functions require, §7.8, §11.2; the frame's
layout depends on them, 4.11), optional result types after `->`,
generics, defaults, overloads, namespaces, UFCS. Its body is an ordinary
body: plain calls, `defer`, `return`, blocks — and no suspension of its
own. The scheduler's handlers are `inherited` parameters (§7.6), and the
binding semantics are the ones that keyword already has: a call draws a
handler from the scope of the call, so `serve`'s `fill(conn)` binds
`fill`'s `await_non_blocking` to the definition — or the parameter — of that
name in `serve`'s scope, and a middle layer with an `inherited` parameter
of the same name passes its binding on. A call may still supply such a
value explicitly, as §7.6 values are; `inherited` only makes the scope
supply it by default.

The **type** names the coroutine and binds it: `type ServeTask =
suspend<serve>;` typechecks a *start*, the way `serve(args)` typechecks a
call. Writing `suspend<serve>` binds `serve`'s `inherited` parameters from
the scope of the instantiation, exactly as §7.6 resolves a call's — without
definitions of `await_non_blocking`, `wait` and `close` there, `suspend<serve>`
is a compilation error, as a call that does not supply all arguments is.
That is why a concrete task is always instantiated in its scheduler:
the type cannot be named anywhere the handlers are not in scope. The type
it produces holds:

* **A fixed-size struct**: the parameters as fields (readable from
  outside, writable only if declared `var`, as `let` fields are, §3.2),
  the results (if any), the locals of the whole spliced call tree — the
  function, its callees, the handlers — that are live across a suspension
  as hidden fields (4.5, R3), and a hidden tag saying where the activation
  is parked. It has the size class, flatness, root bindings and
  copyability that a struct with those fields has (4.8, 4.9).
* **A specialization**: the layout follows from the parameters, the body
  *and* the bound values (4.11), so two `suspend<serve>` instantiations
  whose scopes bind different handlers are different specializations.

The results are fixed-size. A coroutine that produces growable data
appends it to a buffer it is given, as a cycle function does (§7.8); it
cannot be a `[>..]` result built at the resumer's destination, since the
resume that completes it is not statically known from the ones that do
not (6.8). A coroutine's entry is not `extern`, `export`, `thread_fn` or
`recursive`; `checkfree` and `simd` apply as to any function; `inline`
means nothing, since what is spliced is the whole activation.

### 4.2 Starting: a literal, and nothing runs

```goose
type ServeTask = suspend<serve>;                    // binds the handlers from this scope (4.1)
var task = ServeTask { conn };                      // a task that has not run yet
resume(ServeTask { &conns.take(Conn { ev.fd }) });  // built and started at once by the loop
```

A task is constructed by the struct literal of its `suspend<F>` type: the
coroutine's parameters positionally or by name, defaults as field
defaults, built in place like any struct (§4.2, §4.3). Nothing of the body
runs — the literal only stores the arguments and a zero tag. This is
Rust's and Python's laziness, chosen because it is also the thinnest: no
code can run that the owner of the task did not ask for; the scheduler's
loop starts a task by resuming a fresh literal (4.3). The handlers were
already bound where `suspend<serve>` was written (4.1), so the literal
binds nothing further: it is a plain struct literal whose type happens to
know how to run. The parameter fields are readable from outside
(`task.conn`), and writable only if declared `var`, as `let` fields are
(§3.2).

### 4.3 Resume and suspend

```goose
resume(tasks.release(f.task));   // runs to the task's next suspension point or to its end
```

* `resume(f)` runs a task in any form it takes — a variable, the value a
  pool's `release` hands back, a freshly built literal — and **consumes
  it**: the value becomes the running activation, which is why the
  scheduler releases a task out of its pool to resume it (section 5). It
  runs from where the task is parked, or from the initial state of a
  literal that has not run yet, to the end of the body or to the next
  executed `suspend { }`. `resume(f)` does not return a value.
* `suspend { task => … }` — the statement that parks, in the scheduler's
  handler: the activation stops on it, the entire call tree's live locals
  are stored (R3), and the block runs once, right there, with `task`
  bound to the freshly parked task — the frame, its tag set — whose
  ownership passes to the block's body. Then `resume` returns to
  its caller. The block's free variables are the handler's declaration
  scope (§7.5), so the body registers the task against the fd or the
  deadline the handler was called with, exactly as section 5 does. When
  the task is next resumed, execution continues with the statements after
  the block, and the handler then returns to its coroutine caller like
  any completed call. Nothing is handed *back* through a suspension: what
  the coroutine learns arrives as the handler's return value, and what
  the scheduler learns arrived as its arguments (2.7).

The suspend block is checked per binding (4.11): `task` has the
`suspend<F>` type of the coroutine whose tree binds the handler, so a
handler whose block records a specific task type can be bound only under
that coroutine, and one whose block merely forwards `task` (a `wait`, say)
works under any of them. `resume` is a builtin so it can be specialized
per task type without a declaration; by UFCS, `task.resume()` works too
(§7.1).

### 4.4 Calling: the activation is a whole call tree

```goose
fn read_line<
    inherited await_non_blocking: fn(fd: i64, flags: i32) -> bool,
>(c: Conn&, out: u8[..4096]&) -> bool { … }   // an ordinary fn, like fill

// in serve's body:
while read_line(c, c.line) { handle(c.line[..]); }   // a plain call
```

A call from a coroutine's body is a plain call, and it stays part of the
same activation. If the callee — directly, or through another callee —
reaches a `suspend { }`, the park stops the *one* running task, and the
parked frame carries the callee's live locals as readily as the caller's.
The callee's `inherited` parameters draw their values from its call scope
(§7.6): `read_line`'s `await_non_blocking` is bound to the definition — or the
parameter — of that name in `serve`'s scope, so the scheduler's handler
reaches any depth without being written at each layer. This is Rust's
`.await` and Python's `yield from` with no keyword and no protocol type,
and Kotlin's suspend call with no coloring: nothing in either body marks
this out, and the same functions run to completion under a scheduler
whose handlers never suspend.

The live locals of the whole tree belong to the one frame, so a task's
frame is sized by its whole call tree, statically. That is why a
coroutine's tree cannot be recursive: there is no bound. A coroutine *can*
call ordinary recursive functions between suspensions, and code outside a
`suspend<F>` activation cannot reach a `suspend { }` at all (R7). Whether
a callee's code is spliced into the caller's segments (one tag, one
switch) or kept as a separate resume function with a tag of its own (one
extra switch per level per resume) is an implementation choice per call
site, like inlining; the community sketch measured the difference at 45%
on a patrol benchmark (section 7), so the spliced form is the default and
the nested form is for large callees with many callers.

### 4.5 Rules at a suspension point

A *suspension point* is an executing `suspend { }` statement. What the
rules govern is everything the park reaches past: the statements whose
scope or temporaries would still be live while the tree is parked and
resumed elsewhere. Since the park happens inside a callee (4.4), the
coroutine-visible form of a suspension point is the **suspending call** —
a call whose spliced tree reaches a `suspend { }`, in the coroutine's body
or in a block it passes to a HOF (4.6) — and R1 and R2 apply to it as they
apply to a call into a recursive cycle (§7.8), with the callee's park in
the role of the call into the cycle. The checker sees every suspending
call once the handlers are bound (4.1).

* **R1. No nonfixed local in scope.** §7.8's rule, verbatim: no variable-
  or resizable-class `let`, `var`, by-value parameter or by-value `match`
  payload may be in scope at a suspension point, nor may one whose
  initializer contains it. Declare scratch in a block that ends before
  the suspending call, or own the growable data outside and pass it in.
* **R2. No nonfixed temporary live**: §7.8's cases (a) to (d), with the
  suspending call in the role of the call into the cycle. So
  `print(str(n), conn.fd.await_non_blocking(net::READ))` is an error and
  `let s = str(n).len; conn.fd.await_non_blocking(net::READ);` is not. Nor may
  a nonfixed *result* be pending: its destination stack is in use from
  the call until the value arrives (§10.3), and the arrival would be in a
  later resume, possibly from another resumer with other stacks in use. A
  coroutine has fixed results (4.1), so this concerns calls inside blocks
  and handlers where a suspension nests under a call returning a nonfixed
  result: `each_rev(xs) { conn.fd.await_non_blocking(net::READ); }` is fine,
  `map(xs) { conn.fd.await_non_blocking(net::READ); … }` and
  `exception::try { conn.fd.await_non_blocking(flags); … }` (whose `handler`
  returns an `E[]`) are errors.
* **R3. What crosses is stored.** A local declared before a suspension
  point and named after it is live across it and becomes a hidden field
  of the frame. The test is syntactic, as the shrink rules' liveness test
  is (§5.1): any mention after the suspending call in the local's scope
  counts, loops count their whole body, and a block or nested function
  that names the variable counts. "Named after" is decided per
  suspension, so a local that dies before a later park does not widen
  that park's state. The same holds for the locals of every spliced
  callee and handler between the call site and the `suspend` (2.4): each
  joins the frame, `each`'s loop index as readily as `serve`'s own.
* **R4. No reference into the activation's own locals crosses.** A park
  ends an activation's run: the locals that are live after it are
  reloaded from the frame, in a different place, in a segment of some
  resumer's choosing. So a reference or slice rooted at a coroutine's
  local or parameter copy, or at a temporary of a suspending call's
  statement, may not be live across it. This is the outlives rule (§9.1)
  with the park as a scope exit, and it is checked as a return of such a
  reference is. References rooted *outside* the frame (a pool, a global,
  a reference parameter's pointee, static data) cross freely: they are
  fixed values. This rule is what makes the frame a plain value (no
  `Pin`, 3.2) and an ordinary holder (4.8). Section 6.4 discusses
  relaxing it.
* **R5. A `defer` may be live across a suspension.** Tasks are consumed
  by `resume()`, but the scheduler will always be required to take back
  ownership except when the coroutine runs to completion. This implies
  that a task will always be run to completion (which will run `defer`s
  normally) or will be cancelled (which run `defer`s exclusively, §4.10),
  so a `defer` live across a suspension will never be dropped.
* **R6. Narrowing survives, flow state is the body's.** An optional
  narrowed before a suspension is narrowed after it: it is a `let` or
  `var` of the body, saved and restored, and nothing but the body writes
  it. (A `var` parameter is writable by whoever holds the task between
  resumes, so it is not narrowed across one.) Definite assignment is
  checked on the body as a whole, as for any function.
* **R7. Where a park may be.** A `suspend { }` may stand only on the
  scheduler's side of the splice: in a function bound to an activation's
  `inherited` parameters or called from one (4.1, 4.4) — in the design's
  shape, the scheduler's handlers and their helpers. Not in the
  coroutine's own declarations, a global initializer, a default, or a
  `defer` body; not in a function of a recursive cycle (whose frame has
  no bound, 4.4); not in code outside any `suspend<F>` activation, which
  would park a task nobody holds. By the same token, `resume` may not be
  called from any function whose own call graph reaches a `suspend { }`:
  an activation does not start another one, because a nested activation's
  saved state would have to be captured inside the parking task's frame.
  The scheduler's loop calls `resume`; its handlers never do.
* **R8. From the coroutine's side, nothing happens.** A suspension has no
  expression and no value, and no syntax of its own in the coroutine's
  body: a suspending call reads exactly like a slow call, and the flow
  between suspension points is the coroutine's own. The only code that
  observes a park is the scheduler's — the `suspend { }` statement and
  whatever the handler runs after it, which executes in the resumed
  segment. Evaluation order is left-to-right everywhere (§2), so a
  suspending call in `print(a, await_non_blocking(x))` parks only after `a`
  has been evaluated — which is exactly why R2 refuses to let a nonfixed
  `a` be there.

Everything else a function may do, a coroutine may do between two
suspensions: grow pools and buffers through references, declare and use
scratch buffers in inner blocks, call anything, `return … from` within
the activation (4.7), spawn workers, block on `qget` (a logic hazard, not
an error).

### 4.6 Blocks, HOFs and nested functions

A block written in a coroutine body is spliced into whatever function it
is passed to (§7.6), and that specialization is part of the activation: R1
to R4 apply to the HOF's own locals at the block's suspending calls as
they would to the coroutine's, and its locals that cross join the frame.
So `each_rev(xs) { guard fill(conn) else { break; } }` works with std's
ordinary `each_rev`, whose only local across the suspension is its loop
index. A HOF used this way needs no marker of its own: whether a call in
its body can reach a `suspend { }` is a property of what is bound at each
call site, not of its text. The generic wait is therefore an ordinary HOF
over a predicate and a parking handler:

```goose
fn wait_until<inherited park: fn()>(pred: fn() -> bool) {
    while !pred() { park(); }     // the scheduler's park handler; its park joins this frame
}
```

called from inside an activation as `wait_until { ready(conn) }`, with the
scheduler's park drawn from scope (§7.6). A block whose calls reach a
suspension can only be passed to a function whose own locals and results
obey the rules at that point (R1, R2: fixed results, no nonfixed local in
scope), which the specialization checks; such a block cannot be passed
into a recursive cycle (R7). `return` inside such a block returns from the
lexically enclosing coroutine, as it returns from the enclosing named
function today (§7.6).

A nested `fn` declared in a coroutine body is on the coroutine's side of
the splice: it may call the handlers, its parks join the frame (4.4), and
it may contain no `suspend { }` of its own (R7) — parking is defined
exactly once, by the scheduler, and every park goes through it.

### 4.7 `return … from`, `defer` and the other exits

* `return` from a coroutine body finishes the task. Any return value is
  dropped.
* `return E from f` where `f` is the coroutine itself, a callee of it
  (4.4), or a function inlined into the activation by a block (4.6)
  works as today: these frames are on the native stack whenever the body
  runs, since activation code only ever runs through a `resume()` of its
  task. The thread-local discriminant (§C.3) is set only
  between the throw and the catch inside one `resume`. A target *outside*
  the activation (the function that built the task, or the one resuming
  it) is an error: it is not on the compile-time path from the
  `resume` to the body, which is the condition §7.9 already checks.
  `exception::try` as written cannot wrap a suspending call, because its
  `handler` returns a nonfixed `E[]` (R2); a coroutine-friendly `try` with
  a fixed-size error slot (an ADT with a "none" variant) would be a small
  library addition.
* `defer` may be live across a suspension (R5), stored in the frame, as
  section 5's `defer close(conn)` is. `guard`, `block`, `break`,
  `continue` and loops are unaffected: a suspension inside a `for` over a
  slice keeps the index; a `for` over a fixed array local of the coroutine
  iterates by index over the hoisted array, as a `for` over a resizable
  already does (§6.5).

### 4.8 Lifetimes: the frame is a holder

A frame type is a struct with reference fields wherever its parameters or
hoisted locals are references, slices, or values holding them. So it is
"implicitly generic over those fields' roots" (§9.2) and checked as any
such struct: storing a frame into a pool, a local or a global is a store of
what it holds, under the store rule; a frame parked in the scheduler's
pools holds only references that outlive them (the scheduler's other
pools, static data), which is the server's shape (5); a frame built from a
reference to a local of
`main`'s loop can live in that loop's locals and nowhere deeper. The
read-back rules (§9.5) give a frame read out of a pool the roots of the
pool's candidates, and `in pool` links stay exact wherever the frame is
(§3.9), which is why section 5's tasks reach their `Conn` through a
`slot_pool`.
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
  `print` renders the parameters and the tag; `default<suspend<serve>>()`
  exists when every parameter has a default, and is the unstarted frame.

### 4.10 Finishing and cancelling

* **Finished.** The `resume` that runs the body to its end hands the
  results back and returns `false` (4.3), and the task value is gone: it
  was consumed by that resumer, and no code ran to park a new one. There
  is no finished task value to resume by mistake — the checked abort of
  the old design's finished frame has nothing left to name it.
* **Cancellation** is a built-in: `cancel(task)` consumes and terminates
  the coroutine, running no code except any active `defer`s.

### 4.11 Generics and checking order

A generic coroutine function `fn step<T>(…)` is monomorphized like any
function. But a `suspend<F>` type is per **binding**, not per function:
the hoisted locals follow from the spliced tree — `F`'s body, the
function values bound to `F`'s `inherited` parameters, and theirs in turn
— so the same `suspend<serve>` written under a scheduler whose handlers
watch one fd and one whose handlers park on timers are different
specializations with different layouts, like two calls under two
different function values (§7.6, §10.2). Checking an instantiation
therefore checks the tree against its binding where the type is first
used (a literal, a field, an array element) — which is why the type is
written where the handlers are defined, 4.1 — and each `resume` site
then specializes the tree for real, as a call of
`fn resume(f: own ServeTask)` with the frame's roots,
writability and `in pool` provenance, so that the body's stores and
shrinks are checked for the storage that frame actually names.
Two frame instances with different root bindings are distinct for
checking and identical in layout, as reference-holding struct instances
are (§9.2).

### 4.12 Lowering to C, and what it costs

The frame is a packed C struct; the task is one C function with a `switch`
over the tag that jumps to the label after the `suspend { }` it was parked
at (the Protothreads lowering, 3.8, which is legal C: a `goto` may enter a
loop body, and the generated C already declares every aggregate local at
the top of the function, implementation.md §6.7). Sketch for section 5's
shape, where the only park in `fill`'s slice of the tree is the spliced
`await_non_blocking`, and `fill`'s `n` and the handler's `fd` are live across
it:

```c
typedef struct serve_g {              // parameters, live set, tag
    uint32_t conn_g;                  // the pooled Conn the task was started with
    uint8_t gs_tag;
    int64_t n_g, fd_g;                // live across the park: fill's recv count, the fd
} serve_g;

static void serve_resume(serve_g *f, int64_t gs_sp) {
    int64_t n, fd;
    switch (f->gs_tag) {
        case 0: goto L0;                            // the initial state
        case 1: n = f->n_g; fd = f->fd_g; goto L1;  // reload what is live after the park
    }
L0: for (;;) {
        n = net_recv(f->conn_g, …);                 // fill's own code is straight-line
        while (n == AGAIN) {
            fd = f->conn_g;                         // conn.fd.await_non_blocking(READ), spliced:
            net_watch(fd, WANT_READ);               //   its statements before the suspend
            f->n_g = n; f->fd_g = fd; f->gs_tag = 1;//   save what is live after the park
            by_fd[fd] = status(tasks_take(f));      //   suspend { task => by_fd[fd] = … }:
            return;                                 //   parked
        L1:                                         // after the suspend block, when resumed:
            by_fd[fd].task = NULL;                  //   the handler's tail
            if (!by_fd[fd].ready)                   //   its return; fill's guard acts on it
                goto finish;                        //   (fill false -> serve breaks -> defer close)
            n = net_recv(f->conn_g, …);
        }
        …
    }
finish:                                             // the body has run out: results, done
    // defer close
}
```

* **Copy in, copy out.** Between two parks, the locals are C locals of
  the resume function, so the C compiler allocates registers for them as
  it would in any function; a park stores the ones live after it and a
  resume reloads them. The frame is not addressed in the hot path, which
  also keeps the C compiler's aliasing worries (implementation.md §6.2)
  out of the segment. R4 is what permits this.
* **Overlap.** Two hoisted locals never live at the same park can share
  a slot (Rust's layout, 3.2). The frame is then parameters + tag + the
  largest live set over the parks, not the sum. v1 can lay them out
  without overlap and add it later; the C never changes shape.
* **The handlers and any called function** (4.4) are spliced in as a
  block of the caller's resume function with their parks numbered among
  the caller's (one tag), or compiled as a resume function of their own
  called from the caller's segment, with its own tag in a nested frame
  struct — the two forms the earlier draft gave called coroutines. The
  optimizer chooses as it chooses inlining today (implementation.md §4);
  since every callee, handlers included, is inlined per binding (§7.6),
  the common case is one function with one tag.
* **Data stacks.** The resume function takes `gs_sp` like any function
  and builds its between-park temporaries above the resumer's watermark;
  by R1 and R2 nothing of the activation's is on a data stack at a park,
  so a park restores nothing. `NoStackAcrossCycleCall`'s assertion
  applies to parks unchanged.
* **The resumer and the parked.** `resume(f)` is a direct call of the
  task type's resume function, inlined into the scheduler loop by the
  existing policy; the suspend block compiles to inline stores and
  bookkeeping just before that return. No thread-local, no runtime
  function, no table.

What it costs, per operation:

| Operation | Cost |
|---|---|
| start (`ServeTask { … }`) | the parameters stored, a zero tag |
| `resume` | one direct call (often inlined), one switch, the loads of the locals live at that point |
| `suspend { … }` | the stores of the locals live after it, a tag store, the block body inline, a return |
| finish | the result stores, a return |
| a called function | nothing per call when spliced; one extra switch per level per resume when nested |
| frame size | parameters + the largest set of locals live across one park (+ result) + 1 tag byte |
| non-users | nothing: no runtime state, no hidden parameters, no change to any other function |

This is the cost of the hand-written state machine, and the community
sketch's measurement (section 7) found a hand-lowered coroutine within
10% of one when the call tree was spliced into a single tag.

### 4.13 What stays out of the language

Each of these is a library, and section 5 shows the first of them:

* **The event loop**: which fds to watch, with which kernel API, and how to
  find the task an event belongs to — all of it inside the handlers,
  which is why no `Wait` type exists: the loop's bookkeeping never leaves
  the loop.
* **Timers**, as a heap of deadlines the loop checks between events;
  section 5's `wait(time)` handler is already all of it.
* **Channels** between coroutines on one thread (a `send` handler that
  queues the value and parks the sender), and across threads (the typed
  queues, §11.2).
* **`select`**: a handler that registers the parked task against several
  fds or deadlines and resumes it from whichever fires first — the same
  bookkeeping `await_non_blocking` already does (the sketch's `await_all`, 7,
  is a handler that parks until its subtasks' results have arrived).
* **Structured concurrency**: a scope whose task is parked until the
  subtasks it built as literals — owned by its await handler once it
  parks — are finished; the outlives rule makes a task unable to outlive
  what it refers to (4.8), which is the half of structured concurrency
  Kotlin has to enforce by discipline.
* **Cancellation** policy: what a handler returns to make its caller
  unwind, and who decides it (4.10).
* **`run_blocking`**: a scheduler whose handlers never park — they block
  (`poll` with no timeout, `qget`) and return — so the same coroutine
  code runs, once, on the current stack, with no frame and no `resume`.
* **Generators as iteration**: a HOF whose `emit` handler is an
  `fn(x: T)` that stores the value where its resumer reads it and parks;
  `for x in gen` could be sugar over `resume` later.
* **Protocol implementations** written sans-I/O (3.2): a parser consumes
  slices and says "need more" or "done"; since nothing about a coroutine
  is marked, the parser serves blocking and coroutine code alike.

## 5. A server on top

A sketch of the layering, with hypothetical `net`, `http` and `slot_pool`
libraries (there are none yet; section 8 lists the network track as a
prerequisite). Names are placeholders. The point the example makes is the
split between its two halves:

* **The coroutines are written with no knowledge of how the scheduler
  will suspend.** `fill` and `serve` are ordinary functions whose three
  function-value parameters — `await_non_blocking`, `wait`, `close` — are the
  whole interface; the types of those parameters are the whole protocol
  (2.7), and the bodies call them as plain blocking calls.
* **The scheduler is written with no control of the coroutine code.**
  `worker` owns the state and the loop, and the handlers it defines in
  the scope that names `suspend<serve>` (4.1) are where every suspension
  happens; nothing of the coroutine's text is reachable from them.

```goose
import std;
import net;
import slot_pool;   // hypothetical: fixed-slot element pools; take/release/…

struct Conn { fd: i64, inbuf: u8[..4096] = [], outbuf: u8[..4096] = [] }

///////////
// coroutines library, written with no knowledge of how the scheduler will suspend
fn fill<
    inherited await_non_blocking: fn(fd: i64, flags: i32) -> bool,
>(conn: Conn&) -> bool {
    loop {
        let n = net::recv(conn.fd, conn.inbuf);
        if n != net::AGAIN { return n > 0; }
        guard conn.fd.await_non_blocking(net::READ) else { return false; }
    }
}

// flush ...                       // the same for net::WRITE

fn serve<
    inherited await_non_blocking: fn(fd: i64, flags: i32) -> bool,
    inherited wait: fn(time: i64),
    inherited close: fn(conn: Conn&),
>(conn: Conn&) {
    defer close(conn);
    loop {
        // By default, inherited function values are drawn from the scope of the call
        // site. Here we call fill(), and its await_non_blocking parameter is automatically
        // bound to the definition of await_non_blocking in this scope.
        guard fill(conn) else { break; }
        let consumed = http::respond(conn.inbuf[..], conn.outbuf);
        guard consumed > 0;
        conn.inbuf.drop_front(consumed);
        guard flush(conn) else { break; }
        if conn.should_be_throttled() {
            wait(std::now_ns() + THROTTLE_DURATION);
        }
    }
}

///////////
// scheduler, written with no control of the coroutine code
struct StatusAnd<T> {
    task: T?,
    ready: bool = false,
};

thread_fn worker(listen_fd: i64) {
    type ServeTask = suspend<serve>;

    var conns = slot_pool<Conn> {};
    var tasks = slot_pool<ServeTask> {};
    var by_fd: StatusAnd<ServeTask>[>..] = [];
    var timers: Deadline[>..<] = [];
    let timer_fd = net::make_timer();

    // The suspension handlers needed by serve are defined by the scheduler in this scope.
    // Without these definitions, `suspend<serve>` would be a compilation error just like
    // a function call which doesn't supply all arguments.
    fn await_non_blocking(fd: i64, flags: i32) -> bool {
        net::watch(fd, flags);

        // suspends execution and block takes ownership of the task:
        suspend { task => by_fd[fd] = StatusAnd<ServeTask>{ &tasks.take(task) }; }
        // execution resumes here when the task is resume()d

        by_fd[fd].task = null;
        return by_fd[fd].ready;
    }

    fn wait(time: i64) {
        suspend { task => timers.heap_push(Deadline { time, &tasks.take(task) }) { l, r => l.time < r.time }; }
    }

    fn close(conn: Conn&) {
        conns.release(conn);
    }

    loop {
        for ev in net::poll_wait(…) {
            if ev.is_new_connection {
                // resuming from the initial state
                resume(ServeTask { &conns.take(Conn { ev.fd }) });
            }

            let f = by_fd[ev.fd];
            assert(f.task != null, "pure scheduler error");
            // the file won't block anymore, store its status and resume
            f.ready = !ev.hangup;
            resume(tasks.release(f.task));
        }

        while timers.len > 0 && timers[0].time >= std::now_ns() {
            let timer = timers.heap_pop();
            resume(tasks.release(timer.task));
        }

        if timers.len > 0 {
            net::set_timer_for(timer_fd, timers[0].time);
        }

        // …
    }
}
```

The buffer is a limited array inside the pooled element, so the kernel
can write into it while the task is parked (§5.3). The example demonstrates
cleanup of a finished task; the scheduler never receives a task in a
finished state.

What this shows about the fit:

* The frame of a `ServeTask` is one reference into `conns`, a tag and the
  few locals that cross the suspensions inside `fill` and `flush` (an
  `n`, a position): tens of bytes. A million connections are a million
  elements in two pools.
* Nothing allocates, locks or calls indirectly: the handlers are spliced
  into every task they run through (2.4, 4.12), and the loop's data
  structures are arrays.
* The buffers are the pool's, so a completion-based loop (io_uring,
  IOCP) can have a handler submit `conn.inbuf`'s free slots to the
  kernel at the park and record the completion in its own tables —
  bookkeeping that never crosses the suspension, which is why no `Wait`
  type is missed (2.6, 2.7).
* Every `thread_fn` runs the whole scheduler — its pools, its timer, its
  loop — as its own program instance (§11.2), accepting on its own
  listening socket. Nothing crosses workers until the program wants a
  queue (4.9).
* The protocol logic (`http::respond`) is uncolored and testable without
  a socket — so uncolored that `serve`'s body never mentions waiting:
  the same code runs unchanged under a blocking scheduler whose handlers
  never park (4.13).

## 6. Options, with pros and cons

Each part of section 4 had alternatives. The recommendation is marked.

**6.1 Stackless (recommended) / stackful / dynamic stacks.** Stackless is
forced (2.3). Stackful is ruled out by the data-stack model. *Dynamic
stacks* (§11.3, a task owning a block of data stacks, switched by setting
`gs_stks`) would lift R1 for a bounded number of heavyweight tasks and is
the community sketch's suggestion for later; it costs address space per
task and a stack switch per resume, and it should be an opt-in on the
declaration (`suspend<F> … with stacks`), never the default. Not in v1.

**6.2 The task type: `suspend<F>` (recommended) / a frame named by the
function / a user struct.** `suspend<F>` is as close to naming the frame
after the function (Zig's `@Frame(f)` in spirit, 3.6) as the HOF shape
gets: one type expression, bound where it is written, giving the type a
name to put in pools and fields, a literal to start a task with, and
fields for the parameters. It is not a new type-system concept, but it
is a new type *former* — and it checks its binding like a call (4.1)
rather than by a declaration's color, which is what removed Zig's 0.10
design once its inference broke (3.6). Naming a frame type after a
`coro fn` declaration — the earlier draft — gives the same conveniences
but requires a colored declaration, which this shape does not want. The
Protothreads form, where the user declares the state struct and the
compiler only checks that nothing else crosses a suspension, is the
thinnest of all and keeps the layout entirely in the user's hands (packed
fields, `varint`s), but it gives up the one convenience the feature
exists for (3.8); it remains available as a style, since an activation
whose locals never cross a suspension has a frame of just its
parameters. A `--frames` listing (section 8) tells the programmer what
was hoisted.

**6.3 No suspension values at all (here recommended) / typed yield and
resume values.** The earlier draft passed a typed `Wait` through each
`yield` and an `Event` back through `resume`, so a loop with 10⁵ tasks
could learn what each waits for "without asking" (2.7). The HOF shape
asks a different question: the loop never asks anything, because its own
handler code is running inside the coroutine when the fd or the deadline
arrives as an argument, and the bookkeeping stays in that handler's
locals (2.7). Typed values through a suspension stay the right tool
where coroutine and resumer are two programs that must communicate at a
boundary — which is no longer this shape — and what a typed yield was
kept for. A protocol that wants values anyway can have a handler take
a `msg: T` parameter; the protocol then simply is that parameter's type (6.7).

**6.4 No self-references across a suspension (recommended) /
self-referential, non-copyable frames.** The sketch chose the second:
build in place, never copy, and the outlives rule keeps references to
the frame's own locals valid. Goose can express "fixed but not copyable"
already (a value holding self-relative references, §3.9), and a
reference into the frame's own locals *is* self-relative in nature. That
form allows a slice of a local (`let s = local[..n]; use(s)`) to stay
live across a park, and it costs: the frame cannot be copied, popped,
sent or serialized, the locals must live in the frame in place
(addressed through `f->` in the hot path), and the layout cannot overlap
slots whose addresses were taken. R4 keeps the frame a plain value and
the hot path in registers, at the price of one error message that points
at the fix. A later relaxation can keep both: a local whose address
crosses a suspension lives in the frame in place, and references to it
are rooted at the frame's
storage (exact when the frame is a variable, the pool's when it is an
element), while the frame stays copyable because TODO 16's "whole-region
copy" is exactly what copying a frame is.

**6.5 Lazy start (recommended) / run to the first suspension at
construction.**
Kotlin and C++ (by default) run eagerly to the first suspension. Lazy is
thinner (a literal), gives the owner control over when work happens, and is
what Rust's experience endorses; the cost is that a coroutine that wants to
do something immediately needs one resume, which a library `start` does.

**6.6 Spliced call trees (recommended by default) / nested frames.** 4.4
and 4.12. Splicing duplicates a callee's code per call site, like
inlining, and gives one switch; nesting shares code and costs a switch per
level. Let the optimizer decide by size, as it does for calls.

**6.7 No `Wait`/`Event` anywhere; the handler types are the convention
(recommended) / a std `Wait`/`Event` pair.** The earlier draft kept a
conventional `io::Wait`/`io::Event` pair in std so library coroutines and
loops could interoperate, accepting that a coroutine is tied to the
`Wait` type it constructs. With the protocol moved into the parameters
(2.7), the question changes shape: a library coroutine binds to whatever
functions the embedding scope defines, so one `serve` runs against an
epoll loop, an io_uring loop, a game tick or a blocking scheduler without
a line changed in it, and there is no `Wait` type for two libraries to
disagree about. What is lost is ad-hoc interop between a coroutine from
one library and a loop from another whose handlers have different
signatures; that is now a plain adapter function, which is what the
`Waker` machinery was already doing with more types.

**6.8 Fixed-size results only (recommended) / nonfixed results.** A
`[>..]` result is built at its destination by the call that produces it
(§7.3); for a coroutine, that call is one of many `resume`s, and the
destination would have to be reserved across all of them. The spec's
idiom for recursion (build into a buffer passed by reference, §7.8)
covers it. Revisit if real code wants `fn lines(…)` as a coroutine start
returning `u8[>..]`.

**6.9 Syntax.** `suspend<F>` for the task type is Zig's `@Frame` spelling
with a keyword; `parked<F>`, `task<F>` and `frame<F>` are the obvious
alternatives. The `suspend { task => … }` statement reads like the block
sugar of §7.6, so one keyword does duty as a type former and a statement;
a `park { … }` statement would split them. `resume` as a builtin (like
`thread_spawn`/`thread_wait`), reached by UFCS as a member. `await` as a
keyword is deliberately absent: a call is a call, and there is nothing
left for it to await — suspension lives in the handlers.

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
| suspension carries | nothing; the resumer polls a predicate each tick | nothing either, and there is no field to carry it: the protocol is the coroutine's function-value parameters, implemented by the loop (2.7, 6.3), so a loop with 10⁵ tasks learns what each waits for inside its own handler code — running inside the coroutines |
| frame | `co<f>`, built in place, never copied, not flat | a `suspend<F>` struct, copyable and flat when its fields are (4.9, 6.4) |
| references to own locals across a suspension | allowed, hence non-copyable | forbidden (R4), hence a plain value and a register-resident hot path |
| threads | a coroutine cannot cross | a flat frame can be queued to another worker (4.9) |
| blocks in HOFs that suspend | open question | allowed; the HOF's specialization joins the activation (4.6) |
| `return … from` | open question | within the activation's call tree, as today; never past `resume` (4.7) |
| generators | open question | the same machinery, an `emit` handler that parks (4.13, 9) |
| game-frame use | the example | works unchanged: a per-tick scheduler's park handler parks every task that calls it, and the loop resumes them all next frame |

Its open question "is a fixed but not copyable kind of value new to the
type system" is answered by R4 (nothing non-copyable is needed) and, for
the relaxation of 6.4, by §3.9's values with self-relative references.

## 8. Steps

1. **Settle the spec text.** A new §7.15 (or a section of its own) with
   4.1 to 4.11 as rules, R1 to R8 numbered, placeholder syntax decided,
   and `suspend<F>` and the suspend statement added beside §7.6's
   inherited parameters. Add TODO items for 6.1, 6.4's relaxation and
   6.8.
2. **Measure the server shape by hand**, as the sketch measured the patrol:
   a hand-lowered C echo loop over 10⁵ pooled frames with a `poll`-based
   reactor, in `bench/coro/`, against the same program in Rust/tokio, Go
   and plain C with `epoll`. This fixes the target before anything is
   built and tests the claim of 1.1 that the per-event cost is a switch and
   a few loads. Replicate the patrol numbers on this machine too.
3. **Checker.** Parse `suspend<F>` and the `suspend { task => … }`
   statement, and turn each `suspend<F>` instantiation into a
   specialization whose inherited arguments come from its scope (§7.6
   already draws them like this for calls); the task type as a struct
   instance built from the parameters plus the hoisted locals (R3's
   syntactic liveness, reusing the shrink rules' `blockpos` scan); R1 and
   R2 by reusing `CycleCallTemps`, `CheckCycleInit` and `Node::nftemp`
   with suspend-reaching calls as the marked calls; R4 as the return
   check on roots at each suspension; R5 to R8 as the simple rejections
   they are; the `resume` builtin and the suspend statement specialized
   per task type and roots (4.11); error messages that name the local and
   the suspension.
4. **Codegen.** The task struct beside the other generated types; one
   resume function per task instantiation with the switch and labels,
   copy-in/copy-out at the segment boundaries and the suspend block
   inlined into the parking segment; splicing of called functions first
   (it needs no second tag), the nested form after, chosen by the
   optimizer's inlining policy; `gs_sp` and the watermark
   discipline unchanged. `--frames` listing each frame's size and hoisted
   locals, and the LSP's hover showing the same.
5. **Tests.** A corpus entry per rule (each error message), the patrol and
   the echo loop as samples, a generator, a suspending block through std's
   HOFs, the R2 rejection of `exception::try` around a suspending call, a
   task through a queue, a `from_bytes` of a task pool, and a resumed
   copy of a running task aborting on its "running" tag in debug.
6. **Networking track** (independent of 1 to 5, and a prerequisite for the
   server): sockets, nonblocking I/O and a readiness poll in `os` or a new
   `net` module as `extern fn`s over the runtime, portable (`poll`) first,
   `epoll`/`kqueue`/IOCP next, `io_uring` when the completion-shaped
   protocol of 5 is wanted.
7. **The reference loop in std**: the handler set of section 5 — a
   reactor with timers, `select`-style bookkeeping, `run_blocking` — and
   the thread-per-core skeleton of section 5 as a sample. Then the server
   benchmark of step 2 in Goose, which is the measurement the brief's
   "absolute fastest" claim stands or falls on.
8. **Later**: overlapped frame slots (4.12), the 6.4 relaxation, `for x in
   gen`, dynamic-stack tasks (6.1), nonfixed results (6.8).

## 9. Open questions

* Whether parameters should be visible as task fields at all, or the
  frame should be opaque apart from `resume`. Visible fields are
  convenient (a cleanup pass can read `task.conn` of an abandoned task)
  and cost nothing; opaque is tidier.
* `defer` across a suspension (R5): now allowed, since `serve` needs one
  (4.7); it costs a frame field for its captured state and runs at its
  block's exit in whichever resume reaches that exit. What remains open
  is what abandonment then implies — its defers never run, and the
  scheduler must clean up explicitly (4.10) — and what
  [ownership.md §5](ownership.md#5-coroutines) works out for an `own`
  qualifier here, a frame that can be neither dropped nor copied and a
  `cancel` that runs the pending defers; that is a separate decision, and
  this design stands without it.
* Large hoisted locals: copy-in/copy-out of a `u8[1024]` local per resume
  is 2 KB of traffic. The 6.4 relaxation (in-place slots) or a size
  threshold would address it; the workaround today is to own the buffer
  outside, which is the idiom anyway.
* Whether a `thread_fn` entry may itself be started as a coroutine (no
  obvious use; no — it owns its stacks and needs no frame), and whether
  `simd` means anything next to `suspend<F>` (not in v1).
* Dynamic-stack tasks (6.1): what the opt-in looks like and how many a
  program may have, given the address-space budget.
* A `for x in gen` form and an iterator protocol for std's containers
  over the emit-handler idiom (4.13), once generators are in use.

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
