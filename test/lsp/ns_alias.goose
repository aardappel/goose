// lsp: fixture for test/lsp_protocol.py: names spelled through a namespace
// alias, `g::x` for geo::x. Hover, definition, references and rename find the
// declaration, and completion after `g::` offers geo's names. Lines and
// columns are 1-based.
namespace g = geo;
struct geo::Pt { x: i64, y: i64 }
fn geo::norm(p: Pt) -> i64 { p.x + p.y }

fn main() {
    let p = g::Pt { 1, 2 };
    print(g::norm(p), " ", geo::norm(p));
    print(spaced(p));
}

// A name spelled with spaces around its `::`, which the language server
// leaves out rather than edit the wrong text: a rename does not touch it.
fn spaced(p: g::Pt) -> i64 { g :: norm(p) }

// lsp: hover 11:14 "fn geo::norm(p: geo::Pt) -> i64"
// lsp: hover 11:11 "fn geo::norm(p: geo::Pt) -> i64"
// lsp: definition 11:14 -> 7:9
// lsp: definition 10:16 -> 6:13
// lsp: references 11:14 3
// lsp: rename 7:9 length -> 3
// lsp: rename 11:14 length -> 3
// lsp: rename 6:13 Spot -> 4
// lsp: rename 10:16 Spot -> 4
// lsp: token 10:13 struct
// lsp: complete 11:14 has norm
// lsp: complete 11:14 has Pt
