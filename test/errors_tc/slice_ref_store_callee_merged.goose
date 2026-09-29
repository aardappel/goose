// error: cannot clear b while s1 is still used: it may hold a reference or slice into it
// put's parameter names s1 or s2, whichever branch ran (§6.4): the call may
// assign either (§3.8), so both may view b when the clear frees it (§5.1).
fn put(p: u8[:]&, q: u8[:]) { p = q; }
fn pick(c: bool) -> bool { c }
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s1 = a[..];
    let s2 = a[0..1];
    let r: u8[:]& = if pick(true) { s1 } else { s2 };
    put(r, b[..]);
    b.clear();
    b.append("QQQ");
    print(s1);
}
