// A variable or parameter of relative type holds the offset (§3.9), read as
// the reference it encodes: compared by identity (§4.5), `in pool` at every
// fixed width and the varint one, optional and not, null and narrowed, local,
// global and parameter, against plain references, elements and each other,
// and self-relative (which only null reaches); and passed on to a case
// function's relative parameter, which a dispatched call encodes the
// argument into as an ordinary call does, before the tag picks the case.
struct Cell { v: i64 }
reusable var cells: Cell[>..] = [];

var g16: Cell&<u16 in cells>? = null;

fn same(r: Cell&<u32 in cells>?, c: Cell&) -> bool { r .== c }
fn differ(r: Cell&<u8 in cells>, s: Cell&<varint in cells>?) -> bool { r .!= s }

enum Sh { C { r: i64 }, R { w: i64 } }

fn f(k: Cell&<u32 in cells>, s: Sh.C, o: Cell&<u16 in cells>?, n: Cell&<u8>?,
     v: Cell&<varint in cells>) -> i64 {
    let ov = if o { o.v } else { -1 };
    let nv = if n { n.v } else { -1 };
    s.r * 1000000 + k.v * 10000 + ov * 100 + nv + v.v * 100000000
}
fn f(k: Cell&<u32 in cells>, s: Sh.R, o: Cell&<u16 in cells>?, n: Cell&<u8>?,
     v: Cell&<varint in cells>) -> i64 {
    let ov = if o { o.v } else { -1 };
    let nv = if n { n.v } else { -1 };
    -(s.w * 1000000 + k.v * 10000 + ov * 100 + nv + v.v * 100000000)
}

fn main() {
    for i in 0..8 { cells.alloc_index(Cell { v: i }); }
    let p: Cell& = cells[2];
    let a8: Cell&<u8 in cells> = cells[2];
    let a16: Cell&<u16 in cells>? = cells[2];
    var a32: Cell&<u32 in cells> = p;
    let a64: Cell&<u64 in cells>? = cells[3];
    let av: Cell&<varint in cells> = cells[2];
    let none: Cell&<u32 in cells>? = null;
    print(a8 .== p, " ", p .== a16, " ", a32 .== cells[2], " ", av .== p, " ",
          a8 .== a16, " ", a8 .== av, " ", a64 .!= p, " ", a64 .== cells[3]);
    print(none .== null, " ", none .!= p, " ", a16 .!= null, " ", null .== a64);
    a32 .= cells[3];
    print(a32 .== a64, " ", a32 .!= p);
    if a16 { print(a16 .== p, " ", a16 .!= a64); }

    g16 .= cells[5];
    print(g16 .== cells[5], " ", g16 .!= a8, " ", same(g16, cells[5]), " ", same(a32, p), " ",
          same(none, p), " ", differ(a8, av), " ", differ(a8, a64), " ", differ(a8, null));

    let n16: Cell&<u16>? = null;
    let nv: Cell&<varint>? = null;
    print(n16 .== null, " ", n16 .!= p, " ", nv .== null, " ", null .!= nv);

    let xs: Sh[] = [Sh.C { r: 1 }, Sh.R { w: 2 }];
    for x in xs {
        print(f(cells[4], x, a16, null, cells[6]), " ", f(a32, x, none, n16, av));
    }
}
