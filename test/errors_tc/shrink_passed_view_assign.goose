// error: cannot call f: it shrinks G while s is still used, and s may refer into G
// Assigning the global whole shrinks it too (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn f(s: i64[:]) {
    G = [];
    G.push(9);
    print(s);
}
fn main() { f(G[..]); }
