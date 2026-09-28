// A local, global or parameter declared with a relative-reference type holds
// the offset (§3.9), so its initializer's reference is encoded into it as a
// rebind's is, range-checked: a reference variable, an element, null, a call
// (inlined at -O2), a relative field or element read back, an if, a match or
// a block, at every fixed width and the varint one, `in pool` and
// self-relative (which only null reaches). A global declared without an
// initializer starts null. An argument is encoded into a relative parameter
// the same way, called or inlined, a function value's parameter included.
// The last declaration needs more than a u8 offset and aborts.
struct Cell { v: i64 }
var cells: Cell[>..] = [];

struct S { r: Cell&<u32 in cells>?, n: i64 }

fn mk(v: i64) -> Cell& {
    cells.push(Cell { v: v });
    cells[cells.len - 1]
}
fn pick(k: i64) -> Cell& { cells[k] }
fn pick_opt(k: i64) -> Cell&? {
    if k < 0 { return null; }
    return cells[k];
}
fn show(r: Cell&?) -> i64 {
    if r { return r.v; }
    return -1;
}

var g8: Cell&<u8 in cells> = mk(100);
var g16: Cell&<u16 in cells>?;
let g32: Cell&<u32 in cells>? = if g8.v == 100 { mk(101) } else { null };
var g64: Cell&<u64 in cells>? = null;
var gv: Cell&<varint in cells>? = { let c: Cell& = mk(102); c };
var gself: Cell&<u16>? = null;
var gselfv: Cell&<varint>?;

fn relay(r: Cell&<u32 in cells>?, w: Cell&<u16 in cells>, v: Cell&<varint in cells>?) -> i64 {
    show(r) * 1000000 + w.v * 1000 + show(v)
}
// A recursive fn is called at every level, and so is what it calls.
recursive fn walk(r: Cell&<u32 in cells>?, w: Cell&<u8 in cells>, z: Cell&<u16>?, k: i64) {
    print(show(r), " ", w.v, " ", show(z), " ",
          relay(r, w, pick_opt(if k == 1 { -1 } else { 30 - k })));
    if k > 0 { walk(pick_opt(if k == 2 { -1 } else { 20 + k }), pick(k), null, k - 1); }
}
fn visit<F>(c: Cell&) -> i64 { F(c) }

fn main() {
    while cells.len < 40 { cells.push(Cell { v: 100 + cells.len }); }
    print(g8.v, " ", show(g16), " ", show(g32), " ", show(g64), " ", show(gv), " ",
          show(gself), " ", show(gselfv));
    g16 .= cells[3];
    g64 .= g32;
    print(show(g16), " ", show(g64));

    let c5: Cell& = cells[5];
    let s = S { r: cells[6], n: 0 };
    var links: (Cell&<u16 in cells>?)[>..] = [cells[7], null];

    let a8: Cell&<u8 in cells> = c5;
    var a16: Cell&<u16 in cells>? = cells[8];
    let a32: Cell&<u32 in cells>? = null;
    var a64: Cell&<u64 in cells> = pick(9);
    let b32: Cell&<u32 in cells>? = s.r;
    let b16: Cell&<u16 in cells>? = links[0];
    let b8: Cell&<u8 in cells>? = links[1];
    let c8: Cell&<u8 in cells>? = if c5.v > 0 { cells[10] } else { null };
    let c16: Cell&<u16 in cells>? = match c5.v { 105 => null, _ => c5 };
    let c32: Cell&<u32 in cells> = { let t: Cell& = cells[11]; t };
    let c64: Cell&<u64 in cells>? = if c5.v > 0 { pick(12) } else { null };
    print(a8.v, " ", show(a16), " ", show(a32), " ", a64.v, " ", show(b32), " ", show(b16), " ",
          show(b8), " ", show(c8), " ", show(c16), " ", c32.v, " ", show(c64));
    print(index_of(cells, a8), " ", index_of(cells, a64), " ", index_of(cells, c32));
    a16 .= cells[13];
    a64 .= a8;
    print(show(a16), " ", a64.v);

    let v1: Cell&<varint in cells> = c5;
    let v2: Cell&<varint in cells>? = null;
    let v3: Cell&<varint in cells>? = pick(14);
    let v4: Cell&<varint in cells>? = if c5.v > 0 { cells[15] } else { null };
    let v5: Cell&<varint in cells>? = s.r;
    let v6: Cell&<varint in cells> = match c5.v { 105 => cells[16], _ => c5 };
    print(v1.v, " ", show(v2), " ", show(v3), " ", show(v4), " ", show(v5), " ", v6.v);

    var n16: Cell&<u16>? = null;
    let n8: Cell&<u8>? = null;
    let nv: Cell&<varint>? = null;
    n16 .= null;
    print(show(n16), " ", show(n8), " ", show(nv));

    walk(s.r, c5, null, 2);
    print(relay(cells[18], pick(19), c5), " ", relay(null, c5, pick(20)));
    print(visit(cells[25]) { r: Cell&<u16 in cells>? => show(r) }, " ",
          visit(cells[26]) { r: Cell&<varint in cells> => r.v });

    // 8 bytes a cell and the offset biased by one: cell 31 is at 249, the
    // last a u8 reaches, and cell 32 needs 257.
    let last: Cell&<u8 in cells> = cells[31];
    print(last.v);
    let far: Cell&<u8 in cells> = if last.v > 0 { cells[32] } else { last };
    print(far.v);   // Not reached.
}
