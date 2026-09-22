// error: cannot call f: it shrinks G while h is still used, and h may refer into G
// A parameter holding a slice holds whatever its argument's slice points
// into (§5.1, §9.2).
struct H { s: u8[:] }
var G: u8[>..] = [];
fn f(h: H) {
    G.clear();
    G.append("zz");
    print(h.s);
}
fn main() {
    G.append("abcd");
    f(H { G[..] });
}
