// error: cannot call show: it shrinks G while t is still used, and t may refer into G
// Each case of a dispatch is checked against the call's arguments (§5.2,
// §8.2).
enum Shape { Circle { r: i64 }, Point { k: i64 } }
var G: i64[>..<] = [1, 2, 3];
fn show(s: Shape.Circle, t: i64[:]) {
    G.clear();
    G.push(9);
    print(t);
}
fn show(s: Shape.Point, t: i64[:]) { print(t); }
fn main() {
    let x: Shape = Shape.Circle { 1 };
    show(x, G[..]);
}
