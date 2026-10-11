// error: cannot pop_n gs while r
// error: is still used: it may refer into gs
// A pop_n while a reference into the array is in scope (§5.2).
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let r = &gs[0];
    gs.pop_n(2);
    print(r);
}
