// error: retained storage may have a self-relative link to a removed element
struct Cell { value: i64 }
struct List { first: Cell&<u32>?, cells: Cell[>..] }

fn empty(cells: Cell[>..]&) { cells.clear(); }

fn main() {
    var list = List { cells: [Cell { value: 1 }] };
    list.first .= list.cells[0];
    empty(list.cells);
    print(list.cells.len);
}
