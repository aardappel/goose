// error: cannot pop a while v (bound at
// A variable whose type is inferred from a reference to a non-fixed value is
// that reference (§3.8): popping through it shrinks the original, which a
// view into the original still in use rejects (§5.2).
fn f(r: i64[>..<]&) {
    let a = r;
    let v = r[..];
    a.pop();
    print(v.len);
}
fn main() {
    var s: i64[>..<] = [1, 2];
    f(s);
}
