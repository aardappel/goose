// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// A by-value holder parameter's references point into the caller's storage
// behind its class (§9.2), which bounds t.a and an element of the array it
// leads to: a grow-shrink array, whose slot the pop frees and the push
// reuses (§5.2).
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
