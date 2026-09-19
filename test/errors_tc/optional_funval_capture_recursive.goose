// error: operator + cannot be applied to i64? and i64
// Nested functions declared in a function value's body, passed to a
// recursive function: its recursive call may rebind the body's optional
// through clear, so use cannot rely on the assert.
fn apply<F>(n: i64) -> i64 { F(n) }
recursive fn rec<F, G>(k: i64) {
    if k > 0 { rec(k - 1, F, G); }
    G();
    F();
}
fn main() {
    print(apply(1) {
        var n = 41;
        var opt: i64? = n;
        fn use() { print(opt + 1); }
        fn clear() { opt .= null; }
        assert(opt);
        rec(it, clear, use);
        0
    });
}
