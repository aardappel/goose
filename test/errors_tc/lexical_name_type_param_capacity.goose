// error: array capacity N names a type parameter, not a constant
// A generic body's sizes resolve in its scope (§3.3): N is the type parameter.
let N = 4;

fn f<N>(x: N) -> i64 {
    var a: i64[..N] = [];
    a.push(1);
    a.len
}

fn main() { print(f(1)); }
