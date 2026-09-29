// error: cannot clear b while s is still used: it may hold a reference or slice into it
// inner's body names s by storing into it through r (§3.8): the second
// pass's call reuses the body checked for the first pass's s, and leaves s
// as that check did (§7.5), viewing b, which o names from the second pass
// on (§5.1).
fn main() {
    var b: u8[>..] = [];
    b.append("xyz");
    var a: u8[>..] = [];
    a.append("ab");
    var o: u8[>..]&? = null;
    for i in 2 {
        let s = a[..];
        let r = &s;
        fn inner() { r = b[..]; }
        inner();
        if o {
            o.clear();
            o.append("QQQ");
            print(s);
        }
        o .= b;
    }
}
