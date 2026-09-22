// error: cannot call h, which shrinks gs: an earlier expression value
// error: may still refer into gs
// g's first argument, a reference to a slice of gs, is live while its
// second argument clears gs (§5.2).
fn g(p: i64[:]&, n: i64) { print(p[0] + n); }
fn h(gs: i64[>..<]&) -> i64 {
    gs.clear();
    return 0;
}
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    g(s, h(gs));
}
