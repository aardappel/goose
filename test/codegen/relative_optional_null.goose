// An optional relative reference variable compared with null (§3.8, §3.9):
// it holds an offset, 0 when null, and a varint one its encoding, so the
// test reads the offset rather than the slot's address. Every width, both
// forms, either operand order, null and set.
struct Cell { v: i64 }
var cells: Cell[>..] = [];

fn null_tests() {
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

// A non-null optional self-relative field whose target is at a nonzero offset
// from it keeps that target, and goes back to null when reset; a non-optional
// self link at offset zero keeps its target, as only the optional form spends
// offset 0 on null (runtime/relative_optional_zero.goose is the one that aborts).
struct Node { value: i64, next: Node&<u8>? }
struct Self { link: Self&<u8>, value: i64 }
fn targets() {
    var nodes: Node[>..] = [];
    nodes.push(Node { value: 42 });
    assert(nodes[0].next == null);
    nodes[0].next .= nodes[0];
    let next .= nodes[0].next;
    if next { print(next.value); }
    nodes[0].next .= null;
    assert(nodes[0].next == null);
    let s = Self { link: self, value: 99 };
    print(s.link.value);
}

fn main() {
    null_tests();
    targets();
}
