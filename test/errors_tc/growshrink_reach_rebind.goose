// error: re-binding x to a reference that may point into a grow-shrink array (reached through the caller's storage behind t), where it pointed into none
// x starts as t.r, read out of a field, which points into no grow-shrink
// array (§5.2); t.a[0] may lie in the one t.a leads to.
struct Cell { v: i64 }
struct Top { a: Cell[>..<]&, r: Cell& }
fn f(t: Top&) {
    var x: Cell& = t.r;
    x .= t.a[0];
    print(x.v);
}
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var solo = Cell { v: 2 };
    let top = Top { a: mine, r: solo };
    f(top);
}
