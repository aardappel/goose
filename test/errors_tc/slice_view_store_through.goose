// error: cannot clear b while r is still used: it may hold a reference or slice into it
// The view swapin's parameter has of the caller's slot is s's slice, a's,
// until the store through p puts b's there: the load after it may view
// either (§9.2), and the clear frees what r still views (§5.1).
fn swapin(p: u8[:]&, o: u8[:]) -> u8[:] {
    p = o;
    return p[0..1];
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    let r = swapin(s, b[..]);
    b.clear();
    print(r);
}
