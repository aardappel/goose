// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// A nested function's body is checked once for every call that reuses it, so
// what it reads of its parent's variables has to hold at the later calls
// too: `v` is null at the first call to keep, but the call after the rebind
// reuses that check. There a read of `v` is the read-back rule's answer
// (§9.5), which may point into cells or spare, not a null (§3.9).
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
