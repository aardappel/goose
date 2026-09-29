// error: function pick cannot return Cell&<u32 in cells>?: a result is a value, never a relative reference
// A relative reference is storage (§3.9): a function returns the plain
// reference, Cell? here, and a relative slot receiving it encodes it.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
fn pick(k: i64) -> Cell&<u32 in cells>? {
    if k < 0 { return null; }
    cells[k]
}
fn main() {
    cells.push(Cell { v: 1 });
    let r: Cell&<u32 in cells>? = pick(0);
    guard r else { return; }
    print(r.v);
}
