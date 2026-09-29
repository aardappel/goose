// error: cannot call grow, which grows spare
// error: the growth would land inside it
// A value assigned to an array is built over its old contents (§4.4), so
// nothing may grow the array meanwhile (§1.3): grow, checked while gs is
// bound to arr, grows spare once aim has retargeted gs.
struct Cell { v: i64 }
var arr: Cell[>..<] = [Cell { v: 1 }];
var spare: Cell[>..<] = [Cell { v: 2 }];
var gs: Cell[>..<]& = arr;
fn grow() -> i64 {
    gs.push(Cell { v: 9 });
    7
}
fn fill() { spare = [Cell { v: grow() }]; }
fn aim() { gs .= spare; }
fn main() {
    fill();
    aim();
    fill();
    print(spare.len);
}
