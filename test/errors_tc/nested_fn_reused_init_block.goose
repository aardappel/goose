// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// The block is written in a global initializer and reads v, a local of it,
// which the rebind at the end of the loop gives spare as well as cells: the
// next pass finds v otherwise than the first did, so apply is checked with
// the block again rather than reused (§7.5, §7.6).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn apply<F>() { F(); }
let made = block {
    var v: Cell&? = cells[0];
    for i in 2 {
        apply() { cells.push(Cell { key: 1 + i, next: v }); };
        v .= &spare[0];
    }
    cells.len
};
fn main() { print(made); }
