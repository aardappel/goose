// error: cannot clear b while r is still used: it may hold a reference or slice into it
// take can assign s, the slot its parameter names, as it does: a load
// through p after that sees b's slice (§9.2), so r may view b, which the
// clear frees (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    fn take(p: u8[:]&) -> u8[:] {
        s = b[..];
        return p[0..1];
    }
    let r = take(s);
    b.clear();
    print(r);
}
