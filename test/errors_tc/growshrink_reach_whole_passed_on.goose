// error: storing a reference into arr, which holds a grow-shrink array
// t.a, read out of the caller's storage behind t, is a whole grow-shrink
// array, so arr's class holds one (§5.2): an element of it is never stored.
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn keep(arr: Cell[>..<]&, loc: (Cell&)[>..]&) { loc.push(arr[0]); }
fn f(t: Top&) {
    var loc: (Cell&)[>..] = [];
    keep(t.a, loc);
    t.a.pop();
    t.a.push(Cell { v: 99 });
    print(loc[0].v);
}
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    f(tops[0]);
}
