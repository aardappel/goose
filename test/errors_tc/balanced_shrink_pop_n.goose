// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A pop_n back down to a mark is not balanced (§5.2): only a resize to the
// mark is, so the call frees what v views as far as the caller can tell.
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s.push(4);
    s.pop_n(s.len - mark);
}
fn main() {
    var s: i64[>..<] = [9];
    let v = s[..];
    helper(s);
    print(v[0]);
}
