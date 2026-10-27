// error: storing a reference into mine, which holds a grow-shrink array
// tops holds only a Top for mine, so its Tops are bound to mine exactly
// (§9.2): first's result, read out of the storage behind t, maps back at
// the call to mine itself rather than to tops as a bound, and a reference
// into mine is never stored. The pop frees its slot and the push reuses it
// (§5.2).
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
