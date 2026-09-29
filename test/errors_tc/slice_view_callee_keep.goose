// error: cannot clear a while bx is still used: a reference into it was stored there
// put stores a slice of what its parameter's slot holds, a's, into bx
// (§9.2): the clear frees what bx still views (§5.1).
struct Box { s: u8[:] }
fn put(p: u8[:]&, b: Box&) { b.s = p[0..1]; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var s = a[..];
    var bx = Box { s: other[..] };
    put(s, bx);
    a.clear();
    print(bx.s);
}
