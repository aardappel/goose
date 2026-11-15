# Minimal namespaces

<details>
<summary>Table of contents</summary>

- [Rules](#rules)
- [Implementation](#implementation)
- [Rust and Zig comparison](#rust-and-zig-comparison)

</details>

Files can declare a namespace and refer to declarations by qualified names.
This extends the existing import loader and whole-program compiler.

```goose
// image/filters.goose
namespace image;
struct Pixel { r: u8, g: u8, b: u8 }
fn brightness(p: Pixel) -> i64 { p.r + p.g + p.b }

// main.goose (a separate file)
import image.filters;
fn main() {
    let p = image::Pixel { 10, 20, 30 };
    print(image::brightness(p));
}
```

## Rules

* `namespace name;` occurs at most once, before declarations (imports may sit
  on either side of it). Files without it keep the global namespace. `name`
  is one identifier. Nested namespaces and namespace-opening directives
  are not included.
* `namespace g = image;` declares an alias, before the file's declarations
  like the directive above, and any number of them. In that file alone,
  every qualified reference `g::x` reads as `image::x`: in expressions,
  types, variants, pools, match-pattern constants and `return ... from`.
  The files it imports and those importing it never see it, so a
  library's short names cannot collide with its users'. For the same
  reason an alias may take the name of a namespace: in its own file `g::x`
  then reads as `image::x`, and that file reaches the namespace `g` only
  through an alias of another name; every other file is unaffected. It may
  not take the name of the file's own namespace. The target is a namespace
  some file declares (with a `namespace` directive, whether or not it
  declares anything in it), never an alias. A declaration's qualifier
  names its namespace itself, not an alias (`fn g::f` is an error), so
  where a declaration goes is never a file-local fact.
* Multiple files may declare the same namespace. `import` still locates and
  loads a file once; its path does not create a namespace or import
  unqualified names out of an explicitly named namespace. All declarations
  are public.
* `image::Pixel` and `image::brightness` name declarations explicitly. `::`
  is distinct from existing `.` field access, enum variants and UFCS; for
  example, `image::Shape.Circle` remains unambiguous. A leading `::name`
  selects a global declaration when a namespaced declaration shadows it, and
  also reaches the builtins.
* A declaration may also write its own namespace in its name:
  `fn image::brightness`, `struct image::Pixel`, `let image::pool = ...`, or
  `fn ::main` for the global namespace from inside a namespaced file. The
  qualifier sets the declaration's namespace. Names inside the declaration
  resolve there first, rather than in the file's namespace. This lets
  `--dump` preserve namespaces when it combines declarations from multiple
  files. Nested functions are lexical and cannot be qualified.
* An unqualified name is looked up first among the lexical locals,
  parameters, type parameters and nested functions (any of which hides the
  declarations of that name), then in the current declaration's namespace,
  then among the global declarations and builtins. For functions, the first
  scope that contains the name supplies the whole overload set. Sets do not
  merge across namespaces, so a namespaced `hash` overload calls the global
  integer ones as `::hash(k.a)`. A generic body resolves names in the
  namespace of its definition, not in that of the caller that instantiates
  it. A field or parameter default does the same, whatever the namespace of
  the construction or call that uses it.
* A UFCS call `x.f(...)` looks in the namespace of `x`'s type first. When
  `x` (past a reference) is a struct or enum declared in a named namespace
  that declares `f`, that namespace's overload set is used. Otherwise the
  function-name rules above apply for the namespace the call is written
  in. Nested functions in scope still come first, and builtin array
  members keep their priority over user UFCS candidates. So
  `p.brightness()` calls `image::brightness` for an `image::Pixel` from
  any namespace, even past a global `brightness` or one in the calling
  namespace, as it would inside `image` itself. Otherwise a namespaced
  library's functions would need global bridges to be called as methods,
  and any global function sharing a name (`length`, `find`, `get`) would
  hide them. A generic body's UFCS call follows the type it is
  instantiated with, so the global `dictionary` module, which calls
  `key.hash()`, finds a namespaced key's `hash` with no bridge. A plain
  call `f(x)` stays with the namespace it is written in (there is no
  C++-style argument-dependent lookup), and the type's namespace never
  merges with another set: a function of the same name for that type in
  another namespace is called as `f(x)`. A type in the global namespace
  has no namespace of its own here, so the rules above apply to it
  unchanged.
* Custom `format` hooks are found by type as well. A matching overload is
  searched for in the namespace that defines the formatted nominal type,
  then in the global namespace, with the existing signature rules. Thus
  `print(p)` finds `image::format` for an `image::Pixel` anywhere, without
  opening that namespace. Rendering depends on the type, not on the
  caller's namespace.
* `return ... from parse` resolves the target in the returning function's
  definition context: a nested function in scope if there is one, otherwise
  the name's overload set by the rules above. `return ... from image::parse`
  is explicit. The target frame is the innermost enclosing call of any
  function in that set, so an unrelated `parse` on the call path cannot catch
  the return.
* Namespace qualification affects type identity, overload lookup and generated
  symbol names. It adds no runtime representation or lifetime rule. An
  imported file's `main` remains ignored as today, and the root file must
  supply a global `main` entry point: a namespaced program declares it as
  `fn ::main()`.

## Implementation

* Aliases are resolved by the parser alone. `ParseQualifiedName`, which
  builds every qualified reference, replaces a qualifier that is one of
  the file's aliases with its target, so nothing past the parser sees an
  alias, and a dump, which merges files, prints the target. The aliases
  are recorded in `Ast::nsaliases`. A `namespace x;` directive creates the
  namespace `x` even if its file declares nothing in it, and once every
  file is parsed, `ParseProgram` checks that each target is a namespace,
  unless an error came first (a file that failed to load or parse may be
  the one declaring it). Diagnostics name the target; an unknown type is
  still underlined over its spelling in the source (`TypeName::written`).
  An alias after a declaration is reported and still kept, so the rest of
  the file reads its uses as meant. The language server takes the spelling of a
  qualified `Ident` from the source text, so hover, definition and rename
  work on `g::x` (a name spelled with spaces around its `::` is left out,
  not given the wrong extent), and resolves the alias before completing
  after `g::`.
* Lexer: the `namespace` keyword and the `::` token. Parser: the directive,
  qualified declaration names, and qualified references wherever a
  declaration can be named (expressions, types, `return ... from`, the
  pool of a `T&<w in pool>` type, and the constants of an integer match's
  patterns). A qualified reference is interned as one string (`Ident::name`
  is `"image::Pixel"`, as written or as an alias reads), so it can never
  coincide with a local's name, and every existing lexical comparison stays
  correct. Every reference also records the namespace it was written in
  (`Ident::ns`, `TypeName::ns`, ...). That namespace is where an
  unqualified name resolves first, so it must survive cloning and generic
  instantiation.
* `Ast` keeps one `Namespace` of declaration maps per namespace name, and one
  lookup rule (`Ast::Lookup`) routes type resolution, function, global and
  enum lookup through it. Unknown qualified type names are errors in
  resolution rather than generic type-parameter candidates. The parser
  tries a `<` after a name as a type argument list first (§7.7); when it
  takes the `<` back as a comparison, it deletes the types it parsed in
  the attempt, so `ns::limit` in `a < ns::limit` is not treated as a type
  name (`test/syntax/namespace_comparisons.goose`).
* The dump prints declarations with their qualified names (`fn
  image::brightness(p: Pixel)`) and references as written (through an
  alias, as its target), so a dump of a multi-file program reparses to the
  same program; resolution runs after the dump so it shows names as written.
* C symbols: a global name `x` is `x_g` as before; a namespaced `ns::x` is
  `ns_x_g` followed by the namespace's length (`image_Pixel_g5`). The digits
  keep the mapping injective without a lossy punctuation replacement: `a::b_c`
  and `a_b::c` differ in those digits, and no global name's C form ends in a
  digit. Foreign C linkage names stay separate: an `extern`'s explicit
  symbol, or its default leaf name, is unchanged. `in pool` names resolve by
  the same rules and the pool declaration's identity is retained as before.
* Tests: `test/syntax/namespaces.goose` with `test/syntax/ns/` and
  `test/syntax/namespaces_lib.goose` cover duplicate leaf names across
  namespaces, same-namespace overloads, generic definition-site lookup,
  qualified types/variants/pools, global fallback and shadowing, imported
  main handling, custom formatting across namespaces, UFCS calls resolving
  in the receiver type's namespace (from the global namespace, from another
  namespace declaring the name, in a generic body, behind a nested
  function, on variants), a dictionary with a namespaced key and no hash
  bridge, same-leaf `return from` targets, and collision-free generated C
  symbols. `test/syntax/match_constants.goose` names another namespace's
  constants in match patterns, `test/errors*/ns_*.goose` cover the
  diagnostics, and `test/lsp/ufcs_ns.goose` the language server's answers
  on UFCS calls into a type's namespace. `test/syntax/namespace_aliases.goose`
  uses aliases in every kind of qualified reference. It imports
  `test/syntax/ns/alias_user.goose`, whose own `im` names another
  namespace, whose `terrain` alias takes the name of a namespace the
  importer uses, and whose `h` names a namespace that declares nothing
  (`ns/hollow.goose`). The `test/errors/ns_alias_*.goose` fixtures reject
  the misuses (one through the helper in `test/errors/ns_alias/`, showing
  an alias does not reach an imported file), and `test/lsp/ns_alias.goose`
  asks the language server about names spelled through an alias.

This extension changes name resolution. It does not add separate compilation,
package resolution, privacy, re-exports, or first-class module values.

An alias is a directive of its own rather than part of an import (`import
image.filters as f;`) because an import loads a file, and a namespace is not a
file: one namespace may span several files, and a file may declare none.

## Rust and Zig comparison

Rust's `mod` builds a hierarchical module tree within a crate, `use` introduces
bindings, and visibility is generally private by default with `pub` controls.
This design borrows the qualified-path syntax but keeps file loading separate
from namespace declaration, permits several files to contribute to one namespace,
and leaves declarations public. See the official [module reference](https://doc.rust-lang.org/reference/items/modules.html),
[use reference](https://doc.rust-lang.org/reference/items/use-declarations.html)
and [visibility rules](https://doc.rust-lang.org/reference/visibility-and-privacy.html).

Zig's `const image = @import("image.zig");` binds the imported file's container
type; declarations are accessed with `.`, and `pub` controls external visibility.
That fits Zig's compile-time type/value model. The Goose namespace is only a
symbol-table scope: it cannot be assigned, returned, parameterized as a value
or inspected at compile time. See the official [Zig language reference](https://ziglang.org/documentation/master/#import).
