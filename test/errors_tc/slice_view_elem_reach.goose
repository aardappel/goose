// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind p
// r is read out of the slice p's slot holds, which only bounds what it
// points at: a grow-shrink array the references in that slice lead to, as
// cells is. Its element's reference is never stored (§5.2), or the pop
// frees what loc keeps.
struct Cell { v: i64 }
fn f(p: (Cell[>..<]&)[:]&) {
    var loc: (Cell&)[>..] = [];
    let r = p[0];
    loc.push(r[0]);
    r.pop();
    r.push(Cell { v: 99 });
    print(loc[0].v);
}
fn main() {
    var cells: Cell[>..<] = [];
    cells.push(Cell { v: 1 });
    var refs: (Cell[>..<]&)[1] = [cells];
    var s = refs[..];
    f(s);
}
