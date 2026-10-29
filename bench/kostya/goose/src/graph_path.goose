// Graph::BFS, Graph::DFS and Graph::AStar: the shortest path from the first
// to the last vertex of one random graph, found by breadth-first search, by
// depth-first search with pruning, and by A* with a distance heuristic.

namespace graph_path;

import std;
import slice_pool;
import bench;
import helper;

let I32_MAX: i32 = 2147483647;

// --- the graph ------------------------------------------------------------------

// Each adjacency list is a run of the slice pool `edges` (see
// GraphPathBenchmark), grown by one element per edge.
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

fn add_edge(graph: Graph&, edges: slice_pool<i32>&, u: i32, v: i32) {
    if u < graph.adj.len && v < graph.adj.len {
        let nu = graph.adj[u].len;
        graph.adj[u] = edges.realloc_slice(graph.adj[u], nu + 1);
        graph.adj[u][nu] = v;
        let nv = graph.adj[v].len;
        graph.adj[v] = edges.realloc_slice(graph.adj[v], nv + 1);
        graph.adj[v][nv] = u;
    }
}

fn generate_random(graph: Graph&, edges: slice_pool<i32>&) {
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

// The reference's GraphPathBenchmark, the base of each search's benchmark:
// one random graph, made once. The graph ends in a growable array, so it is
// the last field.
//
// The graph's adjacency lists are runs of a slice pool. Root owners cannot
// be nested, so the pool is a local of the code that makes the benchmark,
// declared before it and handed to prepare.
struct GraphPathBenchmark { result_val: u32, prepared: bool, graph: Graph }

fn new_base() -> GraphPathBenchmark {
    GraphPathBenchmark { result_val: 0, prepared: false, graph: new_graph(0, 0, 0) }
}

fn prepare(base: GraphPathBenchmark&, class_name: u8[:], edges: slice_pool<i32>&) {
    if !base.prepared {
        let vertices = config_i64(class_name, "vertices") as i32;
        let jumps = config_i64(class_name, "jumps") as i32;
        let jump_len = config_i64(class_name, "jump_len") as i32;
        base.graph = new_graph(vertices, jumps, jump_len);
        base.graph.generate_random(edges);
        base.prepared = true;
    }
}

// --- Graph::BFS -----------------------------------------------------------------

struct Step { v: i32, dist: i32 }

struct GraphPathBFS { base: GraphPathBenchmark }

fn new_graph_path_bfs() -> GraphPathBFS { GraphPathBFS { base: new_base() } }

fn bfs_shortest_path(s: GraphPathBFS&, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var visited: bool[>..] = [];
    visited.resize(s.base.graph.vertices, false);
    // The queue's front is `head`: std has no deque, and the steps it has
    // handed out stay in place until the search returns.
    var queue: Step[>..] = [];
    var head = 0;
    visited[start] = true;
    queue.push(Step { v: start, dist: 0 });
    while head < queue.len {
        let cur = queue[head];
        head++;
        for neighbor in s.base.graph.adj[cur.v] {
            if neighbor == target { return cur.dist + 1; }
            if !visited[neighbor] {
                visited[neighbor] = true;
                queue.push(Step { v: neighbor, dist: cur.dist + 1 });
            }
        }
    }
    return -1;
}

fn prepare(s: GraphPathBFS&, edges: slice_pool<i32>&) { s.base.prepare("Graph::BFS", edges); }

fn run(s: GraphPathBFS&, iteration_id: i64) {
    let total_length = s.bfs_shortest_path(0, s.base.graph.vertices - 1);
    s.base.result_val += total_length as! u32;
}

fn checksum(s: GraphPathBFS&) -> u32 { s.base.result_val }

// --- Graph::DFS -----------------------------------------------------------------

struct GraphPathDFS { base: GraphPathBenchmark }

fn new_graph_path_dfs() -> GraphPathDFS { GraphPathDFS { base: new_base() } }

fn dfs_find_path(s: GraphPathDFS&, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var visited: bool[>..] = [];
    visited.resize(s.base.graph.vertices, false);
    var stack: Step[>..<] = [];
    var best_path = I32_MAX;
    stack.push(Step { v: start, dist: 0 });
    while stack.len > 0 {
        let cur = stack.pop();
        if visited[cur.v] || cur.dist >= best_path { continue; }
        visited[cur.v] = true;
        for neighbor in s.base.graph.adj[cur.v] {
            if neighbor == target {
                if cur.dist + 1 < best_path { best_path = cur.dist + 1; }
            } else if !visited[neighbor] {
                stack.push(Step { v: neighbor, dist: cur.dist + 1 });
            }
        }
    }
    return if best_path == I32_MAX { -1 } else { best_path };
}

fn prepare(s: GraphPathDFS&, edges: slice_pool<i32>&) { s.base.prepare("Graph::DFS", edges); }

fn run(s: GraphPathDFS&, iteration_id: i64) {
    let total_length = s.dfs_find_path(0, s.base.graph.vertices - 1);
    s.base.result_val += total_length as! u32;
}

fn checksum(s: GraphPathDFS&) -> u32 { s.base.result_val }

// --- Graph::AStar ---------------------------------------------------------------

// Ordered by f_score, then by vertex.
struct Node { f_score: i32, vertex: i32 }

fn before(a: Node, b: Node) -> bool {
    a.f_score < b.f_score || (a.f_score == b.f_score && a.vertex < b.vertex)
}

struct GraphPathAStar { base: GraphPathBenchmark }

fn new_graph_path_astar() -> GraphPathAStar { GraphPathAStar { base: new_base() } }

fn heuristic(s: GraphPathAStar&, v: i32, target: i32) -> i32 { target - v }

fn a_star_shortest_path(s: GraphPathAStar&, start: i32, target: i32) -> i64 {
    if start == target { return 0; }
    var g_score: i32[>..] = [];
    g_score.resize(s.base.graph.vertices, I32_MAX);
    var best_f: i32[>..] = [];
    best_f.resize(s.base.graph.vertices, I32_MAX);

    g_score[start] = 0;
    let f_start = s.heuristic(start, target);
    best_f[start] = f_start;

    var open_set: Node[>..<] = [];
    open_set.heap_push(Node { f_score: f_start, vertex: start }, before);

    while open_set.len > 0 {
        let current = open_set.heap_pop(before);
        if current.vertex == target { return g_score[current.vertex]; }
        for neighbor in s.base.graph.adj[current.vertex] {
            let tentative_g = g_score[current.vertex] + 1;
            if tentative_g < g_score[neighbor] {
                g_score[neighbor] = tentative_g;
                let f_new = tentative_g + s.heuristic(neighbor, target);
                if f_new < best_f[neighbor] {
                    best_f[neighbor] = f_new;
                    open_set.heap_push(Node { f_score: f_new, vertex: neighbor }, before);
                }
            }
        }
    }
    return -1;
}

fn prepare(s: GraphPathAStar&, edges: slice_pool<i32>&) { s.base.prepare("Graph::AStar", edges); }

fn run(s: GraphPathAStar&, iteration_id: i64) {
    let total_length = s.a_star_shortest_path(0, s.base.graph.vertices - 1);
    s.base.result_val += total_length as! u32;
}

fn checksum(s: GraphPathAStar&) -> u32 { s.base.result_val }
