// error: cannot call format, which shrinks G: print runs it in the middle of rendering h.o, which may refer into G (§5.2)
// A reference read out of a slot points into no grow-shrink array's
// elements, but it may lead to a whole resizable ADT with a grow-shrink
// tail, whose payload is rendered where it points (§3.7, §4.4, §5.2).
struct A { v: i64 }
struct Q { x: i64, y: i64 }
enum R {
    Big { a: A, q: Q&, items: i64[>..<] },
    Small { w: i64, h: i64, k: i64 },
}
struct H { n: i64, o: R..? }
var q1 = Q { x: 1, y: 2 };
var G: R.. = R.Small { w: 0, h: 0, k: 0 };
fn format(out: u8[>..]&, a: A) {
    G = R.Small { w: 4096, h: 100000, k: 3 };
    format(out, a.v);
}
fn main() {
    G = R.Big { a: A { v: 5 }, q: q1, items: [1, 2] };
    let h = H { n: 1, o: G };
    print(h.o);
}
