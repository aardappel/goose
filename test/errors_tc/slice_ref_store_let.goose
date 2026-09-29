// error: cannot clear b while s is still used: it may hold a reference or slice into it
// A `let` slice is written through a reference as a `var` is (§4.4): the
// store points s into b, whose root has the depth of a's, so s may view b
// when the clear frees its elements (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    let r = &s;
    r = b[..];
    b.clear();
    b.append("QQQ");
    print(s);
}
