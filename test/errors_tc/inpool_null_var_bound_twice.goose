// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly
// A variable bound in either branch holds either binding after them (§9.2):
// no longer null, it may point into spare as well as cells, so it is no
// link into cells (§3.9).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
reusable var spare: Cell[>..] = [];
fn main() {
    spare.push(Cell { key: 1 });
    var v: Cell&? = null;
    if spare.len > 0 { v .= &cells[0]; } else { v .= &spare[0]; }
    cells.push(Cell { key: 2, next: v });
}
