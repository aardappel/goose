// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A literal that is an element of another literal is storage wherever the
// outer one lands, an argument here (§5.2).
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn f(hs: H[2]) {
    G.clear();
    G.push(9);
    print(hs[0].s, hs[1].s);
}
fn main() { f([H { G[..1] }, H { G[1..] }]); }
