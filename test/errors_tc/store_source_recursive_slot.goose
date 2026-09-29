// error: cannot clear a while bx is still used: a reference into it was stored there
// A recursive function's parameter is a slot of the caller's too: the copy
// put stores into bx comes out of s, whose binding, not its stores, says
// it views a (§3.5, §5.1).
struct Box { s: u8[:] }
recursive fn put(p: u8[:]&, b: Box&, n: i64) {
    if n > 0 { put(p, b, n - 1); }
    b.s = p[0..1];
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var s = a[..];
    var bx = Box { s: other[..] };
    put(s, bx, 2);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
