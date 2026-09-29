// error: default<Cell&>() does not exist: Cell& is a non-optional reference
// A dictionary fills its unused slots with default<V>(), which a
// non-optional reference has none of: its reference values are optional
// (`Cell?`).
import dictionary;
struct Cell { v: i64 }
var cells: Cell[>..] = [];
fn main() {
    cells.push(Cell { v: 1 });
    var d = dictionary<i64, Cell&> {};
    d.insert(1, cells[0]);
    print(d.count);
}
