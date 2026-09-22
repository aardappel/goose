// error: cannot call set_g: it shrinks G while s is still used, and s may refer into G
// The array itself passed to a slice parameter is a view of all of it
// (§4.1, §4.4, §5.1).
var G: u8[>..] = [];
fn collect(s: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for c in s { out.push(c + 1); }
    for c in s { out.push(c + 2); }
    return out;
}
fn set_g(s: u8[:]) { G = collect(s); }
fn main() {
    G.append("abcd");
    set_g(G);
    print(G);
}
