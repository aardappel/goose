// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// b, a member of a's cycle, reads v; a calls it twice, and the second call
// shares the check the first made in the same round, which found v with
// the spare a's rebind gave it in the round before. So does the push after
// both, which may link into spare (§3.9, §7.8).
struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [Cell { key: 0 }];
var spare: Cell[>..] = [Cell { key: 1 }];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn main() {
    var v: Cell&? = pick(true, cells[0]);
    recursive fn a(n: i64) {
        if n <= 0 { return; }
        b(n);
        b(n);
        cells.push(Cell { key: n, next: v });
        v .= pick(true, spare[0]);
    }
    recursive fn b(n: i64) {
        if v { print(n); }
        if n > 100 { a(n - 1); }
    }
    a(2);
}
