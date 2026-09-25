// error: storing a reference that may point into spare, which holds a grow-shrink array
// A back edge's result has every root the cycle's returns give (§7.8): the
// second `pick` returns an element of the grow-shrink `spare`, which no
// store may keep (§5.2).
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
