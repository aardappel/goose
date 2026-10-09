// Values holding self-relative references reach their receivers without a
// copy (§3.9): a function's named result is built at its destination from
// its declaration, whether a resizable array of linked cells or a struct
// whose resizable tail holds them (§7.3); a call's resizable result is built
// where it is received, and a fixed-size one holds no link but `self` and
// null, which mean the same wherever the value sits.
struct Cell { id: i64, neighbors: (Cell&<u32>)[..4] }
struct Maze { width: i64, cells: Cell[>..] }
struct Loop { id: i64, me: Loop&<u32> }

fn ring(n: i64) -> Cell[>..] {
    var cells: Cell[>..] = [];
    for i in n { cells.push(Cell { id: i, neighbors: [] }); }
    for i in n { cells[i].neighbors.push(cells[(i + 1) % n]); }
    return cells;
}

fn ring_value(n: i64) -> Cell[] {
    var cells: Cell[>..] = [];
    for i in n { cells.push(Cell { id: i * 2, neighbors: [] }); }
    for i in n { cells[i].neighbors.push(cells[(i + n - 1) % n]); }
    cells
}

fn new_maze(w: i64) -> Maze {
    var m = Maze { width: w, .. };
    for i in w { m.cells.push(Cell { id: i * 10, neighbors: [] }); }
    for i in w { m.cells[i].neighbors.push(m.cells[w - 1 - i]); }
    return m;
}

fn lit_maze(w: i64) -> Maze { Maze { width: w, cells: ring(w) } }

fn one() -> Cell { Cell { id: 7, neighbors: [] } }

fn looped(id: i64) -> Loop { Loop { id: id, me: self } }

fn linksum(cells: Cell[:]) -> i64 {
    var s = 0;
    for &c in cells { for n in c.neighbors { s += n.id; } }
    return s;
}

fn main() {
    let r = ring(5);
    print(linksum(r), " ", r[4].neighbors[0].id);
    var m = new_maze(4);
    print(linksum(m.cells), " ", m.width);
    m.cells.push(Cell { id: 100, neighbors: [] });
    m.cells[4].neighbors.push(m.cells[0]);
    print(linksum(m.cells));
    m = new_maze(2);
    print(linksum(m.cells), " ", m.width);
    let l = lit_maze(3);
    print(linksum(l.cells), " ", l.width);
    var vs: Cell[][>..] = [];
    vs.push(ring_value(3));
    vs.push(ring_value(2));
    for v in vs { print(linksum(v)); }
    var cs: Cell[>..] = [];
    cs.push(one());
    cs.push(one());
    cs[0].neighbors.push(cs[1]);
    print(linksum(cs));
    var x = one();
    x = one();
    print(x.id, " ", x.neighbors.len);
    let lp = looped(3);
    var lps: Loop[>..] = [];
    lps.push(looped(4));
    print(lp.me.id, " ", lps[0].me.id);
}
