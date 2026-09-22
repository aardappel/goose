// error: cannot call f: it shrinks G while s is still used, and s may refer into G
// The callee's own view of a global outlives a shrink of its parameter's
// array, which the caller makes that global (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn f(a: i64[>..<]&) {
    let s = G[..];
    a.clear();
    a.push(9);
    print(s);
}
fn main() { f(G); }
