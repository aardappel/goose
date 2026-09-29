// error: cannot clear b while s is still used: it may hold a reference or slice into it
// The first iteration stores b's view into s through a reference (§3.8), so
// the second finds s viewing b when it clears b (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("ab");
    var b: u8[>..] = [];
    b.append("xyz");
    let s = a[..];
    for i in 2 {
        if i == 1 {
            b.clear();
            b.append("QQQ");
            print(s);
        }
        if i == 0 {
            let r = &s;
            r = b[..];
        }
    }
}
