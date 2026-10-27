// error: storing a reference into t, which holds a grow-shrink array
// tops holds only a Top for mine, so t's argument is bound to mine exactly
// (§9.2) and t holds a grow-shrink array. same hands back the byte view it
// was given, the bytes of t.a, and its result is still that byte view,
// which is never stored (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
struct Keep { v: const u8[:] }
fn same(s: const u8[:]) -> const u8[:] { s }
fn f(t: Top&) {
    var keep: Keep[>..] = [];
    keep.push(Keep { v: same(t.a.bytes_of()) });
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
