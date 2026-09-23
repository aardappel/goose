// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A mark read through a `var` reference is the length of whatever it was
// bound to then (§5.2): after the rebind, the resize shrinks another array.
fn helper(a: i64[>..<]&, b: i64[>..<]&) {
    var r: i64[>..<]& = a;
    let mark = r.len;
    r .= b;
    r.resize(mark);
}
fn main() {
    var s: i64[>..<] = [9, 8, 7];
    var t: i64[>..<] = [1];
    let v = s[..];
    helper(t, s);
    print(v[2]);
}
