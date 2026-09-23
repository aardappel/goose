// error: storing a reference into gs, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A global slice variable is a global, which a view into a grow-shrink array
// is never stored in (§5.2): no shrink of gs looks at it.
var gs: i64[>..<] = [];
var base: i64[1] = [0];
var gv: i64[:] = base[..];
fn main() {
    gs.push(1);
    gv = gs[..];
    gs.clear();
    print(gv.len);
}
