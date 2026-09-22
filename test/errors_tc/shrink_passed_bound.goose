// error: cannot call f: it shrinks G while t is still used, and t may refer into G
// A variable bound from the parameter points where the parameter does
// (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn f(s: i64[:]) {
    let t = s[1..];
    G.clear();
    G.push(9);
    G.push(8);
    print(t);
}
fn main() { f(G[..]); }
