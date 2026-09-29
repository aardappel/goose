// error: cannot pop arr while c
// error: is still used: it may refer into arr
// arr's argument, t.a, is a whole grow-shrink array read out of the
// caller's storage behind t, so the pop through arr frees the slot c names
// (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn shrink(arr: Cell[>..<]&) {
    let c: Cell& = arr[0];
    arr.pop();
    arr.push(Cell { v: 99 });
    print(c.v);
}
fn f(t: Top&) { shrink(t.a); }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    f(tops[0]);
}
