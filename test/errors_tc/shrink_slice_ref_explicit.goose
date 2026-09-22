// error: cannot clear gs while p
// error: is still used: the slice it refers to may point into gs
// An explicit `&s` names the slice variable itself (§3.8), which the callee
// only knows as the caller's storage: its slice may point into any array
// that outlives s, gs included (§5.2).
fn peek(p: i64[:]&, gs: i64[>..<]&) {
    gs.clear();
    print(p[0]);
}
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    peek(&s, gs);
}
