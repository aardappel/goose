// error: returning a reference rooted in mine, which dies with this function
// What a by-value holder parameter holds is bounded by its class only until
// the body stores something of its own there (§9.5): a view of the local
// mine then comes out of it too.
struct Graph { edges: const i32[:], first: const i32[:] }

fn sub(var g: Graph, lo: i32, hi: i32) -> const i32[:] {
    let mine: i32[3] = [7, 8, 9];
    if lo > 0 { g.edges = mine; }
    let es = g.edges;
    return es[lo..hi];
}

fn main() {
    let es: i32[6] = [1, 2, 3, 4, 5, 6];
    let fs: i32[2] = [0, 3];
    print(sub(Graph { edges: es, first: fs }, 1, 2));
}
