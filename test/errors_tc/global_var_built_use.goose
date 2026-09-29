// error: cannot call peek in the value assigned to spare
// error: peek uses gs, which may refer to spare
// A value assigned to an array is built over its old contents (§4.4), so
// nothing may read the array meanwhile: peek, checked while gs is bound to
// arr, reads spare once aim has retargeted gs.
struct Cell { v: i64 }
var arr: Cell[>..<] = [Cell { v: 1 }];
var spare: Cell[>..<] = [Cell { v: 2 }];
var gs: Cell[>..<]& = arr;
fn peek() -> i64 { gs[0].v }
fn fill() { spare = [Cell { v: peek() }]; }
fn aim() { gs .= spare; }
fn main() {
    fill();
    aim();
    fill();
    print(spare[0].v);
}
