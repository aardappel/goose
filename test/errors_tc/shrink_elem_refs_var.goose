// error: cannot clear a while v is still used: the slices it views may point into it
// A `var` may since have been rebound to any array of slices at ss's
// depth, a's included, so its root only bounds what it views (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    var v = ss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
