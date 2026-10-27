# Nested growth: building a tree in place, with JSON as the example

Research note, October 2026. Nothing described here is implemented. The
question is what it would take to write

```goose
struct Member { key: u8[varint], val: Json.. }
enum Json {
    Null,
    Bool { v: bool },
    Num { v: f64 },
    Str { s: u8[varint] },
    Arr { children: Json..[>..] },
    Obj { members: Member..[>..] },
}

var doc: Json.. = Json.Obj { members: [] };
```

and then add values not only to the root object but to containers at any
depth. Two things stand in the way: the type is rejected (a type may not
contain itself by value, and an array element may not be resizable), and
there is no way to say which part of a growable value is the one that
currently grows. This note records what the rules say today, which of the
needed pieces already exist, the options for each of the two problems, and
a recommendation.

<details>
<summary>Table of contents</summary>

- [1. The fork: depth-first building or random mutation](#1-the-fork-depth-first-building-or-random-mutation)
- [2. What the compiler says today](#2-what-the-compiler-says-today)
- [3. The type relaxations](#3-the-type-relaxations)
  - [3.1 R1: recursion through an array](#31-r1-recursion-through-an-array)
  - [3.2 R2: resizable elements, growable while open](#32-r2-resizable-elements-growable-while-open)
- [4. Who decides which part is open](#4-who-decides-which-part-is-open)
  - [4.1 Option A: the construction order decides (static, lexical)](#41-option-a-the-construction-order-decides-static-lexical)
  - [4.2 Option B: open handles tracked by flow](#42-option-b-open-handles-tracked-by-flow)
  - [4.3 Option C: a runtime check that the container is on top](#43-option-c-a-runtime-check-that-the-container-is-on-top)
  - [4.4 Option D: a pool of linked nodes (today's json::Node)](#44-option-d-a-pool-of-linked-nodes-todays-jsonnode)
  - [4.5 Options rejected: a stack per container, memmove insertion](#45-options-rejected-a-stack-per-container-memmove-insertion)
  - [4.6 Option E: append handles, a checked push through a reference](#46-option-e-append-handles-a-checked-push-through-a-reference)
  - [4.7 The options side by side](#47-the-options-side-by-side)
- [5. Costs shared by every in-place option](#5-costs-shared-by-every-in-place-option)
- [6. Prior art](#6-prior-art)
- [7. Recommendation](#7-recommendation)

</details>

## 1. The fork: depth-first building or random mutation

In a packed layout a tree's bytes are its depth-first traversal: a
container, then its first child with everything inside it, then its second
child, and so on. While such a tree is being built there is one place where
bytes can be added without moving anything: the end. The containers that
can still take a child are exactly the ones on the path from the root to the
end, and all of them end at the same byte. Growing any of them is the same
pointer bump.

This is the one-resizable-per-stack invariant (spec §1.3) seen from a
different angle. The invariant need not say "one resizable value per
stack"; it can say "one *open path* per stack", a chain of values each of
which is the last thing in the one before it and the last of which tops the
stack. Nothing moves, no capacity is checked, no bookkeeping is kept. So
"which sub-part is open" has a canonical answer: the innermost container on
the path, and only that one. Its ancestors are locked for growth, which is
already the rule for a value under construction (§1.3(4), §4.2), and they
remain readable, because a container's count is bumped only when a child is
complete.

What this can never do is add to a container that is not on the path: an
earlier sibling, or anything at all once the tree is finished. That needs
either indirection (nodes linked by references, which is what `json::Node`
does today) or moving memory, which the language forbids. So the first
design decision is which of two things the feature is for:

* **Depth-first building**: parsers, deserializers, builders, serializers,
  anything that produces a tree in traversal order. The result is one
  contiguous flat value: no links, so it can be serialized with `to_bytes`
  as is and sent to a worker, since it is flat. Section 3 and sections 4.1
  to 4.3 and 4.6 are about this.
* **Random mutation**: an editable document model, where a member is added
  to an object long after its siblings were. This is a pool of linked
  nodes, section 4.4, and it exists.

JSON parsing and writing are the first kind. The declaration above is a
*tape*, and the rest of this note treats it as one.

## 2. What the compiler says today

Probes against the current compiler, so that the proposals below name the
exact rules they change:

| Program | Result |
|---|---|
| The enum above, or the same with frozen `Json..[]` children | `enum Json contains itself by value`. The ban covers recursion through any array, whatever its class. |
| `struct Inner { n: i64, xs: i64[>..] }` as the element of an `Inner[>..]` | `array elements may not be resizable` (§3.4). |
| `enum Log { Empty, Lines { items: i64[>..] } }` as a local, matched with `Lines &x` | `binding a resizable ADT payload is not supported by the C backend yet`. A resizable ADT exists (§3.5) but its payload cannot be reached by reference, and §8.1 binds it by value only, because a whole assignment may replace the variant. |
| `inner() -> i64[>..]` pushed in a loop by `outer() -> i64[][>..]`, pushed in turn into an `i64[][][>..]` | Works. Each level is a named result (§7.3) built directly at the top of its parent's array; the three levels share one stack. |
| `recursive fn nest(d) -> u8[>..]` whose body pushes, appends `nest(d - 1)`, pushes, and returns its local | `calls into its recursive cycle while non-fixed-size local out is in scope (§7.8)`. |
| `words.push(nest(words, d - 1))` inside the cycle, for `words: u8[][>..]&` owned outside | `the element pushed into words is still under construction, and the growth would land inside it (§1.3)`. The checker already enforces the ancestor lock, through the growth log (`CheckGrowsSince` in `src/typecheck_builtins.h`). |

So the machinery for a depth-first build on one stack exists for a fixed
number of levels: in-place result construction, named-result placement at
the push slot, and the under-construction lock. What is missing is the
recursive type, the recursive *function* (whose named result is today
counted as a stack of its own), and a way to keep a container open across
more than one statement.

## 3. The type relaxations

### 3.1 R1: recursion through an array

**Rule.** A type may contain itself by value only inside an array whose
length is not a positive constant: `T[]`, `T[uN]`, `T[varint]`, `T[>..]`.
The empty array is the base case, as `null` is for `List?` today. A direct
field, a payload field, and a `T[k]` with `k > 0` stay errors. `T[>..<]`,
`T[..k]` and `T[..]` need fixed-size elements, which a recursive type can
never be, so they are excluded already.

Mutual recursion through structs and enums (`Json` contains `Member[>..]`,
`Member` contains `Json..`) is the same rule: every cycle in the
by-value containment graph must pass through such an array. The check is on
the graph of type instances, so a generic `Tree<T>` containing
`Tree<T>[]` is fine, while a non-regular `Tree<T>` containing
`Tree<Pair<T>>[]` instantiates without end; the existing bound on
instantiations in progress (§7.8, polymorphic recursion) rejects it with the
chain.

**Consequences.**

* Such a type is variable-class at best, so its arrays are sequential:
  iterate, never index. A JSON array is walked, as today's `json::each`
  walks it.
* Everything the compiler generates per type by walking its fields (size,
  equality, `print`/`format`, `copy`, the `from_bytes` verifier) becomes a
  recursive C function instead of an unrolled expression, **for the types
  that are recursive and no others**. The by-value containment graph has
  the answer: a type is recursive when it lies on a cycle of that graph
  (a nontrivial strongly connected component), and only those types get
  recursive walkers. A type that merely contains a recursive one (`struct
  Doc { name: u8[], root: Json.. }`) keeps its unrolled walker, which
  calls `Json`'s. Every type the compiler handles today is off every
  cycle and compiles exactly as now. The checker's deferred-type
  machinery (`typesbuilding`, `laterpointees` in `src/typecheck_types.h`)
  already handles cycles through references and needs to admit by-value
  ones.
* Those walkers recurse on the native stack to the data's depth. A
  hostile document can be nested a million deep, and `print(doc)` would
  then overflow the native stack. Every JSON library caps depth for this
  reason (serde_json at 128, simdjson at 1024 by default); the parser
  enforces the cap, and the generated walkers inherit it from the data.
  This is the same exposure a hand-written `recursive fn` over
  `json::Node` has today, moved into generated code.
* A `match` arm's by-value binding of such a payload is an O(size) copy, as
  it is for any variable payload. Reading a tape without copying needs
  `&`-binders, hence variable mode, hence the backend gap in section 2.

**Pros.** Independent of everything else; useful on its own for frozen
trees built by results (`let doc: Json.. = parse(text)` for a `parse` that
builds its result in place). Removes a rule that is stricter than the
representation needs. Trees that are flat values: serializable and sendable
without the relaxation TODO 9 asks for.

**Cons.** Walker generation and the depth exposure above; sequential-only
access; the `&`-binding gap becomes load-bearing.

### 3.2 R2: resizable elements, growable while open

The at-rest representation of a nested `children` is a count and the
children's bytes, the same bytes whether the field is spelled `Json..[]` or
`Json..[>..]`. The two spellings differ in whether the field may grow and,
today, in where the count lives (a `T[>..]` keeps its header in the owning
frame, Appendix C.2; a `T[]` keeps it in front of its elements). There are
two ways to write the field.

**(a) `Json..[]` at rest; openness grants growth.** No placement rule
changes. A container is a frozen variable value, and the mechanism of
section 4 lets it be pushed into for as long as it is under construction,
which is what a push into a variable element already does for the duration
of one expression. The cost is at the root: `var doc: Json.. = Json.Obj {
members: [] }` is a variable-class local, frozen after its initializer, so
there is no way to keep *it* open except by making the whole program's root
a resizable ADT with a `[>..]` payload, which cannot be the same field.
Under (a) a tree is only ever built by a result (`let doc = parse(text)`)
or a builder block, never grown from a local declaration. That covers
parsers and nothing else.

**(b) `Json..[>..]` as written; growable exactly while on the open path.**
Relax §3.4: a resizable value may be an element of a sequential array. The
array is then resizable-class itself, and the element is growable only
while it is the innermost open value (section 4). Everywhere else it is
read as the frozen array it is. The reading of `[>..]` stays uniform with
the one it has on a struct tail today: *growable through its owner, when
its owner is*. A tail field is always the last thing in its struct, so a
tail is growable whenever the struct is; an element is the last thing only
while it is the last element, and the rule says precisely that.
Representation: an element's count is in-tape (a `u32`), the root's where
a resizable ADT keeps it today, or in-tape as well so the walkers see one
shape. `[>..<]` is never allowed here, since a sequential array cannot pop.

The class system needs one refinement for (b): a resizable-class *element*
counts as resizable for placement (it may only live in a sequential array
or at a tail) and as variable for everything keyed on "may this be grown
here" except through the open path. The rules to audit are placement,
`copy`, whole assignment (§4.4, which stays root-only), shrink receivers
(§5.1: an element is never one), frame objects (C.2: an element is never
one) and `SoleResizableIn`, which assumes one resizable per type.

**Pros of (b).** The declaration says what the type is for. One spelling at
every level; the root is an ordinary resizable ADT local that can be grown
in place from its declaration. **Cons.** A type that says resizable and is
frozen in most of its instances; every class-keyed rule needs the element
refinement. (a) is the conservative fallback if that audit turns up too
much.

**Which has fewer side effects.** (a) touches fewer rules but leaves a hole
that the append handles of section 4.6 cannot fill: a `var doc: Json..`
whose arrays are `[]` is a variable-class local, and a variable local may
be buried under later allocations on its stack once built (§1.3(4)), so
nothing can ever grow it, handle or no handle. Growing from a declaration
needs the root to own its stack top, which means resizable class, which
means `[>..]` in the declaration, which is (b). The audit for (b) reduces
to one predicate: **a `[>..]` in element position is a `[]` for every
rule except the two that grow it**, the checked push through a reference
(4.6) and the statically proven push on the open path (4.1). Placement,
copying, whole assignment, shrink receivers, frame objects and
`SoleResizableIn` all take the `[]` answer.

A count that is patched in place as children close must be fixed-width. A
`Json..[varint]` container could be built in one expression (its length is
known when the expression ends, under §4.3's relocation exception) but
never kept open across statements, unless closing it may move the
container's bytes, which would invalidate references into its closed
children. Fixed-width counts for anything openable is the simple rule.

## 4. Who decides which part is open

### 4.1 Option A: the construction order decides (static, lexical)

The open path is the nesting of constructions in the source. Two surface
forms, one mechanism.

**Results, for recursion.** The natural recursive-descent parser:

```goose
recursive fn value(r: json::Reader&) -> Json.. {
    match json::peek(r) {
        '[' => {
            var a = Json.Arr { children: [] };
            json::each_element(r) { a.children.push(value(r)); };
            return a;
        },
        '{' => {
            var o = Json.Obj { members: [] };
            json::each_member(r) { key => o.members.push(Member { key: key, val: value(r) }); };
            return o;
        },
        '"' => Json.Str { s: json::read_string(r, scratch) },
        _ => Json.Num { v: json::read_number(r) },
    }
}
```

`a` is a named result (§7.3), so it is allocated at the caller's destination
from its declaration: the top of the parent's `children`, which is the top
of the parent's parent's, and so on to `let doc = value(r)` in the
non-recursive caller. Every activation builds on the one stack that `doc`
owns. The rule to change is §7.8's "no nonfixed local in scope at a call
into the cycle", whose reason, a stack per activation, does not apply to a
local that holds no stack of its own: **a named result built at the
caller's destination may be in scope at a call into the cycle, provided the
call's own result is pushed into it or into its open tail.** Then the
callee's destination is the top of the same stack, and the activation
contributes no stack. Any other nonfixed local keeps the rule, and so does a
named result that is copied on return rather than placed (§7.3 already
warns about those). The pushed element lock (§1.3(4)) already rejects a
callee growing `a.children` while the element it is building is incomplete,
as the last probe in section 2 shows.

Two other pieces this form needs: `a.children` is a payload field of a
resizable ADT local, which today cannot be reached by reference (section 2);
and a `scratch` for decoded strings, owned outside the cycle and passed
down, as §7.8 already requires. A string decoded straight into the tape,
`Json.Str { s: json::read_string_into(r) }` constructing its result in the
payload, avoids the scratch entirely, by the same in-place rules.

**Builder blocks, for straight-line code.** A push whose element is to be
grown further takes a trailing block, the element open inside it:

```goose
var doc: Json.. = Json.Obj { members: [] };
doc.members.push(Member { key: "name", val: Json.Str { s: "goose" } });
doc.members.push(Member { key: "tags", val: Json.Arr { children: [] } }) { m =>
    m.val.children.push(Json.Str { s: "flat" });      // grows the open element
    // doc.members.push(...) here is the existing error: doc.members is under construction
    m.val.children.push(Json.Obj { members: [] }) { inner => inner.members.push(…); };
};
// doc.members.len is 2 only from here: a push exposes its length when the element is complete
```

The block is the element's construction expression spread over statements.
The checker treats `m` as a reference rooted exactly at the top of
`doc.members`, growable, and the growth log already locks every ancestor
for the block's extent. The element's count becomes visible at the block's
end, which is the rule for a push today ("a push exposes its new length only
after the element is complete", `docs/design/result_construction.md`). A
`return … from` out of the block abandons the partial element, which §7.3's
Exits already specifies. Reads of ancestors inside the block are fine:
their counts exclude the open child, so an iteration never walks into
unfinished bytes.

**Pros.** No runtime cost and no new runtime state; nothing to check at run
time. The pieces are extensions of mechanisms that exist (named-result
placement, destination requests, the growth log, trailing blocks). A
finished tree is one flat value. The §7.8 relaxation is narrow and
explainable: a local that holds no stack is not a stack per activation.

**Cons.** Depth-first only: the open path is the lexical nesting or the
recursion depth. A document of unbounded nesting therefore needs recursion,
which needs a depth cap on untrusted input. Today's `json::parse` is one
loop with an explicit `Open[>..<]` of enclosing containers, and parses any
depth; the tape form cannot be written that way under Option A. Builder
blocks are verbose for deep hand-built trees, and a tree whose shape is
decided by data is built by a recursive function anyway.

### 4.2 Option B: open handles tracked by flow

Instead of a block, an operation that returns an open handle and a point
where it closes:

```goose
let tags .= doc.members.open(Member { key: "tags", val: Json.Arr { children: [] } });
tags.val.children.push(…);
… // other statements, calls that take `tags`
close(tags);                                   // or: at the end of tags's scope
```

The checker tracks "open" as a flow-sensitive property of the handle, the
way it tracks definite assignment and shrink liveness today: while `tags`
is open, `doc.members` is locked; after `close`, `tags` is dead and
`doc.members` grows again. Opening a child of `tags` locks `tags`, and so
on.

**Pros.** Open in one function, add in another, close in a third; a state
machine can hold one open container in a local across loop iterations
without a block around the loop. **Cons.** This is typestate in a language
that has none; every branch, loop, early return and call that may close or
open must be merged and summarized per specialization. The depth is still
static, so it buys nothing for unbounded nesting, and recursion is not made
easier. The same programs can be written with Option A plus a small
restructuring, so B is not worth its checker until a use shows up that A
cannot express.

### 4.3 Option C: a runtime check that the container is on top

Allow `push` through any reference to a nested `[>..]` element, and check
at run time that the container is the innermost open one: that its end is
the stack top. The language already aborts on a bounds check, and
`realloc_slice` already checks containment at run time where the checker
cannot place a slice (`docs/design/owning_dynamic_data.md`), so this is not
without precedent. It is what GNU obstacks do, unchecked.

The difficulty is knowing a container's end. A sequential container's end
is not computable from its count without walking its children, so the
stack would keep an `open` pointer, set on open and restored on close. The
restore needs the parent, which means either a runtime stack of open
containers (bookkeeping §1.3 rules out) or a `close(child, parent)` that
the check validates (`child` must be the current open; `parent` becomes
it). With the second form the program keeps the open chain itself, in an
`(Json.Arr&)[>..<]` or whatever it likes, which is how `json::parse` keeps
its `Open` stack today.

**Pros.** The iterative, any-depth parser becomes possible on the tape form;
the checker rules shrink to almost nothing. **Cons.** A new class of runtime
failure in the one place the language otherwise proves everything
statically, and the failure is an abort, not a result. A compare per push
through a nested reference. The explicit `close(child, parent)` is easy to
get wrong in exactly the way the checker exists to prevent. Reasonable only
as an escape hatch, as `reusable` is, and only if unbounded-depth iterative
parsing of the tape turns out to matter.

### 4.4 Option D: a pool of linked nodes (today's json::Node)

```goose
struct Node { next: Node&<u32>?, first: Node&<u32>?, key: const u8[:], val: Value }
```

Every node is one fixed-size slot in a `Node[>..]`, and a container is a
linked list of its children through 4-byte self-relative references. A
child can be added to any container at any time, in O(1), by linking a new
slot. This is the random-mutation answer and it exists.

**Pros.** Works now; insertion anywhere, any order; one flat array with
relative links, so it serializes; `member` and the accessors are already
written. **Cons.** 8 bytes of links per node plus 16 bytes of slice per
key or string; strings live in the text or a side buffer, so a document is
three arrays, not one value; the children of a container are in link order
and scattered through the array; not flat in the §1.1 sense (it holds
references), so it cannot cross to a worker until TODO 9's relaxation; and
it is not the declaration the question started from.

### 4.5 Options rejected: a stack per container, memmove insertion

* **A data stack per container** (dynamic stacks, §11.3; option A of
  `owning_dynamic_data.md`) would let every container grow independently.
  A JSON document has thousands of containers; a reservation each is
  absurd, and the fat references and lifetime tracking that option needs are
  for a handful of large independent buffers, not a tree.
* **Insertion by moving what follows** (TODO 8's memmove-on-insert) would
  let a container in the middle of the tape grow. It is O(document) per
  insert, and it moves bytes that references may point into, so every
  reference into the tape past the insertion point would dangle. Relative
  references survive only when both ends move together, which links across
  the insertion point do not. Sound only with no references into the tape
  at all, which gives up the thing Goose holds instead of indices.

### 4.6 Option E: append handles, a checked push through a reference

The idea: a handle on a nested container, holding the array and the end of
its storage, obtained whenever the program likes,

```goose
var doc: Json.. = Json.Obj { members: [] };
let ra = doc.members.appender();
let m .= ra.push(Member { key: "tags", val: Json.Arr { children: [] } });
let ta = m.val.children.appender();         // through Arr &a, or a case function
ta.push(Json.Str { s: "flat" });
ra.push(Member { key: "n", val: Json.Num { v: 1.0 } });   // back to the parent
ta.push(Json.Null);                                          // aborts: tags is closed
```

where `push` through a handle is an ordinary push after one comparison of
the handle's end with the stack top, and aborts when they differ. No open,
no close, no required code shape: the growth point is chosen at run time
among the containers on the open path, and a program may hold as many
handles as it likes. Option C's difficulty, knowing a container's end, is
answered by caching it in the handle at the one moment it is known.

**The cached end goes stale.** The version just described rejects the
example above at the fourth push. After `ta.push`, the tags array has
grown, so the member holding it has grown, so `doc.members` has grown: its
end is the stack top again, but `ra` still caches the end as it was when
the member was pushed. Every container on the open path ends at the top,
so when a descendant grows, every ancestor's cached end is stale although
every ancestor is still appendable. A handle that aborted on a stale end
would reject precisely the use handles exist for, returning to a parent
after building a child. The cached end can only be a fast path; the slow
path has to re-derive the answer.

**A sound and cheap form.** A handle is `{ count pointer, stack, end,
last }`, with `last` the start of the last element pushed through it.
`push` is then:

1. `end == top`: push. This is the usual case and the branch predicts it.
2. Otherwise walk the last element with the type's size walker (which R1
   generates): if `last + size(last) == top`, every byte since `end` lies
   inside that element, so the container is still on the open path; set
   `end = top` and push. If not, something was appended after the
   container, and the push aborts, naming the container and where the
   foreign bytes start. An empty container has no last element and aborts
   here outright.

The argument that this is sound: a push is atomic (the count is written
after the element's bytes, as today), so between statements the tape is
complete up to the top; the walk reads only counted elements; and a
successful push through any handle implies its container is on the open
path, so the bytes it adds are the container's. The handle's correctness
rests on nothing outside the handle: no per-stack pointer, no scope-exit
hook. So handles are fixed-size values that may be copied and stored
anywhere a reference may, and a stale copy merely takes the slow path. The
check must be exactly right rather than best-effort: a push accepted into a
closed container would bump a count whose bytes are now a sibling's, and
the walkers would then read that sibling as a child, which is type
confusion inside a packed tape. Deriving the answer from the tape is what
rules that out.

The slow path costs a walk of the last child's subtree each time a parent
is resumed, so a parser built from handles alone re-reads each subtree once
per ancestor level, about `depth` times the parse's own reads. Where the
child's handle is still at hand, which the depth-first parser's own stack
guarantees, `parent.resume(child)` is O(1) and equally sound: `child.end
== top` and the child's array lies inside the parent's last element
(`parent.last <= child.array < top`) together imply that everything since
the parent's cached end is inside that element. Creating a handle is O(1)
on an empty container or on the element just pushed, and otherwise a walk
of the container, which doubles as the check that it is appendable at all;
that is the "call it when it is cheap" instinct, made precise.

A linked alternative (handles pointing at their parent handle, and the
stack keeping a pointer to the last pusher) makes every path O(1), but a
stale per-stack pointer into popped storage could validate a closed
container, so it needs handles confined to variables and a hook at every
handle's scope exit. The derived form needs none of that and is the one to
build.

**What it buys.** Three things the static options cannot do:

* A data-dependent growth point. A `[>..<]` of handles is the explicit
  open stack, so the iterative, any-depth parser that `json::parse` is
  today can be written over the tape form.
* Recursion with no change to §7.8: `recursive fn value(r: Reader&, into:
  Json..[>..]&)` pushes the node into `into`, binds its payload by
  reference and recurses on `a.children`. Every handle is rooted at `doc`
  outside the cycle, and no activation declares a nonfixed local. The
  checker's growth summaries must learn that a nested array's growth is
  its root's stack's growth, which Option A needs too.
* No partial values. Every statement boundary sees a complete tree, since
  a push is atomic, where a builder block leaves its element incomplete
  across the block's statements.

**What it costs.** One register comparison per push, mispredicted once per
return to a parent; the write of the in-tape count; a four-word reference
form. And the failure is dynamic: whether a handle is still usable depends
on the order of pushes, and a program that holds many handles and appends
in a data-dependent order finds out under test, not at compile time. That
is the one place this breaks with the rest of the language. "Append
anywhere" still means anywhere on the open path: a closed sibling is
closed, and the abort says so.

**The same operation as Option A.** A push through a handle and a push in
a builder block or a named-result recursion add the same bytes to the same
stack. The difference is that in Option A the checker has *proved* the
container is on the open path (it is the element the block is building, or
the result the activation is constructing), so the comparison can be
dropped. That makes Option A the check elimination for Option E, exactly as
bounds-check elimination (§10.5) is for indexing, and the same conventions
carry over: the check stays unless proven away, `checkfree fn` can demand
that none remain, and a debug build can keep the check in a push the
checker elided, as an oracle for the proof. The `with` block the idea came
with (`ra.with() { … unchecked pushes … }`) is Option A's builder block
entered from a handle: one check at entry, no comparison inside, and the
growth log statically forbidding any other growth of that stack within the
block.

### 4.7 The options side by side

| | A: construction order | B: flow-tracked handles | C: runtime top check | E: append handles | D: linked pool |
|---|---|---|---|---|---|
| Growth point | innermost open element, by source structure | static handles, by flow | any container on the open path | any container on the open path | any container, any time |
| Decided | at compile time | at compile time | at run time | at run time, elided where A proves it | n/a |
| Per-push cost | none | none | compare, plus an explicit close | compare; a walk or O(1) resume on return to a parent | a link store; 8 bytes a node |
| Failure | compile error | compile error | abort | abort | none |
| Any-depth iterative parse | no | no | yes | yes | yes |
| Recursive parse needs | §7.8 relaxation | §7.8 relaxation | nothing | nothing | nothing |
| Partial values visible | inside the block | while open | no | no | n/a |
| Checker work | medium | high | low | low | done |
| Result | one flat value | one flat value | one flat value | one flat value | three arrays with links |

## 5. Costs shared by every in-place option

* **The resizable-ADT payload gap** (section 2) is on the critical path:
  the root `Json..` is a resizable ADT, and both `a.children.push(…)` and
  `Arr &a =>` on it need a reference into its payload. §8.1 forbids the
  binder because a whole assignment may replace the variant; the binder is
  sound while the assignment is rejected for the binder's extent, which is
  the shrink-liveness check §5.1 already performs for a `clear`.
* **Walkers on recursive types** recurse to the data's depth (section 3.1).
  A depth cap belongs in the parser; `print` of a parsed document is then
  bounded by it.
* **Counts are fixed-width** for anything kept open; `varint` lengths only
  for leaves and for containers built in one expression.
* **Reads during construction** are safe by the count-on-close rule, which
  is in force for `push` today. The one new thing a reader can hold is a
  reference to the open element itself, which is the handle.
* **Whole assignment of the root** (`doc = Json.Null`) is the existing
  clear-then-construct (§4.4) and needs nothing open, by the growth log.
* **Non-regular recursive types** are rejected at instantiation, with the
  existing bound.
* **The stdlib** would carry two JSON representations if the tape is added:
  the linked `Node` for mutation and lookup by key, and the tape for parse,
  walk, serialize and ship. That is the same split simdjson makes between
  its DOM and its tape.

## 6. Prior art

* **GNU obstacks** are the direct ancestor: one object at a time may grow
  (`obstack_grow`), the obstack allocates nothing else until
  `obstack_finish`, and a finished object is fixed. Option A is obstacks
  with the growing object allowed to be *inside* the one being grown, and
  the discipline checked rather than trusted.
* **FlatBuffers' builder** forbids starting a table while another is
  unfinished ("nested tables not allowed"), for the same one-open-object
  reason, and sidesteps the nesting problem by building children before
  parents so that offsets point backwards. Goose's tape builds parents
  first and patches fixed-width counts forward instead.
* **Protocol Buffers** nest length-delimited messages and must know each
  nested size before writing it, hence `ByteSizeLong` and a two-pass
  serializer, or a buffer per level. In-place `u32` counts are what make
  the single pass possible here; a varint count would reintroduce the
  two-pass problem.
* **simdjson's tape** is a flat depth-first array where a container records
  where it ends, built in one pass by a parser that keeps an explicit
  open-container stack, and read sequentially. The finished `Json..` tape
  is the same shape with Goose's own counts; simdjson keeps its mutable
  DOM separate, as section 5 suggests.
* **MLKit regions and ParaSail** are covered in `prior_art.md`; neither has
  the nested-growth-on-one-region idea, since both allocate whole objects.

## 7. Recommendation

1. **R1 first** (section 3.1): recursion through an array, with recursive
   walkers only for the types on a containment cycle. It stands on its
   own, it makes frozen trees expressible, and the result-construction
   machinery already builds them in place a fixed number of levels deep.
   The size walker it generates is also what Option E's slow path runs.
2. **R2 as (b)** (section 3.2), since (a) cannot grow a root from its
   declaration, with the audit reduced to the one predicate above. Close
   the resizable-ADT payload gap at the same time; every option needs it.
3. **Option E as the mechanism, Option A as its check elimination.** Build
   the checked push through a reference to a nested array first: it is the
   smaller checker change, it delivers the whole feature (the recursive
   and the iterative JSON tape parser, hand-built trees, handles stored in
   a stack), and it defines the semantics that the elision must preserve.
   Then teach the checker the two proofs that drop the comparison, the
   builder block and the named result in a recursion (the §7.8 relaxation),
   with the check kept in debug builds as the oracle. `checkfree fn` then
   covers appends as it covers indexing.
4. **Keep `json::Node`** as the mutable, key-addressable document model;
   do not try to make the tape mutable.
5. **Drop Options B and C.** E subsumes C (it is C with the end problem
   solved) and gives B's flexibility without its typestate.
