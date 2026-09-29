// error: cannot clear b while s is still used: it may hold a reference or slice into it
// g's parameter names main's s, which g's body names as well (§7.5): the
// store through pp may assign s, which then views b when the clear frees
// it, before g returns (§3.8, §5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    fn g(pp: u8[:]&) {
        pp = b[..];
        b.clear();
        b.append("QQQ");
        print(s);
    }
    g(s);
}
