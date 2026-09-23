// error: storing a reference that may point into G, which holds a grow-shrink array
// A back edge reuses f's body whatever it passes (§7.8), here a value that
// may be the view of the grow-shrink G where the entry call passed x's. Its
// result, the parameter's pointee, may then be G's view too, and so may f's
// own, which main may not store (§5.2).
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }
recursive fn f(s: const u8[:], n: i64) -> const u8[:] {
    if n == 0 { return s; }
    return f(if n > 1 { s } else { G[..] }, n - 1);
}
fn main() {
    var x: u8[3] = [97, 98, 99];
    var hs: K[>..] = [];
    hs.push(K { s: f(x[..], 1) });
    G.clear();
    G.append("pqr");
    print(hs[0].s);
}
