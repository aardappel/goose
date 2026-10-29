// Maze::Generator, Maze::BFS and Maze::AStar: a grid maze dug out from the
// start by a randomized depth-first walk, then searched breadth-first and
// with A*.

namespace maze;

import std;
import slice_pool;
import bench;
import helper;

let I32_MAX: i32 = 2147483647;

enum CellKind { Wall, Space, Start, Finish, Border, Path }

fn is_walkable(k: CellKind) -> bool {
    match k {
        Space, Start, Finish => true,
        _ => false,
    }
}

fn value(k: CellKind) -> u32 {
    match k {
        Wall => 0,
        Space => 1,
        Start => 2,
        Finish => 3,
        Border => 4,
        Path => 5,
    }
}

// A cell's neighbor links are 4-byte offsets to the neighboring cells, which
// lie in the same slice pool (the maze's rows are runs of it). A cell is an
// array element, which can't hold a growable array, so its neighbor list is a
// limited one with room for the four.
struct Cell { kind: CellKind, neighbors: (Cell&<u32>)[..4], x: i32, y: i32 }

fn new_cell(x: i32, y: i32) -> Cell {
    Cell { kind: CellKind.Wall, neighbors: [], x: x, y: y }
}

fn reset(cell: Cell&) {
    if cell.kind == CellKind.Space { cell.kind = CellKind.Wall; }
}

// An array cannot hold growable arrays, so the rows are runs of a slice pool.
// Root owners cannot be nested, so the pool is a local of the code that
// makes the benchmark, declared before it and handed to its constructor.
// The returned maze's links borrow from that longer-lived pool. The array
// of rows is sized at construction, leaving the benchmark's growable field
// for its path.
//
// The rows follow Crystal's and C++'s array of arrays as closely as Goose
// allows, for the sake of comparing like with like, but this is not idiomatic
// Goose. We would keep all cells in one flat array indexed y * width + x, and
// the searches' bookkeeping in it too, which makes these benchmarks about
// 1.15-1.3x faster.
struct Maze { w: i32, h: i32, start: Cell&, finish: Cell&, cells: (Cell[:])[] }

fn new_maze(pool: slice_pool<Cell>&, width: i32, height: i32) -> Maze {
    let w = max(width, 5);
    let h = max(height, 5);
    // The rows come first, since start and finish are bound at construction.
    var cells: (Cell[:])[>..] = [];
    for y: i32 in 0..h {
        let row = pool.alloc_slice(w);
        for x: i32 in 0..w { row[x] = new_cell(x, y); }
        cells.push(row);
    }
    var maze = Maze { w: w, h: h, start: cells[1][1], finish: cells[h - 2][w - 2],
                      cells: copy(cells) };
    maze.start.kind = CellKind.Start;
    maze.finish.kind = CellKind.Finish;

    // Nested, so that it sees `maze` as the local it is, whose rows are known
    // to be runs of `pool`, which the neighbor links require.
    fn update_neighbors() {
        for y: i32 in 0..maze.h {
            for x: i32 in 0..maze.w {
                let cell .= maze.cells[y][x];
                if x > 0 && y > 0 && x < maze.w - 1 && y < maze.h - 1 {
                    cell.neighbors.push(maze.cells[y - 1][x]);
                    cell.neighbors.push(maze.cells[y + 1][x]);
                    cell.neighbors.push(maze.cells[y][x + 1]);
                    cell.neighbors.push(maze.cells[y][x - 1]);

                    for t in 4 {
                        let i = next_int(4);
                        let j = next_int(4);
                        if i != j { cell.neighbors.swap_at(i, j); }
                    }
                } else {
                    cell.kind = CellKind.Border;
                }
            }
        }
    }

    update_neighbors();
    return maze;
}

fn reset(maze: Maze&) {
    for row in maze.cells {
        for &cell in row { cell.reset(); }
    }
    maze.start.kind = CellKind.Start;
    maze.finish.kind = CellKind.Finish;
}

fn dig(maze: Maze&, start_cell: Cell&) {
    var stack: (Cell&)[>..<] = [];
    stack.push(start_cell);
    while stack.len > 0 {
        let cell .= stack.pop();
        if (count(cell.neighbors) { it.kind.is_walkable() }) == 1 {
            cell.kind = CellKind.Space;
            for &n in cell.neighbors {
                if n.kind == CellKind.Wall { stack.push(n); }
            }
        }
    }
}

recursive fn ensure_open_finish(maze: Maze&, cell: Cell&) {
    cell.kind = CellKind.Space;
    if (count(cell.neighbors) { it.kind.is_walkable() }) > 1 { return; }
    for &n in cell.neighbors {
        if n.kind == CellKind.Wall { maze.ensure_open_finish(n); }
    }
}

fn generate(maze: Maze&) {
    for &n in maze.start.neighbors {
        if n.kind == CellKind.Wall { maze.dig(n); }
    }
    for &n in maze.finish.neighbors {
        if n.kind == CellKind.Wall { maze.ensure_open_finish(n); }
    }
}

fn middle_cell(maze: Maze&) -> Cell& { maze.cells[maze.h / 2][maze.w / 2] }
fn get_start(maze: Maze&) -> Cell& { maze.start }
fn get_finish(maze: Maze&) -> Cell& { maze.finish }
fn get_cell(maze: Maze&, x: i32, y: i32) -> Cell& { maze.cells[y][x] }

fn checksum(maze: Maze&) -> u32 {
    var hasher: u32 = 2166136261;
    let prime: u32 = 16777619;
    for row, y in maze.cells {
        for &cell, x in row {
            if cell.kind == CellKind.Space {
                let val = (x * y) as! u32;
                hasher = (hasher ^ val) * prime;
            }
        }
    }
    return hasher;
}

// --- Maze::Generator ------------------------------------------------------------

struct MazeGenerator { maze: Maze, result_val: u32 }

fn new_maze_generator(pool: slice_pool<Cell>&) -> MazeGenerator {
    let width = config_i64("Maze::Generator", "w") as i32;
    let height = config_i64("Maze::Generator", "h") as i32;
    return MazeGenerator { maze: new_maze(pool, width, height), result_val: 0 };
}

fn prepare(s: MazeGenerator&) {}

fn run(s: MazeGenerator&, iteration_id: i64) {
    s.maze.reset();
    s.maze.generate();
    s.result_val += s.maze.middle_cell().kind.value();
}

fn checksum(s: MazeGenerator&) -> u32 { s.result_val + s.maze.checksum() }

// --- Maze::BFS ------------------------------------------------------------------

// The path, a growable array, is the last field.
struct MazeBFS { maze: Maze, result_val: u32, path: (Cell&)[>..] }

fn new_maze_bfs(pool: slice_pool<Cell>&) -> MazeBFS {
    let width = config_i64("Maze::BFS", "w") as i32;
    let height = config_i64("Maze::BFS", "h") as i32;
    return MazeBFS { maze: new_maze(pool, width, height), result_val: 0, path: [] };
}

// A cell the search reached, and the index in the path array of the one it
// was reached from (-1 for the start).
struct PathNode { cell: Cell&, parent: i32 }

fn bfs(s: MazeBFS&, start: Cell&, target: Cell&) -> (Cell&)[>..] {
    if start .== target { return [start]; }

    // The queue's front is `head`: std has no deque.
    var queue: i32[>..] = [];
    var head = 0;
    var visited_rows = slice_pool<bool> {};
    var visited: (bool[:])[>..] = [];
    for y in s.maze.h { visited.push(visited_rows.alloc_slice(s.maze.w)); }
    var path: PathNode[>..] = [];

    visited[start.y][start.x] = true;
    path.push(PathNode { cell: start, parent: -1 });
    queue.push(0);

    while head < queue.len {
        let path_id = queue[head];
        head++;
        let cell .= path[path_id].cell;

        for &neighbor in cell.neighbors {
            if neighbor .== target {
                var res: (Cell&)[>..] = [];
                res.push(target);
                var current = path_id;
                while current >= 0 {
                    res.push(path[current].cell);
                    current = path[current].parent;
                }
                res.reverse();
                return res;
            }

            if neighbor.kind.is_walkable() && !visited[neighbor.y][neighbor.x] {
                visited[neighbor.y][neighbor.x] = true;
                path.push(PathNode { cell: neighbor, parent: path_id });
                queue.push((path.len - 1) as i32);
            }
        }
    }

    return [];
}

fn prepare(s: MazeBFS&) { s.maze.generate(); }

fn run(s: MazeBFS&, iteration_id: i64) {
    // The new path is not built over the old one while the search reads s:
    // it is built in a local, and copied.
    let path = s.bfs(s.maze.get_start(), s.maze.get_finish());
    s.path = copy(path);
    s.result_val += s.path.len as! u32;
}

fn checksum(s: MazeBFS&) -> u32 {
    let mid_cell_value: u32 = if s.path.len > 0 {
        let cell .= s.path[s.path.len / 2];
        (cell.x * cell.y) as! u32
    } else {
        0
    };
    return s.result_val + mid_cell_value;
}

// --- Maze::AStar ----------------------------------------------------------------

// An open-set entry, ordered by f score and then by cell index.
struct Node { f_score: i32, idx: i32 }

fn before(a: Node, b: Node) -> bool {
    a.f_score < b.f_score || (a.f_score == b.f_score && a.idx < b.idx)
}

struct MazeAStar { maze: Maze, result_val: u32, path: (Cell&)[>..] }

fn new_maze_astar(pool: slice_pool<Cell>&) -> MazeAStar {
    let width = config_i64("Maze::AStar", "w") as i32;
    let height = config_i64("Maze::AStar", "h") as i32;
    return MazeAStar { maze: new_maze(pool, width, height), result_val: 0, path: [] };
}

fn heuristic(s: MazeAStar&, a: Cell&, b: Cell&) -> i32 { abs(a.x - b.x) + abs(a.y - b.y) }

fn astar(s: MazeAStar&, start: Cell&, target: Cell&) -> (Cell&)[>..] {
    if start .== target { return [start]; }

    let width = s.maze.w;
    let size = width * s.maze.h;

    fn idx(y: i32, x: i32) -> i32 { y * width + x }

    var came_from: i32[>..] = [];
    came_from.resize(size, -1);
    var g_score: i32[>..] = [];
    g_score.resize(size, I32_MAX);
    var best_f: i32[>..] = [];
    best_f.resize(size, I32_MAX);

    let start_idx = idx(start.y, start.x);
    let target_idx = idx(target.y, target.x);

    var open_set: Node[>..<] = [];

    g_score[start_idx] = 0;
    let f_start = s.heuristic(start, target);
    open_set.heap_push(Node { f_score: f_start, idx: start_idx }, before);
    best_f[start_idx] = f_start;

    while open_set.len > 0 {
        let current_idx = open_set.heap_pop(before).idx;

        if current_idx == target_idx {
            var res: (Cell&)[>..] = [];
            var cur = current_idx;
            while cur != -1 {
                let y = cur / width;
                let x = cur % width;
                res.push(s.maze.get_cell(x, y));
                cur = came_from[cur];
            }
            res.reverse();
            return res;
        }

        let current_y = current_idx / width;
        let current_x = current_idx % width;
        let current .= s.maze.get_cell(current_x, current_y);
        let current_g = g_score[current_idx];

        for &neighbor in current.neighbors {
            if !neighbor.kind.is_walkable() { continue; }
            let neighbor_idx = idx(neighbor.y, neighbor.x);
            let tentative_g = current_g + 1;

            if tentative_g < g_score[neighbor_idx] {
                came_from[neighbor_idx] = current_idx;
                g_score[neighbor_idx] = tentative_g;
                let f_new = tentative_g + s.heuristic(neighbor, target);

                if f_new < best_f[neighbor_idx] {
                    best_f[neighbor_idx] = f_new;
                    open_set.heap_push(Node { f_score: f_new, idx: neighbor_idx }, before);
                }
            }
        }
    }

    return [];
}

fn prepare(s: MazeAStar&) { s.maze.generate(); }

fn run(s: MazeAStar&, iteration_id: i64) {
    // The new path is not built over the old one while the search reads s:
    // it is built in a local, and copied.
    let path = s.astar(s.maze.get_start(), s.maze.get_finish());
    s.path = copy(path);
    s.result_val += s.path.len as! u32;
}

fn checksum(s: MazeAStar&) -> u32 {
    let mid_cell_value: u32 = if s.path.len > 0 {
        let cell .= s.path[s.path.len / 2];
        (cell.x * cell.y) as! u32
    } else {
        0
    };
    return s.result_val + mid_cell_value;
}
