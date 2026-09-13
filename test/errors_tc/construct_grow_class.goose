// error: cannot call mk, which grows x: the run appended to x is still under construction
// Two reference parameters a call site passes one array to share a root
// class: the append builds into it while the callee grows it (§1.3(4)).
fn mk(y: i64[>..]&) -> i64[>..] { y.push(9); return [1, 2]; }
fn f(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
fn main() {
    var a: i64[>..] = [];
    f(a, a);
    print(a);
}
