// error: storing the result of a recursive call whose returned reference's root the cycle's returns do not determine
// error: it may only be passed down
// A cycle's return roots are predicted by a scan of its returns (§7.8), which
// does not resolve overload sets: `either` is one, so the prediction names
// no root, and the back edge's result carries the root that outlives
// nothing. Storing it into a global is rejected. (Passing it down would be
// fine.)
struct Cell { v: i64 }
struct Other { w: i64 }

var slot: const Cell&? = null;

fn either(a: Cell[>..]&, b: Cell[>..]&, n: i64) -> Cell& {
    if n > 0 { return a.push(Cell { v: n }); }
    return b.push(Cell { v: 0 });
}
fn either(a: Other[>..]&, b: Other[>..]&, n: i64) -> Other& { return a.push(Other { w: n }); }

recursive fn f(p: Cell[>..]&, n: i64) -> Cell& {
    if n > 1 { slot .= f(p, n - 1); }
    return either(p, p, n);
}

fn main() {
    var pool: Cell[>..] = [];
    let r: Cell& = f(pool, 3);
    print(r.v);
}
