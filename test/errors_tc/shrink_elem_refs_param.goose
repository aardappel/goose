// error: cannot clear a while v is still used: the slices it views may point into it
// v's elements lie in the caller's storage, which may hold a view of the
// array a names, at its depth: f cannot tell (§5.1).
fn f(a: u8[>..]&, v: (u8[:])[:]) {
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    f(a, ss[..]);
}
