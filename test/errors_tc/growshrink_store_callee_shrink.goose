// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// f's shrink scan sees its reference and slice variables only (§5.2), never
// what a holder parameter holds, so the view may not reach f in a literal.
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn f(h: H) {
    G.clear();
    G.push(9);
    print(h.s);
}
fn main() { f(H { G[..] }); }
