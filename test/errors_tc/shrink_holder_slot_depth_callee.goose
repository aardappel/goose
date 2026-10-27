// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// rr's field refers to t, which views b: a slice variable behind a reference
// is bounded by its scope depth alone, and t shares a's, so rr may lead to
// a's storage when the clear frees it (§5.1; the store the message names is
// rr's initializer). fill's store into rr of a reference to s, which does
// view a, is checked on the way and leads rr there too.
struct R { r: u8[:]& }
fn fill(o: R&, p: u8[:]&) { o = R { r: p }; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    var t = b[..];
    var rr = R { r: t };
    fill(rr, s);
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
