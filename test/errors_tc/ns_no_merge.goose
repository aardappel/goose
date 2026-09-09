// Overload sets never merge across namespaces: a namespace declaring any `f`
// hides the global f overloads from its own unqualified calls (`::f` reaches
// them).
fn f(x: i64) -> i64 { x }
fn a::f(x: f64) -> f64 { x }
fn a::g() -> i64 { f(1) }
fn main() { print(a::g()); }
