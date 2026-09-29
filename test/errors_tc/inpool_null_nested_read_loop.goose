// error: a relative reference in cells must point into cells
// error: this reference is rooted at spare
// The block reads v as the null it is on the first iteration (§9.5); bind,
// called after it, points v into spare, and the loop's next pass sees that
// rebind, so apply is checked with the block again for the iterations after
// the first, where no link into cells encodes v (§3.9, §9.2).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn apply<F>() { F(); }
fn main() {
    var v: Cell&? = null;
    fn bind() { v .= &spare[0]; }
    for i in 2 {
        apply() { cells.push(Cell { key: 1 + i, next: v }); };
        bind();
    }
}
