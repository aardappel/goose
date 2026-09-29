// error: .index_of needs a reference rooted at the array itself
// error: g is a global var, which any function may bind, and may point into cells or nodes
// index_of turns a reference into an index into the array it is rooted at
// exactly (§3.3); at, checked while g points into cells, is called again
// after rebind has bound g into nodes, where the index would be wrong.
struct Cell { v: i64 }
var cells: Cell[>..] = [Cell { v: 1 }, Cell { v: 2 }];
var nodes: Cell[>..] = [Cell { v: 3 }];
var g: Cell& = cells[1];
fn at() -> i64 { cells.index_of(g) }
fn rebind() { g .= nodes[0]; }
fn main() {
    print(at());
    rebind();
    print(at());
}
