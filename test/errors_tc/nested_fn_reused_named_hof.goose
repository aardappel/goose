// error: storing a reference rooted at loc, which does not outlive the destination
// apply's body calls put, handed to it by name, and stands on what put's
// check found of v: the second apply(put), where v points into loc, checks
// both again (§7.5, §7.6).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
struct K { r: Cell&?, n: i64 }
var keep: K[>..] = [];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn apply<F>() { F(); }
fn run() {
    var v: Cell&? = null;
    fn put() { keep.push(K { r: v, n: 1 }); }
    apply(put);
    var loc: Cell[>..] = [Cell { v: 9 }];
    v .= pick(true, loc[0]);
    apply(put);
}
fn main() {
    cells.push(Cell { v: 5 });
    run();
    print(keep.len);
}
