// error: cannot clear gs while v
// error: is still used: it may refer into gs
// The slice loaded through a parameter bound to an explicit `&s` is the one
// s holds, a view of gs, which peek clears while v is still used (§3.8,
// §5.2).
fn peek(p: i64[:]&, gs: i64[>..<]&) {
    let v = p;
    gs.clear();
    print(v[0]);
}
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    peek(&s, gs);
}
