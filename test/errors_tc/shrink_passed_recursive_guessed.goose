// error: cannot call f: it may shrink G while s is still used, and s may refer into G
// A call into a recursive cycle still being checked counts as shrinking
// every grow-shrink array (§5.2, §7.8); here the inner call's clear is real.
var G: i64[>..<] = [1, 2, 3];
recursive fn f(s: i64[:], n: i64) {
    if n > 0 {
        f(s, n - 1);
        print(s);
    } else {
        G.clear();
        G.push(9);
    }
}
fn main() { f(G[..], 1); }
