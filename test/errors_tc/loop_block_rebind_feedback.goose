// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// The block apply calls after the push gives v spare as well as cells, so
// the next iteration's push may link into spare (§3.9): the loop's next pass
// sees the rebind, which is checked inside apply (§7.6, §9.2). The push
// before the loop has put v's cells into cells already, so no other fact
// changes in the first.
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn apply<F>() { F(); }
fn main() {
    var v: Cell&? = pick(true, cells[0]);
    cells.push(Cell { key: 2, next: v });
    for i in 2 {
        cells.push(Cell { key: 3 + i, next: v });
        apply() { v .= pick(true, spare[0]); };
    }
}
