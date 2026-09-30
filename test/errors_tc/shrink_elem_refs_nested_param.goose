// error: cannot call g: it shrinks a while v is still used, and v may refer into a
// g's parameter may name any array of its parent's: g records the pair,
// and the call names a, which v's elements view (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = ss[..];
    fn g(x: u8[>..]&) {
        x.clear();
        x.append("ZZZZZZZZ");
        print(v[0]);
    }
    g(a);
}
