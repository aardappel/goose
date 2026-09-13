// error: cannot pop gs while x
// error: is still used: it may refer into gs
// The &x binding is a reference in scope (§5.2).
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    for &x in gs {
        if x == 2 { gs.pop(); }
    }
}
