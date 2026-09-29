// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// v is one variable at every level of rec's recursion: rebound to the result
// of a recursive call, it may point into spare where the next level pushes
// it (§3.9). The cycle's first round has no record of that result; the
// second has one and pushes v before rebinding v to it, so it is the third
// round, the first to find v changed by none, whose check stands (§7.8).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn main() {
    var v: Cell&? = pick(true, cells[0]);
    recursive fn rec(n: i64) -> Cell&? {
        if n <= 0 { return null; }
        cells.push(Cell { key: n, next: v });
        v .= rec(n - 1);
        rec(n - 1);
        return pick(true, spare[0]);
    }
    rec(2);
}
