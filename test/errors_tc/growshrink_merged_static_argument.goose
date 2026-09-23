// error: storing a reference that may point into a grow-shrink array, as s may
// A string literal and a view of the grow-shrink G tie, and the argument is
// rooted at static data (§9.2), which gives the parameter no class of its
// own. It may still be G's view, which keepit may not store (§5.2).
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }
var keep: K[>..] = [];
fn keepit(s: const u8[:]) { keep.push(K { s: s }); }
fn main() {
    for k in 2 {
        keepit(if k == 0 { "abc" } else { G[..] });
    }
    G.clear();
    G.append("pqr");
    print(keep[0].s, " ", keep[1].s);
}
