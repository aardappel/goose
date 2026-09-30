// error: cannot call show: it shrinks G while r is still used, and r may refer into G (§5.2)
// A function rendering its parameter's ADT around an overload that replaces
// G's variant passes the pair to its callers, and this one passes G
// (§3.7, §4.4, §5.2).
struct A { v: i64 }
struct Q { x: i64, y: i64 }
enum R {
    Big { a: A, q: Q&, items: i64[>..<] },
    Small { w: i64, h: i64, k: i64 },
}
var q1 = Q { x: 1, y: 2 };
var G: R.. = R.Small { w: 0, h: 0, k: 0 };
fn format(out: u8[>..]&, a: A) {
    G = R.Small { w: 4096, h: 100000, k: 3 };
    format(out, a.v);
}
fn show(r: R..&) { print(r); }
fn main() {
    G = R.Big { a: A { v: 5 }, q: q1, items: [1, 2] };
    show(G);
}
