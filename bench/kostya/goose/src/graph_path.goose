// Graph::BFS, Graph::DFS and Graph::AStar: the shortest path from the first
// to the last vertex of one random graph, found by breadth-first search, by
// depth-first search with pruning, and by A* with a distance heuristic.

namespace graph_path;

import std;
import bench;
import helper;

let I32_MAX: i32 = 2147483647;

// --- the graph ------------------------------------------------------------------

// The adjacency lists in compressed sparse row form: `edges` holds every
// vertex's neighbours back to back, each list in the order its edges were
// added, and vertex v's list is edges[first[v]..first[v + 1]]. The arrays
// belong to the benchmark function; a Graph is a view of them.
struct Graph { edges: const i32[:], first: const i32[:] }

fn vertex_count(graph: Graph&) -> i64 { graph.first.len - 1 }

fn neighbors(graph: Graph&, v: i32) -> const i32[:] {
    graph.edges[graph.first[v]..graph.first[v + 1]]
}

struct Edge { u: i32, v: i32 }

// The edges are generated first, in the order the other implementations add
// them, so that each list can be laid out once its length is known.
fn generate_random(b: Bench&, edges: i32[>..]&, first: i32[>..]&) -> Graph {
    let vertices = config_i64(b.name, "vertices") as i32;
    let jumps = config_i64(b.name, "jumps");
    let jump_len = config_i64(b.name, "jump_len") as i32;

    var added: Edge[>..] = [];
    for i: i32 in 1..vertices { added.push(Edge { u: i, v: i - 1 }); }
    for v: i32 in 0..vertices {
        let num_jumps = next_int(jumps);
        for j in num_jumps {
            let offset = next_int(jump_len) - jump_len / 2;
            let u = v + offset;
            if u >= 0 && u < vertices && u != v { added.push(Edge { u: v, v: u }); }
        }
    }

    // Count the edges at each vertex, turn the counts into starting
    // positions, then put every edge into both its ends' lists.
    first.resize(vertices + 1, 0);
    for e in added {
        first[e.u + 1]++;
        first[e.v + 1]++;
    }
    for v in vertices { first[v + 1] += first[v]; }
    var cursor: i32[>..] = [];
    cursor.append(first[..vertices]);
    edges.resize(added.len * 2, 0);
    for e in added {
        edges[cursor[e.u]] = e.v;
        cursor[e.u]++;
        edges[cursor[e.v]] = e.u;
        cursor[e.v]++;
    }
    return Graph { edges: edges, first: first };
}

// --- Graph::BFS -----------------------------------------------------------------

struct Step { v: i32, dist: i32 }

fn bfs_shortest_path(graph: Graph, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var visited: bool[>..] = [];
    visited.resize(graph.vertex_count(), false);
    // Every vertex enters the queue at most once, so the queue only grows,
    // and `head` is the next one to leave it.
    var queue: Step[>..] = [];
    var head = 0;
    visited[start] = true;
    queue.push(Step { v: start, dist: 0 });
    while head < queue.len {
        let cur = queue[head];
        head++;
        for neighbor in graph.neighbors(cur.v) {
            if neighbor == target { return cur.dist + 1; }
            if !visited[neighbor] {
                visited[neighbor] = true;
                queue.push(Step { v: neighbor, dist: cur.dist + 1 });
            }
        }
    }
    return -1;
}

fn bfs(b: Bench&) -> u32 {
    var edges: i32[>..] = [];
    var first: i32[>..] = [];
    let graph = generate_random(b, edges, first);
    let target = (graph.vertex_count() - 1) as i32;
    var result: u32 = 0;
    measure(b) { result += bfs_shortest_path(graph, 0, target) as! u32; };
    return result;
}

// --- Graph::DFS -----------------------------------------------------------------

fn dfs_find_path(graph: Graph, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var visited: bool[>..] = [];
    visited.resize(graph.vertex_count(), false);
    var stack: Step[>..<] = [];
    var best_path = I32_MAX;
    stack.push(Step { v: start, dist: 0 });
    while stack.len > 0 {
        let cur = stack.pop();
        if visited[cur.v] || cur.dist >= best_path { continue; }
        visited[cur.v] = true;
        for neighbor in graph.neighbors(cur.v) {
            if neighbor == target {
                if cur.dist + 1 < best_path { best_path = cur.dist + 1; }
            } else if !visited[neighbor] {
                stack.push(Step { v: neighbor, dist: cur.dist + 1 });
            }
        }
    }
    return if best_path == I32_MAX { -1 } else { best_path };
}

fn dfs(b: Bench&) -> u32 {
    var edges: i32[>..] = [];
    var first: i32[>..] = [];
    let graph = generate_random(b, edges, first);
    let target = (graph.vertex_count() - 1) as i32;
    var result: u32 = 0;
    measure(b) { result += dfs_find_path(graph, 0, target) as! u32; };
    return result;
}

// --- Graph::AStar ---------------------------------------------------------------

// Ordered by f_score, then by vertex.
struct Node { f_score: i32, vertex: i32 }

fn before(a: Node, b: Node) -> bool {
    a.f_score < b.f_score || (a.f_score == b.f_score && a.vertex < b.vertex)
}

// What the search knows about a vertex: the length of the shortest path to
// it found so far, and the lowest estimate it has entered the open set with.
struct Score { g: i32, best_f: i32 }

fn heuristic(v: i32, target: i32) -> i32 { target - v }

fn a_star_shortest_path(graph: Graph, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var score: Score[>..] = [];
    score.resize(graph.vertex_count(), Score { g: I32_MAX, best_f: I32_MAX });
    let f_start = heuristic(start, target);
    score[start] = Score { g: 0, best_f: f_start };
    var open_set: Node[>..<] = [];
    open_set.heap_push(Node { f_score: f_start, vertex: start }) { a, b => before(a, b) };
    while open_set.len > 0 {
        let current = open_set.heap_pop() { a, b => before(a, b) };
        if current.vertex == target { return score[current.vertex].g; }
        for neighbor in graph.neighbors(current.vertex) {
            let tentative_g = score[current.vertex].g + 1;
            let s .= score[neighbor];
            if tentative_g < s.g {
                s.g = tentative_g;
                let f_new = tentative_g + heuristic(neighbor, target);
                if f_new < s.best_f {
                    s.best_f = f_new;
                    open_set.heap_push(Node { f_score: f_new, vertex: neighbor }) { a, b => before(a, b) };
                }
            }
        }
    }
    return -1;
}

fn astar(b: Bench&) -> u32 {
    var edges: i32[>..] = [];
    var first: i32[>..] = [];
    let graph = generate_random(b, edges, first);
    let target = (graph.vertex_count() - 1) as i32;
    var result: u32 = 0;
    measure(b) { result += a_star_shortest_path(graph, 0, target) as! u32; };
    return result;
}
