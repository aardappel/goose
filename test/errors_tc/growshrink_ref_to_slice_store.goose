// error: storing a reference into G, which holds a grow-shrink array
// A reference to a slice variable leads to what the slice views, the
// grow-shrink G here: storing it keeps a way into G that no shrink of G
// looks at (§5.2).
var G: u8[>..<] = "xyz";
struct R { p: u8[:]& }
fn main() {
    var s: u8[:] = G[..];
    var h = R { p: s };
    G.clear();
    G.append("pqr");
    print(h.p);
}
