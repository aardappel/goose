// error: a function value must be a function name or block literal
// Computed function values are rejected before their captures can escape.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    let k = 100;
    print(apply(5, block { let k = 7; fn h(x: i64) -> i64 { x + k } break h; }));
}
