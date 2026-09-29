// error: storing a reference into c, which holds a grow-shrink array
// t.a[0], bounded by the caller's storage behind t, may lie in the
// grow-shrink array t.a leads to, so c's class may too (§5.2): keep may not
// store it.
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn keep(c: Cell&, loc: (Cell&)[>..]&) { loc.push(c); }
fn f(t: Top&) {
    var loc: (Cell&)[>..] = [];
    keep(t.a[0], loc);
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
