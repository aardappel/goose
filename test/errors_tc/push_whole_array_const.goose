// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// A const array's whole-array slice is read-only (§9.5), which an element
// of type u8[:] does not take.
const table: u8[3] = [7, 8, 9];
fn main() {
    var parts: u8[:][>..] = [];
    parts.push(table);
    parts[0][0] = 42;
    print(table);
}
