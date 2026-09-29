// error: a relative reference in cells must point into cells
// error: it was read out of slots and may point into cells or any global
// link is first called by a global initializer, before nodes is declared,
// but its check serves main's call too, after hold has stored a reference
// into nodes in slots: a read out of a global in a function's body may
// point into any global a later declaration makes (§9.5).
struct Cell { v: i64 }
var cells: Cell[>..] = [Cell { v: 1 }];
struct L { r: Cell&<u16 in cells>?, n: i64 }
var slots: (Cell&)[>..] = [];
var links: L[>..] = [];
fn link() -> i64 {
    if slots.len > 0 { links.push(L { r: slots[0], n: 1 }); }
    0
}
var x = link();
var nodes = [Cell { v: 2 }, Cell { v: 3 }];
fn hold() { slots.push(nodes[1]); }
fn main() {
    hold();
    link();
}
