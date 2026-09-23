// error: storing a reference rooted at mine, which does not outlive the destination
// A real return the cycle's scan did not predict (§7.8), checked before any
// back edge has used the prediction, joins it: the scan resolves both calls
// of `pick` to the first nested function of that name, a global's element,
// but the second is `mine`'s, so the back edge after it is rooted at `mine`
// and may not be stored in a global.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var kept: Cell&? = null;

fn walk(n: i64) {
    var mine: Cell[>..] = [];
    mine.push(Cell { v: 5 });
    recursive fn f(k: i64) -> Cell& {
        if k == 0 {
            fn pick() -> Cell& { return cells[0]; }
            return pick();
        }
        fn pick() -> Cell& { return mine[0]; }
        if k == 1 { return pick(); }
        let r: Cell& = f(k - 1);
        kept .= r;
        return r;
    }
    print(f(n).v);
}

fn main() {
    cells.push(Cell { v: 1 });
    walk(2);
    let k = kept;
    if k { print(k.v); }
}
