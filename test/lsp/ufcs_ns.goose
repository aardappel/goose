// lsp: fixture for test/lsp_protocol.py: UFCS calls on a namespaced type
// from the global namespace, which resolve in the type's namespace before
// the global one, checked and not. Lines and columns are 1-based.
struct geo::Pt { x: i64, y: i64 }
fn geo::norm(p: Pt) -> i64 { p.x + p.y }
fn geo::norm(p: Pt, k: i64) -> i64 { (p.x + p.y) * k }

// A global norm, which the receiver type's namespace comes before.
fn norm(x: i64) -> i64 { x }

fn main() {
    let p = geo::Pt { 1, 2 };
    print(p.norm(), " ", p.norm(3), " ", norm(4));
    print(broken(p));
}

fn broken(p: geo::Pt) -> i64 { p.norm(true) }

// lsp: error 17
// lsp: signature 13:33 "fn geo::norm(p: geo::Pt, k: i64) -> i64" 1
// lsp: hover 13:13 "fn geo::norm(p: geo::Pt) -> i64"
// lsp: hover 17:34 "fn geo::norm(p: geo::Pt, k: i64) -> i64"
// lsp: definition 13:13 -> 5:9
// lsp: definition 13:28 -> 6:9
// lsp: definition 17:34 -> 5:9
