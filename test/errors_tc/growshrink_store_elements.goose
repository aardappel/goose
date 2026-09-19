// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// An array literal's elements are storage wherever it lands (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn f(views: i64[:][2]) {
    G.clear();
    G.push(9);
    print(views[0], views[1]);
}
fn main() { f([G[..1], G[1..]]); }
