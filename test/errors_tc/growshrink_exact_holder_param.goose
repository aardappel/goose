// error: storing a reference into t, which holds a grow-shrink array
// tops holds only a Top for mine, so its Tops are bound to mine exactly
// (§9.2), and pass's parameter keeps that record for its elements: f's t,
// one of them passed by value, holds a reference to mine, a grow-shrink
// array, so an element of t.a is never stored. The pop frees its slot and
// the push reuses it (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn f(t: Top) {
    var loc: (Cell&)[>..] = [];
    loc.push(t.a[0]);
    t.a.pop();
    t.a.push(Cell { v: 99 });
    print(loc[0].v);
}
fn pass(ts: Top[>..]&) { f(ts[0]); }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    pass(tops);
}
