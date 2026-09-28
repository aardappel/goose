// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// A for's copy of an element is read-only, and so is a view of it (§6.5,
// §9.5).
struct R { name: u8[..8] }
fn main() {
    var rows: R[>..] = [];
    rows.push(R { name: "a" });
    for r in rows {
        var w: u8[:] = r.name;
        w[0] = 66;
    }
}
