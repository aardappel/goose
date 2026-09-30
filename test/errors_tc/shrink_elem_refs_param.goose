// error: cannot call f: it shrinks a while v is still used, and v may refer into a
// v's elements lie in the caller's storage, which f leaves its callers to
// judge: here ss, whose element views the array a names (§5.1).
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
