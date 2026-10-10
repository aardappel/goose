// Graph::BFS, Graph::DFS and Graph::AStar: the shortest path from the first
// to the last vertex of one random graph, found by breadth-first search, by
// depth-first search with pruning, and by A* with a distance heuristic.

namespace graph_path;

import std;
import bench;
import helper;

let I32_MAX: i32 = 2147483647;

// --- the graph ------------------------------------------------------------------

// Each adjacency list is a run of the slice pool `edges`, which the
// benchmark owns, grown by one element per edge.
//
// This follows the other implementations' list of growable lists as closely
// as Goose allows, for the sake of comparing like with like, but it is not
// idiomatic Goose. We would collect the edges first and then lay all the lists
// out back to back in one array, each vertex's list found through an offset
// (compressed sparse rows): a third less memory, and Graph::DFS runs about
// 1.4x and Graph::BFS about 1.1x faster.
struct Graph { vertices: i32, jumps: i32, jump_len: i32, adj: (i32[:])[>..] }

fn new_graph(vertices: i32, jumps: i32, jump_len: i32) -> Graph {
    var graph = Graph { vertices: vertices, jumps: jumps, jump_len: jump_len, adj: [] };
    for v in vertices { graph.adj.push(default<i32[:]>()); }
    return graph;
}

fn add_edge(graph: Graph&, edges: i32[>..]&, u: i32, v: i32) {
    if u < graph.adj.len && v < graph.adj.len {
        let nu = graph.adj[u].len;
        graph.adj[u] = edges.realloc_slice(graph.adj[u], nu + 1);
        graph.adj[u][nu] = v;
        let nv = graph.adj[v].len;
        graph.adj[v] = edges.realloc_slice(graph.adj[v], nv + 1);
        graph.adj[v][nv] = u;
    }
}

fn generate_random(graph: Graph&, edges: i32[>..]&) {
    for i: i32 in 1..graph.vertices { graph.add_edge(edges, i, i - 1); }

    for v: i32 in 0..graph.vertices {
        let num_jumps = next_int(graph.jumps);
        for j in num_jumps {
            let offset = next_int(graph.jump_len) - graph.jump_len / 2;
            let u = v + offset;
            if u >= 0 && u < graph.vertices && u != v { graph.add_edge(edges, v, u); }
        }
    }
}

// GraphPathBenchmark::prepare: the graph for benchmark `b`'s configuration.
fn prepare(b: Bench&, edges: i32[>..]&) -> Graph {
    let vertices = config_i64(b.name, "vertices") as i32;
    let jumps = config_i64(b.name, "jumps") as i32;
    let jump_len = config_i64(b.name, "jump_len") as i32;
    var graph = new_graph(vertices, jumps, jump_len);
    graph.generate_random(edges);
    return graph;
}

// --- Graph::BFS -----------------------------------------------------------------

struct Step { v: i32, dist: i32 }

fn bfs_shortest_path(graph: Graph&, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var visited: bool[>..] = [];
    visited.resize(graph.vertices, false);
    // The queue's front is `head`: std has no deque, and the steps it has
    // handed out stay in place until the search returns.
    var queue: Step[>..] = [];
    var head = 0;
    visited[start] = true;
    queue.push(Step { v: start, dist: 0 });
    while head < queue.len {
        let cur = queue[head];
        head++;
        for neighbor in graph.adj[cur.v] {
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
    reusable[] var edges: i32[>..] = [];
    let graph = prepare(b, edges);
    var result_val: u32 = 0;
    measure(b) { result_val += bfs_shortest_path(graph, 0, graph.vertices - 1) as! u32; };
    return result_val;
}

// --- Graph::DFS -----------------------------------------------------------------

fn dfs_find_path(graph: Graph&, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var visited: bool[>..] = [];
    visited.resize(graph.vertices, false);
    var stack: Step[>..<] = [];
    var best_path = I32_MAX;
    stack.push(Step { v: start, dist: 0 });
    while stack.len > 0 {
        let cur = stack.pop();
        if visited[cur.v] || cur.dist >= best_path { continue; }
        visited[cur.v] = true;
        for neighbor in graph.adj[cur.v] {
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
    reusable[] var edges: i32[>..] = [];
    let graph = prepare(b, edges);
    var result_val: u32 = 0;
    measure(b) { result_val += dfs_find_path(graph, 0, graph.vertices - 1) as! u32; };
    return result_val;
}

// --- Graph::AStar ---------------------------------------------------------------

// Ordered by f_score, then by vertex.
struct Node { f_score: i32, vertex: i32 }

fn before(a: Node, b: Node) -> bool {
    a.f_score < b.f_score || (a.f_score == b.f_score && a.vertex < b.vertex)
}

fn heuristic(v: i32, target: i32) -> i32 { target - v }

fn a_star_shortest_path(graph: Graph&, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var g_score: i32[>..] = [];
    g_score.resize(graph.vertices, I32_MAX);
    var best_f: i32[>..] = [];
    best_f.resize(graph.vertices, I32_MAX);

    g_score[start] = 0;
    let f_start = heuristic(start, target);
    best_f[start] = f_start;

    var open_set: Node[>..<] = [];
    open_set.heap_push(Node { f_score: f_start, vertex: start }, before);

    while open_set.len > 0 {
        let current = open_set.heap_pop(before);
        if current.vertex == target { return g_score[current.vertex]; }
        for neighbor in graph.adj[current.vertex] {
            let tentative_g = g_score[current.vertex] + 1;
            if tentative_g < g_score[neighbor] {
                g_score[neighbor] = tentative_g;
                let f_new = tentative_g + heuristic(neighbor, target);
                if f_new < best_f[neighbor] {
                    best_f[neighbor] = f_new;
                    open_set.heap_push(Node { f_score: f_new, vertex: neighbor }, before);
                }
            }
        }
    }
    return -1;
}

fn astar(b: Bench&) -> u32 {
    reusable[] var edges: i32[>..] = [];
    let graph = prepare(b, edges);
    var result_val: u32 = 0;
    measure(b) { result_val += a_star_shortest_path(graph, 0, graph.vertices - 1) as! u32; };
    return result_val;
}
