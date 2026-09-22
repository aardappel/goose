// error: cannot clear gs while p
// error: is still used: the slice it refers to may point into gs
// A clear through a reference parameter while the slice another parameter
// refers to still views the array (§5.2).
fn peek(p: i64[:]&, gs: i64[>..<]&) {
    gs.clear();
    print(p[0]);
}
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    peek(s, gs);
}
