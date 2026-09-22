// error: cannot clear gs while p
// error: is still used: the slice it refers to may point into gs
// A local reference to a slice variable keeps its slice live (§5.2).
fn main() {
    var gs: i64[>..<] = [];
    gs.push(42);
    var s = gs[..];
    let p: i64[:]& = s;
    gs.clear();
    print(p[0]);
}
