// Etc::GameOfLife: Conway's game of life on a torus, where every cell holds
// links to its eight neighbors and each generation counts the live ones.

namespace game_of_life;

import std;
import bench;
import helper;

// The links are 4-byte offsets from the link to the neighbor, which lies in
// the same array, so the whole grid is one block of 35-byte cells with the
// neighbor lists inline.
struct Cell { alive: bool, next_state: bool, neighbors: (Cell&<u32>)[..8] }

fn compute_next_state(c: Cell&) -> bool {
    // Every cell of a torus has all eight neighbors, which makes the count
    // a loop of known length.
    assert(c.neighbors.len == 8);
    let alive_neighbors = count(c.neighbors) { it.alive };
    if c.alive { alive_neighbors == 2 || alive_neighbors == 3 } else { alive_neighbors == 3 }
}

fn run(b: Bench&) -> u32 {
    let width = config_i64(b.name, "w");
    let height = config_i64(b.name, "h");

    // The grid, row after row: the cell at (x, y) is cells[y * width + x].
    var cells: Cell[>..] = [];
    for i in width * height { cells.push(Cell { alive: false, next_state: false, neighbors: [] }); }
    for y in height {
        for x in width {
            let cell .= cells[y * width + x];
            for dy in -1..2 {
                for dx in -1..2 {
                    if dx == 0 && dy == 0 { continue; }
                    let ny = (y + dy + height) % height;
                    let nx = (x + dx + width) % width;
                    cell.neighbors.push(cells[ny * width + nx]);
                }
            }
        }
    }

    for &cell in cells {
        if next_float(1.0) < 0.1 { cell.alive = true; }
    }

    measure(b) {
        for &cell in cells { cell.next_state = compute_next_state(cell); }
        for &cell in cells { cell.alive = cell.next_state; }
    };

    var alive: u32 = 0;
    var hash: u32 = 2166136261;
    for &cell in cells {
        if cell.alive { alive++; }
        hash = (hash ^ (if cell.alive { 1 } else { 0 })) * 16777619;
    }
    return hash + alive;
}
