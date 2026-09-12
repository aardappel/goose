// A callee's shrink of a global is the caller's inside a value-producing
// region, also when that region is where the callee is first checked.
var g: i64[>..] = [1, 2];
fn drop() -> i64 {
    g.clear();
    return 0;
}
fn run<F>() -> i64 { return F(); }
fn main() {
    let v = run() { drop() };
    print(v);
}
