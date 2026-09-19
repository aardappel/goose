// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// An argument passes no destination, since a parameter dies before its
// argument's roots, but the literal's field is storage all the same (§5.2):
// id hands the holder back, and x views G across the clear.
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn id(h: H) -> H { h }
fn main() {
    let x = id(H { G[..] });
    G.clear();
    G.push(7);
    print(x.s);
}
