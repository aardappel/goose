// error: cannot clear g while t is still used: it may hold a reference or slice into it
// set's first call names s, which already views g; its second names t, to
// which no reference had been made when the first was checked: that call
// checks set again, since the store may now assign the t its body reads
// (§3.8, §7.5), which then views g when the clear frees it (§5.1).
fn main() {
    var g: u8[>..] = [];
    g.append("xyz");
    var a: u8[>..] = [];
    a.append("ab");
    let s = g[0..1];
    let t = a[..];
    fn set(p: u8[:]&) {
        p = g[..];
        g.clear();
        g.append("QQQ");
        print(t);
    }
    set(s);
    set(t);
}
