// error: storing a reference that may point into a grow-shrink array reached through the caller's storage behind t
// tops holds Tops for mine and theirs, so t.a is rooted at t's class only
// as a bound (§9.5). The bytes of the array it leads to are a grow-shrink
// array's elements, which the clear frees and the push reuses (§5.2).
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
    var theirs: Cell[>..<] = [Cell { v: 2 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    tops.push(Top { a: theirs });
    f(tops[0]);
}
