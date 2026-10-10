// Maze::Generator, Maze::BFS and Maze::AStar: a grid maze dug out from the
// start by a randomized depth-first walk, then searched breadth-first and
// with A*.

namespace maze;

import std;
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

// A (row, column) pair.
struct Pos { y: i64, x: i64 }

struct Cell { kind: CellKind, neighbors: Pos[4], x: i64, y: i64 }

fn new_cell(x: i64, y: i64) -> Cell {
    Cell { kind: CellKind.Wall, neighbors: [Pos { y: 0, x: 0 }; 4], x: x, y: y }
}

fn reset(cell: Cell&) {
    if cell.kind == CellKind.Space { cell.kind = CellKind.Wall; }
}

// An array cannot hold growable arrays, so the rows are runs of a slice pool
// the benchmark owns.
struct Maze { width: i64, height: i64, cells: (Cell[:])[>..] }

fn new_maze(pool: Cell[>..]&, width: i64, height: i64) -> Maze {
    var maze = Maze { width: max(width, 5), height: max(height, 5), cells: [] };
    for y in maze.height {
        let row = pool.alloc_slice(maze.width);
        for x in maze.width { row[x] = new_cell(x, y); }
        maze.cells.push(row);
    }
    maze.link_neighbors();
    return maze;
}

fn link_neighbors(maze: Maze&) {
    for y in maze.height {
        for x in maze.width {
            if x == 0 || y == 0 || x == maze.width - 1 || y == maze.height - 1 {
                maze.cells[y][x].kind = CellKind.Border;
            } else {
                var neighbors: Pos[4] = [Pos { y: y - 1, x: x }, Pos { y: y + 1, x: x },
                                         Pos { y: y, x: x + 1 }, Pos { y: y, x: x - 1 }];
                for t in 4 {
                    let i = next_int(4);
                    let j = next_int(4);
                    if i != j { neighbors.swap_at(i, j); }
                }
                maze.cells[y][x].neighbors = neighbors;
            }
        }
    }
    maze.cells[1][1].kind = CellKind.Start;
    maze.cells[maze.height - 2][maze.width - 2].kind = CellKind.Finish;
}

fn reset(maze: Maze&) {
    for row in maze.cells {
        for &cell in row { cell.reset(); }
    }
    maze.cells[1][1].kind = CellKind.Start;
    maze.cells[maze.height - 2][maze.width - 2].kind = CellKind.Finish;
}

fn dig(maze: Maze&, start: Pos) {
    var stack: Pos[>..<] = [];
    stack.push(start);
    while stack.len > 0 {
        let p = stack.pop();
        let walkable_neighbors = count(maze.cells[p.y][p.x].neighbors) {
            maze.cells[it.y][it.x].kind.is_walkable()
        };
        if walkable_neighbors == 1 {
            maze.cells[p.y][p.x].kind = CellKind.Space;
            for n in maze.cells[p.y][p.x].neighbors {
                if maze.cells[n.y][n.x].kind == CellKind.Wall { stack.push(n); }
            }
        }
    }
}

recursive fn ensure_open_finish(maze: Maze&, pos: Pos) {
    maze.cells[pos.y][pos.x].kind = CellKind.Space;

    // A limited array: a function cannot keep a growable local across its
    // recursive calls.
    var neighbors_to_check: Pos[..4] = [];
    for n in maze.cells[pos.y][pos.x].neighbors {
        if maze.cells[n.y][n.x].kind == CellKind.Wall { neighbors_to_check.push(n); }
    }

    let walkable_neighbors = count(maze.cells[pos.y][pos.x].neighbors) {
        maze.cells[it.y][it.x].kind.is_walkable()
    };

    if walkable_neighbors > 1 { return; }

    for neighbor_pos in neighbors_to_check { maze.ensure_open_finish(neighbor_pos); }
}

fn generate(maze: Maze&) {
    let start_neighbors = maze.cells[1][1].neighbors;
    for n in start_neighbors {
        if maze.cells[n.y][n.x].kind == CellKind.Wall { maze.dig(n); }
    }

    let finish_pos = Pos { y: maze.height - 2, x: maze.width - 2 };
    let finish_neighbors = maze.cells[finish_pos.y][finish_pos.x].neighbors;
    for n in finish_neighbors {
        if maze.cells[n.y][n.x].kind == CellKind.Wall { maze.ensure_open_finish(n); }
    }
}

fn middle_cell(maze: Maze&) -> Cell& { maze.cells[maze.height / 2][maze.width / 2] }

fn checksum(maze: Maze&) -> u32 {
    var hasher: u32 = 2166136261;
    let prime: u32 = 16777619;
    for row in maze.cells {
        for &cell in row {
            if cell.kind == CellKind.Space {
                let val = (cell.x * cell.y) as! u32;
                hasher = (hasher ^ val) * prime;
            }
        }
    }
    return hasher;
}

fn get_start(maze: Maze&) -> Pos { Pos { y: 1, x: 1 } }
fn get_finish(maze: Maze&) -> Pos { Pos { y: maze.height - 2, x: maze.width - 2 } }
fn get_cell(maze: Maze&, y: i64, x: i64) -> Cell& { maze.cells[y][x] }

// --- Maze::Generator ------------------------------------------------------------

fn generator(b: Bench&) -> u32 {
    reusable[] var pool: Cell[>..] = [];
    var maze = new_maze(pool, config_i64(b.name, "w"), config_i64(b.name, "h"));
    var result_val: u32 = 0;
    measure(b) {
        maze.reset();
        maze.generate();
        result_val += maze.middle_cell().kind.value();
    };
    return result_val + maze.checksum();
}

// --- Maze::BFS ------------------------------------------------------------------

// A cell the search reached, and the index in the path array of the one it
// was reached from (-1 for the start).
struct PathEntry { y: i64, x: i64, parent: i32 }

fn bfs(maze: Maze&, start: Pos, target: Pos) -> Pos[>..] {
    if start == target { return [start]; }

    let width = maze.width;
    let height = maze.height;

    // The queue's front is `head`: std has no deque.
    var queue: i64[>..] = [];
    var head = 0;
    reusable[] var visited_rows: bool[>..] = [];
    var visited: (bool[:])[>..] = [];
    for y in height { visited.push(visited_rows.alloc_slice(width)); }
    var path: PathEntry[>..] = [];

    visited[start.y][start.x] = true;
    path.push(PathEntry { y: start.y, x: start.x, parent: -1 });
    queue.push(0);

    while head < queue.len {
        let path_id = queue[head];
        head++;
        let y = path[path_id].y;
        let x = path[path_id].x;

        for n in maze.get_cell(y, x).neighbors {
            if n == target {
                var result: Pos[>..] = [];
                result.push(target);
                var current = path_id as i32;
                while current >= 0 {
                    result.push(Pos { y: path[current].y, x: path[current].x });
                    current = path[current].parent;
                }
                result.reverse();
                return result;
            }

            if maze.get_cell(n.y, n.x).kind.is_walkable() && !visited[n.y][n.x] {
                visited[n.y][n.x] = true;
                path.push(PathEntry { y: n.y, x: n.x, parent: path_id as i32 });
                queue.push(path.len - 1);
            }
        }
    }

    return [];
}

fn bfs_bench(b: Bench&) -> u32 {
    reusable[] var pool: Cell[>..] = [];
    var maze = new_maze(pool, config_i64(b.name, "w"), config_i64(b.name, "h"));
    maze.generate();
    var path: Pos[>..] = [];
    var result_val: u32 = 0;
    measure(b) {
        path = bfs(maze, maze.get_start(), maze.get_finish());
        result_val += path.len as! u32;
    };
    var mid_cell_value: u32 = 0;
    if path.len > 0 {
        let p = path[path.len / 2];
        mid_cell_value = (p.x * p.y) as! u32;
    }
    return result_val + mid_cell_value;
}

// --- Maze::AStar ----------------------------------------------------------------

// An open-set entry, ordered by f score and then by cell index.
struct Node { f: i32, idx: i64 }

fn before(a: Node, b: Node) -> bool { a.f < b.f || (a.f == b.f && a.idx < b.idx) }

fn heuristic(a: Pos, b: Pos) -> i32 {
    abs(a.y as i32 - b.y as i32) + abs(a.x as i32 - b.x as i32)
}

fn astar(maze: Maze&, start: Pos, target: Pos) -> Pos[>..] {
    if start == target { return [start]; }

    let width = maze.width;
    let height = maze.height;
    let size = width * height;

    fn idx(y: i64, x: i64) -> i64 { y * width + x }

    let start_idx = idx(start.y, start.x);
    let target_idx = idx(target.y, target.x);

    var came_from: i32[>..] = [];
    came_from.resize(size, -1);
    var g_score: i32[>..] = [];
    g_score.resize(size, I32_MAX);
    var best_f: i32[>..] = [];
    best_f.resize(size, I32_MAX);

    var open_set: Node[>..<] = [];

    g_score[start_idx] = 0;
    let f_start = heuristic(start, target);
    open_set.heap_push(Node { f: f_start, idx: start_idx }, before);
    best_f[start_idx] = f_start;

    while open_set.len > 0 {
        let current_idx = open_set.heap_pop(before).idx;
        if current_idx == target_idx {
            var path: Pos[>..] = [];
            var cur = current_idx as i32;
            while cur != -1 {
                let y = cur / width;
                let x = cur % width;
                path.push(Pos { y: y, x: x });
                cur = came_from[cur];
            }
            path.reverse();
            return path;
        }

        let current_y = current_idx / width;
        let current_x = current_idx % width;
        let current_g = g_score[current_idx];

        for n in maze.get_cell(current_y, current_x).neighbors {
            if !maze.get_cell(n.y, n.x).kind.is_walkable() { continue; }

            let neighbor_idx = idx(n.y, n.x);
            let tentative_g = current_g + 1;

            if tentative_g < g_score[neighbor_idx] {
                came_from[neighbor_idx] = current_idx as i32;
                g_score[neighbor_idx] = tentative_g;
                let f_new = tentative_g + heuristic(n, target);

                if f_new < best_f[neighbor_idx] {
                    best_f[neighbor_idx] = f_new;
                    open_set.heap_push(Node { f: f_new, idx: neighbor_idx }, before);
                }
            }
        }
    }

    return [];
}

fn astar_bench(b: Bench&) -> u32 {
    reusable[] var pool: Cell[>..] = [];
    var maze = new_maze(pool, config_i64(b.name, "w"), config_i64(b.name, "h"));
    maze.generate();
    var path: Pos[>..] = [];
    var result_val: u32 = 0;
    measure(b) {
        path = astar(maze, maze.get_start(), maze.get_finish());
        result_val += path.len as! u32;
    };
    var mid_cell_value: u32 = 0;
    if path.len > 0 {
        let p = path[path.len / 2];
        mid_cell_value = (p.x * p.y) as! u32;
    }
    return result_val + mid_cell_value;
}
