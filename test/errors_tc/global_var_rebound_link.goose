// error: a relative reference in cells must point into cells
// error: g is a global var, which any function may bind, and may point into cells or nodes
// Any function may bind a global `var`, and a body is checked once for every
// later call: link is checked while g still has only its initializer's
// binding, into cells, but its check has to hold after rebind, checked
// later, has bound g into nodes. So a read of g in a function's body is the
// read-back rule's answer for a global (§9.5), which may point into either.
struct Cell { v: i64 }
var cells: Cell[>..] = [Cell { v: 1 }];
struct S { r: Cell&<u16 in cells>?, n: i64 }
var nodes: Cell[>..] = [Cell { v: 2 }];
var g: Cell&? = cells[0];
var keep: S[>..] = [];
fn link() { keep.push(S { r: g, n: 1 }); }
fn rebind() { g .= nodes[0]; }
fn main() {
    link();
    rebind();
    link();
}
