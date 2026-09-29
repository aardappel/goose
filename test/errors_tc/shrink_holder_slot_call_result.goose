// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// mk returns a holder of the reference it was given, to s, which views a:
// the record keeps only that rr holds a reference rooted at s (§5.1).
struct R { r: u8[:]& }
fn mk(p: u8[:]&) -> R { return R { r: p }; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = mk(s);
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
