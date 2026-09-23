// error: storing a reference that may point into G, which holds a grow-shrink array
// A call's result is rooted at the deepest of the roots its callee's returns
// give (§9.2), the caller's `b` here, but may still be the view of G the
// other return gives, which is never stored (§5.2): G's clear would leave it
// behind in `hs`.
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }

fn pick(s: const u8[:], k: bool) -> const u8[:] {
    if k { return G[..]; }
    return s;
}

fn main() {
    var b: u8[3] = [97, 98, 99];
    var hs: K[>..] = [];
    hs.push(K { s: pick(b[..], true) });
    G.clear();
    G.append("pqr");
    print(hs[0].s);
}
