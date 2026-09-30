// error: cannot clear G while v is still used: what it views may hold a reference or slice into it
// emit stored a view of G into the holders it then stored where v is read
// from (§5.1).
struct H { s: const u8[:] }
var G: u8[>..] = [];
fn emit(wss: (H[:])[:], hs: H[:]) {
    hs[0].s = G[..];
    wss[0] = hs;
    let v = wss[0];
    G.clear();
    G.append("ZZZZZZZZ");
    print(v[0].s);
}
fn main() {
    G.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "h" });
    var outer: (H[:])[>..] = [];
    outer.push(hs[..]);
    emit(outer[..], hs[..]);
}
