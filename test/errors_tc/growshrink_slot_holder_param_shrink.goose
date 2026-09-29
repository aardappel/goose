// error: cannot pop cells while keep is still used: a reference into it was stored there
// k may store r, read out of an element of rows, into the caller's keep
// (§5.2): the call puts what rows holds there, which points into cells, a
// grow-only array (§5.1).
struct Cell { v: i64 }
struct Row { r: Cell&, c: Cell }
fn k(r: Row, keep: Row[>..]&) { keep.push(r); }
fn main() {
    var cells: Cell[>..] = [Cell { v: 1 }];
    var rows: Row[>..<] = [];
    var keep: Row[>..] = [];
    rows.push(Row { r: cells[0], c: Cell { v: 2 } });
    k(rows[0], keep);
    cells.pop();
    print(keep[0].r.v);
}
