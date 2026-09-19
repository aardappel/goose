// error: storing a reference into G, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// An element bound by reference into a literal's field is stored there like
// a slice would be (§5.2).
struct R { r: i64& }
var G: i64[>..<] = [1, 2, 3];
fn f(x: R) {
    G.clear();
    G.push(9);
    print(x.r);
}
fn main() { f(R { G[2] }); }
