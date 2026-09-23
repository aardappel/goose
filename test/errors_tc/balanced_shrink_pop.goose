// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// Resizing back to a mark balances nothing when the callee also pops the
// array (§5.2): the element v views is freed.
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s.push(4);
    s.resize(mark);
    s.pop();
}
fn main() {
    var s: i64[>..<] = [9];
    let v = s[..];
    helper(s);
    print(v[0]);
}
