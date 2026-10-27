// error: storing a reference that may point into a grow-shrink array reached through tops's references
// first's result is rooted at its argument's root, tops, only as a bound: it
// was read out of the storage behind t. tops holds none, but its references
// lead to mine or theirs: the pop frees a slot of mine and the push reuses
// it (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn first(t: Top&) -> Cell& { t.a[0] }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var theirs: Cell[>..<] = [Cell { v: 2 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    tops.push(Top { a: theirs });
    var loc: (Cell&)[>..] = [];
    loc.push(first(tops[0]));
    mine.pop();
    mine.push(Cell { v: 99 });
    print(loc[0].v);
}
