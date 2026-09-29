// error: cannot pop gs while r
// error: it may refer into spare, which gs may point at
// A pop through a global reference to a grow-shrink array frees an element
// of whichever array it points at when it runs (§5.2): trim, checked while
// gs is bound to arr, pops spare once aim has retargeted gs, with r still
// pointing at the element that goes.
struct Cell { v: i64 }
var arr: Cell[>..<] = [Cell { v: 1 }, Cell { v: 2 }];
var spare: Cell[>..<] = [Cell { v: 3 }, Cell { v: 4 }];
var gs: Cell[>..<]& = arr;
fn trim() {
    let r = &spare[1];
    gs.pop();
    print(r.v);
}
fn aim() { gs .= spare; }
fn main() {
    trim();
    aim();
    trim();
}
