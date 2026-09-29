// error: cannot clear b while s is still used: it may hold a reference or slice into it
// The block stores through main's reference to s (§7.6), which assigns s
// (§3.8): s views b when the clear frees it (§5.1).
fn run<F>() { F(); }
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    let r = &s;
    run() { r = b[..]; };
    b.clear();
    b.append("QQQ");
    print(s);
}
