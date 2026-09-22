// error: cannot call f: it shrinks G while h is still used, and h may refer into G
// The next iteration brings the view stored at the end of the loop body to
// the shrink of the parameter's array (§5.1).
struct H { s: u8[:] }
var G: u8[>..] = [];
fn f(a: u8[>..]&) {
    var local: u8[>..] = [];
    local.append("q");
    var h = H { local[..] };
    for i in 2 {
        a.clear();
        a.append("zz");
        print(h.s);
        h = H { G[..] };
    }
}
fn main() {
    G.append("abcd");
    f(G);
}
