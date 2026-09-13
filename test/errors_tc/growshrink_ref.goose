// error: cannot pop gs while r
// error: is still used: it may refer into gs
// A shrink while a reference into the array is in scope (§5.2).
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let r = &gs[0];
    gs.pop();
    print(r);
}
