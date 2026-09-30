// error: type parameter N is bound to the type i64, not a value
// A name resolves lexically first (§11.1): N is the type parameter, which
// hides the global N and names a type, not a value.
let N = 4;

fn f<N>(x: N) -> i64 { N }

fn main() { print(f(1)); }
