// error: storing a reference into s, which holds a grow-shrink array
// A string literal and a view of the grow-shrink G are the argument's two
// roots (§9.2): the parameter's class is G's, static data being no array to
// name, so keepit may not store it (§5.2).
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
