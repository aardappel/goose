// error: storing a reference rooted at p, which does not outlive the destination
// A cycle's result has every root its returns give (§7.8): here an element
// pushed into p, which the global slot outlives. (Passing it down would be
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
