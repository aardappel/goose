// A slice read back out of a by-value holder parameter whose argument views
// several arrays is bounded by the parameter's class, the storage the
// caller filled before the call, and by nothing of the callee's own: not by
// its other parameters, which can hold the element type but no view the
// caller made (§9.5). So a slice of it, cut with i32 bounds, returns.
struct Graph { edges: const i32[:], first: const i32[:] }

fn sub(g: Graph, lo: i32, hi: i32) -> const i32[:] {
    let es = g.edges;
    return es[lo..hi];
}

fn tail(g: Graph, lo: i32) -> const i32[:] { return g.edges[lo..]; }

fn neighbors(g: Graph, v: i32) -> const i32[:] {
    let start = g.first[v];
    let end = if v + 1 < g.first.len as! i32 { g.first[v + 1] } else { g.edges.len as! i32 };
    g.edges[start..end]
}

fn main() {
    let es: i32[6] = [1, 2, 3, 4, 5, 6];
    let fs: i32[2] = [0, 3];
    let g = Graph { edges: es, first: fs };
    print(sub(g, 1, 4), " ", tail(g, 4), " ", neighbors(g, 0), " ", neighbors(g, 1));
}
