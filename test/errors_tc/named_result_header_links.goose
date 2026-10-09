// error: copying a value of type M, which contains self-relative references, is not supported
// A resizable struct's fields before its resizable tail reach the caller as
// a copy (C.3), and a link from the tail to start would still point at the
// callee's (§3.9, §7.3).
struct Cell { id: i64, neighbors: (Cell&<u32>)[..4] }
struct M { start: Cell, cells: Cell[>..] }

fn make() -> M {
    var m = M { start: Cell { id: 0, neighbors: [] }, cells: [] };
    m.cells.push(Cell { id: 1, neighbors: [] });
    m.cells[0].neighbors.push(m.start);
    return m;
}

fn main() {
    let m = make();
    print(m.cells[0].neighbors[0].id);
}
