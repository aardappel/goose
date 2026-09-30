// error: cannot write through this reference: non-writable provenance
// A `for` binding of a self-relative element is no more writable than the
// array it is read out of (§9.5): the links lead back into the value the
// `const` reference views (§3.9).
struct Cell { v: i64 }
struct Nest { cells: Cell[..4], links: (Cell&<u16>)[..4] }
fn inc(p: i64&) { p = p + 1; }
fn bump(t: const Nest&) {
    for l in t.links { inc(l.v); }
}
fn main() {
    var t = Nest { cells: [], links: [] };
    t.cells.push(Cell { v: 1 });
    t.links.push(t.cells[0]);
    bump(t);
    print(t.cells[0].v);
}
