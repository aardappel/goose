// error: cannot clear a while v is still used: the slices it views may point into it
// A store through a reference to ss's first element puts a view of a
// there, which v views (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var x: u8[>..] = [];
    x.append("x");
    var ss: (u8[:])[>..] = [];
    ss.push(x[..]);
    let r = &ss[0];
    r = a[..];
    let v = ss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
