// A callee first checked inside a value-producing region shrinks its own
// local freely: the region belongs to the caller's expression.
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
