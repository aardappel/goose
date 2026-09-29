// error: a relative reference in cells must point into cells
// error: this reference is rooted at spare
// `v` is null at the first call to keep, which reads it as that null (§9.5),
// and bound into spare at the second, which is checked for what it finds
// then (§7.5): no link into cells encodes that (§3.9).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [];
fn maybe(c: bool, x: Cell&) -> Cell&? {
    if c { return x; }
    return null;
}
fn main() {
    spare.push(Cell { key: 1 });
    var v: Cell&? = null;
    fn keep() { cells.push(Cell { key: 2, next: v }); }
    keep();
    v .= maybe(true, spare[0]);
    keep();
}
