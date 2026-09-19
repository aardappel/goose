// error: storing a reference into s, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// wrap's parameter stands for G[..] at the second call, so the literal it
// returns would hold a view into G there (§5.2); at the first it stands for
// a fixed array, which a literal may hold.
struct H { s: i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn wrap(s: i64[:]) -> H { H { s } }
fn main() {
    var o: i64[2] = [4, 5];
    print(wrap(o[..]).s);
    let x = wrap(G[..]);
    G.clear();
    G.push(7);
    print(x.s);
}
