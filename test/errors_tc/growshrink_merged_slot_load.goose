// error: storing a reference that may point into G, which holds a grow-shrink array
// A slice loaded through a reference to a slice variable is the slice the
// variable holds (§3.8), with everything its binding says: here that it may
// be the view of the grow-shrink G one branch gives (§9.2), which is never
// stored (§5.2).
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }

fn main() {
    var b: u8[3] = [97, 98, 99];
    var hs: K[>..] = [];
    var s: const u8[:] = if hs.len == 0 { G[..] } else { b[..] };
    let r = &s;
    hs.push(K { s: r });
    G.clear();
    G.append("pqr");
    print(hs[0].s);
}
