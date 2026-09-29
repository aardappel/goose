// error: cannot pop tops[0] while e
// error: is still used: it may refer into tops[0]
// A reference to the whole of scratch read back out of tops points at
// scratch exactly (§5.2): a pop through it frees what e refers to.
struct Cell { v: i64 }
var scratch: Cell[>..<] = [Cell { v: 1 }, Cell { v: 2 }];
var tops: (Cell[>..<]&)[>..] = [];
fn main() {
    tops.push(scratch);
    let e: Cell& = scratch[1];
    tops[0].pop();
    print(e.v);
}
