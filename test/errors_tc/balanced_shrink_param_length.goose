// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A length the caller passes in is no mark (§5.2): it can be anything
// shorter than the array was when the call began.
fn helper(s: i64[>..<]&, keep: i64) {
    s.push(4);
    s.resize(keep);
}
fn main() {
    var s: i64[>..<] = [9, 8];
    let v = s[..];
    helper(s, 1);
    print(v[1]);
}
