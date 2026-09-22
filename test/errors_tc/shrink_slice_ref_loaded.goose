// error: cannot clear gs while v
// error: is still used: it may refer into gs
// The slice loaded through a reference to a slice variable points where
// that variable's slice does (§5.2).
fn peek(p: i64[:]&, gs: i64[>..<]&) {
    let v = p;
    gs.clear();
    print(v[0]);
}
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    peek(s, gs);
}
