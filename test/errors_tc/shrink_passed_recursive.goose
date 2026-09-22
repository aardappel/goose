// error: cannot call b: it shrinks G while t is still used, and t may refer into G
// b is still being checked when a calls it back, and shrinks G only after
// that call: the call is checked again once the cycle is (§5.1, §7.8).
var G: u8[>..] = [];
recursive fn a(s: u8[:], n: i64) {
    if n > 0 { b(G[..], n - 1); }
}
recursive fn b(t: u8[:], n: i64) {
    a(t, n);
    G.clear();
    G.append("zz");
    print(t);
}
fn main() {
    G.append("abcd");
    var local: u8[>..] = [];
    local.append("q");
    b(local[..], 1);
}
