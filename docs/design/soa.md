# Column arrays: minimal struct-of-arrays storage

A column array `T[||..]` stores a struct element type as one array per leaf
field, each on its own data stack, with one shared length. It is deliberately
a storage type and little more: an element is read out as an ordinary struct
or written through a field path, and everything else (sorting, slicing,
serializing, handing an element to a function by reference) happens on a
lifted copy.

```goose
import std;
import vec;

struct Particle { pos: float3, vel: float3, age: i32 }   // seven columns

fn spawn(ps: Particle[||..]&, n: i64) {
    for i in n {
        let x = i as f32;
        ps.push(Particle { pos: float3 { x, 1.0, 0.0 },
                           vel: float3 { 0.0, (i % 4) as f32, 1.0 }, age: 0 });
    }
}

fn step(ps: Particle[||..]&, g: float3) {
    for i in ps.len {
        ps[i].vel = ps[i].vel + g;          // three columns in, three out
        ps[i].pos = ps[i].pos + ps[i].vel;
        ps[i].age++;                        // one column
    }
    var i = 0;
    while i < ps.len {
        if ps[i].pos.y < 0.0 {
            ps[i] = ps[ps.len - 1];         // swap-remove: whole elements
            ps.pop();
        } else {
            i++;
        }
    }
}

fn main() {
    var ps: Particle[||..] = [];
    spawn(ps, 1000);
    for s in 8 { step(ps, float3 { 0.0, -0.5, 0.0 }); }
    var highest = ps[0];                    // lifted: an ordinary Particle
    for p in ps { if p.pos.y > highest.pos.y { highest = p; } }
    print(ps.len, " ", highest);
    var sorted: Particle[>..] = [];
    sorted.append(ps);                      // the whole array lifted, to sort
    sort(sorted) { a, b => a.pos.y > b.pos.y };
}
```

With `[||..]` replaced by `[>..<]`, this program compiles and runs today. A
column array changes where the elements live and what else may be done with
them, never what the program means.

## Why a new array kind

Today this layout is written by hand, one array per leaf:

```goose
var px: f32[>..] = []; var py: f32[>..] = []; var pz: f32[>..] = [];
var vx: f32[>..] = []; var vy: f32[>..] = []; var vz: f32[>..] = [];
var age: i32[>..] = [];
```

It stays that way everywhere it goes: seven pushes per element, seven lengths
nothing keeps equal, seven parameters on every helper, and no way to bundle
them, since a struct holds at most one resizable (§3.4). It is also slower
than it has to be. Every one of those arrays starts at the same page offset
of its own stack, which costs 2x on a six-array version of the particles
kernel through 4K aliasing (measurements below), and nothing tells the
compiler the arrays are parallel.

A complete struct-of-arrays feature, where element references and slices
become proxies that ordinary functions accept, layouts are chosen per
specialization, and arrays are returned into column destinations, was
estimated at about 2,300 lines, 90% of it codegen, and most of that is the
cost of letting an element or a slice escape into code written for ordinary
arrays. A column array never lets anything escape: it is its own
array kind, it coerces to nothing, and no reference, slice or view into it
can exist. What remains is storage plumbing and a handful of members, about a
third of the size.

## Rules

**The type.** `T[||..]` is resizable (§1.1) and both grows and shrinks, like
`T[>..<]`: `||` for the columns, `..` for the changing length.

**Elements.** `T` must be fixed-size and flat (§1.1: no references, slices or
relative references at any depth). A field whose type is a struct is split
into its fields, recursively. Any other field is one column: a scalar, a
`bool`, a fixed array, a static-capacity limited array (a `u8[..16]` name),
or a fixed-mode enum. `pad` fields have no column. A `T` that is not a struct
is one column, so generic code may write `T[||..]` for any fixed, flat `T`.

**Where it lives.** A local or global `var` or `let`, initialized with `[]` or
an array literal. It is never a struct field, an array element, an ADT
payload, a by-value parameter, a return value, a thread argument, a queue
value, or a global that a thread program uses. Functions take it by
reference: a `T[||..]&` parameter, or an untyped one, which binds the array by
reference (§4.1). A reference to the whole array is an ordinary reference
under the ordinary rules.

**Elements are paths, not places.** `ps[i]` followed by any field steps and
fixed-array index steps (`ps[i]`, `ps[i].pos`, `ps[i].pos.y`) is an *element
path*. A path can be:

* read as a value (`let p = ps[i]`, `ps[i].pos + d`, `f(ps[i])` into a
  by-value parameter, `print(ps[i].vel)`): a copy, loaded from the columns
  the path spans;
* written (`ps[i] = p`, `ps[i].pos = v`, `ps[i].pos.y = 0.0`), with compound
  assignment and `++`/`--` on a numeric leaf.

Nothing can refer to a path. `&ps[i]`, `var r .= ps[i]`, passing a path where
a reference is expected (`fn f(p: Particle&)`), slicing (`ps[a..b]`, or a
slice of a fixed-array leaf), `for` over a leaf array and `for &p in ps` are
compile errors that point at the copy-out and path forms. A path checks its
index once against the one length, however many columns it touches, and the
bounds-check pass elides that check as for any array.

**Members.**

| Member | Meaning |
|---|---|
| `.len -> i64` | the element count |
| `.push(v)` | appends one element; no result (its index is `len - 1`) |
| `.pop() -> T` | removes the last element and returns it by value |
| `.resize(n, v)`, `.resize(n)`, `.clear()` | as on `T[>..<]` |
| `.append(src)` | appends every element of `src`: any array or slice of `T` (a call's array result included), or another `T[||..]` |

An ordinary growable array of `T` also accepts a column array in its own
`append` (`sorted.append(ps)` above): that is the whole-array lift.
`for p in ps` and `for p, i in ps` iterate by value (§6.5), re-reading the
length as for any array. `print`, `str` and `format` render a column array as
they render `T[>..<]`.

**Shrinking.** `pop`, `resize` and `clear` are legal wherever a grow-shrink
array's are: on a local, through a reference, on a global (§5.2). The §5.2
test is kept for what can still involve a column array, an element path of
the same array held earlier in the statement: `ps[i] = ps.pop()` is rejected
exactly as it is for `T[>..<]` today, and so is a call that shrinks `ps` in
the same position. The scan of reference and slice variables is skipped,
since none can point into a column array.

**Not in v1.** `==`, `copy(ps)`, whole assignment, `index_of`, slices and
views, `to_bytes`, `bytes_of` and `from_bytes`, `reusable`, `in pool`
references to its elements, and every function that takes `T[:]` (`sort`,
`find`, `filter` and the other slice functions of `std`) are compile errors.
The way through is the lift: `xs.append(ps)` into an ordinary array, and back
with `ps.clear(); ps.append(xs)` where needed.

## What the checker does not need

Most of the checker's machinery is about references, and nothing refers into
a column array:

* **Roots and lifetimes** (§9.2). A column array is only ever the root of a
  reference to the whole array, which no shrink invalidates. Its elements
  hold no references, are never a read-back candidate, and are never the
  target of a relative reference, so read-back roots, holders and the
  liveness scans at shrinks never involve them.
* **Growth during construction** (§4.2). A pushed element is fixed-size and
  holds no relative references, so it is evaluated before its slot is claimed,
  and `append` of a call result builds the result first. No column array is
  ever under construction while an expression runs.
* **Bounds-check elimination** (§10.5). A column array is a place with one
  length, changed by the same operations as a `T[>..<]`'s.

## Representation

* **The header** lives in the owning frame, or in the program instance's
  globals: one typed pointer per column plus the count, `{ float *pos_x; ...
  int32_t *age; int64_t len; }`. A `T[||..]&` is a plain pointer to it.
* **Columns** each take a data stack: a local takes N stack indices from its
  function's block (§10.3), a global N reservations of its own. Column k's
  first element sits at its own page offset, k × 4096 / N past a page
  boundary, so parallel columns never share their low 12 address bits at the
  same index. This costs at most a page of address space per column.
* **Stack tops are never used.** A column is the only value on its stack for
  the array's whole life, and its elements are fixed-size, so its next slot is
  always `base + len * size`. Push stores N leaves and increments `len`; pop,
  resize and clear store `len` (resize also fills). The stack's `top` is read
  once, at the declaration, and never written. Column arrays therefore need
  none of the stack-top caching and synchronization of implementation.md §6.10,
  no watermark restores, and no stack identity in references: pushing through
  a `T[||..]&` needs only the header.
* **Element paths** are one new codegen location: the header, the checked
  index held in a temporary, and the range of columns the path spans. A field
  step narrows the range, and a path that reaches a leaf is an ordinary C
  lvalue (`ps.pos_y[ix]`), so compound assignment, `++`, `match` on an enum
  leaf and indexing into a fixed-array leaf need nothing new. A path spanning
  several columns has two consumers: a load gathers the columns into a C
  value, and a store scatters one out. Elementwise math between paths of one
  array goes through `GenElemwiseInto` with column-mapped members, so
  `ps[i].pos = ps[i].pos + ps[i].vel` compiles to per-column loads and
  stores with no gathered temporaries, as the array-of-structs form does
  today.
* **Loops.** Column pointers never change while the array lives (growth
  never moves, and there is no whole assignment), so a loop reads each one
  into a local once. They are declared `restrict` where the loop reaches the
  array through them alone: no growth, shrink or call that can reach the array
  inside the loop (the `hoistrefs` conditions, implementation.md §5.10), and
  no second column array that could be the same one.

## Implementation

Estimates are rough (±30%). They come from the inventory of the full design,
with everything a column array does not have taken out.

* **Parser, AST, dump** (~10 lines): `[` `||` `..` `]` in the type postfix
  loop (`||` already lexes as `T_OROR`), an `A_COLS` array kind, and its
  spelling in `--dump`.
* **Checker** (~150–200):
  * `ValidateType`: the element is fixed-size and flat, and the type appears
    only as a variable's type or a reference's pointee. `ClassOf` makes it
    resizable. `CanContain` leaves it out, so it is no read-back candidate.
  * `CheckLValue`: an element-path bit on `LVal`, set at the index step and
    kept through field and fixed-array index steps. `CheckRefOf`,
    `AutoRef`/`BindsRef`, `.=` declarations and rebinds, slices and `for`
    over a path reject it, with one diagnostic naming the copy-out and path
    forms.
  * Builtins: a `BR_COLS` receiver bit on `len`, `push` (no result), `pop`,
    `resize`, `clear` and `append` (its three source forms), plus a
    column-array source for `append` on ordinary arrays. Shrinks go to
    `ShrinkGrowShrink` with the variable scan off. `for &` is rejected and
    rendering is allowed.
  * Calls: no array-to-slice coercion for `A_COLS`, neither in overload
    tiers nor in `BindTypes`. Thread arguments, queues, thread-program
    globals and `extern fn` reject it.
  * BCE and the optimizer: their array-kind switches only.
* **Codegen** (~550–700):
  * `codegen_types.h`: the header typedef, and a per-element-type leaf table
    giving each column's C type, size and index, and each struct field's
    column range.
  * `codegen_values.h`: `VarLoc`/`DerefLoc` for the header, the `IndexLoc`
    branch returning an element path, `FieldLocAt` narrowing it, the gather
    in `LoadLoc`, a scatter helper, and column-mapped `GenElemwiseInto`.
  * `codegen_nodes.h`, `codegen_stmts.h`: the scatter in `Assign::CgStmt`;
    `ForLoop::CgStmt`'s by-value binding with hoisted, `restrict` columns;
    `BindLocal` taking N stack indices and placing the columns.
  * `codegen_builtins.h`: the members. `append` has three sources: an array
    view, a call result built on a scratch stack, and another column array.
    It also covers an ordinary array appending a column array.
  * `codegen_emit.h`, `codegen_render.h`, `codegen_frames.h`: globals,
    printing, and the plain-pointer parameter.
* **Tests** (~350 lines):
  * a runtime fixture exercising every member and path form on a local,
    through a reference, on a global and inside a generic function, with
    `// bce:` annotations on its loops;
  * a check that pushes interleaved with calls through a reference keep the
    columns aligned;
  * `test/errors_tc/` fixtures for each rejected use.
* **Documentation**: a row in the §3.3 table and a subsection after §5.3, the
  §12 member table, a tutorial paragraph, and an entry in implementation.md
  §6.1.

That is roughly 750–900 lines of compiler: about the size of `to_bytes` and
`from_bytes` (+779 in `src/`), and a third of the full design.

## Measurements

The hand-written six-array version of `bench/goose/particles_scalar.goose`
is the layout Goose offers today, and it is compared with the array-of-structs
original. N = 4,000,000, best of five, on a Ryzen 9 9950X3D. Checksums were
identical, and repeated sessions varied by up to 8%.

| ms | MSVC 19.51 /O2 | clang 20 -O2 | clang 20 -O2 -march=native |
|---|---:|---:|---:|
| array of structs (`particles_scalar.goose`) | 1039 | 956 | 1375 |
| six `f32[>..]` arrays, as generated today | 2015 | 1968 | 674 |
| the same C, column starts 688 bytes apart | 1101 | 1063 | 691 |

* **The 2x at the default targets is 4K aliasing.** Every array starts at
  offset 0 of a fresh stack, so `px[i]` and `vx[i]` share their low 12 address
  bits, and each load after a store predicts a forward that is not there.
  Moving the starts apart removes it.
* **Placed apart, the columns stay within 6–11% of the array of structs** at
  the default targets. The loop touches every field on every iteration and
  branches, which is the array-of-structs layout's best case, and at clang's
  default target it does not vectorize: turning vectorization off changes
  nothing.
* **With `-march=native` (AVX-512) the columns vectorize**: 691 ms, against
  1033 with vectorization off. The array of structs gets slower there (1375
  ms, against 917 with vectorization off), so the fair margin is about 1.3x.
* **The elementwise loop also needed `restrict` under MSVC.** A six-column
  lowering of `particles.goose`'s elementwise loop reads the global `G`
  inside the loop, and without `restrict` a C compiler must assume any float
  store may change it. That version took 3317 ms as is, 1623 staggered, 1295
  with `restrict` and 1005 with both, against 1182 for the array of structs.

The stagger belongs in any column layout the compiler controls. Whether
ordinary data stacks should start at staggered offsets too is a separate
question, since unrelated arrays that one loop reads and writes meet the same
aliasing today.

## Later, in rough order of value

| Extension | What it adds | Rough cost |
|---|---|---|
| `for &p in ps` as a place alias | `p` stands for `ps[i]` in paths and still has no address; kernels then read like their array-of-structs versions | ~100 lines |
| Column views | a real slice over one column (`ps.pos.x` as an `f32[:]`, in some spelling), for FFI and `std` kernels, held by variables only as §5.2's slices are, which brings back the variable scan at shrinks | ~150 |
| `==`, `copy`, whole assignment | per-column compares and copies | ~60 |
| Reference and `in pool` leaves | elements that hold links, checked as element fields of any array are | probably small |
| Struct tails | a frame object (C.2) whose tail is a column header | ~200 |
| Threads, queues, `to_bytes` | images in array-of-structs order, since images carry no layout | ~100 |
| Returns and by-value parameters | N destination stacks and named results placed in columns (§7.3); most of the full design's cost | several hundred |

## Open questions

1. **Spelling.** `T[||..]` reads as columns with a changing length.
   `T[>..||]` would keep the family's `>` for stack growth.
2. **`push`'s result.** Should `push` return the new index, the counterpart
   of the reference other arrays return?
3. **Split depth.** Split struct fields only, or fixed arrays too (a `f32[4]`
   leaf as four columns)?
4. **`append(f())`.** It builds the result on a scratch stack and then splits
   it into the columns. Is that acceptable against §4.3? Static-capacity
   limited arrays already take a call's array result that way
   (implementation.md §6.5). The alternative is for `append` to take stored
   sources only.
5. **The place alias.** Should `for &p in ps` be in v1? It is what makes a
   kernel read like its array-of-structs version.
