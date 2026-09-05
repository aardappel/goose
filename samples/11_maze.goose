// Shortest paths through a maze, two ways: breadth-first search for the
// fewest steps, then Dijkstra for the cheapest route once water costs
// three steps a square. The maze is a string literal, the grid a flat byte
// array indexed by y * W + x, the BFS queue a grow-only array with a read
// head, and the priority queue a grow-shrink array used as a binary heap
// through the library's heap_push/heap_pop with a comparator block.
//
// Build and run (from this directory):
//   goose -o maze.c 11_maze.goose && cl maze.c && maze

import std;

let MAZE = "####################\n#S.....#...........#\n#.####.#.#########.#\n#.#....#.#.......#.#\n#.#.####.#.#####.#.#\n#.#....~~~~~.....#.#\n#.####.~~~~~.#####.#\n#......~~~~~.......#\n#.#####.####.#####.#\n#..................#\n#.#########.####.#E#\n####################";

struct Cell { x: i32, y: i32 }
struct Entry { cost: i32, at: i32 }    // heap entries: cost first

// Grid state, filled in by main and read by the searches.
var grid: u8[>..] = [];
var W = 0;
var H = 0;

fn at(x: i64, y: i64) -> u8 { grid[y * W + x] }
fn passable(x: i64, y: i64) -> bool { x >= 0 && y >= 0 && x < W && y < H && at(x, y) != '#' }
fn step_cost(x: i64, y: i64) -> i32 { if at(x, y) == '~' { 3 } else { 1 } }

// The four neighbours of a cell index, as an array the caller iterates.
fn neighbours(i: i64) -> i64[4] {
    [i - 1, i + 1, i - W, i + W]
}

fn find_char(c: u8) -> i64 {
    for g, i in grid { if g == c { return i; } }
    abort("maze has no such cell");
}

// Breadth-first: every edge costs one, so the first visit is the shortest.
// `parent` records how each cell was reached, for drawing the path.
fn bfs(start: i64, goal: i64, parent: i32[>..]&) -> i64 {
    // The queue never pops: a read head walks it while pushes extend it, so
    // it is a grow-only array, freed whole when the function returns.
    var queue: i32[>..] = [];
    queue.push(start as i32);
    parent[start] = start as i32;
    var head = 0;
    while head < queue.len {
        let i = queue[head] as i64;
        head++;
        if i == goal { break; }
        for n in neighbours(i) {
            if passable(n % W, n / W) && parent[n] < 0 {
                parent[n] = i as i32;
                queue.push(n as i32);
            }
        }
    }
    // Count the steps back to the start.
    var steps = 0;
    var i = goal;
    while i != start { i = parent[i]; steps++; }
    return steps;
}

// Dijkstra: pop the cheapest frontier cell, relax its neighbours. Stale
// heap entries (a cell reached again more cheaply) are skipped when popped.
fn dijkstra(start: i64, goal: i64, parent: i32[>..]&) -> i32 {
    var dist: i32[>..] = [];
    dist.resize(grid.len, 1000000);
    var heap: Entry[>..<] = [];          // grow-shrink: heap_pop shrinks it
    dist[start] = 0;
    parent[start] = start as i32;
    heap.heap_push(Entry { cost: 0, at: start as i32 }) { a, b => a.cost < b.cost };
    while heap.len > 0 {
        let e = heap.heap_pop() { a, b => a.cost < b.cost };
        let i = e.at as i64;
        if e.cost > dist[i] { continue; }
        if i == goal { break; }
        for n in neighbours(i) {
            guard passable(n % W, n / W) else { continue; }
            let c = e.cost + step_cost(n % W, n / W);
            if c < dist[n] {
                dist[n] = c;
                parent[n] = i as i32;
                heap.heap_push(Entry { cost: c, at: n as i32 }) { a, b => a.cost < b.cost };
            }
        }
    }
    return dist[goal];
}

// The cost of a found path, summed back along the parent links.
fn path_cost(parent: i32[:], start: i64, goal: i64) -> i32 {
    var cost: i32 = 0;
    var i = goal;
    while i != start {
        cost += step_cost(i % W, i / W);
        i = parent[i];
    }
    return cost;
}

// The maze with the found path drawn onto a copy of the grid.
fn show(parent: i32[:], start: i64, goal: i64) {
    var pic = copy(grid);
    var i = goal;
    while i != start {
        i = parent[i];
        if i != start { pic[i] = if grid[i] == '~' { 'o' } else { '*' }; }
    }
    for y in H { print(pic[y * W..(y + 1) * W]); }
}

fn main() {
    // Rows of the literal become the grid; the width is the first row's.
    each_split(MAZE, '\n') {
        if W == 0 { W = it.len; }
        assert(it.len == W);
        grid.append(it);
        H++;
    };
    let start = find_char('S');
    let goal = find_char('E');
    print(W, "x", H, " maze, start at ", start % W, ",", start / W, " goal at ", goal % W, ",", goal / W);

    var parent: i32[>..] = [];
    parent.resize(grid.len, -1);
    let steps = bfs(start, goal, parent);
    print("BFS: ", steps, " steps, cost ", path_cost(parent, start, goal), " with water at 3");
    show(parent, start, goal);

    fill(parent, -1);
    let cost = dijkstra(start, goal, parent);
    print("Dijkstra: cost ", cost);
    show(parent, start, goal);
}
