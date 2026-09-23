// error: this return's reference is rooted at a, which the recursive cycle's back edges were not given
// A back edge gives each parameter its own argument (§7.8), so a root it was
// not given may not join the prediction once one has used it, even one the
// entry call rooted at a global, as it did `a`. The scan resolves both calls
// of `pick` to the first nested function of that name, a global's element;
// the second returns `a`, which the back edge passes its own local for.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var gcell = Cell { v: 3 };
var kept: Cell&? = null;

recursive fn f(a: Cell&, k: i64) -> Cell& {
    if k == 0 {
        fn pick() -> Cell& { return cells[0]; }
        return pick();
    }
    fn pick() -> Cell& { return a; }
    var loc = Cell { v: 7 };
    let r: Cell& = f(loc, k - 1);
    kept .= r;
    return pick();
}

fn main() {
    cells.push(Cell { v: 1 });
    print(f(gcell, 2).v);
    let k = kept;
    if k { print(k.v); }
}
