// error: this return may point where a parameter other than its root's does, which the recursive cycle's back edges were not given
// The back edge's result, rooted at the globals x's argument may view, was
// stored (§7.8). The last return is rooted at x too, which the entry call
// gives the deeper argument, but may be an element of the pool, a local of
// run that kept outlives.
struct Cell { v: i64 }
var gcells: Cell[>..] = [];
var gcells2: Cell[>..] = [];
var kept: Cell&? = null;
var flag = true;

recursive fn f(pool: Cell[>..]&, x: Cell&, n: i64) -> Cell& {
    if n == 0 { return x; }
    if n == 2 {
        let g: Cell& = if flag { gcells[0] } else { gcells2[0] };
        let res: Cell& = f(pool, g, 1);
        kept .= res;
        return x;
    }
    return if n > 5 { x } else { pool[0] };
}

fn run() {
    var pool: Cell[>..] = [];
    pool.push(Cell { v: 9 });
    {
        var cell = Cell { v: 1 };
        print(f(pool, cell, 2).v);
    }
}

fn main() {
    gcells.push(Cell { v: 5 });
    gcells2.push(Cell { v: 6 });
    run();
    let k = kept;
    if k { print(k.v); }
}
