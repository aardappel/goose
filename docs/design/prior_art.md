# Prior art: the nearest memory models, and what is new

<details>
<summary>Table of contents</summary>

- [1. What is being compared](#1-what-is-being-compared)
- [2. The closest matches, ranked](#2-the-closest-matches-ranked)
- [3. Multiple stacks put to other uses](#3-multiple-stacks-put-to-other-uses)
- [4. Relatives of the lifetime rule](#4-relatives-of-the-lifetime-rule)
- [5. What no precedent was found for](#5-what-no-precedent-was-found-for)
- [6. How the survey was done](#6-how-the-survey-was-done)

</details>

This note is a survey, made in October 2026, of the languages, libraries and
papers whose memory management comes closest to Goose's. In Goose, all
dynamic data is stored on a set of stacks whose number the compiler
computes, with one growable value on top of each stack. No language or paper
was found that combines Goose's ingredients, but each ingredient has prior
art. This note ranks the candidates by how much of the model they share,
says where each candidate differs, and lists what has no precedent. Scores
are out of ten and are a judgement, not a measurement.

## 1. What is being compared

Goose's model, in the parts that a candidate can share or lack:

1. **Stacks, not a heap.** A program's dynamic data lives on N data stacks.
   Each stack is a large address-space reservation that is committed on
   touch, with a bump pointer and a guard gap. Values are pushed in lifetime
   order and popped en masse at scope exit
   ([spec §1.2](../goose_spec.md#12-memory-model),
   [§10.4](../goose_spec.md#104-runtime-environment)).
2. **One growable value per stack, on top.** At most one resizable value is
   live per stack and it is always the topmost, so growth is a pointer bump,
   nothing moves, and interior references stay valid across growth
   ([§1.3](../goose_spec.md#13-the-stack-invariant)).
3. **N is a compile-time constant.** The whole-program compiler assigns each
   growable local a stack index. Values that are live at the same time get
   distinct stacks, and values that are live one after the other can share
   a stack. This works like register allocation, with stacks in place of
   registers
   ([§10.3](../goose_spec.md#103-stack-assignment-deterministic-recipe)).
4. **Static, annotation-free safety.** Every reference has a compile-time
   root: the variable that bounds its target's lifetime. A reference may not
   outlive its root, a store is checked by scope nesting, and functions are
   specialized per root instead of taking lifetime parameters
   ([§9.1](../goose_spec.md#91-the-outlives-rule),
   [§9.2](../goose_spec.md#92-roots-and-the-depth-check),
   [§10.2](../goose_spec.md#102-monomorphization)). An array shrinks only
   where the compiler can tell that nothing refers into it
   ([§5](../goose_spec.md#5-the-resizableshrink-rules)).
5. **Results built at their destination.** A variable-size result is
   constructed on the stack of the value that receives it, through any depth
   of calls, and the destination stack is known to the callee
   ([§4.3](../goose_spec.md#43-the-in-place-construction-guarantee),
   [§7.3](../goose_spec.md#73-return-values-and-result-placement)).
6. **Nested data inline.** An array of strings is one contiguous block, and a
   struct may end in one growable tail
   ([§3.3](../goose_spec.md#33-the-array-family),
   [§3.4](../goose_spec.md#34-placement-rules-what-may-contain-what)).
7. **Recursion keeps N constant.** No growable local may be in scope across
   a call into a recursive cycle, so recursion costs native stack only
   ([§7.8](../goose_spec.md#78-recursion)).

The same three questions are asked of each candidate. Is its region a
contiguous stack whose top value grows in place with stable interior
references, or an arena of separately allocated objects? Is the number of
regions a compile-time constant, or one per activation? Is safety static,
and if so, how many annotations does it need?

## 2. The closest matches, ranked

| Rank | Candidate | Shares | Lacks | Score |
|---|---|---|---|---|
| 1 | ML Kit region inference | 1, 4, 5, the shrink rule's ancestor | 2, 3, 6, 7 | 6 |
| 2 | GNU obstacks | 2, 5 as convention | 3, 4, 6, 7; growing object may move | 6 |
| 3 | ParaSail | 1 in spirit, 5, no-pointer safety | 2, 3, 6, 7; regions are local heaps | 6 |
| 4 | Ada and GNAT without allocators | 1 with two stacks, 4's depth check, 5 | 2, 3, 6, 7; nothing grows | 5 |
| 5 | Arena idioms in C | 1, 2, 5 as convention | 3 by hand, 4, 6, 7 | 5 |
| 6 | Xhebraj et al., ECOOP 2022 | 5 on a single stack, static modes | 2, 3, 6, 7 | 5 |
| 7 | Cyclone LIFO arenas | 1 in part, 5 | 2, 3, 6, 7; annotated | 5 |
| 8 | OxCaml stack allocation | a second data stack, static escape check | 2, 3, 5 beyond one level, 6, 7 | 5 |
| 9 | Ruggieri and Murtagh, 1988 | 4's placement by enclosing scope | everything else | 4 |
| 10 | Automatic pool allocation | 3's static set of areas | 2, 4, 5, 6, 7 | 4 |
| 11 | Fels | 4's depth check by zone | 2, 3, 5, 6, 7 | 4 |
| 12 | RTSJ scoped memory and its static typings | 1's LIFO areas, 4's store rule | 2, 3, 5, 6, 7 | 3 |

### 2.1 Tofte and Talpin's region inference, and the ML Kit

This is the closest match on the static side. In the region calculus the
store "consisted of a stack of regions"; `letregion` is the only way to make
or drop a region, "hence regions were allocated and de-allocated in a
stack-like manner" ([Tofte, Birkedal, Elsman, Hallenberg, "A retrospective
on region-based memory management", HOSC 2004](https://elsman.com/mlkit/pdf/retro.pdf);
[Tofte and Talpin, POPL 1994](https://dl.acm.org/doi/10.1145/174675.177855)).
Region inference is fully automatic and needs no annotations. A
region-polymorphic function takes the regions for its results as parameters,
so a callee allocates into storage the caller chose. This is point 5, with the
destination passed as a runtime handle. Region representation inference
([Birkedal, Tofte, Vejlstrup, POPL
1996](https://www.cs.tufts.edu/~nr/cs257/archive/mads-tofte/knormal-final.pdf))
places "finite" regions, those holding one value, in the activation record.
The storage mode analysis of the [ML Kit
manual](https://elsman.com/mlkit/pdf/mlkit-4.3.0.pdf) resets a region in place
(`atbot`) where the compiler proves that the region holds nothing live at that
allocation, and `resetRegions` is the checked form of this reset that the
programmer can write. This is the ancestor of Goose's shrink rule, applied to
a whole region.

What differs: an "infinite" region "consisted of a linked list of fixed-size
region pages, allocated from a free-list of region pages", holding many boxed,
fixed-size values, so nothing is contiguous and nothing grows in place. The
number of live regions is dynamic, one per `letregion` evaluation and so per
recursion level. Region parameters are passed at run time rather than
specialized. The retrospective judges infinite regions to "give fragmentation
problems" and the storage mode analysis "complicated and vulnerable to program
changes", and the ML Kit later added a garbage collector to reclaim the memory
that region inference leaks.

### 2.2 GNU obstacks

Obstacks are the one earlier design that states Goose's per-stack invariant as
a rule. An obstack ([glibc manual](https://sourceware.org/glibc/manual/latest/html_node/Obstacks.html))
is a stack of objects: "the last object allocated must always be the first one
freed", and `obstack_free` of an object frees it "along with everything
allocated in obstack-ptr since object". One object at a time may be grown
incrementally with `obstack_grow`. While that object grows, the obstack cannot
allocate anything else. `obstack_finish` closes the object, after which another
object may grow. C code passes an obstack to a callee that builds its result
there, which is point 5 as a convention. The ECOOP 2022 paper of §2.6 lowers
its shadow stack to this API.

What differs: obstacks are a run-time library, the programmer chooses how many
obstacks there are and which object goes where, and nothing is checked.
Storage is chunked, so a growing object is copied when it outgrows its chunk
("its address will change"). Interior pointers into the growing object are
therefore not stable until it is finished, and stable interior pointers are
the property that Goose's reservations exist to provide.

### 2.3 ParaSail

Tucker Taft's pointer-free language has no global heap and no collector. "All
of the objects declared in a given scope are associated with a storage region,
essentially a local heap. As an object grows, all new storage for it is
allocated out of this region. As an object shrinks, the old storage can be
immediately released back to this region. When a scope is exited, the entire
region is reclaimed." Results go where they are needed: "when a function is
called, the region in which the result object should be allocated is passed
as an implicit parameter. This target region is determined by how the function
result is used", so "there is no copying at the call site upon function
return" ([Taft, "ParaSail: A Pointer-Free Pervasively-Parallel Language for
Irregular Computations", Programming 2019, §3.3](https://arxiv.org/abs/1902.00525)).
That is Goose's destination placement, decided at run time. Safety follows
from the absence of pointers: objects are values, and assignment between
regions copies or moves.

What differs: a region is a local heap that reuses space inside it, not a
bump stack. An expandable object grows by linking new pieces, with "pointers
still used behind the scenes", so growth is not contiguous and nothing like
point 2 holds. Regions are created at run time, one per scope entry, so their
number is dynamic. Every object also carries its region's identity at run
time.

### 2.4 Ada, and GNAT without allocators

An Ada program under `pragma Restrictions (No_Allocators)` has two stacks per
task. GNAT's secondary stack ([`s-secsta.ads`](https://gnu.googlesource.com/gcc.git/+/refs/heads/master/gcc/ada/libgnat/s-secsta.ads))
is "part of the runtime support for functions that return results of
caller-unknown size", such as a `String`. The callee pushes the result, the
caller uses it in place or copies it, and `SS_Mark` and `SS_Release` pairs
inserted by the compiler release it en masse at scope boundaries.
Build-in-place ([RM 7.6](https://ada-lang.io/docs/arm/AA-7/AA-7.6)) constructs
a limited result directly in the caller's object; the callee receives the
destination through hidden extra formals. Accessibility levels ([RM
3.10.2](https://ada-lang.io/docs/arm/AA-3/AA-3.10)) are a static scope-depth
rule with no annotations, "in most cases enforced at compile time by Legality
Rules", with run-time checks where a level arrives as an implicit parameter.
They are the direct ancestor of Goose's depth check.

What differs: no Ada array can change its bounds after creation, so there is
no growth, and point 2 does not apply. `Bounded_Vectors` ([RM
A.18.19](https://www.adaic.org/resources/add_content/standards/12rm/html/RM-A-18-19.html))
have a fixed capacity, like Goose's `T[..k]`. The number of stacks is fixed at
two rather than computed. An unconstrained non-limited result is built on the
secondary stack and then copied. An array of variable-length strings needs
access types, which need the heap.

### 2.5 The arena idioms of modern C

These idioms are built on the same substrate as Goose. [Ryan Fleury's
"Untangling Lifetimes: The Arena
Allocator"](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)
reserves "a massive upper-bound of contiguous virtual address space, then
commit[s] physical pages as needed" without relocating anything, and marks and
releases positions like a stack. It also has the caller choose the arena that
a callee builds its result in, which is point 5. His scratch-arena conflict
arises when a function's result arena is also its scratch arena, so that "the
result memory is actually freed before returning to the caller". This
conflict is the dynamic form of Goose's rule that two live growables never
share a stack, and his conclusion that two scratch arenas are enough is N
computed by hand. [Chris Wellons's "Arena allocator tips and
tricks"](https://nullprogram.com/blog/2023/09/27/) passes a scratch arena by
value so that a callee's allocations are freed on return. His [dynamic
arrays](https://nullprogram.com/blog/2023/10/05/) extend in place exactly when
the array ends at the arena's next allocation, which is point 2's "the
growable is topmost". [Our Machinery's "Virtual Memory
Tricks"](https://ruby0x1.github.io/machinery_blog_archive/post/virtual-memory-tricks/index.html)
grows arrays in reserved address space so that "the array never moves".
Varnish's [workspace](https://github.com/varnishcache/varnish-cache/blob/master/bin/varnishd/cache/cache_ws.c)
is an example in production code. One reservation at a time hands the caller
all remaining space to grow into, a release keeps the used prefix, and
snapshots reset in LIFO order.

What differs: all of it is convention. The number of arenas, which value lives
where and whether a reference outlives its arena are the programmer's
responsibility, and an array of strings is an array of pointers.

### 2.6 "What If We Don't Pop the Stack?"

[Xhebraj, Bračevac, Wei and Rompf, ECOOP
2022](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2022.15)
is the one paper that sets out to solve the same problem as Goose. Since
every return pops the stack, "it is difficult to return stack-allocated data
from a callee upwards to its caller — especially variable-size data", and the
known fixes are boxing, reserving space in the parent frame, or "stacks of
resizable regions (with overall increased complexity and loss of performance
predictability)". Their solution is to delay the pop. A callee that returns a
stack value leaves its frame in place until an ancestor returns a heap value,
and a type system of storage modes keeps heap objects from pointing at stack
objects. Results such as a filtered list are built incrementally on the
stack. The implementation, in Scala Native, is a shadow stack that "resembles
Obstacks". On p. 15:16 they write that "it would be an interesting extension
to map individual storage levels to separate physical runtime stacks", which
is the design Goose uses.

What differs: the paper uses one stack, delays the pop where Goose uses a
destination stack, does not make in-place growth a rule, and needs lightweight
annotations where Goose needs none. Work on destination-passing style is the
other line of research close to this paper. [Shaikhha, Fitzgibbon, Peyton
Jones and Vytiniotis, FHPC
2017](https://www.microsoft.com/en-us/research/publication/using-destination-passing-style-compile-functional-language-efficient-low-level-code)
have the caller supply every array's output buffer, sized in advance by shape
inference, and note that this "enables highly-efficient stack-like memory
allocation discipline". [Minamide, POPL
1998](https://dl.acm.org/doi/10.1145/268946.268967) and [Bagrel and
Spiwack, OOPSLA 2025](https://arxiv.org/abs/2503.07489) give types to the
holes and destinations. Goose needs no size ahead of time, since the result
grows at the stack top.

### 2.7 Cyclone's LIFO arenas

Cyclone ([Grossman et al., PLDI
2002](https://www.cs.tufts.edu/comp/150BUGS/cyclone-2002.pdf)) has stack
regions for blocks, lexically scoped dynamic regions that allow unlimited
allocation and are freed at block exit, and a heap. A LIFO discipline on
region lifetimes gives the "outlives" relation that its region subtyping uses,
and a callee allocates into a region handle that the caller passes. Their
experience report ([Hicks, Morrisett, Grossman, Jim, ISMM
2004](https://homes.cs.washington.edu/~djg/papers/cyclone_ismm04.pdf)) found
LIFO arenas "useful when callers know object lifetimes but only callees can
determine object sizes", which is Goose's thesis. It also found them "not
suited" to servers and event loops, which is why Cyclone added unique
pointers, reference counting and keyed dynamic regions.

What differs: a dynamic region is a linked list of pages, each twice the size
of the last, with no in-place growth. Pointer types carry region annotations,
which are inferred only inside functions. Region handles are run-time values
and their number is dynamic ("when throwing an exception, the number of
regions to deallocate is not known statically").

### 2.8 OxCaml's stack allocation

Jane Street's OCaml ([reference](https://oxcaml.org/documentation/stack-allocation/reference/))
allocates local values on "a separately-allocated stack that follows the same
layout as the OCaml minor heap". This stack is distinct from the control
stack, and there is one per domain. A region is a function body or loop body:
entering a region records the stack pointer, and leaving it resets the stack
pointer. The reference states that "the type-checker guarantees that local
values do not escape their region", with modes inferred inside a compilation
unit. `exclave_` ends the current region early so a result is built in the
caller's region, in tail position.

What differs: there is one such stack, and the collected heap is still the
default. No value grows. The reference states that "mutable fields and array
elements require their contents to be global", a restriction that Goose's root
tracking avoids. Building a result in the caller's region with `exclave_`
reaches only one level up.

### 2.9 Ruggieri and Murtagh, 1988

["Lifetime analysis of dynamically allocated objects", POPL
1988](https://dl.acm.org/doi/10.1145/73560.73585) is the earliest static
placement of heap objects by lifetime. Each object goes into a local heap
attached to the activation record of the procedure whose lifetime encloses
the object's lifetime. That procedure is found by an interprocedural analysis
of which allocations a procedure returns. The placement is root-based, as
Goose's is, but the heaps are per activation, there is no growth, and there is
no safety claim beyond the placement.

### 2.10 Automatic pool allocation

[Lattner and Adve, PLDI
2005](https://llvm.org/pubs/2005-05-21-PLDI-PoolAlloc.html) partition a C
program's heap objects into a statically determined set of pools. Each pool is
created and destroyed on entry to and exit from the function where its
non-escaping data structure lives. This is the nearest precedent for a compiler
computing a fixed set of allocation areas with scope-bound lifetimes. The
assignment rule differs: it uses data-structure identity from a points-to
analysis, not liveness. The pools also hold separately allocated objects, and
the transformation adds no safety.

### 2.11 Fels

Fels is a 2025 hobby language ([repository](https://github.com/fels-lang/fels),
[design post](https://dev.to/chrischtel/checking-at-compile-time-that-arena-memory-cant-escape-its-block-1jie))
with named bump zones that are tied to blocks and freed at block exit. Its
checker tags every value with its zone's nesting depth and rejects assigning a
zone value to an outer variable, returning it, or storing it. The only marker
is `borrow` on parameters. Fels has Goose's depth check, applied per zone
rather than per variable. The programmer chooses the number of zones, a result
cannot leave its zone (the opposite of point 5), and there is no in-place
growth rule.

### 2.12 Real-time Java's scoped memory, and its static typings

The RTSJ's
[`ScopedMemory`](https://www.rtsj.org/specjavadoc/javax/realtime/ScopedMemory.html)
areas are entered and left on a scope stack, and reclaimed as a whole. An object
in an outer, longer-lived area may not reference an object in an inner area.
This is Goose's store rule, checked at run time on every assignment. [Scoped
Types (Zhao, Noble, Vitek, RTSS 2004)](https://janvitek.org/pubs/rtss04.pdf)
check it statically at the granularity of Java packages. [Boyapati, Sălcianu,
Beebee and Rinard, PLDI
2003](https://people.csail.mit.edu/rinard/techreport/MIT-LCS-TR-869.pdf) use
static checks with ownership and region parameters on classes. Their "last in
first out" region lifetimes provide an outlives relation. None of these systems
has growth, inline nesting, or destination placement.

## 3. Multiple stacks put to other uses

Several systems have more than one stack, or a stack where objects are freed
en masse, but none of them uses its stacks the way Goose uses its data stacks.

* **Forth** has a data stack, a return stack, and a floating-point stack. They
  hold operands, one cell each. Its dictionary resembles Goose's storage model:
  `HERE` is the next free address, while `ALLOT` and `,` extend the region under
  construction contiguously. A negative `ALLOT` or `MARKER` releases memory in
  LIFO order. Addresses below `HERE` never move ([Forth-2012
  §3.3.3](https://forth-standard.org/standard/usage),
  [`MARKER`](https://forth-standard.org/standard/core/MARKER)). There is only
  one region, and nothing is checked.
* **Prolog's WAM** has a global stack on which structures are built, a local
  stack of environments and choice points, a trail and a push-down list.
  Choice points save the global stack's top so that failure reclaims "all the
  structures and variables constructed during the failed attempt" at once
  ([Aït-Kaci, "Warren's Abstract Machine: A Tutorial
  Reconstruction"](https://wambook.sourceforge.io/)). Memory is reclaimed
  on backtracking, not on scope exit. Terms are immutable, and deterministic
  programs need a collector.
* **PostScript's `save` and `restore`** snapshot virtual memory and reset it
  to the snapshot. Snapshots can be nested, and an `invalidrestore` error is
  raised where a newer object is still referenced
  ([PostScript Language Reference Manual](https://www.adobe.com/jp/print/postscript/pdfs/PLRM.pdf)).
  This is a dynamic version of "a reference may not outlive its root", over
  one stack of levels and with fixed-size objects.
* **Turbo Pascal's `Mark` and `Release`** made the early heap a stack: `New`
  bumped the heap top and `Release` cut it back to a mark, freeing everything
  above at once. There was no growth and no checking.
* **Icon's string region** is one contiguous bump region for all strings, and
  concatenation appends in place when the left operand ends at the region's
  free pointer, because it "was the last string allocated and need not be
  re-allocated" ([`ocat.r`](https://github.com/gtownsend/icon/blob/master/src/runtime/ocat.r)).
  This is point 2 for one data type, under a compacting collector.
* **Odin, Zig, Jai and C3** offer bump arenas and temporary allocators chosen
  by the programmer. Odin's
  [`virtual.Arena`](https://pkg.odin-lang.org/core/mem/virtual/) reserves
  address space and commits in steps, and both it and Zig's
  [`ArenaAllocator`](https://github.com/ziglang/zig/blob/master/lib/std/heap/arena_allocator.zig)
  resize an allocation in place only when it is the most recent one; C3's
  [`@pool`](https://c3-lang.org/language-common/memory/) frees a scope's
  temporaries at its exit. All of these are dynamic, and none is checked.
* **PL/I's `AREA` and `OFFSET`** types are a copyable storage pool and a
  reference relative to it, so an area moved as a whole keeps its links
  valid ([IBM PL/I language
  reference](https://www.ibm.com/docs/SSY2V3_5.3.0/lr/ofda.html)). This is
  a precedent only for Goose's pool-relative references
  ([§3.9](../goose_spec.md#39-relative-references)); areas are not stacks.

## 4. Relatives of the lifetime rule

Goose's lifetime rule is that a reference may not outlive the variable that
owns its target, checked by scope nesting with no annotations. This rule has
relatives that say nothing about where data lives.

* Algol 68 requires the scope of an assignment's left side to be no wider than
  that of its right side. This was the first static check of this kind for
  stack-allocated (`loc`) objects. A runtime check handles cases that the static
  check cannot decide ([Berry,
1970](https://cs.uwaterloo.ca/~dberry/FTP_SITE/reprints.journals.conferences/Berry1970ImpleModelsA68.pdf)).
  Ada's accessibility levels (§2.4) descend from this rule.
* Second-class values ([Osvald, Essertel, Wu, González-Alayón, Rompf, OOPSLA
  2016](https://www.cs.purdue.edu/homes/rompf/papers/osvald-oopsla16.pdf))
  may be passed down but never stored or returned, giving "stack-bounded
  lifetimes" by a first-class/second-class marker. Reachability types
  ([Bao, Wei, Bračevac, Jiang, He, Rompf, OOPSLA
  2021](https://www.cs.purdue.edu/homes/rompf/papers/bao-oopsla21.pdf))
  carry in each type the set of variables a value may reach, which is a
  type-level form of Goose's roots. Capturing types in Scala ([Boruch-Gruszecki
  et al., TOPLAS 2023](https://plg.uwaterloo.ca/~olhotak/pubs/toplas23.pdf))
  do the same for capabilities.
* [Chapel's lifetime checker](https://chapel-lang.org/docs/technotes/lifetimeChecking.html)
  rejects a borrow that would outlive its owner by lexical scope, with
  optional `lifetime` clauses; classes stay on the heap.
* [Hylo](https://docs.hylo-lang.org/language-tour/bindings) keeps references
  second-class as parameter conventions and projections, checked by an
  abstract interpreter "with no need for lifetime annotations"; growable
  collections are heap-backed.
* [Brandon, Driscoll, Dai, Ragan-Kelley, Milano and Aiken, OOPSLA
  2026](https://doi.org/10.1145/3798221) infer borrows and lifetimes with no
  annotations in a pure language, inserting reference counts where inference
  fails; the heap remains.
* D's [DIP1000](https://github.com/dlang/DIPs/blob/master/DIPs/other/DIP1000.md)
  `scope` keeps a reference inside its lexical scope, inferred for parameters
  and checked in `@safe` code, over the native stack.
* Replacing lifetime parameters with specialization per root appears only
  outside the academic literature, in [Fernando Borretti's essay on type
  systems for memory safety](https://borretti.me/article/type-systems-memory-safety)
  and Sean Baxter's [P3444R0, "Memory Safety without Lifetime
  Parameters"](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3444r0.html).

The escape-analysis literature shows the same gap from the other side. Stack
allocation of objects in Java compilers stops at variable-sized arrays,
because a frame's size is fixed ([Kotzmann and Mössenböck, VEE
2005](https://www.usenix.org/legacy/events/vee05/full_papers/p111-kotzmann.pdf);
[OpenJ9's optimistic stack allocation, 2024](https://dl.acm.org/doi/10.1145/3656389)).
Giving a growable value its own stack removes that limit.

## 5. What no precedent was found for

* Assigning growable values to a compile-time number of stacks by liveness,
  as registers are assigned, with sequentially live values sharing a stack.
* "Exactly one resizable per stack, always on top, interior references stable
  across growth" as a language invariant rather than a library convention.
  Obstacks state the rule but relocate objects, and the arena idioms keep
  addresses stable but leave the rule to the programmer.
* Specialization per reference root in place of lifetime parameters, as a
  published technique.
* A restriction on recursion whose purpose is to keep the stack count a
  constant.
* Variable-size data nested inline, an array of strings in one block, on
  stacks of this kind.

The survey found nothing else that combines these points with destination
placement through any depth of calls and with the shrink rule.

## 6. How the survey was done

The survey consisted of five parallel web searches in October 2026, each
covering a group of candidates:

* dynamic arena and obstack-like designs;
* static region systems;
* Ada, OCaml, ParaSail and real-time Java;
* a sweep of production, hobby and research languages (Austral, Verona,
  Cogent, Wuffs, Ivory, occam, Oberon, Mesa, CLU, Scopes, Myrddin, Hare,
  Inko, Carp, Mojo, Koka, Lobster, Nim, V, Bosque, Pony, Jakt, Roc, Gleam,
  Sing#, Deca, Vault, BitC, ATS, Mezzo, SPARK, June, Mercury, Ante, Bone
  Lisp, Hike, Dyon, Vale, Zest, Hume, Virgil, Cone, among others);
* the academic record under multi-stack allocation, stack allocation of
  variable-sized data, lifetime inference without annotations, destination
  passing and frame-based heaps.

The quotations from the ParaSail, ML Kit retrospective and ECOOP 2022 papers
were checked against the papers' text. The
[Hacker News discussion of Goose](https://news.ycombinator.com/item?id=49748954)
surfaced no candidate beyond these. Everything in the sweep not named above
either allocates on a collected or manually managed heap, allocates nothing,
or is fully static with no recursion, and so shares at most one point of §1.
