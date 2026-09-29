// error: cannot clear b while r is still used: it may hold a reference or slice into it
// look's first call loads the slice of a's that the caller's slot holds; by
// its second, f has stored b's there (§9.2), so that call is checked for
// what look finds then, and r may view b, which the clear frees (§5.1).
fn f(p: u8[:]&, o: u8[:]) -> u8[:] {
    fn look() -> u8[:] { return p[0..1]; }
    let x = look();
    p = o;
    return look();
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    let r = f(s, b[..]);
    b.clear();
    print(r);
}
