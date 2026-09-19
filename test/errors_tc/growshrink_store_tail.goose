// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A literal's field is storage wherever the literal lands (§5.2), here the
// value of mk's body, which x would keep viewing G through across the clear.
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn mk() -> H { H { G[..] } }
fn main() {
    let x = mk();
    G.clear();
    G.push(7);
    print(x.s);
}
