// Incremental adjacency build plus BFS: see bench/goose/graph.goose.
// `Vec<Vec<i32>>` is the first thing anyone writes, in any language: one
// allocation and a 24-byte header per vertex, and the inner vectors grow
// independently.
//
// BFS distances are shortest paths, so the checksum does not depend on the
// order edges come out of the adjacency structure.
mod bench;
use bench::*;

const V: i64 = 100000;                   // BENCH_N
const DEG: i64 = 8;
const SOURCES: i64 = 4;

fn main() {
    let nv = V;
    let ne = nv * DEG;
    let mut r: u64 = 12345;
    let mut adj: Vec<Vec<i32>> = (0..nv).map(|_| Vec::new()).collect();
    for _ in 0..ne {
        r = xs_next(r);
        let u = xs_mod(r, nv) as usize;
        adj[u].push(xs_mod(r >> 24, nv) as i32);
    }

    let mut dist: Vec<i32> = vec![-1; nv as usize];
    let mut q: Vec<i32> = Vec::with_capacity(nv as usize);
    let mut total: i64 = 0;
    let mut seed: u64 = 999;
    for _ in 0..SOURCES {
        seed = xs_next(seed);
        let src = xs_mod(seed, nv) as usize;
        for d in dist.iter_mut() { *d = -1; }
        q.clear();
        dist[src] = 0;
        q.push(src as i32);
        let mut qh = 0;
        while qh < q.len() {
            let u = q[qh] as usize;
            qh += 1;
            let d = dist[u];
            total += d as i64;
            // `adj` is borrowed immutably while `dist` and `q` are mutated:
            // separate locals, so the borrow checker allows the iterator form.
            for &e in &adj[u] {
                let w = e as usize;
                if dist[w] < 0 { dist[w] = d + 1; q.push(w as i32); }
            }
        }
    }
    emit(nv + ne);
    emit(total);
}
