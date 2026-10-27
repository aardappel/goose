# `const for`: specialization-time expansion (v1)

Status: implemented. V1 supports the four sources below in statement lists,
constructor initializer lists, and `match` arm lists.

This design replaces a family of compiler-provided field visitors with one
expansion construct inside ordinary Goose functions. The compiler supplies a
small set of structural sequences and specializes a source fragment once per
entry. Libraries choose the resulting functions' names, signatures, callbacks,
and runtime behavior.

The motivating discussion is [issue #35](https://github.com/aardappel/goose/issues/35),
which proposes field visitation to prevent handwritten field lists from
silently falling behind their struct declarations. The direct builtin in
[PR #36](https://github.com/aardappel/goose/pull/36) is an alternative design.

## 1. Scope and cost model

V1 has exactly four expansion sources:

| Source | Bindings | Meaning |
|---|---|---|
| `fields<T>` | `<N, A>` | Each direct field's name and accessor, in declaration order |
| `variants<E>` | `<N, V>` | Each variant's name and the variant, in declaration order |
| `types<T1, T2, ...>` | `<U>` | An explicit sequence of types |
| `[c1, c2, ...]` | `<C>` | An explicit sequence of constants |

Expansion is supported in statement lists, constructor initializer lists, and
`match` arm lists. Initializer lists may contain named field initializers or
positional expressions, subject to the existing prohibition on mixing those
forms. Section 10 records additional sources and placements as future work.

There is no general compile-time execution block, interpreter for arbitrary
Goose code, user-defined expansion-source function, quotation API, or textual
macro substitution. `const` qualifies the expansion: the source and its
bindings are known during specialization. Ordinary code in the body still
executes at runtime.

An expansion emits ordinary Goose operations and constants. It introduces no
runtime iterator, heterogeneous container, type registry, per-object metadata,
or dynamic dispatch. A program that does not use the feature pays no runtime
cost for it. Used expansions may increase compilation time, generated code,
and retained constants such as printed field names. Existing allocation,
copying, and runtime-check costs of the generated operations still apply.

## 2. Syntax

```goose
const for <N, A> in fields<T> {
    F(N, &value.A);
}

const for <U> in types<i8, i16, i32, i64> {
    test_roundtrip<U>();
}

const for <C> in [1, 2, 4, 8] {
    var buffer: u8[C] = [0; C];
    test_buffer(buffer);
}

match shape {
    const for <N, V> in variants<Shape> {
        V v => print(N, " ", v),
    }
}
```

`const`, `for`, and `in` are existing keywords. The angle brackets are required:
they distinguish specialization bindings from ordinary loop variables and
reuse the visual convention of generic parameter lists. Binding names need
not be uppercase; capitalization has no semantic effect.

`fields`, `variants`, and `types` remain identifier tokens. They are contextual query names
recognized only in a `const for` source. They are not ordinary callable
functions, do not participate in overload resolution, and cannot be replaced
or shadowed there by a user declaration. A user function or variable of any of
these names is unaffected elsewhere. There are no parentheses after a query.

Sketch of the added grammar:

```text
expansion      := "const" "for" "<" bindings ">" "in" source "{" fragments "}"
bindings       := ident ("," ident)* ","?
source         := "fields" "<" type ">"
                | "variants" "<" type ">"
                | "types" "<" (type ("," type)* ","?)? ">"
                | "[" (const-expr ("," const-expr)* ","?)? "]"
```

Here the quoted `fields`, `variants`, and `types` are contextual identifiers,
not lexer keywords. `const-expr` is parsed using the expression grammar and
validated against section 3.3 during specialization. The enclosing list
determines the grammar of `fragments`; the compiler does not guess the
fragment category from the body after parsing it.

The binding count is exact: two for `fields` and `variants`, one for either
explicit list.
V1 has no third field-type binding, index binding, binding annotations, or
optional outputs. A binding may go unused. The source is resolved
before its own binders enter scope; enclosing expansion bindings remain
available to it.

## 3. Expansion sources

### 3.1 Fields

`fields<T>` requires a concrete struct type or variant type (`Shape.Circle`,
or a `variants` binding `V`) after aliases and generic type arguments have
been resolved. A variant type's fields are its payload's. Const qualification
does not change the field sequence. References, arrays, slices, and ADTs are
not implicitly unwrapped or traversed: `fields` of an ADT is an error, and its
variants are reached through `variants<E>` (section 3.4).

Fields are visited once each, in declaration order. Padding is omitted. An
empty struct produces zero entries. Nested structs, arrays, and references
remain single fields; there is no automatic recursive traversal.

For each entry:

* `N` is the field name as an immutable string constant, with the same
  semantics as an ordinary string literal. It may appear wherever that
  literal is a valid expression.
* `A` identifies the field declaration in its concrete owning struct
  instantiation. It is neither a string nor a runtime value.

The accessor has two substitution positions in v1:

```goose
value.A             // member access
T { A: expression } // named initializer label
```

The receiver or constructor must have the accessor's owning struct type,
allowing ordinary reference access and const qualifications. For a variant
type, that is a value of the variant type, such as a payload bound by a
`match` arm, or that variant's constructor. A value of the ADT is the wrong
owner, since its variant is not known until it is matched. A different
nominal type, or a different generic instantiation, does not match merely
because it has a field with the same name. Structural matching by name would
be a separate feature.

Access through `A` has exactly the rules of accessing the actual field:
copying and reference binding, writability, lifetime roots, construction
restrictions, and representation adaptations are unchanged. In particular,
`&value.A` may be rejected where a direct reference to that field would be
illegal. Iteration never skips a field because the generated operation fails.

`A` cannot be used as a standalone value, passed as an ordinary function
argument, stored, used as a type, or turned into an offset. V1 does not expose
its declared type, default expression, attributes, or layout metadata. `N`
does not become an accessor simply because it contains a field's name.

### 3.2 Type lists

`types<T1, T2, ...>` uses the normal type grammar for every entry, including
qualified names, aliases, generic instantiations, references, and array
types. Entries resolve in their lexical environment and may mention enclosing
generic or expansion bindings. Each entry must resolve to a concrete type
when expanded. Its eventual use must satisfy the ordinary rules for that
type and position.

The sole binder behaves as a type parameter within each fragment instance:

```goose
const for <U> in types<i32, Pair<u8>, const u8[:]> {
    check_type<U>();
}
```

Types retain their identity and qualifiers; they do not become runtime
values. Repeated entries produce repeated instances. `types<>` is empty.

A separate syntax avoids parsing an entry as both a possible type and a
possible value expression. In particular, `[i32, u8[:]]` is not a type list.

### 3.3 Constant lists

The square-bracket form is an expansion source, not a runtime array. Each
entry is validated independently, and entries need not have the same type.
There is no common-element-type inference, array allocation, or runtime
evaluation of the source. Repeated entries are preserved; `[]` is empty.

The initial version accepts these constant expressions:

* Integer, character, floating-point, boolean, and string literals; a signed
  numeric literal may use unary `-` subject to the normal representability
  rules.
* Named global constants that resolve to such values under the language's
  existing constant-use rules. A local merely declared `const` does not
  acquire compile-time evaluation through this feature.
* Integer constant expressions already supported for array sizes and fill
  counts, with their existing type, overflow, and validity rules.
* Constant bindings of enclosing `const for` expansions, including their use
  as operands in those supported integer expressions.

Arbitrary function calls, runtime variables, aggregate construction, references,
function values, and general floating-point or string computation are outside
this subset. A nonconstant entry is an error; it does not fall back to runtime
iteration. Only the forms listed here are accepted, even if an optimizer could
fold other forms.

Materializing a constant binding preserves the distinction between an
adaptable literal and a value with a declared type. It must not erase a named
constant's type by replacing it with an untyped numeric token. String bindings
obey the normal read-only string-literal rules. The binding itself is not an
addressable loop variable; initializing an ordinary local from it follows
ordinary value semantics.

```goose
const for <Label> in ["small", "medium", "large"] {
    register_label(Label); // Three ordinary runtime calls.
}
```

There is no implicit integer range source, array-fill source, or lookup of a
runtime array variable in v1.

### 3.4 Variants

`variants<E>` requires a concrete ADT type after aliases and generic type
arguments have been resolved; an instantiated generic enum yields the
variants of that instantiation. References are not unwrapped, as for
`fields<T>`. Every variant is visited once, in declaration order, whether or
not it has a payload. An enum without variants produces zero entries.

For each entry:

* `N` is the variant's name as an immutable string constant, as for
  `fields<T>`. It is for display; neither it nor the declaration order is a
  stable persistence identifier, and no index binding is provided.
* `V` identifies the variant of that concrete ADT. It can be used in the
  positions where the variant's own name is written today:

```goose
V                   // a type: the variant type, also fields<V>
V { ... }           // a constructor; V {} for a variant without payload
V, V c, V &c        // a match arm's pattern, with an optional binder
V                   // a value, for a variant without payload only
```

A `V` whose variant has a payload is not a value; it is constructed with
`V { ... }`. `V` cannot be stored, passed as an ordinary function argument,
or converted to an integer. A pattern `V` belongs to its ADT: in a `match` over
another ADT it is an error, even when that ADT has a variant of the same name.

A program that persists variants as numbers chooses the numbers itself, in
one ordinary `match` from variant to number; the reverse conversion can then
be a generic function over `variants<E>`:

```goose
fn number(k: Kind) -> i64 { match k { Town => 10, Catacombs => 11, Burrows => 12 } }

fn from_number<E>(n: i64, fallback: E) -> E, bool {
    const for <N, V> in variants<E> {
        if number(V) == n { return V, true; }
    }
    return fallback, false;
}
```

## 4. Bindings and hygiene

Substitution follows lexical binding identity, not matching identifier text.
The compiler must distinguish a reference to an expansion binder from an
unrelated identifier with the same name, including across cloned bodies
and nested functions. Ordinary shadowing rules apply; generated local
declarations have distinct identities in each instance.

For member and initializer-label positions, an in-scope accessor binding
takes precedence over interpreting that name as a literal field name.
This choice must not depend on whether the receiver happens to declare a
field with that name. For example, adding a real field named `A` must not
change what `value.A` means inside an expansion binding `A`. To refer to that
literal field within such code, choose another accessor binder name.

Only accessor bindings cause member or initializer-label substitution. There
is no general substitution into declaration names, namespace names, arbitrary
tokens, or strings. A binding used in an unsupported category is diagnosed
rather than stringified or interpreted as a runtime object.

The usual definition-site lookup of generic functions remains in force.
Expansion does not import names from the function's caller. No `$` token,
bracketed member splice, or public compiler-AST API is introduced.

## 5. Fragment contexts

### 5.1 Statement lists

In a function or block body, the expansion body is an ordinary statement
block, instantiated once per source entry. Each instance has its own scope,
so local declarations in different instances do not collide. The expansion
has no result value. Its block must be valid as a statement block after the
normal handling of trailing expressions.

Instances execute in source order if control reaches them. Ordinary runtime
`if`, `for`, and `while` constructs inside an instance retain their runtime
meaning. They do not select which fragments are generated or suppress normal
typechecking of a branch.

`return`, `return ... from`, and returns in static function-value callbacks
retain their existing lexical semantics. A runtime return can prevent later
instances from executing; it does not change the structural source sequence.
The expansion is not a runtime loop or a `break`/`continue` target. V1 rejects
`break` or `continue` that would cross the expansion boundary; loops and
breakable blocks wholly within a fragment work normally.

Nested `const for` expansions are permitted. Inner sources may use bindings
of outer expansions, and the inner body is expanded in that environment.

### 5.2 Constructor initializer lists

Inside a struct or variant constructor, an expansion body contains initializer
entries, not statements. Each instance contributes its entries to the
enclosing initializer list. Nested expansions in that list use the same
entry grammar.

```goose
fn transform_fields<T, F>(value: T&) -> T {
    T {
        const for <N, A> in fields<T> {
            A: F(N, value.A),
        }
    }
}
```

For `struct Point { x: i64, y: i64 }`, the resulting constructor is equivalent
to:

```goose
Point {
    x: F("x", value.x),
    y: F("y", value.y),
}
```

Callbacks execute at runtime, and each result goes to its actual field's
construction destination. No intermediate tuple, array, or temporary struct
is introduced. A callback that cannot produce a legal initializer for one
field gives an error at that field's expansion.

An initializer expression may itself be an ordinary block expression when it
needs statements or locals. For non-fixed values, that block must obey the
existing copying and construction rules; expansion adds no implicit copies.

Explicit entries and expansions may appear together. Entry and list
separators are commas; a final comma in either list is optional. An expansion
occupies one syntactic position in its enclosing list, so a following explicit
entry or expansion is separated from its closing brace by a comma. Flattening
operates on AST entries, not comma-bearing text, so an expansion with zero
entries needs no punctuation repair. The constructor's optional trailing
`..` remains outside the expansions and applies to the complete resulting
initializer list.

After flattening, the ordinary constructor checks apply:

* Named and positional entries must not be mixed.
* Unknown, duplicate, excessive, and missing initializers are diagnosed.
* Explicit entries do not override generated entries or vice versa.
* Declared defaults, optional-field omission, and trailing `..` keep their
  normal meanings.
* Construction occurs in declaration order, with the existing checks on
  reordering effectful initializers. Expansion does not introduce temporaries
  to accommodate a different runtime order.
* `self`, relative references, lifetime roots, const fields, and restrictions
  on accessing values under construction are checked normally.

Defaults execute when construction reaches them, not while the expansion is
generated. Expansion must precede missing-field/default processing and the
checking of individual initializer destinations.

### 5.3 Match arm lists

Inside a `match`, an expansion body contains arms. Each instance contributes
its arms to the enclosing arm list, in source order. The arm grammar,
separators, and nesting follow section 5.2: arms are separated by commas, a
final comma is optional, and an expansion occupies one position in the arm
list.

```goose
fn show<T>(value: T&) {
    match value {
        const for <N, V> in variants<T> {
            V &v => {
                print(N);
                const for <F, A> in fields<V> { print(" ", F, ": ", v.A); }
            },
        }
    }
}
```

After flattening, the ordinary `match` checks apply (specification section
8.1, "`match`"), exactly as for a written `match`:

* Over an ADT, every variant is matched once: a missing or repeated variant
  is an error. Because `variants<E>` generates an arm for every variant, any
  variant arm written beside it is a repeat; only a final `_` can accompany
  it.
* Over an integer, a constant-list expansion may sit beside written arms and
  the required `_`. A pattern that earlier arms already match entirely is an
  error, where "earlier" means earlier in the flattened order.
* `_` must be the last arm, so an expansion after it is an error.
* Copy and reference binders are written by the author and checked for each
  generated arm: `V &c` is an error for a fixed-mode or resizable ADT, as
  `Circle &c` is. Lifetime rules apply to what each binder roots.
* The arms' result types unify as in a written `match`.
* A `match` with no arms after flattening (an expansion over an enum without
  variants) is an error, as a written `match` without arms is.

A generated arm may bind a variant without payload (`V v` for every
variant), so generic code needs no `const if` to write one arm for every
variant. The binder is an empty value of that variant type. A written arm
binding such a variant remains an error.

## 6. Library use and receiver semantics

A field visitor can be an ordinary user-named function:

```goose
fn visit_fields<T, F>(value: T&) {
    const for <N, A> in fields<T> {
        F(N, &value.A);
    }
}

struct Point { x: i64, y: i64 }

fn main() {
    var point = Point { 2, 3 };
    visit_fields(point) { name, field => print(name, " = ", field) }
}
```

The receiver is evaluated and bound by the ordinary function call. References
forwarded through variables and generic helpers retain normal semantics; no
special mode depends on whether the caller literally wrote an `&` token.
The library author chooses reference or value expressions for callbacks.

Accessing several objects needs no additional expansion-source form:

```goose
fn visit_pairs<T, F>(left: T&, right: T&) {
    const for <N, A> in fields<T> {
        F(N, &left.A, &right.A);
    }
}
```

There is no blanket once-only evaluation guarantee for arbitrary expressions
written inside an expansion. A call placed there is repeated, just as it
would be in handwritten generated code. Ordinary function parameters and
locals provide explicit control over evaluation.

Constructor expansion is useful for transformations and generated building
code, but v1 does not claim to supply a universal deserializer. Field-type,
default, and schema-policy queries remain future work. In particular,
visiting an existing object's field references cannot overwrite variable-size
fields or `varint` fields in violation of their construction-only rules.

## 7. Compiler integration

The compiler already checks generic functions at concrete specializations
([specification sections 7.7 and 10](../goose_spec.md)). Expansion belongs at
that boundary rather than in a source-text preprocessing pass.

1. Parse `const for` into an expansion node with binders, a source description,
   and fragments of the enclosing syntactic category. Distinguish `const for`
   from ordinary `const` declarations. The contextual source grammar also
   avoids treating the angle brackets in `fields<T>` as comparison operators.
2. Preserve scoped references to expansion parameters through name resolution,
   as generic parameter uses are preserved today. Type-list binders, constants
   used in sizes, accessor uses in member positions, and initializer labels
   all need to retain their binding identity until specialization.
3. Resolve the source in the current specialization. Its types and constants
   must be available without first checking the fragments it will generate.
4. Clone the appropriate fragments per entry with fresh local identities and
   the corresponding expansion bindings. Substitute types, materialize
   constants, and resolve field selectors; recursively expand nested forms.
5. Check the generated constructs with the ordinary type, effect, root,
   writability, flow, and construction machinery. A constructor's complete
   entry list must exist before its normal initializer checks begin, and a
   `match`'s complete arm list before its exhaustiveness and binder checks.
6. Pass ordinary checked constructs to optimization and code generation.

The parser need not resolve a member named `A` or know the members of `T`. It
does need to preserve semantic bindings and distinguish fragment categories
through parsing and resolution. Replacing identifier text alone is not enough.
The existing `FieldInit` name representation will need to distinguish literal
labels from bound accessors.

The compiler need not prove a generic expansion valid for every possible type.
It checks the instances the program reaches, as for other generic code. Empty
sources create no fragment instances; their bodies must still parse, but have
no per-entry semantic checks to perform.

Structural expansion may be cached where its dependencies permit. Checked
results must not be reused across incompatible reference roots, writability
provenances, or other existing specialization inputs. The implementation
bounds expansion growth and reports the expansion chain when the limit is
exceeded. The current limit is 100,000 AST nodes cloned by expansions per
compilation, including nested fragments and nested function bodies.

Ordinary field access is the semantic contract, not a promise that repeated
accesses are already optimal. For variable-size records, check generated code
for repeated prefix walks and permit existing or future optimizations to
reuse a forward cursor when dependencies and mutation rules allow it. Do not
expose fixed byte offsets as the fundamental accessor abstraction.

## 8. Diagnostics and tooling

An expansion error should identify the original source location, the function
specialization, and the relevant field, variant, type, or constant-list entry. An
unsupported operation should point to the operation as well as the entry
that made it invalid. A field must never be silently omitted on failure.

Useful diagnostics include:

* incorrect binding arity;
* a `fields` source that is neither a struct nor a variant type;
* a `variants` source that is not an ADT;
* an unsupported/nonconstant list entry;
* an accessor used on the wrong owning type (including an ADT value for a
  variant's accessor);
* a binding used in the wrong syntactic category;
* a variant with a payload used as a value;
* a pattern variant of another ADT;
* duplicate or missing constructor fields or `match` variants after
  expansion;
* an illegal reference binder;
* an expansion after `_`;
* a `match` left without arms.

A variant must never be silently omitted either.

The source dump should preserve and round-trip unexpanded syntax. An expanded
view would be useful for debugging, but adding a new display mode is not a
prerequisite for the semantics. Source-to-expansion provenance must survive
cloning even if no expanded view is implemented initially.

## 9. Validation targets for an implementation

Regression fixtures are `test/typing/const_for_*.goose` and the corresponding
`test/errors/const_for_*.goose` and `test/errors_tc/const_for_*.goose` rejection
tests. The test runner checks source round trips, expected output at `-O0`
and `-O2` through generated C and TinyCC, and expansion diagnostics. Coverage
includes:

* Statement visitation of heterogeneous, empty, padded, const-qualified, and
  generic structs; declaration order and no implicit recursion.
* Accessor binding despite an actual field with the binder's name;
  lexical shadowing, nested expansions, and fresh local identities.
* Normal reference forwarding, rejected writes and references, and preserved
  lifetime roots. Coverage should include variable-size fields, `varint`,
  relative references, and resizable tails.
* Type lists with qualified and compound types, repeated entries, and empty
  lists. Separate instantiation and no runtime type representation.
* Constant lists with heterogeneous entries, typed named constants, supported
  integer expressions, outer bindings, empty lists, and rejected runtime
  expressions. Constant bindings used in array sizes and fill counts.
* Runtime conditions remain runtime conditions; callbacks and nonlocal returns
  retain their normal behavior; boundary-crossing `break`/`continue` errors.
* Named and positional constructor expansion, nested expansion, zero-entry
  expansion, explicit entries and commas, duplicate/missing fields, and `..`.
  Preserve default evaluation, declaration-order effects, and construction
  directly in each destination, including non-fixed fields.
* Variant tests should cover ADTs with and without payloads, an enum without
  variants, and declaration order. Exercise `variants` in statement lists,
  constructors, and arm lists, as well as `fields<V>` reads and variant
  construction. Test by-value and by-reference arms in fixed and variable mode,
  including lifetime errors. Also test written arms and `_` beside an expansion,
  missing and repeated variants, and a pattern variant from another ADT.
* Parse/dump round trips and errors that identify the failing expansion entry.
* Generated-code inspection for absent reflection infrastructure, ordinary
  callback dispatch, unnecessary copies, and repeated dynamic offset walks.

## 10. Future work, explicitly outside v1

* **Declared field types.** A possible third binding is `<N, A, U>`. V1
  accepts only `<N, A>`. A later design must distinguish storage type from
  read-value type: a `varint` field is declared `varint` but reads as `i64`,
  and not every storage type is legal as an ordinary function result.
* **Constant integer ranges.** Reuse half-open `lo..hi` syntax with constant
  bounds if adopted. V1 supports explicit constant lists only.
* **`const if` or filtering.** Define a set of compile-time predicates for
  selecting fragments. For example, a predicate could expand only some variants
  into arms alongside written arms. This would be distinct from an ordinary
  runtime `if` and would not allow arbitrary compile-time calls.
* **Additional fragment positions.** Argument lists and array-element lists,
  followed by other justified grammar positions. The shared expansion model
  permits these additions, but each needs explicit parsing, ordering, and
  type-inference rules. Top-level declarations and changes to overload sets
  require a separate design.
* **Defaults and annotations.** Queries supporting missing-field construction,
  serialized names, aliases, omissions, and migration policies. Defaults may
  contain runtime work and must not be executed during expansion. Names alone
  do not constitute stable persistence identifiers.
* **Other metadata.** Field counts, display type names, indices, and carefully
  scoped type properties. Display names should not automatically become
  stable type IDs or wire-format contracts.
* **Richer source composition.** User-defined sequence computations, sorting,
  grouping, general macro helpers, and general constant execution. These are
  not implied by the v1 syntax and would need an explicit execution model.

The library can build richer traversal functions over the v1 primitives
without adding another compiler-provided visitor pattern. Future reflection
information remains compile-time-only unless a program explicitly uses it to
generate ordinary runtime data or operations.
