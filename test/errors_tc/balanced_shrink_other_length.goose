// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A mark is the length of the array it resizes (§5.2), not of another one.
fn helper(s: i64[>..<]&, t: i64[>..<]&) {
    let mark = t.len;
    s.push(4);
    s.resize(mark);
}
fn main() {
    var s: i64[>..<] = [9, 8];
    var t: i64[>..<] = [];
    let v = s[..];
    helper(s, t);
    print(v[1]);
}
