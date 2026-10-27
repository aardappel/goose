// error: cannot pop arr while d is still used: it may hold a reference or slice into it
// The grow-only form (§5.1): tops holds only a Top for mine, so t's
// argument is bound to mine exactly (§9.2): shrink's arr is mine and c's
// argument lies in it, so shrink's own body rejects popping arr while d,
// its copy of c, is still used.
struct Cell { v: i64 }
struct Top { a: Cell[>..]& }
fn shrink(arr: Cell[>..]&, c: Cell&) {
    let d: Cell& = c;
    arr.pop();
    arr.push(Cell { v: 99 });
    print(d.v);
}
fn f(t: Top&, c: Cell&) { shrink(t.a, c); }
fn main() {
    var mine: Cell[>..] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    f(tops[0], mine[0]);
}
