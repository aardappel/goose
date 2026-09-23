// error: storing a reference into gs, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// An unannotated global takes its value's type, and is a global all the
// same (§5.2).
var gs: i64[>..<] = [1, 2];
let gv = gs[..];
fn main() {
    gs.clear();
    print(gv.len);
}
