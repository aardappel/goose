// error: cannot clear b while s is still used: it may hold a reference or slice into it
// put's argument is read back out of h, so it may name s or s2, each a slot
// a reference has been made to (§9.5): the call may assign either (§3.8).
struct H { r: u8[:]& }
fn put(p: u8[:]&, q: u8[:]) { p = q; }
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    let s2 = a[0..1];
    let r2 = &s2;
    print(r2);
    let h = H { r: s };
    put(h.r, b[..]);
    b.clear();
    b.append("QQQ");
    print(s);
}
