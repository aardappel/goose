// lsp: fixture for test/lsp_protocol.py: an import that finds nothing, a
// struct with a bad field, an enum with a bad variant and functions with
// bad parameter lists, each reported where it is; the declarations stand
// with what was parsed before their errors, the program goes on without
// the module, and is checked (docs/design/language_server.md §5 D).
import nowhere.at.all;

struct Point { x: i64, y: i64 z: i64 }

enum Shape { Dot, Circle { r: i64 }, Square { s: i64 side } }

fn broken(a: i64, b: ) -> i64 { a + b }
thread_fn worker(n i64) { print(n); }
export fn exported(a: i64 b: i64) -> i64 { a }

fn total(p: Point) -> i64 { p.x + p.y }
fn apply(f, x: i64) -> i64 { f(x) }

// The field and the variant the errors lost, and the functions whose
// headers were lost, called or passed, are consequences: each ends its
// function's check, unreported.
fn lost_field(p: Point) -> i64 { p.z }
fn lost_variant() -> Shape { Shape.Square { s: 1 } }
fn calls_broken() -> i64 { broken(1, 2) }
fn passes_broken() -> i64 { apply(broken, 1) }

// What the missing module would have declared is unknown where it is
// named: a type in a declaration, which stands for a type of its own, a
// value of which ends the check where it is met, and a function where it
// is called. An error past all of these is reported as any other.
struct Body { pos: Vec2, mass: i64 }
fn mass_of(b: Body) -> i64 { b.mass }
fn pos_of(b: Body) -> i64 { let v = b.pos; 1 }
fn from_missing() -> i64 { far_away(1) }
fn real_error(p: Point) -> bool { p.x }

fn main() {
    let p = Point { x: 1, y: 2 };
    let t = total(p);
    let c = Shape.Circle { r: t };
    print(t + mass_of(Body { mass: 2 }));
}

// lsp: error 6
// lsp: error 8
// lsp: error 10
// lsp: error 12
// lsp: error 13
// lsp: error 14
// lsp: error 31
// lsp: error 34
// lsp: error 35
// lsp: hover 39:9 "let t: i64"
// lsp: hover 38:13 "struct Point"
// lsp: hover 16:29 "parameter p: Point"
// lsp: hover 16:31 "x: i64"
// lsp: hover 40:9 "Shape"
// lsp: hover 32:33 "mass: i64"
// lsp: hover 31:20 "unknown type Vec2"
// lsp: definition 31:20 -> none
// lsp: rename 31:20 Vec3 -> error
// lsp: token 31:20 none
// lsp: definition 39:13 -> 16:4
// lsp: symbol Point struct
// lsp: symbol broken function
