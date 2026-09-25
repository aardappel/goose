// error: cannot call f: it shrinks G while s is still used, and s may refer into G
// A call into a recursive cycle applies what the cycle's first round
// recorded (§5.2, §7.8): here the inner call's clear.
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
