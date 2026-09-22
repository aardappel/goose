// error: cannot clear gs while v
// error: is still used: it may refer into gs
// Behind a parameter bound to an explicit `&s`, the caller's slice variable
// only bounds where the slice loaded through it points (§3.8, §5.2).
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
