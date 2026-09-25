// error: storing a reference rooted at mine, which does not outlive the destination
// A back edge's result has every root the cycle's returns give (§7.8): the
// first `pick` returns a global's element, the second an element of `mine`,
// which dies with `walk`, so the result may not be kept in the global kept.
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
        let r: Cell& = f(k - 1);
        kept .= r;
        return pick();
    }
    print(f(n).v);
}

fn main() {
    cells.push(Cell { v: 1 });
    walk(2);
    let k = kept;
    if k { print(k.v); }
}
