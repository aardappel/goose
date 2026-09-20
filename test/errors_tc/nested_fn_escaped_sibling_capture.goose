// error: a function value must be a function name or block literal
// Computed function values are rejected before their captures can escape.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    print(apply(5, block {
        fn h(x: i64) -> i64 { x + g() }
        let k = 7;
        fn g() -> i64 { k }
        let y = h(1);
        break h;
    }));
}
