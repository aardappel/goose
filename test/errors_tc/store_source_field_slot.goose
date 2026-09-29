// error: cannot clear a while bx is still used: a reference into it was stored there
// put copies the slice in the slot its parameter names, which is h's field:
// h's stores say where the slice in it points, so they are the copy's
// source (§3.5, §5.1).
struct Box { s: u8[:] }
fn put(p: u8[:]&, b: Box&) { b.s = p[0..1]; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var h = Box { s: a[..] };
    var bx = Box { s: other[..] };
    put(h.s, bx);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
