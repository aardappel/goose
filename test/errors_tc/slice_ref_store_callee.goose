// error: storing a slice rooted at b into s through put's parameter p: s is bound to one rooted at a, at a different scope depth
// put stores b's view into the slot its parameter names, main's s (§3.8,
// §4.1): the call assigns s as `s = b[..]` would, which the depth of s's
// binding, main's a, does not allow (§9.2).
var b: u8[>..] = [];
fn put(p: u8[:]&) { p = b[..]; }
fn main() {
    b.append("xyz");
    var a: u8[>..] = [];
    a.append("ab");
    var s = a[..];
    put(s);
    b.clear();
    b.append("QQQ");
    print(s);
}
