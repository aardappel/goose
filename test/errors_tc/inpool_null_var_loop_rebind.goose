// error: a relative reference in cells must point into cells
// error: this reference is rooted at spare
// `v` is null on the first iteration only: the rebind after the push hands
// every later one a reference into spare (§9.2), which no link into cells
// encodes (§3.9).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [Cell { key: 1 }];
fn main() {
    var v: Cell&? = null;
    for i in 3 {
        cells.push(Cell { key: i, next: v });
        v .= &spare[0];
    }
}
