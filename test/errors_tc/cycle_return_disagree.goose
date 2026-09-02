// All returns of one function must agree on the returned reference's root
// (§9.2). The cycle's fixpoint cannot predict a root here either, so the back
// edge's result is unusable, but the returns themselves are still checked:
// the second one is rooted at `b` where the first was rooted at `a`.
struct Cell { v: i64 }

recursive fn build(a: Cell[>..]&, b: Cell[>..]&, n: i64) -> Cell& {
    if n == 0 { return a.push(Cell { v: 0 }); }
    build(a, b, n - 1);
    return b.push(Cell { v: n });
}

fn main() {
    var p: Cell[>..] = [];
    var q: Cell[>..] = [];
    let r: Cell& = build(&p, &q, 3);
    print(r.v);
}
