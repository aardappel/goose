// error: storing a reference into t, which holds a grow-shrink array
// t is read out of an element of tops, so the reference it holds is a slot
// read (§5.2), but that reference is to the whole of mine: an element of
// mine lies in a grow-shrink array, and is never stored, though the array
// is main's and reached only through what t's argument holds.
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn f(t: Top) {
    var loc: (Cell&)[>..] = [];
    loc.push(t.a[0]);
    t.a.pop();
    print(loc[0].v);
}
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..<] = [];
    tops.push(Top { a: mine });
    f(tops[0]);
}
