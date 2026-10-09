// A `for` binder over an array of plain references is the reference an
// element holds, rooted where it points (§9.5), and over a local holder
// where the stores into it put it: so a recursive walk can collect its
// neighbours into a local array and recurse on each, every one still a link
// into the array the cycle's entry call gave (§7.8), and a function can
// return a reference it collected.
enum Kind { Wall, Space }
struct Cell { kind: Kind, neighbors: (Cell&<u32>)[..4] }

recursive fn open(c: Cell&) {
    c.kind = Kind.Space;
    var walls: Cell&[..4] = [];
    for n in c.neighbors {
        if n.kind == Kind.Wall { walls.push(n); }
    }
    for w in walls { open(w); }
}

fn second(xs: i64[4]&) -> i64& {
    var refs: i64&[..4] = [];
    refs.push(xs[1]);
    for r in refs { return r; }
    return xs[0];
}

fn main() {
    var cells: Cell[>..] = [];
    for i in 4 { cells.push(Cell { kind: Kind.Wall, neighbors: [] }); }
    cells[0].neighbors.push(cells[1]);
    cells[1].neighbors.push(cells[2]);
    cells[2].neighbors.push(cells[0]);
    open(cells[0]);
    var n = 0;
    for &c in cells { if c.kind == Kind.Space { n++; } }
    var a: i64[4] = [1, 2, 3, 4];
    let r .= second(a);
    r = 20;
    print(n, " ", a);
}
