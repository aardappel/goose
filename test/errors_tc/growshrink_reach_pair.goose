// error: cannot call f: it may shrink mine while c is still used, and c may refer into mine
// shrink frees an element of the array it is given while its copy of c is
// still used. From f the array is t.a, bounded by the caller's storage
// behind t, which leads to mine (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn shrink(arr: Cell[>..<]&, c: Cell&) {
    let d: Cell& = c;
    arr.pop();
    arr.push(Cell { v: 99 });
    print(d.v);
}
fn f(t: Top&, c: Cell&) { shrink(t.a, c); }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    f(tops[0], mine[0]);
}
