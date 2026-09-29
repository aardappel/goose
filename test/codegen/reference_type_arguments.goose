// There are no references to references (§3.8), whatever a type argument
// is: `T?` of a reference makes that reference nullable, and `T&` is the
// reference itself, loaded where it is relative, the way `&` of a location
// holding one gives the stored reference. So std's find and last, which
// return `T?` and `T&`, take arrays of plain and relative references, as
// does a `T&` parameter. An element that is a reference binds as the one
// it holds in a for loop, by `&x` too, which writes through it (by value it
// is a copy), and at the varint width, whose elements bind by reference
// without the `&`.
import std;

struct Cell { v: i64 }
var cells: Cell[>..] = [];

fn getq<T>(xs: T[:], i: i64) -> T? {
    if i < 0 { return null; }
    xs[i]
}
fn setv<T>(r: T&, v: i64) { r.v = v; }
fn peek<T>(r: const T&) -> i64 { r.v }
fn show(r: Cell&?) -> i64 {
    if r { return r.v; }
    return -1;
}

fn main() {
    while cells.len < 20 { cells.push(Cell { v: 100 + cells.len }); }
    var plain: (Cell&)[>..] = [cells[1], cells[2], cells[3]];
    var opt: (Cell?)[>..] = [null, cells[4]];
    var rel: (Cell&<u32 in cells>)[>..] = [cells[5], cells[6], cells[7]];
    var relopt: (Cell&<u16 in cells>?)[>..] = [cells[8], null];
    var relv: (Cell&<varint in cells>)[>..] = [cells[9], cells[10]];

    print(show(getq(plain, 1)), " ", show(getq(plain, -1)), " ", show(getq(opt, 0)), " ",
          show(getq(opt, 1)), " ", show(getq(rel, 2)), " ", show(getq(relopt, 0)), " ",
          show(getq(relopt, 1)));
    print(show(plain.find() { c => c.v > 101 }), " ", show(rel.find() { c => c.v > 105 }), " ",
          show(rel.find() { c => c.v > 200 }), " ", plain.last().v, " ", rel.last().v, " ",
          show(relopt.last()));

    setv<Cell&>(plain[0], 51);
    setv<Cell&<u32 in cells>>(rel[0], 55);
    print(cells[1].v, " ", cells[5].v, " ", peek<Cell&>(plain[0]), " ",
          peek<Cell&<u32 in cells>>(rel[1]));

    for &x in plain { x.v += 1000; }
    for &x, i in rel { x.v += i * 10; }
    for &x in relopt { if x { x.v = -x.v; } }
    for x in relv { print(x.v, " ", index_of(cells, x)); }
    for x in relv { x.v += 1; }
    var sum = 0;
    for x in plain { sum += x.v; }
    for x in rel { sum += x.v; }
    for x in relv { sum += x.v; }
    print(sum, " ", cells[1].v, " ", cells[6].v, " ", cells[8].v, " ", cells[10].v);
}
