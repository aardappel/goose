// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// t's argument is an element of pass's parameter, and the Tops there lead
// to mine or theirs, so what the by-value holder t holds is rooted at the
// caller's storage behind its class only as a bound (§9.2, §9.5): so are
// t.a and an element of the array it leads to, a grow-shrink array, whose
// slot the pop frees and the push reuses (§5.2). With only a Top for mine
// in tops (growshrink_exact_holder_param), or called from main with
// tops[0], t's class holds the arrays themselves.
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
fn f(t: Top) {
    var loc: (Cell&)[>..] = [];
    loc.push(t.a[0]);
    t.a.pop();
    t.a.push(Cell { v: 99 });
    print(loc[0].v);
}
fn pass(ts: Top[>..]&) { f(ts[0]); }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var theirs: Cell[>..<] = [Cell { v: 2 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    tops.push(Top { a: theirs });
    pass(tops);
}
