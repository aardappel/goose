// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// rr's field refers to s, which views b: a slice variable behind a reference
// is bounded by its scope depth alone, as a write through the reference may
// rebind it to any root there, and s shares a's, so rr may lead to a's
// storage when the clear frees it (§5.1; the store the message names is rr's
// initializer). The write through rr's field does rebind s to a view of a,
// which s's depth allows (§9.2).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = b[..];
    var rr = R { r: s };
    rr.r = a[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
