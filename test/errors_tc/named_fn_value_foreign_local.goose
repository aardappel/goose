// error: unknown identifier: z
// A nested function named as a function value inside another nested function
// sees the scope it is declared in, where there is no z.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    fn g(n: i64) -> i64 { n + z }
    fn h() -> i64 { let z = 7; apply(5, g) }
    print(h());
}
