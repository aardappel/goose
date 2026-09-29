// error: operator + cannot be applied to i64? and i64
// A rebind in the right operand of && un-narrows the guarded region for a
// local of any function, not only of the first one on the call path.
fn f(n: i64) {
    var opt: i64? = n;
    fn clear() -> bool { opt .= null; return true; }
    if opt && clear() { print(opt + 1); }
}
fn main() {
    var a = 1;
    f(a);
}
