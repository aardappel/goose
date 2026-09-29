// error: storing a reference rooted at loc, which does not outlive the destination
// relay's first call reuses the check of put that put's own first call made,
// and stands on what that found of v: the second relay, where v points into
// loc, checks both again (§7.5).
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
    fn relay() { put(); }
    put();
    relay();
    var loc: Cell[>..] = [Cell { v: 9 }];
    v .= pick(true, loc[0]);
    relay();
}
fn main() {
    cells.push(Cell { v: 5 });
    run();
    print(keep.len);
}
