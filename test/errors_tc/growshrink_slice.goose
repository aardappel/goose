// error: cannot clear gs while s
// error: is still used: it may refer into gs
// A shrink while a slice of the array is in scope (§5.2).
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let s = gs[0..2];
    gs.clear();
    print(s.len);
}
