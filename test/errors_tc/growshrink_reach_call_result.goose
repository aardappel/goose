// error: storing a reference that may point into a grow-shrink array reached through tops's references
// first's result is rooted at its argument's root, tops, only as a bound: it
// was read out of the storage behind t. tops holds none, but its references
// lead to mine, whose slot the pop frees and the push reuses (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn first(t: Top&) -> Cell& { t.a[0] }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    var loc: (Cell&)[>..] = [];
    loc.push(first(tops[0]));
    mine.pop();
    mine.push(Cell { v: 99 });
    print(loc[0].v);
}
