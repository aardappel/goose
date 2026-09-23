// error: storing a reference that may point into a grow-shrink array, as s may
// What a call returns of its parameter's pointee may point wherever the
// argument may (§9.2): id's result may be the view of the grow-shrink G the
// argument's other branch gives, which is never stored (§5.2).
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }
fn id(s: const u8[:]) -> const u8[:] { return s; }
fn main() {
    var b: u8[3] = [97, 98, 99];
    var hs: K[>..] = [];
    for k in 2 {
        let v = if k == 0 { b[..] } else { G[..] };
        hs.push(K { s: id(v) });
    }
    G.clear();
    G.append("pqr");
    print(hs[1].s);
}
