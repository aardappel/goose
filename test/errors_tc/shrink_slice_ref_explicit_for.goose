// error: cannot clear gs while x
// error: is still used: it may refer into gs
// A `for` through a reference to a slice iterates that slice: x refers into
// gs, whatever names the slice variable (§5.2, §6.5).
fn main() {
    var gs: i64[>..<] = [42, 43];
    var s = gs[..];
    let p = &s;
    for &x in p {
        gs.clear();
        print(x);
    }
}
