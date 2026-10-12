// An optional relative reference variable compared with null (§3.8, §3.9):
// it holds an offset, 0 when null, and a varint one its encoding, so the
// test reads the offset rather than the slot's address. Every width, both
// forms, either operand order, null and set.
struct Cell { v: i64 }
var cells: Cell[>..] = [];

fn main() {
    cells.push(Cell { v: 1 });
    let v0: Cell&<varint in cells>? = null;
    let v1: Cell&<varint in cells>? = cells[0];
    let s0: Cell&<varint>? = null;
    let p0: Cell&<u8 in cells>? = null;
    let p1: Cell&<u32 in cells>? = cells[0];
    let f0: Cell&<u16>? = null;
    print(v0 == null, " ", v1 == null, " ", v0 != null, " ", v1 != null);   // true false false true
    print(null == v0, " ", null != v1, " ", s0 == null, " ", s0 != null);   // true true true false
    print(p0 == null, " ", p1 == null, " ", f0 == null, " ", f0 != null);   // true false true false
}
