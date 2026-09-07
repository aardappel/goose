# Minimal namespaces proposal

Proposal only; the syntax below is not implemented. Add an optional file-level
namespace declaration and explicit qualified names. Keep the current import
loader and whole-program compilation.

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

* `namespace name;` occurs at most once, before declarations. Files without it
  keep today's global namespace. For the first version, `name` is one identifier;
  nested namespaces, aliases and namespace-opening directives can wait.
* Multiple files may declare the same namespace. `import` still locates and loads
  a file once; its path does not create a namespace or import unqualified names
  out of an explicitly named namespace. All declarations stay public initially.
* `image::Pixel` and `image::brightness` name declarations explicitly. `::` is
  distinct from existing `.` field access, enum variants and UFCS; for example,
  `image::Shape.Circle` remains unambiguous. A leading `::name` selects a global
  declaration when a namespaced declaration shadows it.
* Unqualified resolution follows lexical locals/parameters/type parameters,
  then the current declaration's namespace, then today's global declarations
  and builtins. For functions, use the first scope that contains the name; do
  not merge overload sets across namespaces. A generic body resolves names in
  its definition's namespace, not the instantiating caller's.
* The initial UFCS lookup follows those same function-name rules. A caller in
  another namespace uses `image::brightness(p)` explicitly; associated-namespace
  lookup and extension imports can be considered separately. Builtin array
  members retain their existing priority over user UFCS candidates.
* Custom `format` hooks are the one implicit lookup exception: use the formatted
  nominal type's defining namespace, then the global fallback, with the existing
  signature rules. Thus `print(p)` finds `image::format` for an `image::Pixel`
  anywhere, without opening that namespace. This preserves type-directed
  formatting instead of making rendering depend on the caller's namespace.
  Ordinary generic customization remains definition-site lookup: a namespaced
  key's `hash` overload needs a global bridge for today's global `dictionary`
  module (for example `fn hash(k: image::Key) -> u64 { image::hash(k) }`).
  General associated-type lookup could remove that bridge later, but is not
  part of this first namespace feature.
* `return ... from parse` resolves the target in the returning function's
  definition context; `return ... from image::parse` is explicit. Match the
  resolved declaration/overload set against enclosing call frames, rather than
  comparing leaf names, so unrelated `parse` functions cannot catch the return.
* Namespace qualification affects type identity, overload lookup and generated
  symbol names. It adds no runtime representation or lifetime rule. An imported
  file's `main` remains ignored as today; the root file must supply a global
  `main` entry point.

## Fit to the implementation

1. Add `namespace` and `::` tokens and parse qualified identifier paths wherever
   declaration references/types occur. Update dump/roundtrip support alongside it.
2. Intern qualified names with AST lifetime. Attach the defining namespace to
   declarations and preserve it on clones/specializations; keep leaf names for
   diagnostics and lexical bindings.
3. Key the existing `Ast` declaration maps (`ast.h`) by qualified names. Route
   type resolution (`resolve.h`) and function/global lookup through one helper
   implementing the lookup order above. Unknown qualified names are errors,
   rather than generic type-parameter candidates.
4. Include namespace identity in C mangling without lossy punctuation replacement.
   Keep foreign C linkage names separate: an `extern`'s explicit symbol, or its
   default leaf name, remains unchanged. Resolve `in pool` names using the same
   rules and retain the pool declaration's identity as today.
5. Test duplicate leaf names across namespaces, same-namespace overloads, generic
   definition-site lookup, qualified types/variants/pools, global fallback and
   shadowing, imported main handling, custom formatting across namespaces,
   a dictionary with a namespaced key and global hash bridge,
   same-leaf `return from` targets, and collision-free generated C symbols.

This is a name-resolution extension. It deliberately does not add separate
compilation, package resolution, privacy, re-exports or first-class module values.

## Rust and Zig comparison

Rust's `mod` builds a hierarchical module tree within a crate, `use` introduces
bindings, and visibility is generally private by default with `pub` controls.
This proposal borrows qualified-path spelling but keeps file loading separate
from namespace declaration, permits several files to contribute to one namespace,
and leaves declarations public. See the official [module reference](https://doc.rust-lang.org/reference/items/modules.html),
[use reference](https://doc.rust-lang.org/reference/items/use-declarations.html)
and [visibility rules](https://doc.rust-lang.org/reference/visibility-and-privacy.html).

Zig's `const image = @import("image.zig");` binds the imported file's container
type; declarations are accessed with `.`, and `pub` controls external visibility.
That fits Zig's compile-time type/value model. The proposed Goose namespace is
only a symbol-table scope: it cannot be assigned, returned, parameterized as a
value or inspected at compile time. See the official [Zig language reference](https://ziglang.org/documentation/master/#import).
