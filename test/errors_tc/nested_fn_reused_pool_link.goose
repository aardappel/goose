// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// put's first call finds v pointing into cells, and stores it as a link
// within cells. pick's second result joins spare to v's roots, which the
// optional's rebind leaves un-narrowed, so the second call is checked again:
// its link may be to an element of spare (§3.9, §7.5).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [];
struct L { r: Cell&<u64 in cells>?, n: i64 }
var links: L[>..] = [];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn run() {
    var v: Cell&? = pick(true, cells[0]);
    fn put() { links.push(L { r: v, n: 1 }); }
    put();
    v .= pick(true, spare[0]);
    put();
}
fn main() {
    cells.push(Cell { v: 5 });
    spare.push(Cell { v: 6 });
    run();
    let r = links[1].r;
    if r { print(r.v); }
}
