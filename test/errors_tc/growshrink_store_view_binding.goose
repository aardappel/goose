// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A literal built only to read its field back holds the view for a while
// all the same, and a slice variable as the destination does not make its
// field a binding (§5.2).
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn main() {
    let s: i64[:] = H { G[..] }.s;
    print(s);
}
