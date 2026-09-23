// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A call inside a balanced-looking body is balanced only where its callee is
// (§5.2): trim pops below the mark, and the resize grows back over the
// element v views.
fn trim(s: i64[>..<]&) { s.pop(); }
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s.push(4);
    trim(s);
    trim(s);
    s.resize(mark, 0);
}
fn main() {
    var s: i64[>..<] = [9];
    let v = s[..];
    helper(s);
    print(v[0]);
}
