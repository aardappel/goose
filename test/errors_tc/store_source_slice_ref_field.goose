// error: cannot clear a while bx is still used: a reference into it was stored there
// The slice put stores is loaded through a reference read out of rr: rr's
// stores say which slot that reference names, not where the slice in the
// slot points, so rr bounds the slice rather than being its source (§3.5,
// §3.8, §5.1).
struct R { r: u8[:]& }
struct Box { s: const u8[:] }
fn put(h: R&, b: Box&) { b.s = h.r; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var s = a[..];
    var rr = R { r: s };
    var bx = Box { s: other[..] };
    put(rr, bx);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
