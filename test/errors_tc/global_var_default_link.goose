// error: a relative reference in cells must point into cells
// error: g is a global var, which any function may bind, and may point into cells or nodes
// A var global declared without an initializer starts as null (§11.1), which
// link's check sees before any function has bound g, but the check serves
// the call after rebind has bound g into nodes too: a read of g in a
// function's body is the read-back rule's answer for a global (§9.5).
struct Cell { v: i64 }
var cells: Cell[>..] = [Cell { v: 1 }];
struct S { r: Cell&<u16 in cells>?, n: i64 }
var nodes: Cell[>..] = [Cell { v: 2 }];
var g: Cell&?;
var keep: S[>..] = [];
fn link() { keep.push(S { r: g, n: 1 }); }
fn rebind() { g .= nodes[0]; }
fn main() {
    link();
    rebind();
    link();
}
