// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// `v` is null at the first call to keep, but a nested function's body reads
// its parent's null-only optional as the read-back rule's answer (§9.5),
// which may point into cells or spare, not as a null (§3.9).
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
