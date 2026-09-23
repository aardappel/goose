// error: storing a reference into s, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// s stands for the view of gs its caller passed (§5.2), which the global gv
// may not hold.
var gs: i64[>..<] = [];
var base: i64[1] = [0];
var gv: i64[:] = base[..];
fn set(s: i64[:]) { gv = s; }
fn main() {
    gs.push(1);
    set(gs[..]);
    gs.clear();
    print(gv.len);
}
