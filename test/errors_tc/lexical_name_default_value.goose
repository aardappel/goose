// error: type parameter N is bound to the type i64, not a value
// A default sees its function's type parameters (§7.1), which hide the
// global N.
let N = 2;

fn f<N>(x: N, y: i64 = N) -> i64 { y }

fn main() { print(f(1)); }
