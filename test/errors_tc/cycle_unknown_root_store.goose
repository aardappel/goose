// A cycle's return roots are the fixpoint over its returns (§7.8). `either`
// returns one of two parameters' pools, so which root that is depends on the
// call site and no scan of the source can say; the back edge's result then
// carries the root that outlives nothing, and storing it into a global is
// rejected. (Passing it down would be fine.)
struct Cell { v: i64 }

var slot: Cell&? = null;

fn either(a: Cell[>..]&, b: Cell[>..]&, n: i64) -> Cell& {
    if n > 0 { return a.push(Cell { v: n }); }
    return b.push(Cell { v: 0 });
}

recursive fn f(p: Cell[>..]&, n: i64) -> Cell& {
    if n > 1 { slot .= f(p, n - 1); }
    return either(p, p, n);
}

fn main() {
    var pool: Cell[>..] = [];
    let r: Cell& = f(&pool, 3);
    print(r.v);
}
