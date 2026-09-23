// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// Assigning the array whole releases every element before rebuilding it
// (§5.2, §4.4), whatever the resize after it restores.
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s = [1];
    s.resize(mark, 0);
}
fn main() {
    var s: i64[>..<] = [9, 8];
    let v = s[..];
    helper(s);
    print(v[1]);
}
