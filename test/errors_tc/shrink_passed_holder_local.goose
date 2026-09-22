// error: cannot call f: it shrinks G while h is still used, and h may refer into G
// A holder the callee fills from its parameter, known by its record of
// stores (§5.1, §9.2).
struct H { s: u8[:] }
var G: u8[>..] = [];
fn f(s: u8[:]) {
    let h = H { s };
    G.clear();
    G.append("zz");
    print(h.s);
}
fn main() {
    G.append("abcd");
    f(G[..]);
}
