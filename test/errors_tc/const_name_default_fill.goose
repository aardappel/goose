// error: the default of parameter a of f names parameter K
// A default names what a top-level declaration would (§7.1): the parameter
// K where a fill count names K, so the global K is not taken instead.
let K = 3;

fn f(K: i64, a: i64[3] = [0; K]) -> i64 { a.len + K }

fn main() { print(f(5)); }
