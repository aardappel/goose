// error: cannot clear a while bx is still used: a reference into it was stored there
// put copies the slice in the slot its parameter names, which is s: a slice
// variable holds what its binding says, a[..], and no store into s records
// that, so the copy in bx is bounded by s rather than traced through s's
// stores (§3.5, §5.1).
struct Box { s: u8[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var s = a[..];
    var bx = Box { s: other[..] };
    fn put(p: u8[:]&, b: Box&) { b.s = p[0..1]; }
    put(s, bx);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
