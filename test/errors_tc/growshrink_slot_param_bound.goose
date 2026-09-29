// error: storing a reference rooted at p, which does not outlive the destination
// What p.r points at is the caller's storage behind p, which a read back
// out of loc keeps as a bound (§9.5), though the grow-shrink array in that
// storage cannot own it: the bound does not outlive the global other, and
// mine, which p.r points into here, is main's.
struct Cell { v: i64 }
struct Bag { r: Cell&, items: Cell[>..<] }
var cells: Cell[>..] = [Cell { v: 1 }];
var other: (Cell&)[>..] = [];
fn f(p: Bag&) {
    var loc: (Cell&)[>..] = [];
    loc.push(p.r);
    other.push(loc[0]);
}
fn main() {
    var mine: Cell[1] = [Cell { v: 5 }];
    var bag = Bag { r: mine[0], items: [] };
    f(bag);
    print(other[0].v);
}
