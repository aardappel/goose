// error: cannot clear a while v is still used: the slices it views may point into it
// put stores a view of a into the elements it is given, ss's, which v
// views (§5.1).
fn put(w: (u8[:])[:], a: u8[>..]&) {
    w[0] = a[..];
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var x: u8[>..] = [];
    x.append("x");
    var ss: (u8[:])[>..] = [];
    ss.push(x[..]);
    put(ss[..], a);
    let v = ss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
