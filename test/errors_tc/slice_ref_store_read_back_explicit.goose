// error: storing a slice rooted at b into s through a reference that may name it: s is bound to one rooted at a, at a different scope depth
// h.r is rebound to an explicit `&s` (§3.8), which the read-back of it may
// be beside t, as a slice variable bound by reference would (§9.5): the
// store through it may assign s, whose binding is a view of main's a (§9.2).
struct H { r: u8[:]& }
var b: u8[>..] = [];
fn main() {
    b.append("xyz");
    var a: u8[>..] = [];
    a.append("ab");
    var s = a[..];
    var t = b[0..1];
    var h = H { r: t };
    h.r .= &s;
    h.r = b[..];
    b.clear();
    b.append("QQQ");
    print(s, t);
}
