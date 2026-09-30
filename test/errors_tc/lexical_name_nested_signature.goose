// error: array size N names a type parameter, not a constant
// So does its signature, checked where it is declared (§7.5).
let N = 4;

fn f<N>(x: N) -> i64 {
    fn g(a: i64[N]) -> i64 { a.len }
    g([1, 2, 3, 4])
}

fn main() { print(f(1)); }
