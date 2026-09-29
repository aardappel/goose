// A result is a value, which is never relative (§3.9): a generic result a
// relative type argument gives is the plain reference a load gives, and
// default<T>() of one is the plain null. Both reach plain destinations (a
// parameter, a reference variable, index_of) and relative ones of every
// fixed width and the varint one, `in pool` and self-relative: a
// declaration, a parameter, a field, a pushed element, a rebind, an if's
// and a match's value and a global; called, inlined at -O2, and called from
// a recursive fn, whose calls are never inlined. default<Rel[N]>() encodes
// the null in every element. The last store needs more than a u8 offset and
// aborts.
struct Cell { v: i64 }
var cells: Cell[>..] = [];

struct Two<T> { a: T, b: T }
struct Blob<T> { a: T, cs: Cell[3] }
struct S { r: Cell&<u32 in cells>?, w: Cell&<u8 in cells>?, n: i64 }

fn get<T>(a: T[>..]&, i: i64) -> T { a[i] }
fn first<T>(s: Two<T>&) -> T { s.a }
fn both<T>(s: Two<T>&) -> T, T { return s.b, s.a; }
fn fwd<T>(s: Two<T>&) -> T, T { return both(s); }
fn geta<T>(s: Blob<T>&) -> T { s.a }
fn blank<T>(a: T[>..]&) { a.push(default<T>()); }

fn mk(v: i64) -> Cell& {
    cells.push(Cell { v: v });
    cells[cells.len - 1]
}
fn show(r: Cell&?) -> i64 {
    if r { return r.v; }
    return -1;
}
fn relay(r: Cell&<u32 in cells>?, w: Cell&<u16 in cells>, v: Cell&<varint in cells>?) -> i64 {
    show(r) * 1000000 + w.v * 1000 + show(v)
}

var glinks: (Cell&<u16 in cells>)[>..] = [mk(100), mk(101)];
var g32: Cell&<u32 in cells> = get(glinks, 1);
var gv: Cell&<varint in cells>? = get(glinks, 0);
var gd: Cell&<u64 in cells>? = default<Cell&<u64 in cells>?>();
var gl8: (Cell&<u8 in cells>)[>..] = [];

recursive fn walk(k: i64) {
    {
        let r: Cell&<u8 in cells> = get(gl8, k);
        let q: Cell&<u16 in cells>? = if k == 1 { default<Cell&<u16 in cells>?>() }
                                      else { get(gl8, k) };
        let v: Cell&<varint in cells>? = get(gl8, k);
        print(r.v, " ", show(q), " ", show(v), " ", show(get(gl8, k)), " ",
              relay(get(gl8, k), get(gl8, k), default<Cell&<varint in cells>?>()));
    }
    if k > 0 { walk(k - 1); }
}

fn main() {
    while cells.len < 40 { cells.push(Cell { v: 100 + cells.len }); }
    print(g32.v, " ", show(gv), " ", show(gd));
    var l8: (Cell&<u8 in cells>)[>..] = [cells[2], cells[3], cells[4]];
    var l16: (Cell&<u16 in cells>?)[>..] = [cells[5], null];
    var l32: (Cell&<u32 in cells>)[>..] = [cells[6], cells[35]];
    var l64: (Cell&<u64 in cells>?)[>..] = [null, cells[7]];
    let tv = Two<Cell&<varint in cells>> { a: cells[8], b: cells[9] };
    let t16 = Two<Cell&<u16 in cells>?> { a: null, b: cells[10] };
    var bs = Blob<Cell&<u16>?> { a: null, cs: [Cell { v: 1 }, Cell { v: 2 }, Cell { v: 3 }] };
    bs.a .= bs.cs[2];

    print(show(get(l8, 0)), " ", show(get(l16, 1)), " ", show(get(l32, 0)), " ",
          show(get(l64, 1)), " ", show(first(tv)), " ", show(geta(bs)));
    var n: Cell& = get(l8, 1);
    let e .= get(l32, 0);
    print(n.v, " ", e.v, " ", index_of(cells, get(l8, 2)), " ", index_of(cells, first(tv)));
    let x, y .= both(tv);
    let x2, y2 .= fwd(t16);
    print(show(x), " ", show(y), " ", show(x2), " ", show(y2));

    let r8: Cell&<u8 in cells> = get(l8, 0);
    let r16: Cell&<u16 in cells>? = get(l16, 0);
    var r32: Cell&<u32 in cells>? = get(l64, 0);
    let r64: Cell&<u64 in cells> = first(tv);
    let rv: Cell&<varint in cells>? = get(l32, 1);
    print(r8.v, " ", show(r16), " ", show(r32), " ", r64.v, " ", show(rv));
    r32 .= get(l32, 1);
    let s = S { r: get(l32, 0), w: get(l8, 2), n: 1 };
    l16.push(get(l8, 1));
    let c8: Cell&<u8 in cells>? = if r8.v > 0 { get(l8, 2) } else { null };
    let cm: Cell&<u16 in cells> = match r8.v { 102 => first(tv), _ => get(l8, 0) };
    print(show(r32), " ", show(s.r), " ", show(s.w), " ", show(l16[2]), " ", show(c8), " ", cm.v);
    print(relay(get(l32, 0), get(l8, 1), get(l64, 1)), " ", relay(get(l64, 0), first(tv), null));

    gl8.push(cells[20]);
    gl8.push(cells[21]);
    gl8.push(cells[22]);
    walk(2);

    let d8: Cell&<u8 in cells>? = default<Cell&<u8 in cells>?>();
    var d16: Cell&<u16 in cells>? = default<Cell&<u16 in cells>?>();
    let d32: Cell&<u32 in cells>? = default<Cell&<u32 in cells>?>();
    let dv: Cell&<varint in cells>? = default<Cell&<varint in cells>?>();
    let dself: Cell&<u16>? = default<Cell&<u16>?>();
    let dvself: Cell&<varint>? = default<Cell&<varint>?>();
    print(show(d8), " ", show(d16), " ", show(d32), " ", show(dv), " ", show(dself), " ",
          show(dvself), " ", show(default<Cell&<u32 in cells>?>()));
    d16 .= cells[11];
    let ds = S { r: default<Cell&<u32 in cells>?>(), w: cells[12], n: 2 };
    let da = default<(Cell&<u8 in cells>?)[3]>();
    let dsa = default<(Cell&<u64 in cells>?)[2]>();
    let dst = default<S>();
    blank(l16);
    blank(l64);
    print(show(d16), " ", show(ds.r), " ", show(ds.w), " ", show(da[0]), " ", show(da[2]), " ",
          show(dsa[1]), " ", show(dst.r), " ", show(dst.w), " ", show(l16[3]), " ",
          show(l64[2]), " ", l16.len, " ", l64.len);
    d16 .= default<Cell&<u16 in cells>?>();
    print(show(d16), " ", relay(default<Cell&<u32 in cells>?>(), cells[13],
                                default<Cell&<varint in cells>?>()));

    // 8 bytes a cell and the offset biased by one: cell 31 is at 249, the
    // last a u8 reaches, and cell 35 needs 281.
    let near: Cell&<u8 in cells> = get(l32, 0);
    print(near.v);
    let far: Cell&<u8 in cells> = get(l32, 1);
    print(far.v);   // Not reached.
}
