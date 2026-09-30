// error: cannot clear G while v is still used: the slices it views may point into it
// v's elements lie in the caller's storage, which may hold a view of G
// (§5.1).
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
