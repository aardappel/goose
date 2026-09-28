// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// A const array's whole-array slice is a const u8[:] (§9.5), which a slot
// declared u8[:] does not take.
const table: u8[3] = [7, 8, 9];
fn main() {
    var buf: u8[4] = [1, 2, 3, 4];
    let c = buf[0] > 0;
    var s: u8[:] = if c { buf[..] } else { table };
    print(s);
}
