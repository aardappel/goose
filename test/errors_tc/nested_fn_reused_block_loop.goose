// error: a relative reference in cells must point into cells
// error: this reference's root is not known exactly, only that it outlives cells
// The block apply calls reads v, which points into cells alone when the
// loop's first pass checks apply; the next pass starts from the rebind at
// the end of the body, which has given v spare as well, and checks apply
// with the block again rather than reusing that (§3.9, §7.6).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [];
struct L { r: Cell&<u64 in cells>?, n: i64 }
var links: L[>..] = [];
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn apply<F>() { F(); }
fn main() {
    cells.push(Cell { v: 5 });
    spare.push(Cell { v: 6 });
    var v: Cell&? = pick(true, cells[0]);
    for i in 0..2 {
        apply() { links.push(L { r: v, n: 1 }); };
        v .= pick(true, spare[0]);
    }
    print(links.len);
}
