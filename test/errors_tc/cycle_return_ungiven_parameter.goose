// error: storing a reference rooted at loc, which does not outlive the destination
// A back edge gives each parameter its own argument (§7.8): the second
// `pick` returns `a`, which the back edge passes its own local for, so the
// result may be rooted at loc and may not be kept in the global kept.
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
