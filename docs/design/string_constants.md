# String constants, laundering, and threads

The one memory the stack model does not own is static data: string literals,
and the `let` globals a worker may read. Writability launders through storage
(§9.5), so a reference to static data that has been stored and read back is
writable, and with workers that is shared mutable memory. This note pins down
exactly what could be written, works through the ways of closing it, and
records what each costs, measured on a throwaway branch that implemented
it:

1. a literal used as a slice is a hidden local on a data stack;
2a. a `u8` read-back is never writable, and constants are never stored;
2b. "may point at static data" tracked through storage (the
    `codex/readonly-literals` experiment, repaired);
3. static data is per program instance, a worker getting its own copy;
4. a user-level `const` on any type, literals typed `const u8[:]`, `let`
   rebind-only, and no laundering.

Option 4 is what was adopted: it is the two commits preceding this note
(`Add const types; let is rebind-only; literals are const u8[:]` and `Give
every program instance its own globals`, §6). The other branches were
removed once the decision was made. Every one of them passed the full test
suite with its own fixtures and typechecked every benchmark; the samples
and the numbers are in §5.

## 1. What can be written today

The repro from the discussion:

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

`"abc"` is a `u8[:]` into `static uint8_t gs_str0[3]`, `Box { s: "abc" }` stores
it, `box.s` reads it back, and the read-back is writable by design: the
*path* is writable (`box` is a `var`, `s` is not a `let` field) and the slice's
own provenance is gone. The write lands in the C static, which every thread
shares.

Probing the current compiler shows the hole is wider than the repro:

* Every read-back path launders: a field (`box.s[0]`), an element
  (`views[0][0]`), a `for x in views` copy, a slice behind a *reference* to an
  element (`for &x in views { x[0] = … }`, `&views[0]`, the reference `push`
  returns), a `match … &l` binder, a value returned out of a container
  through a reference parameter (`fn view(b: Box&) -> u8[:] { b.s }`).
* **`let` globals have the same hole.** A thread may read any `let` global of
  flat fixed type (§11.2), on the premise that it is never written. But `&W`
  for `let W = 78;` stored into a reference field and read back writes `W`
  — from a worker too — and `TABLE[..]` for `let TABLE: u8[3] = "abc"` writes
  the constant's static storage. So the class of memory to protect is
  *static data plus every thread-visible constant*, not literals alone.
* What does *not* launder: a direct write through a literal or through a
  `var` slice variable holding one (`s[0] = 1` after `let s = "abc"`), a
  `let` variable's field path, a by-value parameter's field path, and a
  write through a slice parameter given a literal (`zap("abc")`), which
  provenance already rejects.
* The `[len][bytes]` image a literal takes as a `u8[]` *value* (an inlined
  `return "abc"` as `u8[]`) is also static, but a slice of it is a slice of a
  temporary, which cannot be stored or returned, so it cannot be laundered.
* Per-thread copies of anything only matter for what a thread can *reach*:
  slices, structs holding slices and every `var` global are already out of a
  thread program's reach, so a `let GREETING = "hello";` global is not a
  concern (a worker cannot name it).

## 2. The options

### Option 1 — a literal used as a slice is a temporary on a data stack

**Rule.** A literal is a `u8` array value: copied into any array type as
today, and used as a `u8[:]` it denotes a hidden `let u8[]` local of the
innermost scope, built from the static bytes where the literal is evaluated,
exactly as if `let s: u8[] = "…";` stood there. Consumers that only read the
literal (`print`, `format`, `abort`, `==`, `append`, `push`, array
construction) keep reading the static bytes and build nothing. In a global
initializer there is no scope to own the local and only main can reach a
global's slice, so it stays static there.

This is what "string literal = array literal" means once the array has to
live somewhere. It is the only option that makes the language's *model*
complete (no memory outside the stacks), and it needs no provenance
machinery at all: the hidden local is an ordinary local, so a slice of it
obeys every existing lifetime rule.

**Damage, language.** The lifetime rules are the damage. A slice of a local
cannot be returned or stored outside its scope, so these no longer compile:

```goose
fn name(s: Shape) -> u8[:] { match s { Circle c => "circle", … } }   // 05_shapes, 19_vm
let verdict = block { guard p.alive else { break "gone"; } … };      // 01_tour
```

Three of the 25 samples fail (`01_tour`, `05_shapes`, `19_vm`), all on
"returning a slice of a literal" or "storing one outside its block". The fix
is to return `u8[]` (a copy at the destination) or `u8[>..]`, or to move the
literal out; none of the benchmarks or tests needed any. Every function
returning a `u8[:]` name for an enum — a common idiom — is affected.

**Damage, runtime.** One memcpy of the bytes plus a stack bump per evaluation
that needs a slice, and a save/restore of the literal stack per function
entry and per loop iteration. The manifest benchmarks do not move: their hot
literals are equality operands and `append`/`format` arguments, which take
the static path. A micro-benchmark that passes a literal to a user function,
stores one in a struct literal and binds one to a `let`, 20M times, is
**+40%** (33.6 → 47.3 ms). That is the "cost for 99.9% of uses" concern made
concrete: small, but paid at every call boundary that takes a slice.

**Implementation**: the typechecker
gives the literal a synthetic `VarDef` at the current depth (so `while`
conditions had to move inside the loop scope, where codegen already puts
them); codegen keeps one literal stack per function whose top is saved on
function entry and on every loop iteration and restored on their exits. That
last point is what makes it sound: a per-literal save would be restored on a
`continue` taken before the literal was reached, reading an unset base — which
is a **pre-existing codegen bug** for ordinary locals, see §6. Recursion works
because each activation's literal stack is one more `gs_sp + k` slot, which
is the spec's "temporaries may consume slots proportional to depth" (§7.8).

Variants considered: (a) treat the literal as an rvalue with a temporary's
root, which would reject `Box { s: "abc" }` outright — far more damage;
(b) hoist all of a function's literals to its entry, which pays for every
literal on every call including the cold `abort("…")` paths; (c) a
statement-scoped temporary, which is what call results are today and would
leave `let s = "abc"; use(s);` dangling (the checker currently allows binding a
slice of a statement temporary, §6).

The branch also carries the no-store rule for constants from option 2a,
since the hidden local does nothing for `let W = 78;`.

### Option 2a — a `u8` read-back is never writable; constants are never stored

**Rules.** Two, with no analysis behind either:

1. A reference or slice read out of storage whose pointee type is `u8` is
   never writable. Which container it came out of says nothing about what
   was stored there (that is what laundering means), but the pointee type
   does: a string literal could have been stored there, and static data is
   shared. Every other pointee type launders as before. The writable path
   through storage is a reference to the array itself (`u8[>..]&`,
   `u8[..]&`), which no literal can be.
2. A reference or slice into a thread-visible constant (a `let` global of
   flat fixed type) lives in variables, is passed down or returned, and is
   never stored into a field, element or global — the rule references into a
   grow-shrink array already follow (§5.2), for the same reason: a store is
   what a read-back could launder.

Rule 2 exists because rule 1 cannot be extended to constants by type: the
first attempt did that, and the aggregate test file failed on `gheld.r = 7`
through an `i64&` field, because *some* file in the program declares a `let`
of type `i64`. Any program with `let W = 78;` would lose writes through every
stored `i64&`. Keeping the constants out of storage instead is local, exact,
and costs nothing in the corpus.

**Damage.** Zero in the corpus: all tests, all samples, all benchmarks. The
language change is visible, though: a `u8[:]` stored in a struct or an array
is read-only through that path, and a design like `struct Cursor { buf:
u8[:], pos: i64 }` that writes `c.buf[c.pos]` must hold a `u8[>..]&` instead.
Since `u8[:]` is the read idiom by convention (§3.10) this is a small loss,
and the diagnostic says why ("it was read out of storage and may point at a
string literal, which every thread shares").

**Implementation**: under a hundred lines in the checker — a predicate on the pointee type consulted at the three places a
read-back is granted writability (`ContainerRead`, `PointeeWritable`,
`ReadBackLVal`), the `for` copy binding, and the return mapping through a
parameter (which used to take the argument's writability wholesale); the
slice behind a reference to an element now counts as read out of storage;
plus the store rule for constants in `FitsAt`. A `maystatic` provenance bit
carries the reason into the diagnostic.

### Option 2b — track "may point at static data" through storage

This was the `codex/readonly-literals` experiment, rebased and repaired,
and it is the analysis the discussion feared: every
`Prov` gains two pointers into a monotone origin graph (`literal`: the
pointee may be static; `contents`: the references inside this value may
be), every store, call, return, default value, iteration, `break` value and
match binding joins nodes, and after the whole program is checked the graph
is saturated and every recorded write through a node that reaches a static
origin is an error. Order-insensitive, so later stores, loop back edges and
later call sites of a shared specialization all count — which the fixtures
cover — at the price of rejecting a write whose particular execution would
precede the literal store.

**What it missed.** The slice behind a reference to an element was not a
read-back to it: `for &x in views { x[0] = 88; }`, `let r = &views[0]; r[1] =
89;` and `let e .= views.push("abc"); e[0] = 88;` all compiled and wrote the
static. The rebased branch fixes that (the same one-line change option 2a
needed) and adds the three fixtures. Everything else probed here it caught,
including the constant-global cases and the return-through-parameter case.

**Damage.** Zero in the corpus, and more precise than 2a: a `u8[:]` field
stays writable in a program that never stores a literal into that
container. The cost is some 300 lines across the checker, a fourth provenance
dimension next to roots, exactness and writability, and diagnostics that
say a write "may point into immutable static storage" without being able to
name the store that made it so.

### Option 3 — static data is per program instance

**Rule.** Static data belongs to a program instance (§1.2). The main program
has one copy; each worker (§11.2) starts from its own copy, taken from the
state the global initializers left. A write laundered through storage stays
in the instance that made it. Nothing else changes: no new errors, no
provenance, and the single-threaded const-cast is exactly what it was.

This is the semantics the spec already implies — a `thread_fn` "is compiled
as a separate program" — applied to the one thing those programs still
shared.

**Damage.** None in the language: every test, sample and benchmark compiles
and runs unchanged. Runtime: a program that spawns no worker keeps the plain
C statics, so it is byte-for-byte what it was. One that does gathers the
literals and thread-visible constants into a struct, main's instance being
the struct itself; the snapshot is one `malloc` + `memcpy` at startup, each
worker copies it at spawn (kilobytes, next to reserving its stack block),
and every access goes through the thread-local base `gs_st` — the same
addressing every data-stack access already uses through `gs_stks`. Forcing
the struct form on a single-threaded program (`--per-program-statics`) is the
worst case for that indirection: the literal micro-benchmark and the
manifest benchmarks are within noise of the baseline (§5), and the threaded
Mandelbrot sample, which reads three constants in its inner loop, times the
same.

**Implementation**: about 150 lines, all in codegen and the runtime; the typechecker only settles which globals are
thread-visible constants. Constants with runtime initializers are
initialized into main's copy by `gs_init_globals` before the snapshot, so a
worker sees what main computed.

### Option 4 — `const` in the type, mutable by default

**Rule.** Any type may be qualified `const`: `const i64[3]` is a value
whose contents cannot be written into, and `const T&` / `const T[:]` are a
reference and a slice through which the pointee, or the elements, cannot
be — shallow (a reference read out of a `const Box&`'s field is as writable
as the field's type says). A string literal is a `const u8[:]`. `let` is
orthogonal to all of this: it keeps a binding from being reassigned and
says nothing about contents (`let xs = [1, 2, 3]; xs[0] = 9;` and a write
through `&xs` are fine), `const x = e;` is the sugar for `let x: const T`,
and `const f: T` for `let f: const T` in a struct. Nothing else is
annotated: a parameter or result declared `u8[:]` is *generic over
constness* — checked read-only in the instantiation that gets read-only data,
which is exactly what provenance already did — and `const u8[:]` on a
parameter documents that the function only reads. The one place `const` is
*required* is a **slot**: a field, an element, a global, an annotated
variable or an assignment target holds a read-only reference or slice only
if its type says `const`. So what a slot yields is exactly as writable as
its type says, and the laundering rule is gone: a `u8[:]` field can only
ever have been given a writable slice. Adding `const` is implicit,
dropping it never is, and an `extern fn`'s parameters are exact. A
by-value `for` or `match` binding is a copy and is not written (the write
would go nowhere), which is the one place `let`'s old strictness was worth
keeping as a rule of its own.

This differs from 2b in kind, not degree: 2b infers what a container may
hold and forbids the write; 4 has the programmer say what the container
holds and forbids the store. The check is local and exact, the diagnostic
names the slot and the fix ("declare the slot const"), the model loses a
rule (laundering) instead of gaining an exception, and the same feature
lets a struct hold a read-only view of anything — a `let` buffer, a
`bytes_of` view — which today is only possible by laundering. Constants
close for free: `&W` is a `const i64&`, which no `i64&` slot can hold.

**Damage, language.** Annotation churn at slots that hold read-only data,
and `const` where a global must be a constant. In the corpus: 11 sites
outside the standard library — three `extern fn` declarations taking a
literal (`s: const u8[:]`), a `let s: const u8[:] = ""`, two
`dictionary<const u8[:], …>` keyed by literals, a `dictionary<key, const
u8[:]>` holding literal values, a struct field `word: const u8[:]` filled
from that dictionary's keys, the mandelbrot sample's three `const W = 78;`
globals its worker reads (a `let` global is no longer a constant a worker
may read, since `&W` is a writable `i64&`), and a `const N = 4;` used as an
array size — plus the standard library's eight `os` extern parameters and
`split`, whose parts become `const u8[:]` so one function serves literals
and buffers alike. No benchmark changed. The shape of the churn is the C++
one: generic containers keyed by views need the view type spelled `const`
(a `dictionary<u8[:], V>` given a literal key is an error asking for
`dictionary<const u8[:], V>`), and a function that *stores* views of its
input must commit to a constness for the container, since the container's
element type is a slot. Three places bit during the implementation and are
worth knowing: `null` and `default<T>()` are "empty" values that must count
as writable or they refuse every non-`const` slot; a load of a `const`
value is a plain copy while a reference to one is a `const T&`, so the
qualifier has to move from the storage type to the reference type at every
`&`; and the flag for the `const` binding form has to survive the clones
the checker and the inliner make of every declaration, or the contents
silently become writable — the test that caught it is `const_value_write`.
The first version of the branch had `let` make derived references `const`,
the old transitive rule in new clothes; the second commit takes that out,
which is what made the mandelbrot constants and the array size need
`const`.

**Damage, runtime.** None; the generated C is the baseline's, with the
literal data now emitted `static const` (the checker admits no write to it,
so a hole in the analysis would fault rather than corrupt).

**Implementation** (the first of the two commits): about 250 lines: a `cq` bit on every type, in type equality, substitution and
the dumper (`const` binds to the first `&` or `[:]` after the base type,
else to the whole type; `(const i64[3])&` and `const (u8[:])&`
parenthesize), the store rule at slots (a scoped flag set by field, element,
assignment and annotated-declaration checks and cleared by argument and
return checks), const-adding coercions in `FitsAt` and branch unification,
constness on read-back, parameter and result constness, the `extern`
check, and — in place of the transitive-`let` rule, which is deleted — a
`letbound` mark on the last step of an lvalue path (the only thing `let`
now forbids), contents writability read from the type at each path step,
and the copy-binding rule for `for`/`match`. Narrowing a `const T?`, loading
a relative reference, and resolving a `const Alias` all had to keep the
qualifier.

### Others considered

* **Read-only pages.** Emit the literal data `const` (or `mprotect` it) so a
  laundered write faults. Zero cost and one word of change, but it turns a
  compile-time question into a crash, and it does nothing for constants
  with runtime initializers. Worth doing *on top of* 2a or 2b as a check
  that the analysis has no holes, not as the fix.
* **Copy on store.** Materialize a literal only where a slice of it is stored
  into a container. The slice being stored may be a variable bound to one of
  several literals, so the copy would be a runtime memcpy of an arbitrary
  slice: option 1's cost at option 1's lifetime rules, with a stranger rule.
* **No laundering at all.** Track writability through storage precisely for
  every provenance, not just static. It is 2b generalized, and it rejects the
  documented const-cast (`let` locals laundered through a struct) that the
  spec chose deliberately.
* **Threads may not read `let` globals.** Closes the constant half without
  any of the above, but the mandelbrot sample's `let W = 78;` in a worker is
  exactly the use the exception exists for.

## 3. Damage summary

| | tests (280) | samples (25) | benchmarks | spec change | runtime cost |
|---|---|---|---|---|---|
| 1 stack temporaries | all pass | **3 fail** (return/store of a literal slice) | all compile | literals become locals; §3.7 rewritten | memcpy per slice use; +39% on the literal micro-benchmark, benchmarks unchanged |
| 2a read-back by type | all pass | all pass | all compile | one rule each for `u8` read-backs and constants | none |
| 2b origin graph | all pass | all pass | all compile | laundering has an exception, described as an analysis | none |
| 3 per-program static | all pass | all pass | all compile | static data is per program instance | none without workers; snapshot + per-worker copy + TLS base with them, unmeasurable |
| 4 `const` types | all pass after 8 annotations | all pass after 4 (an extern, three constants) | all compile | `const` on any type; `let` is rebind-only; slots must say `const`; laundering removed | none |

## 4. Opinion

Option 1 is the one to rule out. It is the cleanest *model* — no memory
outside the stacks — but the model's cleanliness is paid for by the user:
every function that returns a name as `u8[:]` stops compiling, every slice
parameter given a literal costs a copy, and the implementation is not
smaller than the others (a new stack per function with its own scoping, and
it still needs option 2a's rule for constants). The spec's own line is that
the language exists to avoid exactly this kind of hidden copy.

Option 2b works and is the most precise, but it is the "complex analysis"
the discussion anticipated, in some 300 lines that touch every place provenance
flows, and its diagnostics cannot say which store poisoned the container.
Having found three paths it missed on the first pass, I would not want to be
the one maintaining its soundness argument next to the roots one.

Option 4 is the one I would actually want the language to have, and it
changes the calculus. It is the only option that *removes* a rule: the
laundering clause and its "const-cast" loophole go, writability becomes a
property of a type you can read off a declaration, and the same feature
pays for itself elsewhere — read-only views in data structures, `extern`
declarations that say what the C side may do, a `split` that serves
literals. Its costs are real but bounded and predictable: `const` at slots
that hold read-only data, which in the corpus meant nine sites and the
`os` externs, and the C++-shaped consequence that a generic container keyed
by views must be told `const u8[:]`. What it is *not* is a runtime fix for
the thread race by itself if the annotation is ever wrong — but it cannot
be wrong, because dropping `const` is never implicit; static data is
provably unwritten and is emitted as C `const`.

Against 3: 3 costs nothing in the language and nothing at runtime without
workers, and it keeps laundering, which the spec chose deliberately. If
laundering is worth keeping, 3 is the fix. If the const-cast loophole was a
compromise rather than a feature — the discussion that produced §9.5 says
it "buys most of const-correctness with none of the type-soup churn", and
4 keeps that property, since nothing but slots is annotated — then 4 is the
better language, and 3 becomes unnecessary for literals.

Between the other two, the real choice is between **2a** and **3**.

* 2a is a language rule with no machinery: "a `u8` slice you read out of
  storage is read-only; a reference into a constant is never stored." It
  costs nothing at runtime, it costs nothing in the corpus, and it is
  honest about *why* — the compiler genuinely cannot know what was stored,
  and `u8[:]` is the read idiom anyway. Its cost is that the language now
  has a rule shaped by an implementation limit, and a struct holding a
  writable byte view has to hold a reference to the array instead.
* 3 changes nothing a program can see except that workers stop sharing
  static data, which is what the spec's thread model says already. It is
  the option that keeps "acceptable as a corner case" true in exactly the
  single-threaded sense in which it was acceptable, and it keeps the
  laundering rule the spec chose on purpose. Its cost is a runtime mechanism
  (~60 lines) that exists only in threaded programs, and an indirection on
  static-data accesses in those programs that did not show up in any
  measurement.

My recommendation is now **option 4**: it is the design the language
reads as having wanted all along, its churn is small and lands exactly
where a reader benefits from seeing it, and it needs no runtime machinery.
If the laundering rule is to stay as a feature, take **option 3** with rule
2 of option 2a — constants are never stored — so that thread-visible
constants stay plain C statics the C compiler can fold and only the
literals go into the per-program image; that is the smallest change with
the smallest language cost. 2a alone remains a sound, zero-cost fallback.

## 5. Measurements

Windows 11, MSVC 19.51 `/O2`, best of the runs shown after two warm-ups
(`bench/run_bench.py`'s method), medium sizes.

`lit_micro` is the literal micro-benchmark described under option 1 (20M
iterations; a literal argument to a user function, a struct literal holding
one, a `let` bound to one, an equality against one). The baseline is listed
twice, from two separate runs, to show the noise: `words` moves by 13%
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
every global was a process-wide C static — fixed ones as `static T name`,
resizable and variable ones as a static header plus a static dedicated
stack — which is why a worker was only ever allowed to read a constant.
That contradicts §11.1's own model, in which the program runs inside an
implicit outermost function whose locals the globals are, and a
`thread_fn` is a separate program with its own instance of them.

The second commit makes that true. Every
global that is not read-only static data (a `const` global with a
compile-time initializer, which every instance can share as it is) is a
member of one struct per program instance, with the dedicated stacks of the
resizable ones inside it. Main's instance is a static; a worker's is
allocated by its entry thunk and filled from the spawn image, which carries
the globals the worker's program uses behind the arguments as
`[size][image]` records (a resizable's is the queue image; a `reusable`
pool's carries its freelist). The worker reads and writes its copies, a
worker's worker copies the worker's, and a global whose type is not flat is
an error in a thread program, as a non-flat argument is. Every access goes
through `GS_GL->`, which is `(&gs_globals_main)` in a program without
workers — a link-time constant, so the generated code is what it was — and
the thread-local `gs_gl` in one with them, the addressing every data-stack
access already uses.

With that, `let W = 78;` read by a worker is simply the worker's own `W`,
whatever anyone does with `&W`, and the mandelbrot sample keeps its `let`s.
The typechecker's rule shrinks to "flat globals only", and the only C
statics left are string literals and compile-time `const` globals, both
provably never written. `test/thread_globals.goose` exercises a counter, a
resizable log, a `let` array written through a reference, a struct, a
variable-size string, a pool with a freelist and a `const`, from a worker
and from a worker's worker, checking main's values after each. This also
retires option 3's per-thread static image: the same mechanism now covers
every global, and string literals no longer need a per-instance copy since
nothing can write them.

## 7. Side findings

Found while probing; none is caused by the branches, and each has a
one-file repro in the session's scratchpad:

* **`continue` before a nonfixed local crashes** (base compiler, `-O0` and
  `-O2`): `for x in xs { if x == 0 { continue; } let s: u8[] = "abc"; … }`
  faults, because the loop's continue label restores every save of the loop
  scope, including the base of a local whose declaration the `continue`
  jumped over, so `s`'s base is read unset. Option 1 hit this immediately and
  works around it for its own stack by saving at scope entry instead; the
  same fix applies to ordinary locals.
* **`push` through a fat reference read out of a struct field at `-O0`**
  writes the pushed byte elsewhere: `struct Buf { bytes: u8[>..]& }` … `fn
  bump(b: Buf&) { b.bytes.push('!'); }` prints `Hello\0` at `-O0` and `Hello!`
  at `-O2`. The option 2a positive test had to avoid the pattern.
* **A slice of a statement temporary can be bound to a variable**: `let s =
  f()[..]` where `f` returns a `u8[]` typechecks (the temporary's sentinel
  root is accepted as a binding) while codegen restores the temporary's stack
  at the end of the statement. It works today only because each temporary
  gets a stack of its own within the function.
* The `codex/readonly-literals` experiment missed the reference-to-element
  paths listed under option 2b.
