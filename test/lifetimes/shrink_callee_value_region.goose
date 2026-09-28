// A callee first checked inside a function value's body, in the middle of
// the caller's expression, shrinks its own local freely: nothing of that
// expression can refer into it.
fn scratch() -> i64 {
    var buf: i64[>..] = [1, 2];
    buf.clear();
    return buf.len;
}
fn run<F>() -> i64 { return F(); }
fn main() {
    let v = run() { scratch() };
    let w = scratch();
    print(v, " ", w);
}
