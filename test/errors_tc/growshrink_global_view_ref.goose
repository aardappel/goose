// error: storing a reference into gs, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// Rebinding a global reference stores into a global (§5.2).
var gs: i64[>..<] = [1, 2];
var zero = 0;
var gr: i64& = zero;
fn main() {
    gr .= gs[0];
    gs.clear();
    print(gr);
}
