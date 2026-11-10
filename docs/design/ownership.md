# Ownership without destructors: an `own` qualifier for pool handles and coroutine frames

<details>
<summary>Table of contents</summary>

- [0. Summary](#0-summary)
- [1. What the three notes decided, and where they meet](#1-what-the-three-notes-decided-and-where-they-meet)
- [2. Ownership is two features](#2-ownership-is-two-features)
- [3. The precedent: a non-copyable fixed-size class exists already](#3-the-precedent-a-non-copyable-fixed-size-class-exists-already)
- [4. The design: `own` as a qualifier](#4-the-design-own-as-a-qualifier)
  - [4.1 What is owned](#41-what-is-owned)
  - [4.2 Not copyable](#42-not-copyable)
  - [4.3 What consumes](#43-what-consumes)
  - [4.4 The flow rules](#44-the-flow-rules)
  - [4.5 Linear now, affine later](#45-linear-now-affine-later)
  - [4.6 Containers: the boundary where static becomes checked](#46-containers-the-boundary-where-static-becomes-checked)
  - [4.7 Threads, images, generics, `return … from`](#47-threads-images-generics-return--from)
  - [4.8 The other half of 5.4: a release is a shrink](#48-the-other-half-of-54-a-release-is-a-shrink)
  - [4.9 `own` beside `defer`](#49-own-beside-defer)
- [5. Coroutines](#5-coroutines)
  - [5.1 R5 restated](#51-r5-restated)
  - [5.2 Design A: `own` joins R1](#52-design-a-own-joins-r1)
  - [5.3 Design B: own frames and `cancel`](#53-design-b-own-frames-and-cancel)
  - [5.4 What B would change in coroutines.md](#54-what-b-would-change-in-coroutinesmd)
  - [5.5 The server, both ways](#55-the-server-both-ways)
  - [5.6 Prior art specific to frames](#56-prior-art-specific-to-frames)
- [6. Where the compiler changes, and where it does not](#6-where-the-compiler-changes-and-where-it-does-not)
- [7. Directions, side by side](#7-directions-side-by-side)
- [8. Recommendation and order](#8-recommendation-and-order)
- [9. Open questions](#9-open-questions)
- [10. Implementation status](#10-implementation-status)
- [References](#references)

</details>

Research note, November 2026. The linear form of section 4 has since been
built on a branch; [section 10](#10-implementation-status) says what was
built and where it departs from this note. The rest is not implemented, and
its syntax is a placeholder. The question was whether Goose can have a notion
of ownership strong enough to settle what [coroutines.md](coroutines.md)
leaves open at R5 (a suspended coroutine can be dropped or copied, so no
cleanup written inside it is reliable) and what
[llm_frontier.md §5.4](llm_frontier.md#54-owning-handles-a-free-is-a-shrink)
sketches for pools (every allocation freed exactly once), without giving up
the property the language is built on: every value is plain data, a copy is
a `memcpy`, a drop is nothing, and no code runs at a scope exit that the
programmer did not write. The answer proposed here is that the notes use
"ownership" for two different things, that only one of them is expensive,
and that the cheap one is enough for both problems.

## 0. Summary

* **Ownership is two features wearing one word.** An *obligation* is a
  fact the checker tracks about a value: it is not copied, and it is handed
  to exactly one consumer before it dies. A *release* is code the compiler
  writes where a value dies. Rust, C++ and Swift bundle the two; Austral,
  Cogent and OCaml 5's continuations have the first without the second.
  Every cost [scope_exit_cleanup.md §4.7](scope_exit_cleanup.md#47-what-destructor-types-would-add)
  lists against destructor types is a cost of the release (section 2).
* **An obligation does not touch codegen.** A program the checker accepts
  never copies an `own` value and never lets one die unconsumed, so the
  backend keeps emitting `memcpy` for copies and nothing at exits; no
  optimizer pass, runtime function, thread or serialization code changes.
  What the feature touches is every place the *checker* judges a copy or a
  death, and Goose has enumerated those already: a value holding
  self-relative links is a fixed-size non-copyable class today, enforced at
  8 general sites and 10 builtin-specific ones in the typechecker, with 3
  assertions in codegen. `own` rides the same predicate (section 3).
* **`own` is a qualifier, like `const`.** `own T&` is a handle to a pool
  slot, `own T[:]` to a run, `own Texture` a native handle. What is owned
  is a claim on the slot, never the memory: pools own memory and scopes
  free it, as before. The new machinery is one flow bit per `own` variable
  (held or consumed), merged at joins as definite assignment is, with
  Austral's rule that branches must agree, and a consume is the shrink that
  the handle's references are checked against (section 4).
* **Linear first, affine as sugar later.** The linear form (the program
  writes every release; `defer` claims a handle for every exit) needs no
  function the compiler calls on its own. The affine form (the compiler
  releases what the program did not) needs one release function per handle
  kind and is the only destructor-shaped thing in the design. Build the
  first; the second is an increment on the `defer` emission (4.5). `defer`
  keeps its place beside both forms, as the cleanup a call site writes
  for itself (4.9).
* **At a container boundary the static guarantee needs help.** An `own`
  field of a pool element is read destructively, and the pool's `free` must
  consume what the element still holds. The cleanest form is a finalizer
  block the program writes once per pool and the checker verifies to be
  complete: [scope_exit_cleanup.md §3.3](scope_exit_cleanup.md#33-pool-finalizers)
  with a completeness check. A plain array of such elements removes them the
  same way: `pop` moves an element out, a bulk removal takes a finalizer
  block, and a local array may not reach its scope exit holding any (4.6).
* **For coroutines, own-ness is exactly the missing property.** R5 is not
  about `defer`. It is that a frame may be dropped or copied, and `defer` is
  the one construct whose meaning depends on neither happening. Two
  designs: (A) `own` joins R1's list and may not cross a yield, so frames
  stay plain and resources live in pool elements the loop frees; (B) a
  `defer` or an `own` local in scope at a yield makes the *frame* `own`, a
  compiler-generated `cancel(f)` runs the defers pending at the frame's
  suspension point, and the frame can be neither copied (a fork would run
  them twice) nor dropped (the obligation). A costs one line and gives up
  nothing in the current design; B makes `defer` in coroutines meaningful,
  and it is where Rust's scoped-task trilemma, OCaml's "resume or
  discontinue exactly once" and C++'s `destroy()` all point (section 5).
* **Cost.** Linear `own` with design A is about the size of `defer` (1.3k
  lines landed) in the checker and front end, plus producers and consumers
  on the two stdlib pools and a spec section. Design B adds a cancel emitter
  in codegen and three rule changes in coroutines.md. The "several thousand
  lines" of llm_frontier.md §5.4 is the affine form with containers
  (sections 6, 7).

## 1. What the three notes decided, and where they meet

[scope_exit_cleanup.md](scope_exit_cleanup.md) chose `defer` and rejected
destructor types, for six reasons listed in its §4.7: a new value class
touching every destination rule; destruction at every removal from every
container; abandoned partial results; exclusion from everything that copies
bytes; double destruction; and control flow at exits where the language has
none. It recommended pool finalizers (§3.3) and scoped allocations (§3.4)
next, and neither is built. `defer` landed as recommended, including the
rule that a body may not use long-distance returns, checked with one
transitive flag over the call graph (`CheckDefers`).

[coroutines.md](coroutines.md) makes a frame an ordinary fixed-size value,
copyable and droppable, by forbidding references into its own locals across
a yield (R4). Abandonment is then free ("it owns no stack, there are no
destructors, and whatever it names in pools is the pools' to free", §4.10),
and `defer` is banned in scope at a yield (R5) because its block's exit
would happen in a later resume, from a different resumer, if it happens at
all. The server of its §5 frees a connection by convention: the loop that
sees the frame finish calls `conns.free` on the connection the frame's
parameter names. Nothing checks that it does, and nothing can free what a
coroutine acquired inside its body after the first yield. The note's §9
keeps the `defer` question open and suggests a flag byte per `defer`.

[llm_frontier.md §5.4](llm_frontier.md#54-owning-handles-a-free-is-a-shrink)
sketches owning handles: a pool operation returns an `own T&` that is
consumed exactly once, the compiler frees what the program does not, and a
release is a shrink for every plain reference derived from the handle. It
is marked the most invasive item in that note, "a value class the language
does not have (non-copyable), touching assignment, `copy`, literals, `match`
by value, HOF captures, generics, `return … from`, `defer`, workers and
images", at "several thousand lines", and ordered last.

The three meet at one sentence in coroutines.md §2.6: pools own what a
coroutine cannot. That settles *where* a coroutine's resources live; it
does not settle *who releases them when the coroutine is abandoned*, and
every answer to that question is some form of ownership. The alarm the
question raises is the one scope_exit_cleanup.md §4.7 wrote down: an owned
value makes memory special. The rest of this note argues that it need not,
because the expensive half of ownership is separable from the half that
coroutines and pools need.

## 2. Ownership is two features

Take the Rust `Box<T>` as the full bundle: it cannot be copied (the type
system), it must not be used after a move (a flow check), and when it dies
the compiler calls `drop` (emitted code). The first two are **an
obligation**: facts the checker establishes about the program text, which
reject programs and emit nothing. The third is **a release**: a call the
compiler writes at every point where a value of the type dies, which means
the compiler must find every such point, in every container and on every
exit path, and must know a function to call.

Apply the split to scope_exit_cleanup.md §4.7's six costs:

| §4.7 cost | Belongs to | Why |
|---|---|---|
| 1. a new non-copyable value class in every destination rule | obligation | the checker refuses a copy; codegen never sees one |
| 2. destruction at every removal (`pop_n`, `clear`, shrinks, reassignment, a container's death) | release | each is a point where a value dies and a call must be emitted |
| 3. abandoned partial results | release | a half-built result is dying values with no program text for them |
| 4. exclusion from everything that copies bytes (queues, images, worker globals) | obligation | one predicate, as `flat` is one predicate |
| 5. double destruction | release | only emitted code can run twice |
| 6. invisible control flow and calls at exits | release | by definition |

Four of the six are costs of the release alone. The two that are costs of
the obligation (1 and 4) are costs Goose has paid once already, for values
with self-relative links (section 3).

The distinction is not new. Austral's linear types reject a program that
drops a linear value and never call anything on their own; the author
reports the checker at 600 lines ([llm_frontier.md §5.5](llm_frontier.md#55-prior-art)).
Wadler's original proposal is the same shape: linearity is a typing
discipline, and what the compiler gets from it is permission (there, to
update in place) rather than a duty ([wadler]). OCaml 5's continuations are
linear at run time and released by nobody: "every captured continuation
must be resumed either with a continue or discontinue exactly once", and
"not resuming continuations will leak the memory allocated for the fibers
as well as any resources that the suspended computation may hold"
([ocaml-effects]). Rust's async working group recorded the two readings of
"linear" explicitly: a type-system `#[must_use]`, or a guarantee that
`Drop` runs ([wg-async-linear]). Goose wants the first reading, because
the second is the thing its memory model deliberately has no room for.

What the obligation preserves, stated as the invariants the rest of the
compiler assumes:

* a copy of a fixed-size value is a `memcpy` (C assignment) — unchanged,
  since an accepted program contains no copy of an `own` value;
* a scope exit is watermark stores and the defers written there — unchanged,
  since an accepted program consumed every `own` value before the exit or
  wrote a `defer` that does; the one addition is a length compare at the
  exit of a local array of `own`-holding elements (4.6), a check in the
  sense a bounds check is one, not a call;
* a pool slot is a valid value of its type at all times (§5.4, §9.4) —
  unchanged, since freeing a slot does nothing to its bytes;
* threads receive flat values; images hold image-safe values — unchanged,
  with `own` added to the two predicates.

Nothing in codegen, the optimizer, the runtime, the JIT backend or the Wasm
plan reads a type's own-ness. That is the sense in which the feature does
not bleed: the set of files it touches is the checker and the front end.
What it *does* reach is every place in the checker that judges a copy or a
death, and that set is the subject of the next section.

## 3. The precedent: a non-copyable fixed-size class exists already

Reference §11 ("Copying values that hold self-relative links"): a value
containing a `T&<u32>` "cannot be copied in any way: by assignment from a
variable, as a by-value argument or match binder, as an element copy, or by
`copy(x)`"; it is constructed in place, bound with `.=`, matched with
`&`-binders, and returned only as a fresh value built at the receiver. The
class is transitive through fields (`HasRelRefT`), so a struct holding such
a value is non-copyable too. That is precisely the "non-copyable fixed-size
value class" of §4.7 item 1, and the compiler enforces it today at these
sites:

| Where | Function | What copy it refuses |
|---|---|---|
| `copy(x)` | `NoRelRefCopy` in the builtin | the explicit copy |
| a `return` value | `NoRelRefCopy` in `CheckReturn` | copying out of storage; a fresh value built at the receiver is allowed (`BuiltWhereReceived`) |
| a function value's tail | `NoRelRefCopy` | a block's result |
| a value used as an rvalue | `NoRelRefCopy` in the value path | reading storage by value |
| a value coerced to an expected type | `NoRelRefCopy` in `CheckValue` | arguments, stores, literal fields, assignments |
| a `let`/`var` from a decayed reference result | `NoRelRefCopy` | `let n = f()` where `f` returns a reference |
| a `let`/`var` from an initializer | `NoRelRefCopy` | `let n = other` |
| `.move_from`, `.resize`'s fill, `.realloc_slice`, `.append` | `HasRelRefT` | element copies inside builtins |
| the `format` hook's by-value parameter | `HasRelRefT` | the copy into a user overload |
| a payload passed by value to a case function | `HasRelRefT` | dispatch |
| a `match` arm binding a payload by value | `HasRelRefT` | `&`-binders required |
| a `for` binding an element by value | `HasRelRefT` | `for &x in xs` required |
| in-place decisions (`BuiltInPlace`, `InitOrderStoreSafe`, a temporary's contents) | `HasRelRefT` | where a value may be materialized |
| codegen: an assignment and two return paths | `NoSelfRelCopy` | assertions that the checker caught everything |

Eight general sites, about ten builtin-specific ones, three assertions.
Every one of them already asks "is this type non-copyable, and why?" and
reports a message naming the fix. An `own` qualifier adds a second reason
to the same predicate (a `NonCopyable(t)` returning which of the two) and a
second message; no site is added for copying, because the list above *is*
the list of copies the checker can see, found the hard way. This is the
measured cost of §4.7's item 1, and it is small. It is also the one place
where llm_frontier.md §5.4 says ordinary code changes ("reading it out of an
array or a field is by reference, `.=`, `&`, and never by the silent copy");
that change is the one a `Node` with a `Node&<u32>` field already imposes,
and the language has lived with it.

What self-relative values do *not* have is an obligation: they may be
dropped freely, since they are just bytes. The obligation is the new part
of `own`, and it is a flow property, not a copying one (4.4).

## 4. The design: `own` as a qualifier

### 4.1 What is owned

`own` qualifies a fixed-size type the way `const` qualifies one: `own T&`,
`own T[:]`, `own T&<u32 in pool.items>` (the hard case, excluded in v1,
4.6), and `own S` for a fixed struct such as a native handle. An `own`
value is produced by an operation that declares its result `own` (a pool's
`take`, an extern `create`) and consumed by an operation that declares a
parameter `own` (the pool's `release`, an extern `destroy`), or by a few
language positions (4.3).

The important sentence: **what is owned is a claim, not memory.** An
`own Node&` into a `slot_pool<Node>` is eight bytes (four as a relative
link) pointing at a slot whose bytes are as plain as any other slot's. The
pool owns the memory and frees it at its own scope exit as today; `release`
pushes an index onto a freelist as today; a plain `Node&` to the same slot
is as valid after the release as it is now (type-safe staleness, §9.4),
unless the program opts into the stronger rule of 4.8. The handle carries
an obligation the checker discharges; the slot carries nothing. This is
the answer to the alarm bells: no byte of memory becomes special, and no
operation on memory changes. Like `const`, `own` is checked per
specialization and invisible to codegen. Unlike `const`, it cannot be
inferred, because an obligation is a promise the program makes, not a fact
it exhibits; it must be written.

### 4.2 Not copyable

The sites of section 3, with `own` as the second reason. By-value `for` and
`match` bindings over `own` values are errors naming the `&` form, as for
self-relative values. `let m = h` is a *move*: `h` is consumed (4.3) and
`m` is held. A struct with an `own` field is `own`, transitively. `copy`
of an `own` value is an error. `default<T>()` for a T with a non-optional
`own` field does not exist; with an optional one it is the empty handle
(4.6).

### 4.3 What consumes

A held `own` value is consumed, once, by:

1. passing it to a parameter declared `own` (by-value parameters of plain
   type reject it, by 4.2; reference parameters of plain type *borrow* it,
   4.8);
2. returning it, which hands the obligation to the caller;
3. storing it into an `own` field: in a struct literal, or by `replace`
   (4.6);
4. a `let`/`var` binding to a new name, which moves it;
5. a `defer` body that does any of the above, which *claims* it: every exit
   of the block then counts as consuming it there, and any other consume is
   a double consume;
6. the trusted end. A consumer has to end somewhere: `slot_pool::release`
   reads the index out of the handle and pushes it, and after that the
   obligation must simply stop. Two forms: an `extern fn` parameter declared
   `own` is consumed by the call (the C side takes responsibility, which is
   what `destroy(handle)` means), and a builtin available only to the
   standard library ends the obligation of a handle whose own fields are all
   empty (4.6). The stdlib already uses privileged builtins (`check_length`,
   `range_index`), so this is in character, and it keeps the set of trusted
   consumers to the pools and the externs.

`index_of(h)` yields a plain index with no obligation, as §5.4 rule 6 says;
the integer escape hatch stays, and a program that mixes `free(i)` with
`own` handles into the same pool has only the guarantees of the part that
stays in the `own` world.

### 4.4 The flow rules

The checker keeps, for every `own` variable in scope, whether it is *held*
or *consumed*, beside the definite-assignment bits the `FlowState` already
holds and merges (`JoinFlow`, `MergeFlow`). The rules:

* **At most once.** A use of a consumed variable is an error naming the
  consuming line, found as a use of an unassigned variable is found: from
  the flow bit at the use, not by a search.
* **Branches agree.** At a join, a variable consumed on one incoming path and
  held on another is an error at the join ("`h` is released in the `if` and
  not in the `else`"). This is Austral's rule ("a linear variable declared
  outside an `if` must be consumed in every branch or none"), and it is what
  makes every exit's state static: at any point, the compiler knows whether
  `h` is held, with no flag at run time. Paths that leave (a `return`, a
  `break`) do not reach the join and are judged by the exit rule instead.
* **Loops.** A loop whose body consumes a variable held on entry fails at
  the back edge, by the same agreement rule applied where the checker
  already merges a loop's passes. A handle produced and consumed inside one
  iteration is fine. A block passed to a HOF is checked spliced into the
  HOF's specialization, so a block that consumes a captured `own` fails
  this rule whenever the HOF loops over it, and passes whenever the HOF
  calls it once: the capture question of llm_frontier.md §8 answers itself,
  because the liveness and flow machinery already treat a function value's
  body as code that may run again (`UsedAfter`).
* **Every exit consumes.** At every exit of the declaring block (falling
  off, `break`, `continue`, `return`, a block's return through a HOF), the
  variable is consumed or claimed by a `defer` written before the exit. For
  a `return … from` passing through, the exits are the calls that may
  propagate, which the checker knows per specialization (`FnSpec::needs`,
  the flag `CheckDefers` already walks): a call that may propagate while an
  unclaimed `own` is held is an error at the call, naming the `defer` that
  would fix it. Aborts run nothing, as for `defer`.
* **Consume is a shrink.** The consuming site is checked as a shrink of the
  handle's slot for every plain reference rooted at the handle (4.8), with
  the `UsedAfter` test the shrink rules use.

The error messages come out in the shape llm_frontier.md §3.9 found
tractable for machine authors: a complete, local rule whose message names
the fix ("`h` is held at this `return`; release it, return it, or claim it
with `defer release(h)`").

### 4.5 Linear now, affine later

llm_frontier.md §5.4 proposed the affine default: where the program did not
consume a handle, "the compiler frees it at the block's exit, as a `defer`
it wrote itself". That is the one piece of the design that is a release in
the sense of section 2, and it has a cost the linear form does not:

* **A function to call.** The compiler must know, for an `own Node&` rooted
  at some `slot_pool<Node>`, that the release is `slot_pool::release` on
  that pool. The root is known per specialization; the function is a
  declaration the language does not have, either on the producer
  (`fn take<T>(p: slot_pool<T>&, v: T) -> own T& released by release`) or on
  the type. It is a destructor for one category of type, declared once per
  pool kind and once per extern handle kind, and called by the compiler at
  exactly two places: the exits of a block holding an unconsumed handle
  (emitted through `RegisterDefer`, which exists) and the free of a pool
  element holding an unconsumed field (4.6).
* **What it buys.** The idiom `let h = p.take(v); defer p.release(h);` with
  an early `p.release(h)` on one path is a double consume under the linear
  rules; the program chooses between the claim and the early release. Affine
  release makes both legal at once. Mojo and Swift both arrived at affine
  with a linear opt-in ([llm_frontier.md §5.5](llm_frontier.md#55-prior-art)),
  and §5.4 followed them.

The recommendation here is the other order. The linear form is strictly
smaller: it has no declaration to add, calls nothing the program did not
write, and keeps "no code runs at an exit that is not in the source" true
to the letter, since a `defer` is in the source. It is also what coroutines
need (5.3 uses the claim rule and nothing else). The affine form is an
increment that adds one declaration and routes it through emission that
`defer` already has; it can wait for evidence that the double-consume
restriction bites in real code, as `errdefer` is waiting. 4.9 sets `defer`
beside the two forms.

### 4.6 Containers: the boundary where static becomes checked

The flow rules of 4.4 hold for variables, because a variable's history is
in the program text. A pool element's history is not: a `Conn` in
`conns.items[i]` was filled by some earlier call and will be freed by some
later one, and nothing in the text of either says what its `own` fields
hold. So at the container boundary the guarantee changes character, and
the design has to say how.

**Own fields.** A fixed struct may have `own` fields of a kind with an
*empty* value: `own T&?` (null) or `own T[:]` (the empty slice, which
`free_slice` already accepts harmlessly). A literal provides each such
field with an `own` value or leaves it empty. Reading one is destructive:
`take(e.buf)` yields the `own` value and leaves the field empty; a plain
read `e.buf` yields a borrow (a plain `T&?` or `T[:]`, 4.8). Writing one
is `replace(e.buf, v)`, which consumes `v` and yields the previous content
as an `own` value the caller must consume (`release` of an empty handle is
a no-op, so `p.release(replace(e.buf, v))` is the idiom). Cyclone's swap
operator and Pony's destructive read are this ([llm_frontier.md §5.5](llm_frontier.md#55-prior-art)).
A plain assignment `e.buf = v` is an error naming `replace`, because it
would drop the old content. These are builtins, like `copy`; they add two
sites to the checker and nothing to codegen (a `take` is a load and a store
of the empty value).

**Freeing an element.** When a slot whose element has `own` fields is
freed, something must consume what the fields still hold, or the guarantee
has a hole exactly where pools are used. Three ways:

1. **A runtime check.** `free` aborts if any `own` field of the element is
   non-empty, with the line. A load and a compare per `own` field, at a
   `free`, which is already a call. It turns the leak into an abort, as
   `gen_pool` turns a stale read into one; it is not static.
2. **A compiler-written release**, the affine form of 4.5 applied to
   fields: `free` releases each non-empty `own` field through the declared
   release function. This is the "finalizer the compiler writes from the
   field types" of §5.4 rule 3.
3. **A finalizer the program writes, checked for completeness.** The pool's
   consuming free takes a trailing block (Goose has them, and the stdlib's
   `alloc_with` already takes one), `conns.release(h) { bytes.release(take(it.buf)); }`,
   and the checker requires that at the block's end every `own` field of
   `it` is definitely empty: each `take(it.f)` records emptiness of the
   path `it.f` as a flow fact, in the path-keyed form the flow state keeps
   for kills already, and the trusted end (4.3 item 6) that follows the
   block in the library's `release` demands those facts. Since the block is
   spliced into the library function's specialization, the facts flow from
   the block to the trusted end with nothing new. The finalizer is written
   once per pool, next to the pool, and a forgotten field is a compile error
   there.

The third is [scope_exit_cleanup.md §3.3](scope_exit_cleanup.md#33-pool-finalizers)'s
finalizer, with the completeness check that note could not have without
`own`, and without the language hook that note needed, since a trailing
block on `release` is library-expressible. It is the recommended form. The
first is the fallback for a pool whose release the program does not want to
write out. The second belongs with the affine increment.

**Arrays.** What makes an array different from a variable is bulk death:
`clear`, `pop_n`, `resize` down, assignment of the whole array, and the
scope exit of a local array end many elements at once, with no program
text per element, so the checker cannot know what each element's `own`
fields hold. The rules that keep this static, except for one compare:

* `pop` of an `own`-holding element is a move out, like `take`: the bytes
  leave the array and the local that receives them holds the obligation. A
  grow-shrink slot reused by another type afterwards holds nothing of it.
* The bare `clear`, `pop_n`, `resize` and whole assignment are errors on
  such elements at the instantiation of the builtin, as "elements must be
  fixed-size" is today. The stdlib offers the forms that take a finalizer
  block (`tasks.clear_all { cancel(it); }`), which walk the elements, are
  checked to empty `it` exactly as the pool's `release` is, and end in the
  privileged clear.
* A global such array never exits, which is the per-worker task table of
  coroutines.md §5. A local one must hold nothing at its scope exit: its
  length is compared with zero and the program aborts otherwise, with the
  line. This is the one runtime check the design adds at a scope exit; it
  is paid only by a local whose elements are declared to own something,
  and it is the same shape as the pool boundary above. The affine form of
  4.5 would walk the elements with the declared release instead, which is
  the one O(n) scope exit in the whole space and the reason the linear
  form asks for the compare.
* Reads are borrows: `for &e in xs`, `xs[i].f`, `resume(xs[i], ev)`;
  `xs[i]` by value is rejected, as for self-relative elements.

The same applies to a pool itself: a local pool of `own`-holding elements
must have every slot free at its scope exit (`free_slots.len == items.len`,
one compare), since the finalizer block emptied each element as it was
freed, or it releases them all through a library walk
(`conns.release_all { … }`). The server's pools are globals and never exit.
`own` in a plain global variable, outside an array or a pool, has no scope
exit and nothing that could consume it, and is forbidden in v1.

### 4.7 Threads, images, generics, `return … from`

* `own` is not flat and not image-safe: one line each in `IsFlat` and
  `ImageSafe`, both of which already recurse through fields. A frame or an
  element holding an `own` value does not cross a queue (the receiving
  worker could not release into the sender's pool anyway) and does not
  appear in a `to_bytes` image. §5.4 rule 5.
* Generics are monomorphized and the checker runs per specialization, so a
  `dictionary<K, own V>` fails where the library's body copies a `V`
  (`slots[i].val = v`), with the instantiation chain in the message, as an
  index type fails a body that computes into it. No `~Copyable`-style
  constraint system is needed, which is where Swift paid most
  ([llm_frontier.md §5.5](llm_frontier.md#55-prior-art)). The two pools are
  the containers that hold `own` values, and they get `own`-aware operations
  beside their existing ones.
* `return … from` is covered by the exit rule of 4.4: a call that may
  propagate while an unclaimed handle is held is an error, and the `defer`
  that claims it obeys the `defer` body rules (no long-distance machinery),
  which a pool's `release` satisfies. This is Austral's "incompatible with
  exceptions" turned into a local rule with a local fix.
* Recursion and cycles are unaffected: an `own` handle is fixed-size, so R1
  of §7.8 does not apply to it; a cycle function with an `own` parameter
  consumes it like any function.

### 4.8 The other half of 5.4: a release is a shrink

§5.4's second idea is independent of the first and can be built after it:
treat `release(h)` as a shrink of the slot for every plain reference
*derived from* `h` (`let r: Node& = h;`), so that no such reference is
used after the release or stored in a holder. The machinery is the
grow-shrink rule of §5.2: references into a `[>..<]` live in variables
only, and a shrink is an error while any such variable is used afterwards
(`UsedAfter`, the store record, the shrink summaries through calls). A
handle as a root for the references bound from it is the new piece: those
references are rooted at the handle variable exactly, as `&x` is rooted
at `x`, so the existing test finds them.

What it buys is the "no stale read through a reference" claim for handles
that stay in the `own` world. What it does not reach is a stored relative
link in another element, which §5.4 already concedes to generations. It
adds no codegen either. It is listed here as a separable increment because
the leak and double-free guarantees do not depend on it, and because it is
the part of §5.4 whose checker cost is least clear until the first part
exists.

### 4.9 `own` beside `defer`

Both attach a cleanup to the exits of a scope, so the overlap has to be
stated. The forms differ in who *forces* the cleanup and who *writes* the
release call:

| form | who forces the cleanup | who writes the release call |
|---|---|---|
| `defer` alone | nobody | the call site, at every use |
| linear `own` | the producer's declaration (`-> own T&`) | the call site, as `defer release(h)`; the checker verifies it is on every exit |
| affine `own` | the producer's declaration | the declaration, once per handle kind; the compiler emits the call |

Under the linear rules, then, a `defer` is not a way to satisfy the
checker. It *is* the release, and `own` is what makes omitting it,
doubling it, or using the handle after it an error. The affine form moves
the release text from every call site to one declaration, and leaves
`defer` four jobs:

* **Cleanup the producer's author cannot know.** A release is a function
  of the handle's payload, fixed by the producer. A `defer` body is a block
  over the scope's locals, read at the exit. Of the defers in the
  repository, all but one are handle-shaped or a global-state pair an empty
  guard covers, the conditional `defer if sound_ready { audio::close(); }`
  of `31_mini_doom` included (an `own Audio?` whose empty state is the
  condition). The one that is not is `defer Finally();` in
  `stdlib/exception.goose`: it runs a function value the caller passed,
  spliced in at the call, which no field can hold. Outside the repository
  the common shape is the scoped push a compiler writes,
  `scopes.push(s); defer scopes.pop();`, which acts on another local. A
  handle for it is not empty: it carries a reference to `scopes`, which
  makes it a holder under the reference-field rules, and it needs a type, a
  producer returning `own` and a release declaration besides. Four
  declarations for one line.
* **Pay per kind against pay per site.** An empty guard fits a
  global-state pair (`let w = gfx::open(…)` returning `own Window`,
  released by `gfx::close`): the declaration is paid once per kind, and
  every call site is then correct without writing anything, which is the
  stronger form for the machine authors of llm_frontier.md §3. `defer` pays
  a line per site and declares nothing. The library should model what has
  many sites (pools, windows, files, locks), and `defer` stays for the
  one-off. The conclusion is `own` producers in the stdlib, not fewer
  defers in the language.
* **The vehicle.** The affine release is a `defer` the compiler wrote: it
  is emitted through `RegisterDefer`, it obeys the `defer` body rules (no
  yield, no long-distance return), and under design B `cancel` runs the
  pending defers and the pending releases through the same switch (5.3).
  Removing `defer` would mean rebuilding it inside the compiler.
* **Until affine exists.** Section 8 has it wait for demand; until then
  the claim is the only release text there is.

Two things the overlap gives:

* **The affine release is `errdefer` for handles.** In
  [scope_exit_cleanup.md §3.6](scope_exit_cleanup.md#36-only-on-failure)'s
  one example, `let id = nodes.take(…); … return id;` consumes on the
  success path, and a `return … from` passing through before it releases,
  with no keyword. The linear form cannot write this: `defer
  nodes.release(id)` beside `return id` is a double consume, so each
  failing path releases by hand. For handles, the demand section 8 step 5
  waits for and the demand `errdefer` waits for are one demand.
* **Under affine the explicit `defer release(h)` is deleted, not kept.**
  Beside an early `release(h)` on one path it stays a double consume, since
  the body names a consumed `h`. The idiom becomes: release early where
  the program wants to, or write nothing.

On spelling, `let h: own Foo? = init();` is longer than it needs to be. A
local needs no `?`: held or consumed is a flow fact, and `?` is for fields,
where emptiness has to be a state at run time (4.6). It needs no
annotation either, since the type comes from the producer, so
`let g = lock(m);` is the whole guard (the first question of section 9).
What needs a rule is the *unnamed* guard, a bare `lock(m);` that drops its
result. Under the linear rules that is an error already: an `own` value
died unconsumed. The affine form should keep it an error and require a
name, rather than release at the end of the statement, which is the
mistake Rust's `let _ = m.lock()` makes: the guard is gone before the code
it was meant to cover.

## 5. Coroutines

### 5.1 R5 restated

coroutines.md gives two reasons for R5 (no `defer` in scope at a yield):
the block's exit "would now be in a later resume, from a different
resumer", and (§4.10) a frame may be abandoned, in which case the exit never
comes. The first is not a problem once a frame's locals are hoisted: a
`defer` body that names a local keeps it live across the yield (R3), so it
is reloaded in whichever resume reaches the exit, and which thread or loop
that is does not matter, because the body names only the frame's fields and
things outside the frame (R4). The second reason is the real one, and it
has two halves: a suspended frame may be **dropped**, so the exit may never
happen, and it may be **copied**, so the exit may happen twice. `defer` is
the only construct in Goose whose meaning depends on both never happening,
and a frame is the only value in Goose for which both can happen to code
that has started running.

Stated this way, the fix is not a flag per `defer` (coroutines.md §9), which
would make the normal exit run the right bodies but does nothing for a frame
nobody resumes. The fix is a frame that can be neither dropped nor copied,
and that is what an `own` value is. Two designs follow, one that keeps
frames out of the `own` world and one that lets them in.

### 5.2 Design A: `own` joins R1

R1 says no nonfixed local may be in scope at a suspension point, because a
suspended frame cannot own a data stack. Add: no `own` local may be in scope
at one, because a suspended frame cannot discharge an obligation. The fix
the error names is the fix §7.8 names for growable data: own it outside.

```goose
struct Conn { fd: i64, inbuf: u8[..4096], scratch: own u8[:] }   // the run is the connection's

coro fn serve(c: Conn&) resume Event {
    loop {
        guard fill(c) else { return; }
        {
            let s = bytes.take_slice(need);          // own u8[:], held
            parse_into(c.inbuf[..], s);
            replace(c.scratch, s) |> bytes.release;  // consumed before the yield below
        }
        guard flush(c) else { return; }              // yields inside
    }
}

// The loop frees the connection; the pool's finalizer block is checked to
// consume everything a Conn owns, so the frame never has to.
guard suspended else {
    conns.release(conns.handle_of(tasks[i].c)) { bytes.release(take(it.scratch)); }
    tasks.free(i);
    continue;
}
```

(Syntax placeholders throughout; `|>` is a stand-in for passing the
`replace` result to `release`.) Frames stay plain: copyable, droppable,
flat when their fields are, serializable, exactly as coroutines.md §4.9
and §4.10 describe. What changes: one item in R1, and the guarantee that
every resource a coroutine uses is owned either by a local of one resume
segment, which 4.4 discharges, or by an `own` field of a pool element,
which the pool's finalizer discharges when the loop frees it. A leak is then
only possible where the loop forgets the element, and that is one `free`
in one function, which is where coroutines.md's convention already puts it.

What A gives up: a resource acquired before a yield and released after one
must pass through a field between them, even a scratch run used only by the
one coroutine. This is the discipline the language already asks for
growable locals across a yield and across a cycle call, and the server
shape wants it anyway (the kernel writes into the connection's buffer while
the frame is suspended). `defer` stays out of coroutines at yields.

### 5.3 Design B: own frames and `cancel`

Let an `own` local cross a yield, and let a `defer` be in scope at one,
under one rule: **at a yield, every `own` local in scope is claimed by a
pending `defer`** (4.3 item 5). Then:

* **The frame type is `own`** whenever any yield of the body has a `defer`
  or an `own` local in scope. A frame with neither is plain, as today. So a
  generator or a patrol pays nothing; a coroutine that holds a resource
  across a wait is a resource itself, which is what it is.
* **`cancel(f)`** is a second builtin beside `resume`, and the frame's
  consumer. The compiler generates it per `own` frame type as the sibling of
  the resume function: a `switch` on the tag whose arm for yield *k* reloads
  the hoisted locals the pending bodies name and runs the `defer` bodies
  pending at yield *k*, latest first, as the normal exit of their blocks
  would (`EmitDeferBody`), then marks the frame finished. The set of pending
  defers at each yield is static (the scopes open at the yield), so no flag
  is stored: the tag coroutines.md already keeps is the flag its §9 asked
  for. On an unstarted or finished frame, `cancel` is a tag compare and
  nothing else. Because every `own` local in scope at a yield is claimed by
  a pending `defer`, running the pending defers releases every resource the
  frame holds, and the compiler never needs to know how to release anything
  itself; `cancel` runs code the program wrote, where the program wrote it.
* **Copying is rejected** at the sites of section 3, since the frame type
  is `own`: a fork would run the defers twice. **Dropping is rejected** by
  the flow rules: a local `own` frame must be cancelled on every exit
  (`var task = serve { c }; defer cancel(task);` is the idiom; `resume` is
  a borrow and `cancel` the consume, idempotent on a finished frame), and an
  `own` frame in a pool is consumed by the pool's finalizer block, which the
  checker requires to `cancel(it)`.
* **`own` parameters are not allowed on a `coro fn`** in this form: an
  unstarted frame (tag 0) has run no `defer`, so nothing could claim them.
  A coroutine borrows its connection (`c: Conn&`) and the task element owns
  it, as in A.
* A `defer` body still may not yield (R7) or use long-distance returns, so
  `cancel` is straight-line code, and it runs on the thread of whoever
  calls it, as `resume` does.

B is A plus three things: the frame type's own-ness derived from its
yields, the `cancel` emitter, and the relaxation of R5 to "a `defer` in
scope at a yield makes the frame `own`". What it buys is that cleanup can be
written inside the coroutine, next to the acquisition, and is guaranteed to
run exactly once, on the normal exit or on cancellation, whichever comes:

```goose
coro fn serve(c: Conn&) resume Event {
    loop {
        guard fill(c) else { return; }
        let s = bytes.take_slice(need);
        defer bytes.release(s);                      // claims s: runs at the block's exit, or at cancel
        parse_into(c.inbuf[..], s);
        guard flush(c) else { return; }              // yields inside; s is held, and claimed
    }
}

// The loop: hangup or shutdown cancels the task, which releases whatever
// its body holds at the point it is suspended; then both slots go back.
guard suspended else {
    tasks.release(tasks.handle_of(i)) { cancel(it); }
    conns.release(conns.handle_of(c)) { bytes.release(take(it.scratch)); }
    continue;
}
```

The first reader of that frame's generated C sees a `serve_resume` and a
`serve_cancel`, each a `switch` over the same tag, and recognizes both.

### 5.4 What B would change in coroutines.md

* **R5** would become: a `defer` in scope at a yield, or an `own` local in scope
  at one, makes the frame type `own`; an `own` local in scope at a yield
  must be claimed by a `defer` written before it. Under A alone, R5 stands
  and R1 gains `own`.
* **§4.9 (copying, flatness)** holds for plain frames and is replaced, for
  `own` frames, by section 3's rejections: no fork, no `pop` by value, no
  queue, no image. An `own` frame type is still fixed-size and still laid
  out as §4.12 says.
* **§4.10 (abandonment)** holds for plain frames; an `own` frame is
  abandoned only through `cancel`, which is the point.
* **§4.3** gains `cancel(f)`; **§4.12** gains the cancel function's lowering;
  **§8 step 4** gains its emission (a second switch over the same labels'
  pending defers) and **step 5** the tests: cancel at each yield, cancel of a
  finished frame, a copy of an `own` frame rejected, a frame in a pool freed
  without `cancel` rejected.
* **§4.1**: a `coro fn` has no `own` parameters (5.3).

Nothing in §4.4 (calling a coroutine from a coroutine) changes: a called
coroutine's locals and defers are spliced into the caller's segments and
tags, so a `defer` in the callee pending at the caller's yield *k* is simply
among the bodies `cancel` runs for *k*.

### 5.5 The server, both ways

| | Design A | Design B |
|---|---|---|
| where a connection's resources live | `own` fields of the pool element | the same, or `own` locals claimed by `defer` inside the body |
| who releases on hangup | the loop, through the pool's finalizer block | `cancel`, then the pool's finalizer block |
| frame type | plain | plain unless the body holds a resource or a `defer` across a yield |
| frames through queues, in images | yes | plain ones yes; `own` ones no |
| `defer` in a coroutine | not at a yield | anywhere; claims what it releases |
| what the compiler generates | nothing new | one `cancel` function per `own` frame type |
| rules changed in coroutines.md | one line in R1 | R1, R5, 4.9, 4.10 conditional; `cancel` added |
| the forgotten-release failure | the loop forgets `conns.release` (one site) | the same site, or a cancel the finalizer check forces |

A is a subset of B: a program written under A is a valid B program with
plain frames. So A can ship with the coroutine v1 and B can follow when a
body wants to hold a resource across a wait, with no change to code written
in between.

### 5.6 Prior art specific to frames

* **C++** destroys a suspended coroutine explicitly through
  `coroutine_handle::destroy()`, which runs the destructors of the variables
  live at the suspension point ([cpp-destroy], [oldnewthing-cleanup]). B's
  `cancel` is that operation with `defer` bodies in the place of destructors,
  and the handle's non-copyability in the place of the frame's.
* **Zig's first async design** destroyed a frame at a suspend point by
  running the `defer` and `errdefer` expressions in scope ([zig-1261]), and
  documented that every suspend point is a point at which the coroutine may
  be destroyed; the feature was later removed with the rest of that design
  ([coroutines.md §3.6](coroutines.md#36-zig-twice)). Its frames were
  droppable, so the running of those defers depended on the owner
  remembering to destroy, which is the gap own-ness closes.
* **Rust** lets any future be dropped or leaked without being polled to
  completion. Its consequences are the cancellation-safety hazards
  coroutines.md §3.2 cites, and the scoped-task trilemma: a borrowing
  concurrency API cannot rely on a destructor to join its children, because
  any value can be leaked, so it must be lexically scoped; linear types
  would change that but are judged too costly to retrofit ([boats-trilemma]).
  "Linearity and Control" makes the same argument for async scopes
  ([wuyts-linearity]), and "Asynchronous clean-up" treats linear types as
  needing an async cancellation path and a `do … final` construct as
  prerequisites ([boats-cleanup]). Goose has no compatibility to keep:
  coroutines do not exist yet, and B decides frame linearity before any
  code holds a frame.
* **OCaml 5** makes continuations one-shot at run time ("resumed either
  with a `continue` or `discontinue` exactly once", `Continuation_already_resumed`)
  and admits that "there is no check to ensure that the continuations are
  resumed at least once", so an unresumed one leaks "any resources that the
  suspended computation may hold" ([ocaml-effects]). That is the affine
  half enforced dynamically and the linear half not enforced at all; B
  enforces both statically.
* **Kotlin and Python** run `finally` blocks on cancellation by delivering
  an exception at the suspension point; Python's `close()` is also called
  from the garbage collector, which is why its timing is unpredictable
  ([coroutines.md §3.4, §3.7](coroutines.md#34-kotlin)). B's cancellation is
  a call the owner makes, and the owner is known because the frame is
  non-copyable.

## 6. Where the compiler changes, and where it does not

For the linear design with A, then B:

| Area | Change | Size, anchored on what exists |
|---|---|---|
| lexer, parser, AST, clone, dump, LSP, tree-sitter, reference appendix A | the `own` qualifier on types, `take`/`replace`/`cancel` as builtins | as `const` and `defer` were: parser 45 lines for `defer` |
| type predicates | `NonCopyable(t)` with a reason, replacing the bare `HasRelRefT` test at the sites of section 3; `IsFlat`, `ImageSafe` | the 8 + 10 sites change a call and a message; two one-line additions |
| flow | a held/consumed bit in `FlowState`, joined in `JoinFlow` with the agreement rule, checked at exits with the definite-assignment exit logic; path emptiness facts for `take` | the largest new piece: `defer`'s flow work was 196 lines in `typecheck_flow.h`; Austral's whole checker is reported at 600 lines |
| consuming positions | `own` parameters at call sites, `return`, `own` fields in literals, `let` moves, `defer` claims, the trusted end, extern parameters | a case each where arguments, returns and literals are checked today |
| `return … from` | one query of `FnSpec::needs` at calls while an unclaimed `own` is held | reuses `CheckDefers`'s flag |
| coroutines, A | `own` in R1's class test | one line, in the `CycleCallTemps` reuse step 3 of coroutines.md §8 plans |
| coroutines, B | frame own-ness from the yields' scopes; a `cancel` function per `own` frame type: a `switch` over tags, each arm the pending defers via `EmitDeferBody` | the one codegen change in the whole design; bounded by the number of `own` frame types |
| stdlib | `take`/`release`/`handle_of` on `slot_pool` and `slice_pool` beside the existing operations, with the finalizer-block form of `release` | tens of lines of Goose |
| spec | a section beside §9.5 (writability) for the qualifier and the flow rules; pool additions in §5.4's successor; coroutines' R1/R5 | comparable to §5.2 |
| codegen of copies, assignments, exits, returns; optimizer; runtime; JIT; Wasm; threads; serialization | nothing, apart from the length compare at the scope exit of a local array of `own`-holding elements (4.6) | the invariants of section 2 hold by construction |

The last row is the answer to the question this note was asked. The sites
that must know about `own` are the checker's copy sites (already
enumerated), its flow state (one bit), its exit logic (one query) and, for
B, one generated function per `own` frame type. Everything that makes
Goose's values plain stays as it is, because an accepted program never asks
it to do anything new.

## 7. Directions, side by side

| Direction | What it is | Guarantees | What it rejects that compiles today | Compiler | Runtime |
|---|---|---|---|---|---|
| 0. Status quo plus `gen_pool` | generations in a library; finalizers and scoped allocations as scope_exit_cleanup.md recommends | stale reads and double frees abort; leaks reported in debug | nothing | none | a generation compare per lookup |
| 1. Linear `own`, design A | sections 3, 4 (linear), 5.2 | a handle in the `own` world is released exactly once; a pool element's fields are released at its free; a coroutine holds nothing across a yield | `own` in a plain global; a bulk removal of `own`-holding elements without a finalizer block; early release plus `defer` on one handle; `own` through queues and images; `own` across a yield | checker and front end, as section 6 | none |
| 2. Direction 1 plus design B | 5.3 | plus: cleanup written inside a coroutine runs exactly once, on exit or cancel; `own` frames cannot be forked or forgotten | plus: copying or queuing a frame that holds a resource | plus one emitted function per `own` frame type | none |
| 3. Affine release | 4.5 on top of 1 or 2 | the same guarantees with the compiler writing the release the program omitted | as 1, less the early-release restriction: a handle released on one path is released at the exit on the others (4.9) | a release declaration per handle kind; emission through the `defer` path | none |
| 4. Destructor types | scope_exit_cleanup.md §3.2 | release on every death, in every container | nothing | §4.7's list | walks at container death |

Dependencies: 1 needs nothing (the pools are in the stdlib; `defer` exists);
2 needs 1 and the coroutine v1; 3 needs 1; 4 stays rejected. 4.8's
release-is-a-shrink is an increment on 1 with no dependents.

## 8. Recommendation and order

`own` is a decision of its own. coroutines.md is the coroutine design of
record and stands without it: R5 and §4.10 are consistent on their own
terms, the loop's convention plus `gen_pool`'s debug report is the leak
story in that case, and nothing in this note is a precondition for
building it. If `own` is adopted, it is built first and the coroutine
design is built on top of it, with design A as one line in R1 and design B
as the later relaxation; if it is declined, coroutines go ahead as written.
The order below is for the first case.

1. **Decide the vocabulary, build nothing yet.** Write into the spec's
   TODO list that ownership in Goose, if it comes, is an obligation on
   handles and never a release of memory: `own` as a qualifier, linear with
   `defer` claims, pools and finalizer-block removals as the only ways an
   element dies, no destructor on plain data. This costs a paragraph, and
   it follows llm_frontier.md §5.5's lesson to decide before `i64` handles
   are everywhere in user code.
2. **Ship `gen_pool` first**, as llm_frontier.md §7 step 2 says. It costs
   nothing to write and it produces the evidence this note cannot: whether
   leaks and double frees in real pool code are frequent enough to pay for a
   checker feature.
3. **If they are, build direction 1**: the qualifier at the section 3
   sites, the flow bit, the trusted end, `take`/`replace`, the pool
   operations and array removals with the finalizer block, two predicate
   lines, the spec section. Rewrite `26_file_tree`'s `release` and the LRU
   benchmark's eviction on it as the acceptance test, and measure that the
   generated C for both is unchanged apart from the finalizer calls the
   program wrote. Design A's rule goes into R1 at the same time, whether
   coroutines are specified by then or not.
4. **Design B when a coroutine body wants to hold a resource across a
   wait** and A's field discipline is what stops it: the `cancel` emitter
   and the rule changes of 5.4, on top of direction 1 and the coroutine v1,
   never instead of either. The server benchmark of coroutines.md §8 step 2
   is where that want would first show.
5. **Affine release (direction 3) and release-is-a-shrink (4.8)**: wait for
   demand, as `errdefer` waits; for handles the two demands are one (4.9).

## 9. Open questions

* **Spelling.** `own` as a type qualifier reads well in signatures and
  fields (`entries: own Link[:]`, `fn release(h: own Node&)`); whether a
  local needs it (`let h = p.take(v)` can take its type from the producer)
  or should repeat it for the reader.
* **The trusted end.** A stdlib-only builtin, an `extern fn` rule, or a
  `trusted` marker on a function whose `own` parameters end there. The last
  reads best and is the easiest to audit (`grep trusted`).
* **Field emptiness as a flow fact.** The finalizer completeness check of
  4.6 needs "`it.buf` is empty here" to survive from the block into the
  library function; the path-keyed facts the flow state keeps for kills
  look sufficient, but this is the one piece of 4.6 that should be
  prototyped before the design is written into the spec.
* **Owned relative links** (`own Node&<u32 in nodes.items>` stored in a run
  of children): §5.4 defers them and so does this note. The finalizer block
  of the parent would have to consume each link, which is a loop over a run
  of `own` values: the finalizer-block form of 4.6 applied to `free_slice`.
* **Cancel while running.** `cancel` on a frame whose body is executing
  (through an alias, from inside a `defer`) is the re-entry case of
  coroutines.md §4.10, a logic error with a debug-build abort; whether the
  `own` rules can make it a compile error (a borrow of the frame live across
  its own `resume`) is worth a look, since the frame is a variable and the
  rules of 4.8 already track borrows of handles.
* **What `result(f)` means for an `own` frame** that finished: the frame
  holds nothing, but its type is still `own`; `cancel` is a no-op and still
  required. Whether a finished frame should decay to plain is a question of
  how much the idiom `defer cancel(task)` irritates in practice.
* **Linear pools in the sense of §5.4** ("a pool declared linear turns the
  implicit free into an error") disappear under this design, since every
  pool is linear; the question becomes whether any pool wants the affine
  release of direction 3.

## 10. Implementation status

The linear form of section 4 is implemented on the `rfc-ownership` branch
(November 2026), to gain experience with it: `own` and `trusted fn`, the
flow rules of 4.4, `take` and `replace`, the finalizer check of 4.6, arrays
of `own`-holding elements with the scope-exit compare, owning operations on
both pools, owned relative links, and a sample
([33_owned_handles](../../samples/33_owned_handles.goose)). The rules as
built are in [reference §14](../reference.md#owning-handles) and
[spec §5.5](../goose_spec.md#55-owning-handles-experimental). Not built: the
affine form (4.5), release as a shrink (4.8), `clear_all`, `handle_of`, and
all of section 5.

Where the implementation departs from sections 4 and 6:

* **Borrowing is by type.** 4.2 lists the uses that borrow. As built, any
  use of an `own` value at a destination whose type holds no `own` value
  reads it as its plain type, and a destination whose type does hold one
  consumes it. An untyped parameter therefore borrows. This is what makes an
  `own u64` handle from C usable at all.
* **A move is written.** A destination with no type written takes over only
  a value that nothing holds yet: `let h = pool.take(v)`. Where a variable
  holds the value, `let m = h;` and a function with an inferred result
  returning `h` are errors that name the spellings: `let m: own T& = h`
  moves, `let m .= h` borrows, `copy(h)` copies the pointee. Having the
  untyped `let` move where the untyped parameter borrows left two readings
  of one shape, with nothing at either to tell them apart.
* **`trusted fn` covers three things, not one.** 4.6 and section 6 speak of
  the trusted *end* of an obligation. A pool needs its *beginning* as well
  (a plain `T&` returned as `own T&`) and the *overwrite* of a slot that
  held `own` fields when it is reused. All three are what `trusted fn`
  permits.
* **Emptiness is a flow fact of its own.** 4.6 has the kill facts of §9.2
  carry "this field was taken" from the release block to the trusted end.
  They cannot: a call of a function value clears them in both directions.
  Each tracked variable has a bit per `own` field instead, set by `take`,
  joined with the flow state, and cleared by any other store of an `own`
  value and by a call that may make one. A plain reference carries such
  bits too, which is what lets `c.buf = pool.resize(take(c.buf), n)` be an
  assignment.
* **A known-empty state.** An `own T?` or `own T[:]` variable written as
  `null` or `[]`, emptied by `take`, or found null by a condition, owes
  nothing. Without it a list could not be walked and freed, and no
  finalizer block could hand a field to a variable outside it.
* **Codegen is not untouched.** Section 6's last row has no codegen change
  beyond the length compare. A struct variable that is moved has its `own`
  fields emptied in the generated code, because a reference bound to it
  earlier would otherwise still read the moved value as live; `take` and
  `replace` are stores; and the bounds-check eliminator sees all three as
  writes.
* **A stored handle forgets its pool.** An `own T?` field is a pointer; the
  handle taken out of it is rooted nowhere the checker knows, and the static
  check behind `free_ref` fails. `release` therefore checks the handle as
  it runs (`index_of` with a message). `handle_of`, which would make an
  `own` handle from any reference or index, was not provided: it is the
  forge that makes the exactly-once claim false.
* **The plain operations are closed to elements that own.** 4.6 leaves
  `free` beside `release`. With both open, `free` of a slot whose element
  still owned a run compiled, and the next `take` overwrote the run's only
  handle. The pools' plain operations now begin with
  `const assert !holds_own<T>`: a query and a statement
  ([const_for.md 11.3 and 11.7](const_for.md#117-const-assert)) added for
  this, since a library had no way to refuse an instantiation with a message
  of its own. A `slice_pool` of such elements is refused outright, where
  before every operation on one failed somewhere in its body.
* **Owned relative links are in.** 4.6 and section 9 defer them. They took
  forty lines, since a link loads as the reference it stands for, and they
  are how Goose's pool structures link: four bytes, and the type names the
  pool.
* **A local pool ends by `release_all`.** 4.6 gives a local pool the compare
  `free_slots.len == items.len`. The compiler knows arrays, not the
  library's pool, so the rule is the array's: `items` must be empty, which
  `release_all` brings about by popping. A pool whose elements own slots of
  the same pool cannot shrink (§5.1), so it cannot be a local.
* **Runs of `own`-holding elements are rejected**, where a trusted fn would
  end one: nothing in the text could say each element was emptied.
* **Size.** About 1,600 lines of C++ (700 of them the new
  `typecheck_own.h`), 70 lines of Goose in the two pools, and 80 error
  fixtures, against section 6's "the largest new piece: `defer`'s flow work
  was 196 lines".

## References

Spec and implementation sections referred to above:
[§3.9 relative references](../goose_spec.md#39-relative-references),
[§4.1 passing by size class](../goose_spec.md#41-everything-is-a-value-passing-by-size-class),
[§5.1 grow-only](../goose_spec.md#51-grow-only-),
[§5.2 grow-shrink](../goose_spec.md#52-grow-shrink-),
[§5.4 reusable arrays](../goose_spec.md#54-reusable-arrays-safe-allocation-escape-hatch),
[§6.6 `defer`](../goose_spec.md#66-defer),
[§7.8 recursion](../goose_spec.md#78-recursion),
[§7.9 `return … from`](../goose_spec.md#79-return--from-long-distance-return),
[§9.4 type-safe reuse](../goose_spec.md#94-type-safe-reuse-and-stale-references),
[§9.5 writability](../goose_spec.md#95-writability);
reference.md [§11 relative references](../reference.md#11-relative-references),
[§12 lifetimes](../reference.md#12-lifetimes-roots-stores-and-writability),
[§13 growing and shrinking](../reference.md#13-growing-shrinking-and-assigning-whole-arrays),
[§14 pools](../reference.md#14-reusable-pools),
[§16 `defer`](../reference.md#defer);
the stdlib's [slot_pool](../stdlib.md#slot_pool) and [slice_pool](../stdlib.md#slice_pool);
[scope_exit_cleanup.md](scope_exit_cleanup.md), [coroutines.md](coroutines.md),
[llm_frontier.md](llm_frontier.md), whose §5.5 holds the survey of linear
and affine type systems (Austral, Swift, Mojo, Rust, OxCaml, Cyclone, Pony,
Vale, Cogent) that this note does not repeat.

Compiler functions named: `NoRelRefCopy`, `NoRelRefTempRead`, `HasRelRefT`
(`src/typecheck_types.h`), `NoSelfRelCopy` (`src/codegen_construct.h`),
`CheckDefer`, `CheckDefers`, `JoinFlow`, `MergeFlow` (`src/typecheck_flow.h`),
`UsedAfter` (`src/typecheck_exprs.h`), `RegisterDefer`, `EmitRestores`,
`EmitDeferBody` (`src/codegen_frames.h`), `IsFlat`, `ImageSafe`
(`src/typecheck_types.h`), `FnSpec::needs` (`src/ast.h`). The `defer`
landing is commit `e1d95ed0`.

[wadler]: https://homepages.inf.ed.ac.uk/wadler/topics/linear-logic.html "Wadler: Linear types can change the world! (1990), and related papers"
[ocaml-effects]: https://ocaml.org/manual/5.2/effects.html "OCaml 5.2 manual, chapter: Effect handlers (one-shot continuations, discontinue, leaks of unresumed fibers)"
[wg-async-linear]: https://hackmd.io/@wg-async/ryDIrSATn "Rust wg-async notes: two readings of linearity (type-system must_use, or Drop guaranteed to run)"
[wuyts-linearity]: https://blog.yoshuawuyts.com/linearity-and-control/ "Wuyts: Linearity and Control (March 2023)"
[boats-trilemma]: https://without.boats/blog/the-scoped-task-trilemma/ "boats: The Scoped Task Trilemma (April 2023)"
[boats-cleanup]: https://without.boats/blog/asynchronous-clean-up/ "boats: Asynchronous clean-up (February 2024)"
[cpp-destroy]: https://en.cppreference.com/w/cpp/coroutine/coroutine_handle/destroy "std::coroutine_handle::destroy"
[oldnewthing-cleanup]: https://devblogs.microsoft.com/oldnewthing/20211007-00/?p=105777 "Chen: C++ coroutines, the cleanup path destructs the variables live at the point of suspension (October 2021)"
[zig-1261]: https://github.com/ziglang/zig/issues/1261 "Zig issue 1261: cancel of a coroutine at a suspend point runs defer and errdefer in scope"
