// error: is still under construction, and the shrink would free the storage it is built in
// A shrink in a branch the construct joins as a slice is judged when the
// branches are checked again as the slice (§1.3(4), §6.4).
struct R { n: i64, data: u8[] }
var rows: R[>..] = [];
fn main() {
    rows.push(R { n: 1, data: "x" });
    var name: u8[..8] = "nm";
    let c = rows.len > 0;
    rows.push(R { n: (if c { rows.clear(); name } else { "none" }).len, data: "y" });
    print(rows.len);
}
