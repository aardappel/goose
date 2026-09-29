// error: storing a reference into a, which holds a grow-shrink array
// a is given a reference read out of an element of tops, a slot read
// (§5.2), but one to the whole of mine: an element of it lies in a
// grow-shrink array, and is never stored.
struct Cell { v: i64 }
fn g(a: Cell[>..<]&) {
    var loc: (Cell&)[>..] = [];
    loc.push(a[0]);
    a.pop();
    print(loc[0].v);
}
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: (Cell[>..<]&)[>..] = [];
    tops.push(mine);
    g(tops[0]);
}
