// `self` cannot fill an optional relative reference: offset 0 is its null
// (§3.9), so a value pointing at itself would be indistinguishable from one.
struct Cell { v: i64, link: Cell&<u16>? }
fn main() {
    var pool: Cell[>..] = [];
    pool.push(Cell { v: 1, link: self });
}
