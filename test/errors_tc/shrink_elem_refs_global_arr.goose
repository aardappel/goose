// error: cannot call f: it shrinks G while v is still used, and v may refer into G
// v's elements lie in the caller's storage, which f leaves its callers to
// judge: here ss, whose element views G (§5.1).
var G: u8[>..] = [];
fn f(v: (u8[:])[:]) {
    G.clear();
    G.append("ZZZZZZZZ");
    print(v[0]);
}
fn main() {
    G.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(G[..]);
    f(ss[..]);
}
