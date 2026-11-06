// error: cannot clear other while rows is still used
// A nested function stores a copy of its own local holder into the parent's
// rows, called through another nested function (§5.1).
import std;
struct Row { v: const u8[:] }
fn main() {
    var other: u8[>..] = [];
    other.push('o');
    var rows: Row[>..] = [];
    fn put(x: const u8[:]) { var z = Row { x }; rows.push(z); }
    fn relay(x: const u8[:]) { put(x); }
    relay(other[..]);
    other.clear();
    print(rows[0].v);
}
