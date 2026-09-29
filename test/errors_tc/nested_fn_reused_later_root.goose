// error: storing a reference rooted at loc, which does not outlive the destination
// put's body is checked for what it finds of v: null at the first call,
// which reads as whatever can hold a Cell where v lives -- only cells yet --
// but bound into loc, declared after that call, at the second, which is
// checked again: that put stores a reference into run's local in a global
// (§7.5, §9.2).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
struct K { r: Cell&?, n: i64 }
var keep: K[>..] = [];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn run() {
    var v: Cell&? = null;
    fn put() { keep.push(K { r: v, n: 1 }); }
    put();
    var loc: Cell[>..] = [Cell { v: 9 }, Cell { v: 10 }];
    v .= pick(true, loc[1]);
    put();
}
fn main() {
    cells.push(Cell { v: 5 });
    run();
    let r = keep[1].r;
    if r { print(r.v); }
}
