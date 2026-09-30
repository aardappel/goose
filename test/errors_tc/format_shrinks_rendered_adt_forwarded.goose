// error: cannot call show2: it shrinks G while h is still used, and h may refer into G (§5.2)
// show's pair reaches show2 about what show2's caller's storage holds,
// where show2 passed the reference it read out of that storage, and main's
// holds G (§3.7, §4.4, §5.2).
struct A { v: i64 }
struct Q { x: i64, y: i64 }
enum R {
    Big { a: A, q: Q&, items: i64[>..<] },
    Small { w: i64, h: i64, k: i64 },
}
struct H { n: i64, r: R..& }
var q1 = Q { x: 1, y: 2 };
var G: R.. = R.Small { w: 0, h: 0, k: 0 };
fn format(out: u8[>..]&, a: A) {
    G = R.Small { w: 4096, h: 100000, k: 3 };
    format(out, a.v);
}
fn show(r: R..&) { print(r); }
fn show2(h: H&) { show(h.r); }
fn main() {
    G = R.Big { a: A { v: 5 }, q: q1, items: [1, 2] };
    var hh = H { n: 1, r: G };
    show2(hh);
}
