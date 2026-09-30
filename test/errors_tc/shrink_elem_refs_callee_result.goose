// error: cannot clear a while v is still used: the slices it views may point into it
// view returns a slice of its argument's elements: v views ss's (§5.1).
fn view(xs: (u8[:])[>..]&) -> (u8[:])[:] {
    return xs[..];
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = view(ss);
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
