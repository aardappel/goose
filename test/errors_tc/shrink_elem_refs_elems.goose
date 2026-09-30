// error: cannot call clr, which shrinks a: the elements of v, which the statement reads after it, may refer into a
// The index runs clr before v[...] reads the element, a view of a (§5.1).
fn clr(a: u8[>..]&) -> i64 {
    a.clear();
    a.append("ZZZZZZZZ");
    return 0;
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = ss[..];
    print(v[clr(a)]);
}
