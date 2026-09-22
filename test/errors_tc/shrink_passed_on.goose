// error: cannot call pass: it shrinks G while t is still used, and t may refer into G
// A caller passing its own parameter on cannot tell yet; its callers can,
// by what they pass it (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn f(s: i64[:]) {
    G.clear();
    G.push(9);
    print(s);
}
fn pass(t: i64[:]) { f(t[1..]); }
fn main() {
    var local: i64[>..<] = [4, 5];
    pass(local[..]);
    pass(G[..]);
}
