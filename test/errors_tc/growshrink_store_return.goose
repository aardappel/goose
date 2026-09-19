// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A variant literal's field is storage too, returned or not (§5.2).
enum E { View { s: i64[:] }, Empty }
var G: i64[>..<] = [1, 2, 3];
fn show(e: E) { match e { View v => print(v.s), Empty => print(0), } }
fn mk() -> E { return E.View { G[..] }; }
fn main() {
    let x = mk();
    G.clear();
    G.push(7);
    show(x);
}
