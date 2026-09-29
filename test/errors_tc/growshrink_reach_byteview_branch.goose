// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// Either branch's value is the one stored: the bytes of the grow-shrink
// array t.a leads to, or a literal (§5.2).
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
