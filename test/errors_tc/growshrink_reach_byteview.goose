// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// The bytes of the array t.a leads to are a grow-shrink array's elements,
// which the clear frees and the push reuses (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
struct Keep { v: const u8[:] }
fn f(t: Top&) {
    var keep: Keep[>..] = [];
    keep.push(Keep { v: t.a.bytes_of() });
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
