// error: storing a reference into mine, which holds a grow-shrink array
// bytes returns the bytes of the array t.a leads to, which no slot has
// held. tops holds only a Top for mine, so its Tops are bound to mine
// exactly (§9.2), and from main the result is a byte view of mine itself,
// which the clear frees and the push reuses (§5.2).
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
