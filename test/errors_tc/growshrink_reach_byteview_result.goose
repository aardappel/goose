// error: storing a reference that may point into a grow-shrink array reached through tops's references
// bytes returns the bytes of the array t.a leads to, which no slot has
// held; from main it is bounded by tops, whose references lead to mine
// (§5.2).
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
struct Keep { v: const u8[:] }
fn bytes(t: Top&) -> const u8[:] { t.a.bytes_of() }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var tops: Top[>..] = [];
    tops.push(Top { a: mine });
    var keep: Keep[>..] = [];
    keep.push(Keep { v: bytes(tops[0]) });
    mine.clear();
    mine.push(Cell { v: 99 });
    print(keep[0].v);
}
