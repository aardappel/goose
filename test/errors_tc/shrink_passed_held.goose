// error: cannot call h: it shrinks G while s is still used, and s may refer into G
// An argument is still in use while the arguments after it run (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn clear_g() -> i64 {
    G.clear();
    G.push(9);
    return 0;
}
fn show(s: i64[:], n: i64) { print(s); }
fn h(s: i64[:]) { show(s, clear_g()); }
fn main() { h(G[..]); }
