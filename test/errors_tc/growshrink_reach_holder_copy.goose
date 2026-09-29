// error: storing a reference that may point into a grow-shrink array reached through tops's references
// A holder copied out of tops holds what tops's elements hold, bounded by
// tops (§9.2); an element of the array its reference leads to lies in mine,
// whose slot the pop frees and the push reuses (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn copy_of(t: Top) -> Top { t }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    var loc: (const Cell&)[>..] = [];
    loc.push(copy_of(tops[0]).a[0]);
    mine.pop();
    mine.push(Cell { v: 99 });
    print(loc[0].v);
}
