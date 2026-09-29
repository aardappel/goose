// error: cannot clear b while s is still used: it may hold a reference or slice into it
// pass hands its parameter on to put, whose store into the slot is pass's,
// and so main's into s (§3.8): s views b when the clear frees it (§5.1).
fn put(p: u8[:]&, q: u8[:]) { p = q; }
fn pass(p: u8[:]&, q: u8[:]) { put(p, q); }
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    pass(s, b[..]);
    b.clear();
    b.append("QQQ");
    print(s);
}
