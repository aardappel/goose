// error: cannot clear b while v is still used: it may hold a reference or slice into it
// r refers to s itself (§3.8, §4.1), so loading it gives what s holds then:
// a view of b after s is rebound at its root's depth (§9.2), which the
// clear frees while v still views it (§5.1).
fn main() {
    var a: u8[>..] = [];
    var b: u8[>..] = [];
    a.append("ab");
    b.append("xyz");
    var s = a[..];
    let r: u8[:]& = s;
    s = b[..];
    let v = r;
    b.clear();
    b.append("QQQ");
    print(v);
}
