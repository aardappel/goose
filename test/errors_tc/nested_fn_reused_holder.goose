// error: storing a reference rooted at loc, which does not outlive the destination
// put copies h into a global: at its first call h holds a reference into
// cells, at the second one into loc too, so that call is checked again
// (§7.5, §9.2).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
struct K { r: Cell&?, n: i64 }
var keep: K[>..] = [];
fn run() {
    var h = K { r: cells[0], n: 0 };
    fn put() { keep.push(h); }
    put();
    var loc: Cell[>..] = [Cell { v: 9 }];
    h = K { r: loc[0], n: 1 };
    put();
}
fn main() {
    cells.push(Cell { v: 5 });
    run();
    print(keep.len);
}
