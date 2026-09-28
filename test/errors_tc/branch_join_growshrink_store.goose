// error: storing a reference into gs, which holds a grow-shrink array
// A view of a grow-shrink array lives only in variables (§5.2), the one a
// join gives included (§6.4).
struct Holder { s: const u8[:] }
fn main() {
    var gs: u8[>..<] = [];
    gs.push(65);
    let c = gs.len > 1;
    var h = Holder { s: if c { "x" } else { gs } };
    print(h.s);
}
