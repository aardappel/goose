// error: cannot clear a while v is still used: the slices it views may point into it
// g shrinks its parent's a and then reads its parent's v, whose elements
// view a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = ss[..];
    fn g() {
        a.clear();
        a.append("ZZZZZZZZ");
        print(v[0]);
    }
    g();
}
