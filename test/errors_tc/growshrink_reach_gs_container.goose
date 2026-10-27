// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// t's argument lies in a grow-shrink array of Tops, which cannot hold a
// Cell, and tops holds Tops for mine and theirs, so t.a is rooted at t's
// class only as a bound (§9.5): the Cell t.a[0] names lies in the array
// t.a leads to (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn f(t: Top&) {
    var loc: (Cell&)[>..] = [];
    loc.push(t.a[0]);
    t.a.pop();
    t.a.push(Cell { v: 99 });
    print(loc[0].v);
}
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var theirs: Cell[>..<] = [Cell { v: 2 }];
    var tops: Top[>..<] = [];
    tops.push(Top { a: mine });
    tops.push(Top { a: theirs });
    f(tops[0]);
}
