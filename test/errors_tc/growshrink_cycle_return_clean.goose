// error: this return may point into a grow-shrink array, but the recursive cycle already used a result that does not
// A back edge is given what the returns checked before it give (§7.8), here
// something of cells or others, which kept may hold. The return checked
// after it may be an element of the grow-shrink spare, which no store may
// keep (§5.2), so it would take back what the back edge was given.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var others: Cell[>..] = [];
var spare: Cell[>..<] = [];
var kept: Cell&? = null;
var pickcells = true;

recursive fn f(k: i64) -> Cell& {
    if k == 0 { return if pickcells { cells[0] } else { others[0] }; }
    let r: Cell& = f(k - 1);
    kept .= r;
    return if k > 5 { cells[0] } else { spare[0] };
}

fn main() {
    cells.push(Cell { v: 1 });
    others.push(Cell { v: 2 });
    spare.push(Cell { v: 3 });
    print(f(2).v);
    spare.clear();
    spare.push(Cell { v: 77 });
    let k = kept;
    if k { print(k.v); }
}
