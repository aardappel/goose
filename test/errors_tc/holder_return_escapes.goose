// A struct holding a reference to the function's own local is returned by
// value: the reference inside it dies with the function (§9.2).
struct S { r: i64&, k: i64 }
fn make() -> S { var x = 5; let s = S { r: x, k: 1 }; return s; }
fn main() { let s = make(); print(s.k); }
