// error: cannot clear b while r is still used: it may hold a reference or slice into it
// f's parameters name the same slot, main's s, which f cannot tell: g's own
// class for s has no view, s being within g's reach. The store through p
// may be into the slot q's view stands for, so the load through q after it
// may view b (§9.2), which the clear frees under r (§5.1).
fn f(p: u8[:]&, q: u8[:]&, o: u8[:]) -> u8[:] {
    p = o;
    return q[0..1];
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    fn g(pp: u8[:]&) -> u8[:] { return f(pp, s, b[..]); }
    let r = g(s);
    b.clear();
    print(r);
}
