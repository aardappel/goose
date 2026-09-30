// error: array size N names a type parameter, not a constant
// A nested function names what is in scope at its declaration (§7.5): f's
// type parameter N, which hides the global N.
let N = 4;

fn f<N>(x: N) -> i64 {
    fn g() -> i64 {
        let a: i64[N] = [1, 2, 3, 4];
        a.len
    }
    g()
}

fn main() { print(f(1)); }
