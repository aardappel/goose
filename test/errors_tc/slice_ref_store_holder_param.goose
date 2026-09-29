// error: cannot clear b while s is still used: it may hold a reference or slice into it
// put's holder parameter is a copy of the literal, whose reference names s
// (§3.8, §4.1): the store through it assigns s, which then views b (§5.1).
struct H { r: u8[:]& }
fn put(h: H, q: u8[:]) { h.r = q; }
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    put(H { r: s }, b[..]);
    b.clear();
    b.append("QQQ");
    print(s);
}
