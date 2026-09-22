// error: cannot clear gs while p
// error: is still used: the slice it refers to may point into gs
// A reference taken with an explicit `&` of a slice variable reaches the
// variable's slice (§3.8, §5.2).
fn main() {
    var gs: i64[>..<] = [42];
    var s = gs[..];
    let p = &s;
    gs.clear();
    print(p[0]);
}
