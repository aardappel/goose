// error: cannot pop cells while keep is still used: a reference into it was stored there
// A holder read out of rows may be stored into keep (§5.2), and what it
// holds points into cells, a grow-only array: the store record follows the
// copy to what was stored into rows (§5.1).
struct Cell { v: i64 }
struct Row { r: Cell&, c: Cell }
fn main() {
    var cells: Cell[>..] = [Cell { v: 1 }];
    var rows: Row[>..<] = [];
    var keep: Row[>..] = [];
    rows.push(Row { r: cells[0], c: Cell { v: 2 } });
    keep.push(rows[0]);
    cells.pop();
    print(keep[0].r.v);
}
