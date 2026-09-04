// A recursive cycle may store references rooted at a pool parameter (no
// cycle function can own the pool, so it outlives every activation), but
// only while every back edge passes the same pools the entry call did (§7.8):
// swapping two pools would let a relative link cross from one into the other.
struct Cell { v: i64, link: Cell&<u32>? }
recursive fn build(a: Cell[>..]&, b: Cell[>..]&, n: i64) -> Cell& {
    if n == 0 { return a.push(Cell { v: 0 }); }
    let prev: Cell& = build(b, a, n - 1);
    return a.push(Cell { v: n, link: prev });
}
fn main() {
    var p: Cell[>..] = [];
    var q: Cell[>..] = [];
    let r: Cell& = build(p, q, 3);
    print(r.v);
}
