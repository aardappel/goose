// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// Any function may bind a global `var`, and a body checked once serves every
// later call: link's check has to hold after rebind has bound `last` into
// spare, so a read of it is the read-back rule's answer for a global (§9.5),
// which may point into cells or spare, not a null (§3.9).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [Cell { key: 1 }];
var last: Cell&? = null;
fn link() { cells.push(Cell { key: 2, next: last }); }
fn rebind() { last .= &spare[0]; }
fn main() {
    link();
    rebind();
    link();
}
