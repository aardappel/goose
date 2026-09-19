// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// The value of an `if` is a copy of what its branch built (§9.2), and the
// literal there is storage wherever that copy lands (§5.2).
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn f(h: H) {
    G.clear();
    G.push(9);
    print(h.s);
}
fn main() {
    for k in 2 { f(if k > 0 { H { G[1..] } } else { H { G[..] } }); }
}
