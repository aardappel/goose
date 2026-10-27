// error: a function value must be a function name or block literal
// Computed function values are rejected before their captures can escape.
fn apply<F>(n: i64) -> i64 { F(n) }
fn pass<G>(n: i64) -> i64 { apply(n, G()) }
fn main() {
    let k = 100;
    print(pass(5) { let k = 7; fn h(x: i64) -> i64 { x + k } h });
}
