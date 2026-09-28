// error: cannot pop garr: the value, evaluated earlier in the statement, may still refer into garr
// Branches joined as a slice view the array itself (§6.4), so the call
// still holds a view of garr when its later argument pops it (§5.1).
var garr: u8[>..] = [];
fn show(s: const u8[:], last: u8) { print(s, " ", last); }
fn main() {
    garr.push(65);
    garr.push(66);
    let c = garr.len > 1;
    show(if c { garr } else { "x" }, garr.pop());
}
