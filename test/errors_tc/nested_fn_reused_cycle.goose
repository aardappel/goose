// error: storing a reference rooted at loc, which does not outlive the destination
// b reads v only through a, its recursive cycle's head, whose result it
// returns: a reads v after the call to b, so b's back edge to a finds none
// of that, but b's check stands on a's record all the same. The call of b
// after v points into loc checks the cycle again (§7.5, §7.8).
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
    recursive fn a(n: i64) -> Cell&? {
        if n <= 0 { return null; }
        let r = b(n - 1);
        if r { return r; }
        return v;
    }
    recursive fn b(n: i64) -> Cell&? { a(n - 1) }
    let first = a(2);
    var loc: Cell[>..] = [Cell { v: 9 }];
    v .= pick(true, loc[0]);
    keep.push(K { r: b(3), n: 1 });
}
fn main() {
    cells.push(Cell { v: 5 });
    run();
    print(keep.len);
}
