// A default value holds no self-relative reference but nulls, or none at
// all in an empty limited array (§3.9): a literal ending in `..` takes one
// for such a field, and so does an explicit default<T>().
struct Cell {
    alive: bool,
    id: i64,
    links: (Cell&<u32>?)[3],
    neighbors: (Cell&<u32>)[..8],
}

fn main() {
    var cells: Cell[>..] = [];
    cells.push(Cell { .. });
    cells.push(Cell { alive: true, id: 1, .. });
    cells[0].neighbors.push(cells[1]);
    cells[0].links[1] .= cells[1];
    let e: (Cell&<u32>)[..8] = default<(Cell&<u32>)[..8]>();
    let a = cells[0].links[0];
    let b = cells[0].links[1];
    print(cells.len, " ", cells[0].neighbors.len, " ", cells[0].neighbors[0].alive, " ", e.len,
          " ", a == null);
    if b { print(b.id); }
}
