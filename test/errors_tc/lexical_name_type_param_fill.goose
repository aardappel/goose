// error: array fill count N names a type parameter, not a constant
// A fill count resolves where it is written (§4.2): N is the type parameter.
let N = 4;

fn f<N>(x: N) -> i64 {
    let a = [x; N];
    a.len
}

fn main() { print(f(1)); }
