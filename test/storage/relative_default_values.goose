// A default value holds no self-relative reference but nulls, or none at
// all in an empty limited array (§3.9): a literal ending in `..` takes one
// for such a field, and so does an explicit default<T>().
struct Cell {
    alive: bool,
    id: i64,
    links: (Cell&<u32>?)[3],
    neighbors: (Cell&<u32>)[..8],
}

fn relative_defaults() {
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

// A literal's `..` fills a field holding self-relative references with the
// field type's default (§3.2, §4.2): an empty limited array or null links,
// whose bytes mean the same wherever they land, so the default is no copy
// of offsets measured elsewhere (§3.9).

struct Life { alive: bool, next_state: bool, neighbors: (Life&<u32>)[..8] }
struct Ring { id: i64, slots: (Ring&<u32>?)[3] }
struct Holder { name: i64, cell: Life }

fn relative_default_fields() {
    var cells: Life[>..] = [];
    cells.push(Life { .. });
    cells.push(Life { alive: true, .. });
    cells[0].neighbors.push(cells[1]);
    cells[1].neighbors.push(cells[0]);
    print(cells.len, " ", cells[0].neighbors.len, " ", cells[0].neighbors[0].alive, " ",
          cells[1].neighbors[0].alive);

    var rings: Ring[>..] = [];
    for i in 3 { rings.push(Ring { id: i, .. }); }
    rings[0].slots[1] .= rings[2];
    var linked = 0;
    for &r in rings {
        for s in r.slots { if s { linked += s.id + 1; } }
    }
    print(linked);

    var hs: Holder[>..] = [];
    hs.push(Holder { name: 7, .. });
    hs[0].cell.neighbors.push(hs[0].cell);
    print(hs[0].name, " ", hs[0].cell.neighbors.len, " ", hs[0].cell.neighbors[0].alive);
}

fn main() {
    relative_defaults();
    relative_default_fields();
}
