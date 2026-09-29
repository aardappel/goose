// error: storing a slice rooted at b into s through a reference: s is bound to one rooted at a, at a different scope depth
// h.r is read back out of h, so it may name any slot of its type in scope
// that a reference has been made to (§9.5), s among them: the store through
// it assigns s, whose binding is a view of main's a (§9.2).
struct H { r: u8[:]& }
var b: u8[>..] = [];
fn main() {
    b.append("xyz");
    var a: u8[>..] = [];
    a.append("ab");
    var s = a[..];
    let h = H { r: s };
    h.r = b[..];
    b.clear();
    b.append("QQQ");
    print(s);
}
