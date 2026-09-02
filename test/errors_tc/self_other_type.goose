// A `self` field must point at the literal's own type (§3.9); this one is
// declared to hold a relative reference to a different struct.
struct Other { w: i64 }
struct Cell { v: i64, link: Other&<u16> }
fn main() {
    var pool: Cell[>..] = [];
    pool.push(Cell { v: 1, link: self });
}
