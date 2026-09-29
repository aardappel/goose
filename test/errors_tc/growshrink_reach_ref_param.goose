// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// t.a is read out of the caller's storage behind t, so it is rooted at t's
// class only as a bound (§9.5), and so is an element of the array it leads
// to. t's argument lies in a grow-only array, but what its references lead
// to is a grow-shrink one, whose slot the pop frees and the push reuses
// (§5.2).
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
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    f(tops[0]);
}
