// error: a relative reference in cells must point into cells
// error: this reference is rooted at spare
// v is one variable at every level of rec's recursion, null until a level
// rebinds it to a recursive call's result: rec's second call pushes it at
// the next level, where it may point into spare (§3.9). The cycle's rounds
// go on until a round changes v no more (§7.8), and that round's push
// reads it bound.
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn main() {
    var v: Cell&? = null;
    recursive fn rec(n: i64) -> Cell&? {
        if n <= 0 { return null; }
        cells.push(Cell { key: n, next: v });
        v .= rec(n - 1);
        rec(n - 1);
        return pick(true, spare[0]);
    }
    rec(2);
}
