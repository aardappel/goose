// error: a relative reference in cells must point into cells
// error: this reference is rooted at spare
// A null branch names no array (§9.2), but the other branch still says where
// the merged value points: into spare, which no link into cells encodes.
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [Cell { key: 1 }];
fn main() {
    let m: Cell&? = if spare.len > 3 { default<Cell&?>() } else { spare[0] };
    cells.push(Cell { key: 2, next: m });
}
