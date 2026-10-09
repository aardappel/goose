// error: cannot clear es while s is still used
// A view read back out of a holder parameter maps back to what the call's
// argument viewed (§9.2): s may point into es.
struct Graph { edges: const i32[:], first: const i32[:] }

fn sub(g: Graph, lo: i32, hi: i32) -> const i32[:] {
    let es = g.edges;
    return es[lo..hi];
}

fn main() {
    var es: i32[>..] = [];
    for i in 6 { es.push(i as i32); }
    let fs: i32[2] = [0, 3];
    let s = sub(Graph { edges: es, first: fs }, 1, 4);
    es.clear();
    print(s);
}
