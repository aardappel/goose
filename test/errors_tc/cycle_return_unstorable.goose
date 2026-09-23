// error: this return may point into a grow-shrink array, but the recursive cycle already used a result that does not
// A back edge's result may be stored where every root the cycle's scan
// predicts could be (§7.8), so a real return the prediction missed, checked
// after it, may not be one no store may keep. The scan resolves both calls
// of `pick` to the first nested function of that name, a global's element;
// the second returns an element of the grow-shrink `spare` (§5.2).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var others: Cell[>..] = [];
var spare: Cell[>..<] = [];
var kept: Cell&? = null;

recursive fn f(k: i64) -> Cell& {
    if k == 0 {
        fn pick() -> Cell& { return cells[0]; }
        return pick();
    }
    if k == 1 { return others[0]; }
    fn pick() -> Cell& { return spare[0]; }
    let r: Cell& = f(k - 1);
    kept .= r;
    return pick();
}

fn main() {
    cells.push(Cell { v: 1 });
    others.push(Cell { v: 3 });
    spare.push(Cell { v: 2 });
    print(f(3).v);
    spare.clear();
    let k = kept;
    if k { print(k.v); }
}
