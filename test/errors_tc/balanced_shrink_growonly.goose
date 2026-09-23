// error: cannot call helper, which shrinks s while v is still used: it may hold a reference or slice into it
// Only grow-shrink arrays have balanced calls (§5.2): a grow-only array's
// shrinks are checked at every call as before (§5.1).
fn helper(s: i64[>..]&) {
    let mark = s.len;
    s.push(4);
    s.resize(mark);
}
fn main() {
    var s: i64[>..] = [9];
    let v = s[..];
    helper(s);
    print(v[0]);
}
