# String constants, writability, and threads

<details>
<summary>Table of contents</summary>

- [1. What could be written before the fix](#1-what-could-be-written-before-the-fix)
- [2. The options](#2-the-options)
- [3. Compatibility and cost summary](#3-compatibility-and-cost-summary)
- [4. Recommendation](#4-recommendation)
- [5. Measurements](#5-measurements)
- [6. Globals were C statics](#6-globals-were-c-statics)
- [7. Side findings](#7-side-findings)

</details>

Before the changes described here, static data consisted of string literals
and `let` globals that workers could read. Storing a reference and reading it
back could make read-only data writable, a behavior called *laundering* in
the former §9.5. With workers, this allowed shared mutable memory. This note
records the affected paths and compares fixes measured on temporary
implementation branches:

1. a literal used as a slice is a hidden local on a data stack;
2a. a `u8` read-back is never writable, and constants are never stored;
2b. "may point at static data" tracked through storage (the
    `codex/readonly-literals` experiment, repaired);
3. static data is per program instance, and each worker gets its own copy;
4. a user-level `const` on any type, literals typed `const u8[:]`, `let`
   rebind-only, and no laundering.

Option 4 was adopted: it consists of the two commits preceding this note
(`Add const types; let is rebind-only; literals are const u8[:]` and `Give
every program instance its own globals`, §6). The other branches were
removed once the decision was made. Every one of them passed the full test
suite with its own fixtures and typechecked every benchmark; the samples
and the numbers are in §5.

## 1. What could be written before the fix

The original reproducer:

```goose
struct Box { s: u8[:] }
thread_fn worker() {
    var box = Box { s: "abc" };
    box.s[0] = 88;
}
fn main() {
    print("abc");
    thread_wait(thread_spawn(worker));
    print("abc");       // Xbc
}
```

`"abc"` is a `u8[:]` into `static uint8_t gs_str0[3]`. `Box { s: "abc" }`
stores it, and `box.s` reads it back. The read-back is writable by design:
the *path* is writable (`box` is a `var`, `s` is not a `let` field), and the
slice's own provenance has been lost. The write goes to the C static, which
every thread shares.

Tests of the compiler before the fix found additional affected paths:

* Every read-back path launders:
  * a field (`box.s[0]`);
  * an element (`views[0][0]`);
  * a `for x in views` copy;
  * a slice behind a *reference* to an element
    (`for &x in views { x[0] = … }`, `&views[0]`, the reference `push`
    returns);
  * a `match … &l` binder;
  * a value returned out of a container through a reference parameter
    (`fn view(b: Box&) -> u8[:] { b.s }`).
* **`let` globals have the same hole.** A thread may read any `let` global of
  flat fixed type (§11.2), on the assumption that it is never written. But
  for `let W = 78;`, storing `&W` into a reference field and reading it back
  gives a reference through which `W` can be written, from a worker too. In
  the same way, `TABLE[..]` for `let TABLE: u8[3] = "abc"` can write the
  constant's static storage. So the memory to protect is *static data plus
  every thread-visible constant*, not only literals.
* What does *not* launder: a direct write through a literal or through a
  `var` slice variable holding one (`s[0] = 1` after `let s = "abc"`), a
  `let` variable's field path, a by-value parameter's field path, and a
  write through a slice parameter given a literal (`zap("abc")`), which
  provenance already rejects.
* When a literal is used as a `u8[]` *value* (an inlined `return "abc"` as
  `u8[]`), its `[len][bytes]` image is also static. But a slice of it is a
  slice of a temporary, which cannot be stored or returned, so it cannot be
  laundered.
* Per-thread copies only matter for what a thread can *reach*. Slices,
  structs holding slices, and every `var` global are already out of a thread
  program's reach, so a `let GREETING = "hello";` global is not a concern (a
  worker cannot name it).

## 2. The options

### Option 1 — a literal used as a slice is a temporary on a data stack

**Rule.** A literal is a `u8` array value. It is copied into any array type
as today. When used as a `u8[:]`, it denotes a hidden `let u8[]` local of the
innermost scope, built from the static bytes at the point where the literal
is evaluated, exactly as if `let s: u8[] = "…";` were written there.
Consumers that only read the literal (`print`, `format`, `abort`, `==`,
`append`, `push`, array construction) keep reading the static bytes and
build no local. In a global initializer there is no scope to own the local,
and only main can reach a global's slice, so the literal stays static there.

This is what "string literal = array literal" means once the array has to
be stored somewhere. It is the only option that makes the language's *model*
complete (no memory outside the stacks), and it needs no provenance
machinery: the hidden local is an ordinary local, so a slice of it obeys
every existing lifetime rule.

**Language impact.** The lifetime rules restrict existing uses. A slice of a local
cannot be returned or stored outside its scope, so these no longer compile:

```goose
fn name(s: Shape) -> u8[:] { match s { Circle c => "circle", … } }   // 05_shapes, 19_vm
let verdict = block { guard p.alive else { break "gone"; } … };      // 01_tour
```

Three of the 25 samples fail (`01_tour`, `05_shapes`, `19_vm`), all on
"returning a slice of a literal" or "storing one outside its block". The fix
is to return `u8[]` (a copy at the destination) or `u8[>..]`, or to move the
literal out; none of the benchmarks or tests needed such a fix. Every
function that returns a `u8[:]` name for an enum, a common idiom, is
affected.

**Damage, runtime.** Each evaluation that needs a slice costs one memcpy of
the bytes plus a stack bump, and each function entry and loop iteration costs
a save/restore of the literal stack. The manifest benchmarks do not change:
their hot literals are equality operands and `append`/`format` arguments,
which take the static path. A micro-benchmark that passes a literal to a
user function, stores one in a struct literal and binds one to a `let`, 20M
times, is **+40%** (33.6 → 47.3 ms). The cost is small per use, but applies
at every call boundary that takes a slice.

**Implementation**: the typechecker gives the literal a synthetic `VarDef` at
the current depth (so `while` conditions had to move inside the loop scope,
where codegen already puts them). Codegen keeps one literal stack per
function. Its top is saved on function entry and on every loop iteration, and
restored when they exit. Saving at those points is what makes it sound: a
per-literal save would be restored on a `continue` taken before the literal
was reached, reading an unset base. That is a **pre-existing codegen bug**
for ordinary locals, see §6. Recursion works because each activation's
literal stack is one more `gs_sp + k` slot, which the spec allows:
"temporaries may consume slots proportional to depth" (§7.8).

Variants considered:

* (a) Treat the literal as an rvalue with a temporary's root. This would
  reject `Box { s: "abc" }` outright, which is far more damage.
* (b) Hoist all of a function's literals to its entry. This builds every
  literal on every call, including on the cold `abort("…")` paths.
* (c) A statement-scoped temporary, which is what call results are today.
  This would leave `let s = "abc"; use(s);` dangling (the checker currently
  allows binding a slice of a statement temporary, §6).

The branch also includes the no-store rule for constants from option 2a,
since the hidden local does not help with `let W = 78;`.

### Option 2a — a `u8` read-back is never writable; constants are never stored

**Rules.** There are two, and neither needs any analysis:

1. A reference or slice read out of storage whose pointee type is `u8` is
   never writable. The container it came out of does not tell what was
   stored there (that is what laundering means), but the pointee type does:
   a string literal could have been stored there, and static data is
   shared. Every other pointee type launders as before. A writable path
   through storage is still possible with a reference to the array itself
   (`u8[>..]&`, `u8[..]&`), which no literal can be.
2. A reference or slice into a thread-visible constant (a `let` global of
   flat fixed type) may be held in variables, passed down or returned, but
   is never stored into a field, element or global. References into a
   grow-shrink array already follow this rule (§5.2), for the same reason:
   a read-back can only launder what was stored.

Rule 2 exists because rule 1 cannot be extended to constants by type. The
first attempt did that, and the aggregate test file failed on `gheld.r = 7`
through an `i64&` field, because *some* file in the program declares a `let`
of type `i64`. Any program with `let W = 78;` would lose writes through every
stored `i64&`. Keeping the constants out of storage instead is a local and
exact rule, and it required no changes in the corpus.

**Damage.** None in the corpus: no test, sample or benchmark is affected.
The language change is visible, though: a `u8[:]` stored in a struct or an
array is read-only through that path. A design like `struct Cursor { buf:
u8[:], pos: i64 }` that writes `c.buf[c.pos]` must hold a `u8[>..]&` instead.
Since by convention `u8[:]` is used for reading (§3.10), this is a small
loss, and the diagnostic explains why ("it was read out of storage and may
point at a string literal, which every thread shares").

**Implementation**: under a hundred lines in the checker:

* a predicate on the pointee type, consulted at the three places a read-back
  is granted writability (`ContainerRead`, `PointeeWritable`,
  `ReadBackLVal`), at the `for` copy binding, and at the return mapping
  through a parameter (which used to take the argument's writability
  wholesale);
* the slice behind a reference to an element now counts as read out of
  storage;
* the store rule for constants in `FitsAt`.

A `maystatic` provenance bit carries the reason into the diagnostic.

### Option 2b — track "may point at static data" through storage

This was the `codex/readonly-literals` experiment, rebased and repaired. It
requires analysis across the whole program:

* every `Prov` gains two pointers into a monotone origin graph (`literal`:
  the pointee may be static; `contents`: the references inside this value
  may be);
* every store, call, return, default value, iteration, `break` value and
  match binding joins nodes;
* after the whole program is checked, the graph is saturated, and every
  recorded write through a node that reaches a static origin is an error.

The analysis is order-insensitive, so later stores, loop back edges and
later call sites of a shared specialization all count (the fixtures cover
these). The price is that it rejects a write even when, in a particular
execution, the write would happen before the literal is stored.

**What it missed.** It did not treat the slice behind a reference to an
element as a read-back: `for &x in views { x[0] = 88; }`,
`let r = &views[0]; r[1] = 89;` and `let e .= views.push("abc"); e[0] = 88;`
all compiled and wrote the static data. The rebased branch fixes that (with
the same one-line change option 2a needed) and adds the three fixtures. It
caught everything else probed here, including the constant-global cases and
the return-through-parameter case.

**Damage.** None in the corpus, and it is more precise than 2a: a `u8[:]`
field stays writable in a program that never stores a literal into that
container. The cost is about 300 lines across the checker, a fourth
provenance dimension next to roots, exactness and writability, and
diagnostics that say a write "may point into immutable static storage" but
cannot name the store that caused it.

### Option 3 — static data is per program instance

**Rule.** Static data belongs to a program instance (§1.2). The main program
has one copy; each worker (§11.2) starts from its own copy, taken from the
state the global initializers left. A write laundered through storage only
affects the instance that made it. Nothing else changes: there are no new
errors and no provenance tracking, and the single-threaded const-cast behaves
as before.

The spec already implies these semantics, since a `thread_fn` "is compiled
as a separate program"; option 3 applies them to the one thing those
programs still shared.

**Damage.** None in the language: every test, sample and benchmark compiles
and runs unchanged. At runtime, a program that spawns no worker keeps the
plain C statics, so its generated code is byte-for-byte unchanged. A program
that does spawn workers gathers the literals and thread-visible constants
into a struct, and main's instance is the struct itself. The snapshot is one
`malloc` + `memcpy` at startup, and each worker copies it at spawn
(kilobytes, small next to reserving its stack block). Every access goes
through the thread-local base `gs_st`, the same addressing every data-stack
access already uses through `gs_stks`. Forcing the struct form on a
single-threaded program (`--per-program-statics`) is the worst case for that
indirection. Even then, the literal micro-benchmark and the manifest
benchmarks are within noise of the baseline (§5), and the threaded
Mandelbrot sample, which reads three constants in its inner loop, runs in
the same time.

**Implementation**: about 150 lines, all in codegen and the runtime; the
typechecker only determines which globals are thread-visible constants.
Constants with runtime initializers are
initialized into main's copy by `gs_init_globals` before the snapshot, so a
worker sees what main computed.

### Option 4 — `const` in the type, mutable by default

**Rule.** Any type may be qualified `const`. `const i64[3]` is a value
whose contents cannot be written into, and `const T&` / `const T[:]` are a
reference and a slice through which the pointee, or the elements, cannot be
written. The qualifier is shallow: a reference read out of a `const Box&`'s
field is as writable as the field's type says. A string literal is a
`const u8[:]`. `let` is independent of all this: it keeps a binding from
being reassigned and says nothing about contents
(`let xs = [1, 2, 3]; xs[0] = 9;` and a write through `&xs` are fine).
`const x = e;` is sugar for `let x: const T`, and in a struct,
`const f: T` is sugar for `let f: const T`. Nothing else is annotated. A
parameter or result declared `u8[:]` is *generic over constness*: it is
checked as read-only in an instantiation that gets read-only data, which is
what provenance already did. `const u8[:]` on a parameter documents that
the function only reads.
The one place where `const` is *required* is a **slot**: a field, an
element, a global, an annotated variable or an assignment target holds a
read-only reference or slice only if its type says `const`. So what a slot
yields is exactly as writable as its type says, and the laundering rule is
gone: a `u8[:]` field can only ever have been given a writable slice.
`const` is added implicitly but never dropped implicitly, and an
`extern fn`'s parameters mean exactly what they declare. A by-value `for` or
`match` binding is a copy and cannot be written (the write would have no
effect). This is the one place where `let`'s old strictness was worth
keeping as a rule of its own.

This differs from 2b in kind: 2b infers what a container may hold and
forbids the write, while 4 has the programmer say what the container holds
and forbids the store. The check is local and exact, and the diagnostic
names the slot and the fix ("declare the slot const"). The model loses a
rule (laundering) instead of gaining an exception. The same feature lets a
struct hold a read-only view of anything, such as a `let` buffer or a
`bytes_of` view, which today is only possible by laundering. The same rule
protects constants: `&W` is a `const i64&`, which no `i64&` slot can hold.

**Damage, language.** Annotations are needed at slots that hold read-only
data, and `const` is needed where a global must be a constant. In the corpus
this meant 11 sites outside the standard library:

* three `extern fn` declarations taking a literal (`s: const u8[:]`);
* a `let s: const u8[:] = ""`;
* two `dictionary<const u8[:], …>` keyed by literals;
* a `dictionary<key, const u8[:]>` holding literal values;
* a struct field `word: const u8[:]` filled from that dictionary's keys;
* the mandelbrot sample's three `const W = 78;` globals its worker reads (a
  `let` global is no longer a constant a worker may read, since `&W` is a
  writable `i64&`);
* a `const N = 4;` used as an array size.

The standard library also needed `const` on its eight `os` extern
parameters and on `split`, whose parts become `const u8[:]` so that one
function serves literals and buffers alike. No benchmark changed. The churn
has the same shape as in C++: generic containers keyed by views need the
view type written as `const` (a `dictionary<u8[:], V>` given a literal key
is an error asking for `dictionary<const u8[:], V>`), and a function that
*stores* views of its input must choose a constness for the container,
since the container's element type is a slot. Three details required care
during implementation:

* `null` and `default<T>()` are "empty" values that must count as writable,
  or every non-`const` slot would refuse them;
* a load of a `const` value is a plain copy, while a reference to one is a
  `const T&`, so the qualifier has to move from the storage type to the
  reference type at every `&`;
* the flag for the `const` binding form has to survive the clones the
  checker and the inliner make of every declaration, or the contents
  silently become writable (the test that caught this is
  `const_value_write`).

In the first version of the branch, `let` made derived references `const`,
which reproduced the old transitive rule. The second commit takes that out,
and that is why the mandelbrot constants and the array size need `const`.

**Damage, runtime.** None. The generated C is the same as the baseline's,
except that the literal data is now emitted `static const` (the checker
allows no write to it, so a hole in the analysis would cause a fault rather
than corrupt memory).

**Implementation** (the first of the two commits): about 250 lines, covering:

* a `cq` bit on every type, handled in type equality, substitution and the
  dumper (`const` binds to the first `&` or `[:]` after the base type, and
  otherwise to the whole type; `(const i64[3])&` and `const (u8[:])&` are
  written with parentheses);
* the store rule at slots (a scoped flag set by field, element, assignment
  and annotated-declaration checks, and cleared by argument and return
  checks);
* const-adding coercions in `FitsAt` and branch unification;
* constness on read-back, and parameter and result constness;
* the `extern` check;
* in place of the transitive-`let` rule, which is deleted: a `letbound` mark
  on the last step of an lvalue path (the only thing `let` now forbids),
  contents writability read from the type at each path step, and the
  copy-binding rule for `for`/`match`.

Narrowing a `const T?`, loading a relative reference, and resolving a
`const Alias` all had to keep the qualifier.

### Others considered

* **Read-only pages.** Emit the literal data `const` (or `mprotect` it) so a
  laundered write faults. This has no cost and is a one-word change, but it
  turns a compile-time question into a crash, and it does nothing for
  constants with runtime initializers. It is worth doing *on top of* 2a or
  2b, as a check that the analysis has no holes, but not as the fix.
* **Copy on store.** Materialize a literal only where a slice of it is stored
  into a container. The slice being stored may be a variable bound to one of
  several literals, so the copy would be a runtime memcpy of an arbitrary
  slice. That has option 1's cost and option 1's lifetime rules, plus a
  stranger rule.
* **No laundering at all.** Track writability through storage precisely for
  every provenance, not just static. This is 2b generalized, and it rejects
  the documented const-cast (`let` locals laundered through a struct) that
  the spec chose deliberately.
* **Threads may not read `let` globals.** This closes the constant half
  without any of the above, but the mandelbrot sample's `let W = 78;` in a
  worker is the very use the exception exists for.

## 3. Compatibility and cost summary

| | tests (280) | samples (25) | benchmarks | spec change | runtime cost |
|---|---|---|---|---|---|
| 1 stack temporaries | all pass | **3 fail** (return/store of a literal slice) | all compile | literals become locals; §3.7 rewritten | memcpy per slice use; +39% on the literal micro-benchmark, benchmarks unchanged |
| 2a read-back by type | all pass | all pass | all compile | one rule each for `u8` read-backs and constants | none |
| 2b origin graph | all pass | all pass | all compile | laundering has an exception, described as an analysis | none |
| 3 per-program static | all pass | all pass | all compile | static data is per program instance | none without workers; snapshot + per-worker copy + TLS base with them, unmeasurable |
| 4 `const` types | all pass after 8 annotations | all pass after 4 (an extern, three constants) | all compile | `const` on any type; `let` is rebind-only; slots must say `const`; laundering removed | none |

## 4. Recommendation

Option 1 should be ruled out. It keeps all memory on stacks, but every function
that returns a name as `u8[:]` would stop compiling, and passing a literal to a
slice parameter would copy it. The implementation is no smaller than the
alternatives: it adds a stack per function with its own scoping and still needs
option 2a's rule for constants. These hidden copies also conflict with the
spec's stated goals.

Option 2b works and is the most precise, but it is the "complex analysis"
the discussion anticipated: about 300 lines that touch every place
provenance flows. In addition, its diagnostics cannot say which store
poisoned the container. The three paths missed by the first implementation
also suggest a substantial maintenance burden alongside the existing root
analysis.

Option 4 is preferable for the language design. It removes the laundering rule
and its "const-cast" loophole. Writability becomes a type property visible in
the declaration. This also supports read-only views in data structures, `extern`
declarations that state what C may write, and a `split` that accepts literals.

The cost is extra annotations on slots that hold read-only data: nine sites and
the `os` externs in the corpus. As in C++, a generic container keyed by views
must specify `const u8[:]`. Correct annotations are necessary to prevent the
thread race, but the checker enforces them: `const` is never dropped implicitly.
Static data is therefore provably unwritten and emitted as C `const`.

Compared with option 3: 3 has no cost in the language and no cost at runtime
without workers, and it keeps laundering, which the spec chose deliberately.
If laundering is worth keeping, 3 is the fix. The discussion that produced
§9.5 says the aim was to preserve const-correctness while limiting
annotations, and 4 keeps that property, because only slots require
annotations. So if the const-cast loophole was a compromise rather than a
feature, 4 is the better language, and 3 becomes unnecessary for literals.

Of the other options, the real choice is between **2a** and **3**.

* 2a is a language rule that needs no machinery: "a `u8` slice you read out
  of storage is read-only; a reference into a constant is never stored." It
  has no runtime cost and needed no changes in the corpus, and its
  restriction follows from the compiler's inability to determine what was
  stored. `u8[:]` is also commonly used for reading. Its cost is that the
  language now has a rule shaped by an implementation limit, and a struct
  holding a writable byte view has to hold a reference to the array instead.
* 3 changes nothing a program can see, except that workers stop sharing
  static data, which the spec's thread model already says. It keeps
  "acceptable as a corner case" true in the same single-threaded sense in
  which it was acceptable before, and it keeps the laundering rule the spec
  chose on purpose. Its cost is a runtime mechanism (~60 lines) that exists
  only in threaded programs, plus an indirection on static-data accesses in
  those programs, which did not show up in any measurement.

The recommendation is **option 4**. It requires few source changes, places
annotations where readers need to know whether stored views are writable,
and requires no runtime mechanism.
If the laundering rule is to stay as a feature, take **option 3** together
with rule 2 of option 2a (constants are never stored). Thread-visible
constants then stay plain C statics that the C compiler can fold, and only
the literals go into the per-program image. That is the smallest change
with the smallest language cost. 2a alone remains a sound, zero-cost
fallback.

## 5. Measurements

Windows 11, MSVC 19.51 `/O2`, best of the runs shown after two warm-ups
(`bench/run_bench.py`'s method), medium sizes.

`lit_micro` is the literal micro-benchmark described under option 1 (20M
iterations; a literal argument to a user function, a struct literal holding
one, a `let` bound to one, an equality against one). The baseline is listed
twice, from two separate runs, to show the noise: `words` differs by 13%
between them, the rest by 1–2%. Option 3 is measured with
`--per-program-statics`, i.e. the struct form forced on single-threaded
programs; without workers its output is identical to the baseline.

| program | baseline | baseline (again) | 1 stack temporaries | 3 per-program, forced |
|---|---|---|---|---|
| `lit_micro` | 33.6 ms | 33.8 ms | **47.3 ms** | 33.9 ms |
| `words` | 322.9 ms | 365.0 ms | 348.9 ms | 311.7 ms |
| `words_dictionary` | 274.1 ms | 310.3 ms | 267.4 ms | 269.6 ms |
| `strlist` | 76.9 ms | 78.0 ms | 76.8 ms | 77.2 ms |
| `calc` | 139.5 ms | 141.2 ms | 141.1 ms | 140.2 ms |
| `sexp` | 484.8 ms | 490.9 ms | 498.8 ms | 494.3 ms |
| `sum` | 48.4 ms | 48.5 ms | 48.7 ms | 49.6 ms |

The threaded sample (`23_mandelbrot_threads`, which reads `W`, `H` and
`MAXITER` in its inner loop and does use the struct form under option 3):

| | baseline | 1 stack temporaries | 3 per-program |
|---|---|---|---|
| serial render | 20.2 ms | 20.2 ms | 20.5 ms |
| 32 workers | 3.5 ms | 3.6 ms | 3.2 ms |

Options 2a, 2b and 4 generate the same C as the baseline for every program
that compiles under them (4 adds `const` to the literal data), so they are
not in the table.

## 6. Globals were C statics

The constants half of §1 had a second cause, independent of laundering:
every global was a process-wide C static. Fixed globals were
`static T name`, and resizable and variable ones were a static header plus
a static dedicated stack. That is why a worker was only ever allowed to read
a constant. This contradicts §11.1's own model, in which the program runs
inside an implicit outermost function whose locals are the globals, and a
`thread_fn` is a separate program with its own instance of them.

The second commit makes that model true. Every global that is not read-only
static data is a member of one struct per program instance, and the
dedicated stacks of the resizable globals are inside that struct.
(Read-only static data means a `const` global with a compile-time
initializer, which every instance can share as it is.) Main's instance is a
static. A worker's instance is allocated by its entry thunk and filled from
the spawn image. After the arguments, the spawn image carries the globals
the worker's program uses, as `[size][image]` records (a resizable global's
record is the queue image; a `reusable` pool's record carries its
freelist). The worker reads and writes its own copies, and a worker's worker
copies the worker's. A global whose type is not flat is an error in a thread
program, as a non-flat argument is. Every access goes through `GS_GL->`. In
a program without workers this is `(&gs_globals_main)`, a link-time
constant, so the generated code is the same as before. In a program with
workers it is the thread-local `gs_gl`, the addressing every data-stack
access already uses.

With that, `let W = 78;` read by a worker is the worker's own `W`, whatever
anyone does with `&W`, and the mandelbrot sample keeps its `let`s. The
typechecker's rule is reduced to "flat globals only", and the only C statics
left are string literals and compile-time `const` globals, both provably
never written. `test/threads/thread_globals.goose` exercises a counter, a
resizable log, a `let` array written through a reference, a struct, a
variable-size string, a pool with a freelist and a `const`, from a worker
and from a worker's worker, checking main's values after each. This also
makes option 3's per-thread static image unnecessary: the same mechanism now
covers every global, and string literals no longer need a per-instance copy
since nothing can write them.

## 7. Side findings

These were found while probing. None is caused by the branches, and each has
a one-file repro in the session's scratchpad:

* **`continue` before a nonfixed local crashes** (base compiler, `-O0` and
  `-O2`): `for x in xs { if x == 0 { continue; } let s: u8[] = "abc"; … }`
  faults. The loop's continue label restores every save of the loop scope,
  including the base of a local whose declaration the `continue` jumped
  over, so `s`'s base is read before it is set. Option 1 ran into this
  immediately and works around it for its own stack by saving at scope entry
  instead; the same fix applies to ordinary locals.
* **`push` through a fat reference read out of a struct field at `-O0`**
  writes the pushed byte elsewhere: `struct Buf { bytes: u8[>..]& }` … `fn
  bump(b: Buf&) { b.bytes.push('!'); }` prints `Hello\0` at `-O0` and `Hello!`
  at `-O2`. The option 2a positive test had to avoid the pattern.
* **A slice of a statement temporary can be bound to a variable**: `let s =
  f()[..]` where `f` returns a `u8[]` typechecks (the temporary's sentinel
  root is accepted as a binding) while codegen restores the temporary's stack
  at the end of the statement. It works today only because each temporary
  gets a stack of its own within the function. (Since fixed: a temporary is
  rooted at its statement's depth, and a binding without an annotation is
  held to the store rule, §9.2.)
* The `codex/readonly-literals` experiment missed the reference-to-element
  paths listed under option 2b.
