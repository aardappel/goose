// error: storing a reference into mine, which holds a grow-shrink array
// r refers to the variable b, whose binding is the bytes of mine: the slice
// loaded through r is that byte view, never stored (§5.2).
struct Cell { v: i64 }
struct Keep { v: const u8[:] }
fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var keep: Keep[>..] = [];
    var b = mine.bytes_of();
    let r: const (const u8[:])& = &b;
    keep.push(Keep { v: r });
    mine.clear();
    mine.push(Cell { v: 99 });
    print(keep[0].v);
}
