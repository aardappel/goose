// Maze::Generator, Maze::BFS and Maze::AStar: a grid maze dug out from the
// start by a randomized depth-first walk, then searched breadth-first and
// with A*.

namespace maze;

import std;
import bench;
import helper;

enum Kind { Wall, Space, Start, Finish, Border, Path }

fn is_walkable(k: Kind) -> bool {
    match k {
        Space, Start, Finish => true,
        _ => false,
    }
}

fn value(k: Kind) -> u32 {
    match k {
        Wall => 0,
        Space => 1,
        Start => 2,
        Finish => 3,
        Border => 4,
        Path => 5,
    }
}

// A cell, and links to its four neighbors in the shuffled order the walk
// tries them: 4-byte offsets to cells of the same array. Border cells have
// no neighbors.
struct Cell { kind: Kind, neighbors: (Cell&<u32>)[..4] }

// The cells row by row: cell (y, x) is cells[y * width + x], so a cell's
// index gives its position.
struct Maze { width: i64, height: i64, cells: Cell[>..] }

// A cell's row and column.
struct Pos { y: i64, x: i64 }

fn pos_of(m: Maze&, i: i64) -> Pos { Pos { y: i / m.width, x: i % m.width } }

// Builds the cells of a maze declared empty (`Maze { .. }`). A value holding
// self-relative links is constructed where it stays rather than returned.
fn build(m: Maze&, w: i64, h: i64) {
    m.width = max(w, 5);
    m.height = max(h, 5);
    for i in m.width * m.height { m.cells.push(Cell { kind: Kind.Wall, neighbors: [] }); }
    link_neighbors(m);
}

fn cell_at(m: Maze&, y: i64, x: i64) -> Cell& { m.cells[y * m.width + x] }
fn start_of(m: Maze&) -> Cell& { cell_at(m, 1, 1) }
fn finish_of(m: Maze&) -> Cell& { cell_at(m, m.height - 2, m.width - 2) }
fn middle_cell(m: Maze&) -> Cell& { cell_at(m, m.height / 2, m.width / 2) }

fn link_neighbors(m: Maze&) {
    let w = m.width;
    let h = m.height;
    for y in h {
        for x in w {
            let c = y * w + x;
            if x == 0 || y == 0 || x == w - 1 || y == h - 1 {
                m.cells[c].kind = Kind.Border;
            } else {
                var order: i64[4] = [c - w, c + w, c + 1, c - 1];
                for t in 4 {
                    let i = next_int(4);
                    let j = next_int(4);
                    if i != j { swap(order[i], order[j]); }
                }
                for n in order { m.cells[c].neighbors.push(m.cells[n]); }
            }
        }
    }
    start_of(m).kind = Kind.Start;
    finish_of(m).kind = Kind.Finish;
}

fn reset(m: Maze&) {
    for &c in m.cells {
        if c.kind == Kind.Space { c.kind = Kind.Wall; }
    }
    start_of(m).kind = Kind.Start;
    finish_of(m).kind = Kind.Finish;
}

fn walkable_neighbors(c: Cell&) -> i64 { count(c.neighbors) { is_walkable(it.kind) } }

fn dig(start: Cell&) {
    var stack: Cell&[>..<] = [];
    stack.push(start);
    while stack.len > 0 {
        let c: Cell& = stack[stack.len - 1];
        stack.pop();
        if walkable_neighbors(c) == 1 {
            c.kind = Kind.Space;
            for &n in c.neighbors {
                if n.kind == Kind.Wall { stack.push(n); }
            }
        }
    }
}

recursive fn ensure_open_finish(c: Cell&) {
    c.kind = Kind.Space;
    // Which neighbors are walls before any of them is opened.
    var walls: i64[..4] = [];
    for n, i in c.neighbors {
        if n.kind == Kind.Wall { walls.push(i); }
    }
    if walkable_neighbors(c) > 1 { return; }
    for i in walls { ensure_open_finish(c.neighbors[i]); }
}

fn generate(m: Maze&) {
    for &n in start_of(m).neighbors {
        if n.kind == Kind.Wall { dig(n); }
    }
    for &n in finish_of(m).neighbors {
        if n.kind == Kind.Wall { ensure_open_finish(n); }
    }
}

// FNV-1a style hash of x * y over the open cells, row by row.
fn checksum(m: Maze&) -> u32 {
    var hasher: u32 = 2166136261;
    let prime: u32 = 16777619;
    for &c, i in m.cells {
        if c.kind == Kind.Space {
            let p = pos_of(m, i);
            hasher = (hasher ^ (p.x * p.y) as! u32) * prime;
        }
    }
    return hasher;
}

// Maze::Generator: the maze dug out again on every run.
fn generator(b: Bench&) -> u32 {
    var m = Maze { .. };
    build(m, config_i64(b.name, "w"), config_i64(b.name, "h"));
    var result: u32 = 0;
    measure(b) {
        reset(m);
        generate(m);
        result += value(middle_cell(m).kind);
    };
    return result + checksum(m);
}

// --- the search benchmarks ----------------------------------------------------

// Maze::BFS and Maze::AStar: Search(maze, from, to) finds a path through a
// generated maze, and the checksum is the summed path lengths plus x * y of
// the middle cell of the last path.
fn search_bench<Search>(b: Bench&) -> u32 {
    var m = Maze { .. };
    build(m, config_i64(b.name, "w"), config_i64(b.name, "h"));
    generate(m);
    var path: Cell&[>..] = [];
    var result: u32 = 0;
    measure(b) {
        path = Search(m, start_of(m), finish_of(m));
        result += path.len as! u32;
    };
    if path.len > 0 {
        let i = m.cells.index_of(path[path.len / 2]);
        let p = pos_of(m, i);
        result += (p.x * p.y) as! u32;
    }
    return result;
}

fn bfs_bench(b: Bench&) -> u32 { search_bench(b, bfs) }
fn astar_bench(b: Bench&) -> u32 { search_bench(b, astar) }

// Puts a path traced back from its end into walking order. The swaps rebind
// the references (`.=`), where std's reverse would assign through them.
fn reverse_path(path: Cell&[:]) {
    var i = 0;
    var j = path.len - 1;
    while i < j {
        let t .= path[i];
        path[i] .= path[j];
        path[j] .= t;
        i++;
        j--;
    }
}

// --- breadth-first search ----------------------------------------------------

// A cell the search reached, and the step it was reached from (-1 for the
// start).
struct Step { cell: Cell&, parent: i32 }

fn bfs(m: Maze&, start: Cell&, target: Cell&) -> Cell&[>..] {
    var path: Cell&[>..] = [];
    if start .== target {
        path.push(start);
        return path;
    }
    var visited: bool[>..] = [];
    visited.resize(m.cells.len, false);
    // The steps in the order they are reached, which is also the queue:
    // steps[head..] are still to be expanded.
    var steps: Step[>..] = [];
    var head = 0;

    visited[m.cells.index_of(start)] = true;
    steps.push(Step { cell: start, parent: -1 });

    while head < steps.len {
        let id = head;
        head++;
        let c .= steps[id].cell;
        for &n in c.neighbors {
            if n .== target {
                path.push(target);
                var cur = id;
                while cur >= 0 {
                    path.push(steps[cur].cell);
                    cur = steps[cur].parent;
                }
                reverse_path(path);
                return path;
            }
            let i = m.cells.index_of(n);
            if is_walkable(n.kind) && !visited[i] {
                visited[i] = true;
                steps.push(Step { cell: n, parent: id as i32 });
            }
        }
    }
    return path;
}

// --- A* ------------------------------------------------------------------------

// i32's maximum: a cell no path has reached yet.
let NO_SCORE = 2147483647;

// The search state of one cell.
struct Score { came_from: i32, g: i32, best_f: i32 }

// An open-set entry, ordered by f score and then by cell index.
struct Node { f: i32, idx: i32 }

fn before(a: Node, b: Node) -> bool { a.f < b.f || (a.f == b.f && a.idx < b.idx) }

fn heuristic(a: Pos, b: Pos) -> i32 { (abs(a.y - b.y) + abs(a.x - b.x)) as i32 }

fn astar(m: Maze&, start: Cell&, target: Cell&) -> Cell&[>..] {
    var path: Cell&[>..] = [];
    if start .== target {
        path.push(start);
        return path;
    }
    let start_idx = m.cells.index_of(start);
    let target_idx = m.cells.index_of(target);
    let target_pos = pos_of(m, target_idx);
    var scores: Score[>..] = [];
    scores.resize(m.cells.len, Score { came_from: -1, g: NO_SCORE, best_f: NO_SCORE });
    var open_set: Node[>..<] = [];

    scores[start_idx].g = 0;
    let f_start = heuristic(pos_of(m, start_idx), target_pos);
    open_set.heap_push(Node { f: f_start, idx: start_idx as i32 }, before);
    scores[start_idx].best_f = f_start;

    while open_set.len > 0 {
        let current = open_set.heap_pop(before).idx;
        if current == target_idx {
            var cur = current;
            while cur != -1 {
                path.push(m.cells[cur]);
                cur = scores[cur].came_from;
            }
            reverse_path(path);
            return path;
        }
        let tentative_g = scores[current].g + 1;
        let c .= m.cells[current];
        for &n in c.neighbors {
            if !is_walkable(n.kind) { continue; }
            let i = m.cells.index_of(n);
            let s .= scores[i];
            if tentative_g < s.g {
                s.came_from = current;
                s.g = tentative_g;
                let f_new = tentative_g + heuristic(pos_of(m, i), target_pos);
                if f_new < s.best_f {
                    s.best_f = f_new;
                    open_set.heap_push(Node { f: f_new, idx: i as i32 }, before);
                }
            }
        }
    }
    return path;
}
