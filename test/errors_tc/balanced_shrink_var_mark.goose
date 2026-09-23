// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A mark is a `let` (§5.2): a `var` initialized to the length can be
// assigned anything before the resize.
fn helper(s: i64[>..<]&) {
    var mark = s.len;
    s.push(4);
    mark = 0;
    s.resize(mark);
}
fn main() {
    var s: i64[>..<] = [9];
    let v = s[..];
    helper(s);
    print(v[0]);
}
