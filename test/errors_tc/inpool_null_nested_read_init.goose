// error: a relative reference in cells must point into cells
// error: this reference is rooted at spare
// A block written in a global initializer reads v, a local of it, as the
// null it is on the loop's first iteration (§9.5); the rebind at the end of
// the loop points it into spare, so apply is checked with the block again
// for the next pass, where no link into cells encodes v (§3.9, §7.6).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn apply<F>() { F(); }
let made = block {
    var v: Cell&? = null;
    for i in 2 {
        apply() { cells.push(Cell { key: 1 + i, next: v }); };
        v .= &spare[0];
    }
    cells.len
};
fn main() { print(made); }
