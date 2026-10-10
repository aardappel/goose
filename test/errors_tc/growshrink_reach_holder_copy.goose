// error: storing a reference that may point into mine, which holds a grow-shrink array
// A holder copied out of tops holds what was stored into tops, a reference to
// mine or to theirs (§9.2, §9.5); an element of the array its reference leads
// to lies in one of them, whose slot the pop frees and the push reuses (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn copy_of(t: Top) -> Top { t }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    var theirs: Cell[>..<] = [Cell { v: 2 }];
    tops.push(Top { a: mine });
    tops.push(Top { a: theirs });
    var loc: (const Cell&)[>..] = [];
    loc.push(copy_of(tops[0]).a[0]);
    mine.pop();
    mine.push(Cell { v: 99 });
    print(loc[0].v);
}
