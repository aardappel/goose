// error: cannot clear a while v is still used: the slices it views may point into it
// A store through w, another slice of ss's elements, puts a view of a
// there, which v views (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var x: u8[>..] = [];
    x.append("x");
    var ss: (u8[:])[>..] = [];
    ss.push(x[..]);
    let w = ss[..];
    w[0] = a[..];
    let v = ss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
