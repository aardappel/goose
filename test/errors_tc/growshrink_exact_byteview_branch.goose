// error: storing a reference that may point into a grow-shrink array, as t may
// Either branch's value is the one stored: the bytes of t.a, or a literal.
// tops holds only a Top for mine, so t's argument is bound to mine exactly
// (§9.2) and t holds a grow-shrink array: s, which may be either value, may
// point into what t does, and is never stored (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
struct Keep { v: const u8[:] }
fn f(t: Top&) {
    var keep: Keep[>..] = [];
    let s = if t.a.len > 0 { t.a.bytes_of() } else { "none" };
    keep.push(Keep { v: s });
    t.a.clear();
    t.a.push(Cell { v: 99 });
    print(keep[0].v);
}
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    f(tops[0]);
}
