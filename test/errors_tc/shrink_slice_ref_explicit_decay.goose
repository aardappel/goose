// error: cannot clear gs while v
// error: is still used: it may refer into gs
// A slice parameter takes the slice `&s` refers to, which views gs (§4.1,
// §5.2).
fn f(v: i64[:], gs: i64[>..<]&) {
    gs.clear();
    print(v[0]);
}
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    f(&s, gs);
}
