// error: array fill count N names a type parameter, not a constant
// A block written in f names what f's body does (§7.6): N is f's type
// parameter, which hides the global N.
let N = 4;

fn apply<F>(x: i64) -> i64 { F(x) }

fn f<N>(x: N) -> i64 {
    apply(2) { k => [k; N].len }
}

fn main() { print(f(1)); }
