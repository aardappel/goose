// error: cannot pop a while v (bound at
// An inferred result that is a reference to a non-fixed value is the
// original (§7.1): popping through it shrinks the caller's array, which a
// view into that array still in use rejects (§5.2).
struct Ctx { args: i64[>..<]& }
fn args(c: Ctx&) { c.args }
fn f(c: Ctx&) {
    let a = args(c);
    let v = c.args[..];
    a.pop();
    print(v.len);
}
fn main() {
    var s: i64[>..<] = [1, 2];
    var c = Ctx { args: s };
    f(c);
}
