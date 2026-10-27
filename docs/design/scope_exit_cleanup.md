# Cleanup at scope exit: `defer`, destructor types and pool finalizers

<details>
<summary>Table of contents</summary>

- [1. Where Goose stands](#1-where-goose-stands)
- [2. What needs cleaning up](#2-what-needs-cleaning-up)
- [3. Design options](#3-design-options)
  - [3.1 `defer`](#31-defer)
  - [3.2 Destructor types](#32-destructor-types)
  - [3.3 Pool finalizers](#33-pool-finalizers)
  - [3.4 Scoped pool allocations](#34-scoped-pool-allocations)
  - [3.5 Library helpers on top of `defer`](#35-library-helpers-on-top-of-defer)
  - [3.6 Only on failure](#36-only-on-failure)
- [4. Consequences for the implementation](#4-consequences-for-the-implementation)
  - [4.1 How exits are emitted today](#41-how-exits-are-emitted-today)
  - [4.2 A `defer` is a scope entry](#42-a-defer-is-a-scope-entry)
  - [4.3 `return … from` and block returns](#43-return--from-and-block-returns)
  - [4.4 Checking the body](#44-checking-the-body)
  - [4.5 Code generation and the optimizer](#45-code-generation-and-the-optimizer)
  - [4.6 What never runs them](#46-what-never-runs-them)
  - [4.7 What destructor types would add](#47-what-destructor-types-would-add)
  - [4.8 General downsides](#48-general-downsides)
- [5. Examples](#5-examples)
  - [5.1 Code in the repository today](#51-code-in-the-repository-today)
  - [5.2 Patterns to come](#52-patterns-to-come)
- [6. Recommendations](#6-recommendations)

</details>

Research note, October 2026. Of what is described here, `defer` (3.1) is
implemented as section 6 recommends, with `exception::try`'s `finally`
(3.5); the pool finalizers, scoped allocations, `errdefer` and destructor
types are not. The question was whether Goose should run code when a scope
ends, and if so in which of the forms other languages have settled on: an
explicit `defer` statement, destructors on types, or something shaped by
Goose's own pools.

## 1. Where Goose stands

Goose frees memory by scope exit alone. The spec says it directly:
"There is no ownership transfer beyond the return move, no destructors, no
`Drop`, no reference counting. Deallocation is exclusively scope exit
resetting stack pointers" (§4.1), and the tutorial makes a feature of it:
at a closing brace "No destructor runs. No list is walked. The cost of
'freeing' that buffer is one store." Two other parts of the language lean
on the same fact:

* `return … from` (§7.9) needs no unwinder because every frame it leaves
  has nothing to destroy: each intermediate frame restores its watermarks
  and returns a dummy value, and its epilogue does nothing else.
* The `exception` module does not offer `finally`
  ([stdlib.md](../stdlib.md#exception)). A cleanup callback can run after
  normal completion or a caught throw, but a block's lexical `return` or a
  `return … from` aimed past the helper leaves through it without running
  anything, so a library helper cannot promise cleanup on every exit. Only
  the language can make that promise.

[owning_dynamic_data.md](owning_dynamic_data.md) left the matter open for
its proposed opaque resources: "Cleanup remains unresolved."

Since then two kinds of thing that are not memory, and do need releasing,
have become ordinary in Goose programs: pool slots and runs (`free`,
`free_slice`, spec §5.4), and native handles from the graphics, audio,
physics and ui packages (`release`, `destroy`, `close`). Both are released
by hand today, and the samples show both the discipline this requires and
the places where it slips (section 5).

## 2. What needs cleaning up

There are three kinds of thing, with different consequences when their
release is forgotten:

1. **Pool storage.** A slot or run that is not given back stays allocated
   until the pool's scope ends. Nothing unsafe happens; the pool only
   grows. The pairs are `alloc_index`/`alloc_ref` with `free`, and
   `alloc_slice` with `free_slice`. The hard cases are nested ones: a node
   in one pool owns a run in another
   ([26_file_tree](../../samples/26_file_tree.goose)), or two strings in a
   byte pool (`bench/kostya/goose/src/cache_simulation.goose`).
2. **Native resources.** A handle into C: a GPU buffer or texture, a sound,
   a physics world or body, a ui context. Forgetting one leaks GPU or
   library memory until the package's `close()` or the process ends. These
   handles carry generations, so using a released one is a reported misuse
   rather than a crash; forgetting the release is silent. The `os` module
   has no handles at all: every file operation is whole-file
   (`read_file`, `write_file`, `list_dir` into a builder), which is the
   clearest sign of a library designed to avoid the question. Streaming file
   access, sockets or child processes would bring it back.
3. **State to restore.** A global or a field changed for the duration of a
   scope: a nesting depth, a bound pipeline, a current directory. Today
   every exit of the scope restores it by hand, or the code is arranged so
   there is only one exit.

These do not need cleanup: memory of any kind, strings, the dictionary and
the other library containers (their contents are values), threads (a
worker's return releases its storage and `thread_wait` joins it), and
queues.

## 3. Design options

### 3.1 `defer`

A statement whose body runs when the enclosing block ends:

```goose
fn load_level(path: u8[:]) -> Level {
    let f = os::open(path);                       // hypothetical handle API
    guard f.id != 0 else { return Level { .. }; }
    defer os::close(f);
    let header = read_header(f);                  // may `return … from` out of here
    return parse_body(f, header);                 // close runs after the body is built
}
```

Proposed semantics:

* **Bound to the block, not the function.** This is Swift's and Zig's rule,
  not Go's. The body runs when the block the `defer` is written in ends,
  however it ends: falling off the end, `break`, `continue`, `return`, a
  block's lexical return through a higher-order function, or a
  `return … from` passing through the frame. A `defer` in a loop body runs
  once per iteration. In Goose the block is already the unit of ownership,
  so a cleanup belongs with the storage it pairs with; Go's function-scoped
  `defer` inside a loop is that language's best-known trap.
* **Only those run past.** An exit runs the defers that lie textually before
  it in the scopes it leaves, latest first. This is the rule the code
  generator already applies to watermark restores ("every exit path restores
  only the watermarks of declarations it ran past",
  `src/codegen_stmts.h`), and it means the set of pending defers at any
  exit is known at compile time, so no runtime list is needed.
* **Evaluated at exit.** The body is ordinary code run later; it sees
  variables as they are then. Go's evaluation of the deferred call's
  arguments at the `defer` is a second thing to remember and is left out.
* **After the value, before the storage.** On a `return`, the result is
  constructed at its destination first (§7.3), then the defers run, then the
  scope's stacks are released. So `defer close(f); return read(f);` reads
  before closing, and a `defer` may read the scope's locals.
* **The body cannot leave, and is free of long-distance returns.** No
  `return`, `break` or `continue` may leave the body, and it may not
  contain a `return … from`, `exception::throw` or trailing block whose
  `return` targets an outer function. Beyond that, it may not call a
  function that *contains* long-distance return machinery anywhere beneath
  it, including a catch, even a function that throws and catches entirely
  inside itself. Section 4.3 says why the stronger rule is needed and what
  the relaxation would be; the compiler can check it with one transitive
  flag computed in call-graph order. The body may abort.
* **No writing what is being returned.** The body may not write a local
  that any `return` of the function returns, or storage rooted at one
  (section 4.3). Reads are harmless.
* **Not outside a function.** Globals have no scope exit. A `defer` is an
  error at top level and in a global initializer.
* **Syntax.** `defer <statement>;` and `defer { … }`. `defer` can be a
  contextual keyword like `from`: at the start of a statement, followed by
  `{` or an expression, it does not form a valid statement today (a
  trailing block needs a call's parentheses before it). A reserved word
  reads more plainly and matches `guard`; either works.

`defer` puts the release next to the acquisition and runs it on every exit,
including the exits the author did not write (a `return … from` out of a
callee). It does nothing about ownership: a resource created in one
function and released in another, or stored in an array element, is not
affected by it.

### 3.2 Destructor types

This is the C++ and Rust approach: a type declares what to do when a value
of it dies, and the compiler calls that at the end of the value's life.
What "the end of its life" means depends entirely on ownership: exactly one
owner runs the destructor. This is where Goose's value semantics get in the
way. A handle is a fixed-size struct, `struct Texture { id: u32 }`, and
fixed-size values are copied freely: assigned, passed by value, pushed,
stored in fields. "Release a `Texture` when it leaves scope" releases it
twice at the first copy.

A destructor type therefore has to be **non-copyable**. Goose already has
the machinery for that, because non-fixed values are non-copyable: they
bind by reference at reference-typed destinations and are an error at
value-typed ones without an explicit `copy` (§4.1, passing by size class).
A destructor type could keep its fixed layout but be treated like them for
copying. It would be constructed in place where it will live (an rvalue
into a `let`, a `push`, a field initializer) and bound by reference
everywhere else, `copy()` would be an error, and the return move would be
the only way out of a scope. Then every value has exactly one storage
location and the destructor runs when that location dies:

* scope exit for a local or a by-value parameter;
* removal from an array: `pop` moves the value out, but `pop_n`, `resize`
  downward, `clear`, a shrink of a grow-shrink array, and a pool's `free`,
  `free_slice` and shrinking `realloc_slice` destroy what they drop;
* reassignment of a `var`, a field or an element;
* the death of the container that holds them: a walk over the elements, the
  first O(n) scope exit in Goose.

The benefit is real: a forgotten release becomes impossible, and the cleanup
composes into containers. `struct Mesh { vertices: Buffer, indices: Buffer }`
in an array of chunks releases itself when the array does or when a chunk
is overwritten. Section 4.7 lists the costs. In short, the feature touches
every destination rule in the checker, every builtin that copies or drops
elements, partial results abandoned by exits, threads and queues,
serialization, and the standard library's containers, which were written
against plain value semantics.

### 3.3 Pool finalizers

A pool finalizer is a cleanup tied to the pool slot rather than to the
value. The idea behind it is that the awkward manual frees in the
repository are the nested ones, where freeing a slot must also free what
that slot's element owns in another pool. The pool declares what to do
with an element as its slot or run is given back:

```goose
reusable var nodes: Node[>..] = [] { runs.free_slice(it.entries); };
```

The compiler calls the body from `free(i)` before the slot joins the
freelist, from `free_slice(s)` for each element of the run, and from
`realloc_slice` for the elements a shrink drops. Optionally it also runs for
every live slot when a local pool's scope ends; a global pool never ends.
The live set is everything not on the freelist. To find it, a slot pool
would keep a freed bit per slot or sort its freelist once; a slice pool's
freelist is already a tree of spans sorted by index, so walking the gaps
between them is natural. For a pool of native handles, the exit walk is the
main reason to have a finalizer. For a pool whose finalizer only frees runs
of another pool that dies with it, the walk is wasted work, and the
declaration could say so.

Two properties make this cheaper than destructor types. There is no copy
problem, since the hook runs on the slot's bytes when the slot is released,
whatever copies of the element exist elsewhere; a stale copy or index is the
type-safe staleness §5.4 already allows. And there is no new value class or
analysis: it is a wrapper function around `free` that the compiler does not
let the program bypass, whereas today the discipline is "always call
`release_node`, never `nodes.free`".

The hard part is context. A finalizer frees into some other pool, and that
pool must be reachable from it. Where the sub-pool is a global, as in
`26_file_tree`, a plain function serves. Where it is a local handed down by
parameter, as `strings` is in `cache_simulation`, the finalizer must capture
it. The trailing-block form above can do that: a block can use the
enclosing function's locals (§7.6), and a pool is a variable of some
function. A pool reference parameter carries the freelist beside the
address, and the finalizer's environment would be passed along with it in
the same way. Since callers are specialized per root, the hook is usually
known statically; a pool reference that may refer to one of several pools
would require those pools to share one finalizer.

What finalizers do not cover: handles held in locals, state restores,
anything not in a pool. They are a second mechanism to learn next to
`defer`, and the two overlap only where a pool element is also a local's
responsibility.

### 3.4 Scoped pool allocations

For the case "allocated in this scope, released at its end", there could be
a form where the compiler writes the `defer` and the checker forbids
escape:

```goose
scoped let s = pool.alloc_slice(n);     // freed when this block ends; cannot escape
```

A slice from `alloc_slice` is rooted exactly at the pool (§5.4, §9.2). The
scoped form would root it at the declaring variable instead. Everything else
is done by the checker as it stands: the store rule rejects keeping the slice
anywhere that outlives the scope, the return rule rejects returning it, the
branch-value rule rejects handing it out of a branch, and a function it is
passed to is specialized for that root and gets the same checks. A manual
`free_slice` of it is a compile error, so there is no double free. The
compiler emits the `free_slice` at the scope's exits exactly as a `defer`
would. `alloc_ref` admits the same treatment; `alloc_index` does not, since a
plain integer escapes freely, which is already the one hole §5.4 accepts.

This is the answer that fits Goose for "allocation inside the reusable
arrays cleaned up automatically" in the local case, and it needs no
analysis that the checker does not already have. The open question is
`realloc_slice` of a scoped run, which moves the run: either that is an
error, or the result is scoped the same way and must be assigned back.

### 3.5 Library helpers on top of `defer`

Once `defer` runs on propagation paths, helpers the library cannot write
today become sound:

```goose
fn with_pass<F>(target: Texture) { begin_pass(target); defer end_pass(); F(); }

fn try<E, Body, Catch, Finally>() {
    defer Finally();
    for error in handler<E>(Body) { Catch(error); }
}
```

`Finally` runs after a normal completion, after a caught throw, after a
throw that escapes to an outer handler (a propagation through this frame),
and after a lexical `return` out of `Body` (also a propagation through this
frame). That is the `finally` that stdlib.md explains it cannot offer. The
note there would change, and `exception::try` would gain the overload. A
`Finally` body is a defer body and follows its rules: it cannot throw, and
it cannot call anything that uses exceptions internally (3.1, 4.3).

### 3.6 Only on failure

Zig's `errdefer` runs only when the scope is left by an error. Goose can
tell the two apart at compile time: "left by error" is a long-distance
return passing through, and the code generator already emits a separate
path for it (`PropagateReturn`). A `defer` variant that runs only on that
path lets a builder undo a half-made structure (for example, free the node
whose children failed to parse) without a flag and a second exit. It is a
small addition once `defer` exists. Nothing in the repository asks for it
yet.

## 4. Consequences for the implementation

Most of this section is about `defer`, because it is the primitive: a
finalizer or a scoped allocation is a `defer` the compiler writes, and a
destructor is a `defer` on every storage location. Section 4.7 lists what
the destructor form adds.

### 4.1 How exits are emitted today

The code generator keeps a stack of scopes, `CScope`, in
`src/codegen_frames.h`. A declaration that takes data-stack storage records
a (stack, saved top) pair in each scope where the stack must be restored
(`SaveBase`). `EmitRestores` emits one scope's restores, latest first, and
`EmitExitRestores(to)` emits those of every scope down to `to`. Every way
out of a scope goes through them:

| Exit | Where | What happens |
|---|---|---|
| falling off the end | `PopSc` | restores |
| `break` | `GenBreakPath` | the exit's value is built, then restores down to the loop or block, then a `goto` |
| `continue` | loop emission | restores for the declarations it ran past, then a `goto` to a label placed after the fall-through restores |
| `return` | `Epilogue` | the value is already at its destination; `EmitExitRestores(0)`, cached tops flushed, the `return … from` destination records restored, C `return` |
| a frame a long-distance return passes through | `PropagateReturn` | the same, then a dummy C value |
| the `return … from` site | `GenFromReturn` | the values go into the target's channels, then `PropagateReturn` |
| the catch after a call | `EmitRfCheck` | the in-flight value is stored at the caller's destination, then `Epilogue` |

A valued exit from a block that has storage open builds its value above that
storage and moves it down after the restores (`ScopeTop0`, `DstTop0`,
implementation.md §6.5). The typechecker has already removed statements
after a proven exit, and `termjump` stops emission after one.

### 4.2 A `defer` is a scope entry

The natural implementation is for a `CScope` to hold its deferred bodies in
the same ordered list as its watermark saves, and for `EmitRestores` to walk
that list backwards, emitting a body where it meets one and a store where it
meets a save. Every exit path in the table above then runs the right defers
in the right order with no further change, and the rule "only those run
past" follows from the list being built as statements are emitted.
Reverse order also puts each body before the release of every declaration
that precedes it, which is the storage it can name.

Inlining and function values need no new mechanism. An inlined call becomes an
`InlineBlock`, and its `return`s exit that block (`Return::CgStmt`,
implementation.md §4). A function value's body is spliced into the caller, with
its locals treated as ordinary locals (`EmitFvCall`, §6.6). `const for` copies
its body for each iteration. In all three cases, a `defer` ends up in an
ordinary scope of the enclosing function.

### 4.3 `return … from` and block returns

The three sites are already distinct, so the behaviour is well defined:

* **An intermediate frame** runs its defers in `PropagateReturn`, after the
  in-flight values are in the target's channels. A nonfixed value sits on
  the target's destination stack, which is in the in-use set from the call
  site through the target's whole dynamic extent (§10.3): no frame in
  between can own data there, and neither can anything that a defer body in
  one of those frames allocates or calls. Fixed values sit in the per-target
  thread-local channels. So the body cannot disturb what is in flight.
* **The target** runs its defers at the catch, in the `Epilogue` that
  `EmitRfCheck` emits after storing the value at its destination, so the
  result is complete before they run.
* **The returning function** runs its own defers after `GenFromReturn` has
  built the values into the channels, which matches an ordinary return.

A block's lexical return through a higher-order function is the same
machinery and behaves the same way: the function the block is written in is
the target.

What can go wrong is **any long-distance return machinery running while a
long-distance return is in flight**. Today nothing executes between a
`return … from` and its catch except epilogues, and the propagation state
relies on that: `gs_rf` is one thread-local flag, and the channels are per
target. A defer body in an intermediate frame runs in exactly that window.

A deferred body that performs or propagates a long-distance return would
overwrite the flag already being propagated. Its own propagation would start
inside the frame's `PropagateReturn`.

A **contained** throw can also overwrite that flag. If a callee both throws and
catches internally, its `hasrf` is false because it does not propagate. Its
throw still writes `gs_rf`, and its catch clears it (`EmitRfCheck` sets `gs_rf =
0` where propagation ends). The intermediate frame then finishes
`PropagateReturn` with `gs_rf == 0`. Its caller sees no propagation and accepts
the dummy return value as a real result, without an error. For example, `defer
close(x)` would silently miscompile if `close` used `exception::try` three calls
down.

A callee that re-enters the target of the pending return could also overwrite
that target's `gs_lret` and `gs_fval` channels if its inner return fires. For
these reasons, forbidding calls to propagating specializations is not enough.

This is C++'s "destructors must not throw", and unlike C++ it can be a
compile-time rule, in one of two strengths:

* **Rf-free bodies.** A body may not contain, or call anything that
  transitively reaches, a `return … from` site or a catch, propagating or
  contained. This needs one more flag beside `hasrf`, computed in
  call-graph order like `hasrf` (inside a recursive cycle the flag is known
  once the cycle's rounds settle, so a body that calls into a cycle is
  checked then), and one rejection where the body is written, naming the
  callee that brings the machinery in. Nested propagation and re-entry of
  the in-flight target are then impossible by construction, and the
  interaction needs no runtime state: nothing executes during propagation
  except epilogues, and defers are part of the epilogue. The drawback is
  that a cleanup cannot call code that uses exceptions internally.
* **Save and restore.** Alternatively, each body runs with `gs_rf` saved,
  cleared and restored around it, which admits contained try/throw with
  *other* targets, since their channels are distinct. It does not cover a
  body that re-enters the in-flight target, because the body would
  overwrite that target's channels. Covering that case needs either saving
  that target's channels too (chosen at run time by `gs_rf`), or a check
  that the targets the body can reach internally are disjoint from the
  targets that can pass through the frame. Escaping propagation stays
  forbidden under either strength, since it would start a `PropagateReturn`
  inside another one.

The first strength is the one to ship; the second is a relaxation to adopt
if the first proves too strict for real cleanups. Without one of these
rules, the validity rule for `return … from` ("every call of a function
containing it lies within a call of the target") would also have to treat
defers as call sites on every exit path; with either rule it does not have
to.

**Named results.** A returned local is placed at the destination from its
declaration (guaranteed NRVO, §7.3), so after `return x` the local's bytes
are the caller's value, and the epilogue has already written its count into
the receiving header. A defer that then pushes to `x` changes what the caller
receives and disagrees with the length already recorded. Hence the rule that
a body may not write a local that any `return` returns, or storage rooted at
such a local; the checker's roots can identify both. The simplest form of
the rule forbids naming such a local at all. A relaxation that allows reads
is possible, but the NRVO analysis, which requires the return to be the
local's last use, would then need to learn that a read in a defer is not a
use that blocks placement.

### 4.4 Checking the body

The body is a block, checked as though written at the end of its scope:

* **Stack assignment.** Its temporaries get indices above everything live
  at the scope's end. Assigning them where the `defer` is written would let
  a local declared after it take the same index (`AllocStk` hands out the
  lowest free one), and the two would then be live together at the exit. A
  `return`'s destination stack is in use from the caller's side already, so
  a body's temporaries cannot be placed on it.
* **Lifetimes and effects.** At the scope's end every local of the scope is
  live, which is a superset of what is live at any earlier exit, so
  checking there makes the store, shrink and growth rules conservative for
  those earlier exits. The body's writes and growth join the function's
  summary like any other statement's.
* **Reachability.** A body that cannot complete (it aborts) makes every exit
  of the scope abort; `mayreturn` composes as for any call.
* **The front end.** A statement kind, parser recovery for it, the LSP's
  outline and hover (`src/lsp_analysis.h`), the
  tree-sitter grammar and the VS Code highlighting, and the keyword list in
  reference.md appendix A.
* **Tests.** Execution fixtures for each exit kind, including a block return
  and a `return … from` through a frame with a defer; rejection fixtures for
  each body restriction.

### 4.5 Code generation and the optimizer

* **Cached stack tops** (implementation.md §6.10). A body with a call is a
  call site at each exit. `Epilogue` and `PropagateReturn` already flush.
  A `break` or `continue` inside a loop whose stack tops are cached in
  locals gains a flush and reload where the body can reach that stack
  (`SyncReach`). The cost is modest, and arises only in functions that use
  `defer`.
* **Code size.** Each exit path copies the bodies textually, as C++ does for
  destructors. If that ever matters, one cleanup label per scope with a
  small "where next" variable is the standard alternative; bodies are
  mostly a single call, so duplicating them is the right way to start.
* **Tail recursion** (`src/optimize_tre.h`). The
  accumulator rewrite turns `return E op self(args)` into a loop. With a
  `defer` in a scope around that return the body must run after the call
  returns, which the loop cannot do, so such specializations are excluded
  from the rewrite. A tail-call optimization of any other kind would meet
  the same exclusion.
* **Inlining** is covered by 4.2; the thresholds should count a deferred
  body's nodes once, not per exit.
* **`--unsafe-no-rf-check`**, the JIT backend and the Wasm plan are
  unaffected: the output is plain C.

### 4.6 What never runs them

An `abort`, a failed `assert` or bounds check, `exit(code)` and a guard-page
hit end the process, and the operating system reclaims files and GPU
objects. A contained worker's abort returns to `gs_run_contained` through
`setjmp`/`longjmp`
(`src/runtime/runtime_threads.h`), discarding the
worker's frames without running anything, so §11.2's statement that
"resources it opened through `extern fn` stay open" remains true.
Workers still running when `main` returns are killed. A global never exits:
a resource kept in one is released by code in `main`, or by the process
ending. None of this is different from `defer` in Go or Zig, and it should
be documented beside the feature rather than worked around.

### 4.7 What destructor types would add

On top of everything above, in rough order of size:

1. **A new value class.** Non-copyable fixed-size values, which means
   extending every destination rule in §4 and the checker's size-class
   handling: declarations, parameters, pushes, fields, elements, literals,
   branch values, function-value results, dispatch results, `copy`,
   `default`.
2. **Destruction at every removal.** `pop_n`, `resize`, `clear`, grow-shrink
   shrinks, reassignment of variables, fields and elements, pool `free`,
   `free_slice` and shrinking `realloc_slice`, and the death of a container,
   which is a walk. Generic code has to do the right thing per
   specialization: `dictionary<K, Texture>` must destroy on remove, clear and
   overwrite, and the library's containers were written with plain
   assignment (`slots[i].val = v`), so every one needs auditing.
3. **Abandoned partial results.** An exit taken while its destination holds
   part of a value abandons that part (§7.3, Exits). A half-built
   `Texture[>..]` named result abandoned by a `return … from` from a callee
   would leak its elements unless the exit destroys the completed prefix.
   That is a new emission site, which must know how many elements were
   complete.
4. **Everything that copies bytes.** Thread arguments, queue elements,
   `to_bytes`, `from_bytes` and global copies into worker programs all
   duplicate values; destructor types would be excluded from all of them,
   which adds a new restriction to "flat".
5. **Double destruction.** A pool slot freed twice runs the destructor
   twice. The hook has to tolerate it, as the packages' generation-checked
   handles already do for a double `release`.
6. **Invisible cost.** Destructors would put control flow and calls at
   exits, which the language has deliberately kept free of them. That
   includes the container walk that the tutorial says never happens.

The benefit is limited to resources that are stored in containers or move
between scopes. Today these are the chunk meshes of `30_mini_minecraft`,
and the entity handles a game would keep in a pool, which section 3.3's
finalizers also cover.

### 4.8 General downsides

* **Exits become implicit.** Reading a function means scanning for defers
  before trusting a `return`. Bodies are short, and the rule that only the
  defers an exit has run past apply keeps the set small, but defers are one
  more thing to look for, in a language that has few such things.
* **Cost on every exit.** A function with a `defer` runs the deferred code
  on every return path; a loop with a `defer` runs it once per iteration.
  The cost is visible in the source, which is the most that can be said for
  it.
* **Not ownership.** A resource that outlives the scope that created it is
  released by hand, as before. Section 5.1 shows that this is where the
  repository's real release bugs would be, if it had any.
* **No error channel.** A failing cleanup can only abort or be ignored, as
  in every language with the feature.
* **Two mechanisms.** `defer` plus finalizers is more to learn than either
  alone; the finalizer's justification is that its cases are exactly the
  ones `defer` cannot express.

## 5. Examples

### 5.1 Code in the repository today

**An early return past an open.**
[29_ui_todo.goose](../../samples/29_ui_todo.goose) opens the window and
then, if the ui context cannot be created, writes the error and returns
without `gfx::close()`. The leak is harmless because the process ends, but
every handle API produces code of this shape. With `defer`:

```goose
guard gfx::open("Goose: a ui to-do list", 760, 520, gfx::WINDOW_RESIZABLE) else { … }
defer gfx::close();
var ctx = ui::create_from_memory(ROBOTO, 16.0);
guard ctx.id != 0 else { write_stderr(str("ui: ", ui::error(), "\n")); return; }
defer ui::destroy(ctx);
```

The cleanup calls at the end of `27_gfx_cube`, `28_physics_boxes`
(`physics::destroy(world); gfx::close();`), `30_mini_minecraft` and
`31_mini_doom` (`if sound_ready { audio::close(); } gfx::close();`) all
become a `defer` next to the open, and `31_mini_doom`'s conditional close
becomes `if audio::open() { defer audio::close(); … }` or stays a
conditional in a `defer` body.

**A node that owns a run.** `release` in
[26_file_tree.goose](../../samples/26_file_tree.goose) does the pair of
frees that every free of a `Node` needs:

```goose
recursive fn release(n: Node&) {
    for e in n.entries { assert(e); release(e); }
    runs.free_slice(n.entries);
    nodes.free(nodes.index_of(n));
}
```

A finalizer on `nodes` makes the run's release part of the slot's:

```goose
reusable var nodes: Node[>..] = [] { runs.free_slice(it.entries); };

recursive fn release(n: Node&) {
    for e in n.entries { assert(e); release(e); }
    nodes.free(nodes.index_of(n));
}
```

The recursion over children stays where it is: it is a traversal, not a
cleanup, and a finalizer that recursed would be a pool walking itself.

**Two strings per node, in a local pool.** `put` in
`bench/kostya/goose/src/cache_simulation.goose`
frees the old value run before replacing it, and `remove_oldest` frees a
node's key and value runs before recycling the node through the cache's own
free list. The node array is a plain `[>..]` with a hand-made free list,
because a struct cannot hold a pool. A finalizer form that captures the
`strings` pool would merge the two free sites into one; `defer` does not
help here, since none of these releases is scope-shaped.

**Buffers owned by array elements.**
[30_mini_minecraft.goose](../../samples/30_mini_minecraft.goose) releases a
chunk's vertex and index buffers before rebuilding its mesh, and
[31_mini_doom.goose](../../samples/31_mini_doom.goose)'s `upload` releases a
buffer before growing it. These are reassignments of handles held in
elements and globals across frames. That is the case for destructor types,
which neither `defer` nor a finalizer covers. The same sample's HUD buffer
is kept across iterations on purpose, which is a reminder that scope exit
is not always the lifetime wanted.

**A `finally` the library cannot write.** `stdlib/exception.goose` says in
its header that it does not provide one. Section 3.5 shows the four-line
version that `defer` makes possible.

**Pool frees that are not scope-shaped.** The LRU benchmarks
(`bench/goose/lru.goose`) free the evicted node, and
[13_linked_list](../../samples/13_linked_list.goose) frees an unlinked one.
Nothing in them needs `defer`; they are the ordinary use of `free` and
stay as they are under every option.

Nothing in the repository allocates a scratch run from a pool for a
function's own duration and frees it on the way out. A local growable array
already serves as scratch space that is freed at scope exit, which is what
the language is built around. The pool-scratch case (section 3.4) arises
only where the scratch must be stored in the pool's address space, for
relative links, or where a struct's single growable is already taken.

### 5.2 Patterns to come

**File and process handles**, when `os` grows a streaming API:

```goose
fn count_lines(path: u8[:]) -> i64 {
    let f = os::open(path);
    guard f.id != 0 else { return -1; }
    defer os::close(f);
    var n = 0;
    var line: u8[>..] = [];
    while os::read_line(f, line) { n++; line.clear(); }
    return n;
}
```

**A resource per frame or per pass**, released whatever the frame does:

```goose
while gfx::frame() {
    let shadow = gfx::depth_target(2048, 2048);
    defer gfx::release(shadow);
    render_shadow(shadow);
    if gfx::key_pressed("Escape") { break; }      // shadow still released
    render_scene(shadow);
}
```

**Save and restore**, the third traditional use:

```goose
fn emit_block(out: u8[>..]&, body: Node&) {
    let saved = depth;
    depth++;
    defer depth = saved;
    for s in body.stmts { emit(out, s); }         // any early return restores depth
}
```

**Entities that own native objects**, with the pool doing the release:

```goose
reusable var entities: Entity[>..] = [] { physics::destroy(it.body); gfx::release(it.mesh); };
```

**Undo on failure** inside a builder that reports through `return … from`
(section 3.6), the only place where `errdefer` would be worth having:

```goose
fn parse_node(p: Parser&) -> NodeId {
    let id = nodes.alloc_index(Node { .. });
    errdefer nodes.free(id);                      // only if a parse error unwinds past here
    nodes[id].children = parse_children(p);       // may `return … from parse_file`
    return id as NodeId;
}
```

**Helpers that bracket a region** (`with_pass`, `with_file`, a `try` with
`finally`), written in the library once `defer` exists (section 3.5).

**Tests** that open the graphics or audio package headless and must close
it on every path, which today means a single exit per test body.

## 6. Recommendations

1. **Add `defer`**, scope-bound and evaluated at exit, with the three body
   restrictions of 3.1 (cannot leave, free of long-distance returns in the
   strong sense of 4.3, cannot write a returned local). Implement it as
   entries in the scope list beside the watermark saves (4.2), check it at
   the scope's end (4.4), exclude it from the tail-recursion rewrite, and
   document that it does not run on abort. It is the smallest change that
   makes handle APIs safe to use with `return … from`, and it is the
   primitive the other forms are built from. With it, give
   `exception::try` a `finally` and replace stdlib.md's note explaining why
   there is none.
2. **Then pool finalizers** (3.3), as a trailing block on a `reusable` or
   `reusable[]` declaration, run on `free`, `free_slice` and a shrinking
   `realloc_slice`, and at a local pool's scope exit. The motivating code is
   `26_file_tree` and `cache_simulation`; settle the pool-reference rule
   (pools a reference may choose between share one finalizer) and whether
   the exit walk is optional before building it.
3. **Scoped allocations** (3.4) only if alloc-at-entry, free-at-exit turns
   up in real code; it is `defer` plus a root the checker already enforces,
   so it can wait for demand without losing anything.
4. **Do not add destructor types now.** They need a non-copyable value
   class, destruction at every removal, abandonment handling, exclusion from
   everything that copies, and an audit of the library's containers (4.7).
   The code that would benefit is handles kept in arrays across frames, which
   the packages' generation-checked handles already turn from a hazard into a
   leak, and which finalizers cover when the holder is a pool. Revisit if
   handle-heavy programs show release bugs that `defer` and finalizers do
   not catch; the samples are where that evidence would appear first.
5. **`errdefer`**: wait. It is cheap once `defer` exists and nothing asks
   for it yet.
