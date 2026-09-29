// error: cannot clear b while s is still used: it may hold a reference or slice into it
// The block's parameter is run's, which names main's s (§3.8, §7.6): the
// store through it assigns s, which views b when the clear frees it (§5.1).
fn run<F>(s: u8[:]&) { F(s); }
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    run(s) { p: u8[:]& => p = b[..]; };
    b.clear();
    b.append("QQQ");
    print(s);
}
