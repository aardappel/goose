// error: cannot call f: it shrinks G while s is still used, and s may refer into G
// The same for a grow-only global: the append lands where the slice still
// points (§5.1).
var G: u8[>..] = [];
fn f(s: u8[:]) {
    G.clear();
    G.append("zz");
    print(s);
}
fn main() {
    G.append("abcd");
    f(G[..]);
}
