// error: cannot clear a while h is still used: a reference to a slice that may point into it was stored there
// h holds a reference to the caller's slot, which may hold a view of the
// array a names, at its depth: use cannot tell (§5.1).
struct R { r: u8[:]& }
fn use(h: R, a: u8[>..]&) {
    a.clear();
    a.append("ZZZZZZZZ");
    print(h.r);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    use(rr, a);
}
