// error: storing a reference into x, which holds a grow-shrink array
// keep_cell is checked first for e.r, a slot read that never points into a
// grow-shrink array (§5.2), which it may store; given an element of
// scratch, whose argument does, it is checked again, and may not.
struct Cell { v: i64 }
struct Row { r: Cell&, c: Cell }
var cells: Cell[>..] = [Cell { v: 1 }];
var rows: Row[>..<] = [];
var scratch: Cell[>..<] = [Cell { v: 2 }];
var other: (Cell&)[>..] = [];
fn keep_cell(x: Cell&) { other.push(x); }
fn via(e: Row&) { keep_cell(e.r); }
fn main() {
    rows.push(Row { r: cells[0], c: Cell { v: 3 } });
    via(rows[0]);
    keep_cell(scratch[0]);
    print(other.len);
}
