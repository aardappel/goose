// error: cannot pop arr while d
// error: is still used: it may refer into arr
// tops holds only a Top for mine, so t's argument is bound to mine exactly
// (§9.2): shrink's arr is mine, as in shrink(mine, mine[0]), and c's
// argument lies in it, so shrink's own body rejects popping arr while d,
// its copy of c, is still used (§5.2). An element of another array as c
// would leave the call clean.
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
