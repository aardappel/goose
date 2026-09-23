// error: this return's reference is rooted at mine, deeper than the result the recursive cycle's back edges were given
// A back edge takes the roots the cycle's scan predicts its returns give
// (§7.8), and every real return is checked against what back edges were
// given. The scan resolves both calls of `pick` to the first nested function
// of that name, which returns a global's element, so the back edge's result
// is rooted at the global and stored in one; the second `pick` returns an
// element of `mine`, which dies with `walk`, after that result was used.
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
