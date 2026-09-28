// error: storing a reference into gs, which holds a grow-shrink array
// A view of a grow-shrink array lives only in variables (§5.2), whichever way
// it is spelled (§3.10).
struct H { s: const u8[:] }
fn main() {
    var gs: u8[>..<] = [];
    gs.push(1);
    var h = H { s: "x" };
    h.s = gs;
    print(h.s);
}
