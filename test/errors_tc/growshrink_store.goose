// error: storing a reference into gs, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A slice of a grow-shrink array may not be stored (§5.2).
struct H { s: i64[:] }
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let h = H { s: gs[0..2] };
    print(h.s.len);
}
