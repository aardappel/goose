// error: a relative reference in cells must point into cells
// error: g is a global var, which any function may bind, and may point into cells or any global
// link is first called by a global initializer, before nodes is declared,
// but its check serves main's call too, after rebind has bound g into
// nodes: a read of a global var in a function's body may point into any
// global a later declaration makes (§9.5).
struct Cell { v: i64 }
var cells: Cell[>..] = [Cell { v: 1 }];
struct L { r: Cell&<u16 in cells>?, n: i64 }
var g: Cell&? = cells[0];
var links: L[>..] = [];
fn link() -> i64 {
    links.push(L { r: g, n: 1 });
    0
}
var x = link();
var nodes = [Cell { v: 2 }, Cell { v: 3 }];
fn rebind() { g .= nodes[1]; }
fn main() {
    rebind();
    link();
}
