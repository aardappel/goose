# `const for` and `const if`: specialization-time expansion (v1)

Status: implemented. V1 supports the four sources below in statement lists,
constructor initializer lists, and `match` arm lists. `const if`, which
selects fragments in those same lists on compile-time predicates, is
section 11, and `const assert`, which requires one, section 11.7.

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
* **`const if` or filtering.** Designed in section 11: a set of compile-time
  predicates for selecting fragments, distinct from an ordinary runtime `if`
  and without arbitrary compile-time calls. Its `has_default<A>` query is
  the narrowest form of the defaults item below.
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

## 11. `const if`: specialization-time selection

Status: implemented.

`const if` chooses one of its fragment lists while a function is
specialized, and generates nothing of the others. It is the selection
counterpart of `const for`'s repetition, with the same sensibilities: a
small, closed source language, fragments of the enclosing syntactic
category, ordinary runtime code inside, and no fallback to runtime behavior
when something is not constant.

The motivating request is [issue #200](https://github.com/aardappel/goose/issues/200).
Honk, a web framework, validates JSON request bodies against plain Goose
structs. A `fields<T>` expansion can read every field's value in `T { .. }`,
but cannot tell `discount: f64 = 0.0` from `price: f64`, so it cannot tell a
required field from an optional one. The answer is a compile-time predicate
on the field. A predicate alone is not enough: a runtime `if` on it checks
both branches for every field (section 5.1), so the branch a field does not
take must still typecheck for that field. `const if` is where such
predicates live. They are valid only in its condition, as `fields<T>` is
valid only as a `const for` source, which keeps compile-time code visibly
apart from runtime code.

### 11.1 Syntax

```goose
const for <N, A> in fields<T> {
    const if !has_default<A> {
        if !seen[i] { add_error(errors, N, "Field required"); }
    }
    i++;
}

const for <U> in types<i8, i16, i32, i64, f32, f64> {
    const if same<U, f32> || same<U, f64> { check_float<U>(); }
    else { check_integer<U>(); }
}

const for <C> in [1, 2, 4, 8] {
    const if C >= 4 { check_wide<C>(); } else const if C == 2 { check_pair(); }
}

const if TRACE { print("checked ", i, " fields"); }
```

`const`, `if` and `else` are existing keywords. A `const if` is followed by
a condition, a brace-delimited fragment list, and optionally `else` with
either another `const if` or a fragment list. The condition is in scrutinee
position, as an `if`'s is: a `{` ends it and begins the fragments.

```text
constif    := "const" "if" condition "{" fragments "}" constelse?
constelse  := "else" ("const" "if" condition "{" fragments "}" constelse?
                     | "{" fragments "}")
condition  := expr                   // the expression grammar, validated by 11.2
query      := "has_default" "<" ident ">"
            | "same" "<" type "," type ">"
```

A chain continues with `else const if`, as its first branch was written. A
plain `else if` directly after a `const if` is an error: whether it is a
compile-time or a runtime `if` would be invisible at the keyword, and a
runtime `if` in the else branch is written out as `else { if ... }`.

The enclosing list determines the grammar of `fragments`, as for `const for`
(section 2). A `const if` occupies one position of the enclosing statement,
initializer or arm list, and every branch holds fragments of that category.

Queries are contextual names, as `fields`, `variants` and `types` are. Inside
a `const if` condition, `has_default` or `same` followed by `<` begins a
query, and the angle brackets hold a binding name or types, read by the type
grammar. Elsewhere the names are ordinary identifiers, so a user function or
variable called `same` is unaffected, and so is a comparison `same < x`
outside a condition. There are no parentheses after a query: a query is not a
call, and its operand is not a value. `has_default(A)` would suggest an
argument, which an accessor cannot be (section 3.1). The angle brackets say
what `fields<T>` says: what stands inside is a type or a binding known
during specialization.

### 11.2 Conditions

A condition is a constant of the specialization. It is parsed by the
ordinary expression grammar, so precedence and parentheses are the ordinary
ones (specification appendix D), and validated during specialization against
the following list. Anything else is an error; a nonconstant condition does
not become a runtime branch.

A condition is one of:

* `true`, `false`, or a named global `bool` constant under section 3.3's
  rules for named constants;
* a query (11.3);
* `!c`, `c && d`, `c || d` and `(c)` for conditions `c` and `d`. Both
  operands of `&&` and `||` are validated, even when the left one decides
  the result;
* a comparison `a op b`, with `op` one of `== != < <= > >=`, between two
  constant operands of one kind.

The operands are exactly the constant-list entries of section 3.3: integer,
character, floating-point, boolean and string literals, named global
constants, the integer constant expressions array sizes accept, and the
constant bindings of enclosing `const for` expansions. The comparison rules
per kind:

| Kind | Operators | Typing |
|---|---|---|
| integers | all six | the operands unify as the integer constant expressions of section 3.3 unify them for `+` (specification section 6.1): equal types stay, a literal adapts to the other's type and must fit it; the comparison is signed or unsigned per that type |
| floats | all six | two float constants; an integer constant beside one converts as specification section 6.3 converts it |
| booleans | `==`, `!=` | |
| strings | `==`, `!=` | by content |

Operands of different kinds do not compare: `N == 0` for a name binding `N`
is an error, not `false`. A type is never a comparison operand; `same` is the
only way to compare types (11.3). A literal parameter (specification section
7.7) has no known value in the body it reaches and is not a constant here.
Neither is a local, whatever it is declared with, a field read such as
`v.A`, a call, or an aggregate value.

This is deliberately the constant language the compiler already evaluates
for array sizes and `const for` sources, plus comparisons and the boolean
connectives on literal results. No evaluator for further operators, no
constant-ness of locals or calls, and no execution model are implied. A
program that wants a computed constant writes a named global constant, which
keeps the two languages visibly apart in the source.

A use of a named constant in a condition relies on the constant keeping its
initializer's value, as an array size does (specification section 3.1), and
is recorded the same way.

### 11.3 Queries

**`has_default<A>`** requires an accessor bound by `fields<T>` (a struct's
field, or a variant payload's through `fields<V>`) to be in scope under that
name. It is `true` exactly when the field's declaration has a written
`= ...`. The expression is not evaluated, inspected or compared: a default
equal to the type's zero (`discount: f64 = 0.0`) counts, and a field whose
type happens to have a default of its own (a nested struct whose fields all
declare defaults, or an optional) does not. The answer is the same for every
instantiation of a generic struct, since the declaration is written once. The
default value is still obtained as today, through `T { .. }` at runtime;
section 10's rule that defaults are not executed during expansion stands.

**`same<T1, T2>`** takes two types of the ordinary type grammar, resolved as
`types<...>` entries are (section 3.2): in their lexical environment, with
enclosing generic parameters and expansion bindings available, each required
to be concrete when the condition is evaluated. It is `true` when the two are
identical after resolution, by the equality the typechecker uses for
concrete types: aliases resolve, generic instantiations compare by their
arguments, array sizes by their value, `const` qualification counts, and a
reference type differs from its pointee. A variants binding `V` is a type, so
`same<V, Shape.Dot>` selects a variant, as `N == "Dot"` does by name.

**`holds_own<T>`** takes one type, resolved as `same`'s are and required to
be concrete. It is `true` when a value of the type carries an obligation or
has a part that does, in the sense of
[ownership.md](ownership.md#10-implementation-status): an `own` type, a
struct with an own field, an array of either. It is what a library needs to
keep an operation that copies an element, or reuses its storage, away from
elements that own something, and was added for the pools' plain operations.

Queries are boolean constants, so they compose with the connectives and may
even be compared with `==`, though there is no reason to.

Section 11.6 records the queries considered and left out of v1.

### 11.4 Semantics

A `const if` is evaluated when ordinary checking reaches the list it stands
in, at the same point a `const for` there is expanded. The first branch
whose condition holds supplies its fragments; with no `else` and no holding
condition, nothing is supplied. The chosen fragments replace the `const if`
in the enclosing list, as a `const for`'s instances do, and then receive the
ordinary checks of that list.

A branch that is not chosen is parsed and name-bound, so it must be
syntactically valid and its own `const for` and `const if` bindings must
resolve, but it is never cloned, expanded or checked. A call to a function
that does not exist, or a write to a `varint` field, in a branch the
specialization does not take is not an error. This is the difference from a
runtime `if` on a constant, and the point of the construct. It is the rule
`const for` already applies to a body over an empty source (section 7). A
branch no specialization ever takes is therefore never checked at all.

A `const if` need not stand inside a `const for`. Its condition may use
generic parameters (`same<T, i64>`), named constants (`const if TRACE`), or
nothing specialization-dependent at all; it is evaluated the same way in
every case.

**Statement lists.** The chosen branch is one nested block with its own
scope. It is not an expansion boundary: `break`, `continue` and `return` in
it mean what they mean in a runtime `if`'s branch, and a `const if` inside a
`const for` body still cannot `break` out of the expansion (section 5.1).
Flow after the `const if` is the flow of the generated code, since the
generated code is the program: a `const if C { return; }` makes what follows
unreachable when `C` holds, and both arms of
`const if C { x = 1; } else { x = 2; }` definitely assign `x`. A nested
function declared in a branch exists when the branch is chosen.

```goose
fn numbers_distinct<E>() -> bool {
    const for <N1, V1> in variants<E> {
        const for <N2, V2> in variants<E> {
            const if N1 != N2 {
                if number(V1) == number(V2) { return false; }
            }
        }
    }
    true
}
```

Here the diagonal is never generated, where a runtime `if N1 != N2` would
generate and then optimize away each comparison.

**Constructor initializer lists.** Each branch holds initializer entries;
the chosen branch's entries join the enclosing list in place, and flattening
precedes the ordinary constructor checks of section 5.2. A `const if` that
supplies nothing leaves its fields to explicit entries, declared defaults or
a trailing `..`:

```goose
// A copy of src with every field that declares a default reset to it.
fn reset_defaults<T>(src: T&) -> T {
    T { const for <N, A> in fields<T> { const if !has_default<A> { A: src.A, } }, .. }
}
```

**Match arm lists.** Each branch holds arms; the chosen branch's arms join
the enclosing list in place, before the checks of section 5.3. This is the
filtering that section 10 anticipated: a `const for` over `variants<E>` may
leave some variants to written arms, which the exhaustiveness check then
demands.

```goose
fn payload_area(s: Shape) -> i64 {
    match s {
        const for <N, V> in variants<Shape> {
            const if !same<V, Shape.Dot> { V v => area(v), }
        },
        Dot => -1,
    }
}
```

The shape walk that decides whether a block's tail is a statement
(`IsValuelessTail` in `typecheck_expansion.h`) does not evaluate conditions. It considers every
branch of a `const if`, as it does for a runtime `if`: a `match` whose arms
come from a `const if` is a statement when any branch's arms are.

**Bindings and hygiene.** The condition's names bind lexically, as section 4
binds everything else. `A` in `has_default<A>` is the accessor binding of
that name in scope; a local declared with that name hides it, and the query
is then an error rather than a lookup of a field called `A`. A type in `same`
resolves as a type in a `types<...>` source does.

### 11.5 Compiler integration

1. Parse `const if` into a selection node with a list of branches, each a
   condition (null for the final `else`) and fragments of the enclosing
   category, next to `ParseConstFor` in the three places `const for` is
   recognized: statement, initializer and arm lists. The condition is parsed
   by the expression parser in scrutinee mode, under a flag that makes
   `has_default<` and `same<` begin query nodes: a query kind, a binding
   name, and types read by the type grammar, with the usual `>>` splitting.
   A query node is a primary expression, so `&&`, `||` and comparisons
   continue around it normally. After `else`, an `if` is the diagnosed
   `else if`.
2. Bind the names in conditions as section 7 step 2 binds expansion
   parameters. A query's binding name resolves through the same scope
   stack, and `has_default` requires it to resolve to an accessor. Query
   types go through the binder's type walk, so type bindings in them survive
   cloning.
3. Clone conditions and fragments with the branch structure intact.
   Constant bindings in a condition substitute to their literals as they do
   anywhere else; a query copies its accessor's owner and field name from
   the expansion argument, as a member access does today; query types clone
   through `CloneExpansionType`.
4. Evaluate the condition where the enclosing list is expanded (the
   statement loop, `ExpandInitializers`, `ExpandArms`). Literal results come
   from `ExpansionConstant`, which already yields typed integer, float,
   boolean and string literals and records reliance on named constants;
   comparisons fold on those; `has_default` reads the field's declaration in
   the owner's instantiation; `same` substitutes both types, rejects one
   that is not concrete, and compares them with the checker's type equality.
5. Splice the chosen branch's fragments in place of the node: the statement
   block as a nested block, the entries or arms into the enclosing list.
   They need no cloning of their own, since the body they stand in is
   already the specialization's clone, or the enclosing entry's. Recurse for
   nested expansions and selections.
6. Round-trip the unexpanded syntax in the source dump; give the query
   brackets the angle-bracket token role the `const for` header uses; add
   the construct to the tree-sitter and VS Code grammars beside
   `const_for_statement`; extend the specification's section 6.5 and
   appendix D.

The chosen fragments need no expansion trace line of their own: their
locations are their own, inside any enclosing `const for` trace.

Useful diagnostics:

* a condition that is not constant, naming the offending subexpression;
* a query outside a `const if` condition;
* `has_default<X>` where `X` is not a `fields` accessor in scope (a type or
  constant binding, a hidden name, an unknown name);
* a `same` type that is not concrete or not valid where written;
* operands of different kinds, an ordering on booleans or strings, or a type
  written as a comparison operand;
* a literal parameter, local, call or field read in a condition;
* `else if` after `const if`;
* and the flattened-list diagnostics of sections 5.2 and 5.3 for whatever
  the chosen fragments produce.

Regression fixtures follow section 9's layout as `test/typing/const_if_*.goose`,
`test/errors/const_if_*.goose` and `test/errors_tc/const_if_*.goose`:

* `has_default` over a struct, a variant payload, and a generic struct at
  two instantiations; a default equal to zero; a nested struct whose fields
  all default; an optional field.
* An unchosen branch left unchecked: an unknown function and a `varint`
  write in it; `const if false` in a non-generic function.
* Comparisons of each kind, literal beside typed named constant, integer
  beside float, strings by content, and precedence with the connectives.
* `same` over `types<...>` bindings, variants, generic parameters,
  qualifiers, references and generic instantiations.
* Chains, `else`, and a false condition without `else` that supplies
  nothing.
* Statement context: `break` and `continue` through a `const if` inside a
  runtime loop, `return`, definite assignment through both arms, branch
  scoping, nesting with `const for` both ways, a nested function in a branch.
* Initializer context: `reset_defaults` with `..`, an empty contribution,
  and mixed or duplicate entries detected after flattening.
* Arm context: filtered variants with written arms for the rest, a missing
  variant when none is written, arms beside `_`, and a constant list's arms
  under a `const if`.
* Every diagnostic above, and the query names used as ordinary identifiers
  elsewhere.
* Parse and dump round trips.

### 11.6 Considered for v1 and left out

* **The field's type.** A predicate `has_type<A, T>` would answer whether
  the field's declared type is `T`. It was dropped to avoid deciding, in a
  throwaway form, the storage-versus-read-type question section 10 raises
  for a third binding `<N, A, U>`: a `varint` field is declared `varint`
  and reads as `i64`. Once `U` exists, `same<U, i64>` covers fields, and a
  v1 `has_type` that had picked one answer could disagree with it. Until
  then, per-type behavior of a field's value is written as overloads, as
  Honk writes it today.
* **Type kind predicates.** `is_struct<T>`, `is_adt<T>`, `is_array<T>`,
  `is_integer<T>` and the like. Overloading already dispatches on kind at
  runtime, and without a field-type binding a kind predicate cannot drive
  `fields<U>` recursion, which is where it would earn its place. A concrete
  need should fix the set.
* **A value form.** `let x = const if C { a } else { b };` is cheap, since
  the chosen block is already an expression and no branch joining is needed,
  but it needs an expression-position parse, the tail rule above and a
  decision on whether an unchosen branch's shape counts. V1 follows
  `const for`: fragments only, no result value.
* **Counts, indices and positions.** `const for` gives no index binding by
  design; a runtime counter does the job, as Honk's `enum_message` shows.
* **`has_field<T, "name">`.** A `const for` over `fields<T>` with
  `const if N == "name"` already reaches the field; a pure existence check
  can set a runtime flag the same way.
* **The default's value.** Defaults may contain runtime work and are not
  executed during expansion (section 10); `T { .. }` is the way to the value.
* **Top-level selection.** Conditional declarations need the separate design
  section 10 already requires for top-level `const for`.

### 11.7 `const assert`

```goose
fn free<T>(pool: slot_pool<T>&, i: i64) {
    const assert !holds_own<T>, "slot_pool.free: an element that holds own values is given up with release";
    ...
}
```

`const assert c, "message";` is a statement. Its condition is one section
11.2 describes, queries included, evaluated in each specialization of the
function it is written in. Where it is true the statement is nothing; where
it is false the compile fails with the message, which is a string literal.
The error is an ordinary one raised inside a specialization, so it carries
the instantiation trace, and one in the standard library is reported at the
user's call, as every error there is.

It is the sibling section 11.6 anticipated, added when the pools needed it:
a library function that must not be instantiated for some types had only
ways to fail obscurely, deep in its body. A `const if` whose branch cannot
compile says the same less clearly and with the wrong message.

* It is a statement of a function body only, the one list a requirement on a
  specialization belongs in.
* `assert` is contextual after `const`. `const assert = 1;` and
  `const assert: i64 = 1;` still declare a constant of that name.
* The message is not optional: the condition language is too small for a
  failed condition to explain itself.
* A parsed-only node: the checker replaces it by an empty block, as it
  replaces a `const if` by the branch chosen, so no later pass sees one.
