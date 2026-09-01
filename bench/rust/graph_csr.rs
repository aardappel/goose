// The same graph built as CSR with the two-pass count-then-fill: the fastest
// and most compact thing a Rust programmer writes, and the most code. Identical
// in shape to bench/goose/graph_csr.goose and to the C++ CSR row.
mod bench;
use bench::*;

const V: i64 = 100000;                   // BENCH_N
const DEG: i64 = 8;
const SOURCES: i64 = 4;

fn main() {
    let nv = V;
    let ne = nv * DEG;
    let mut r: u64 = 12345;
    let mut esrc: Vec<i32> = vec![0; ne as usize];
    let mut edst: Vec<i32> = vec![0; ne as usize];
    for e in 0..ne as usize {
        r = xs_next(r);
        esrc[e] = xs_mod(r, nv) as i32;
        edst[e] = xs_mod(r >> 24, nv) as i32;
    }
    let mut start: Vec<i32> = vec![0; nv as usize + 1];
    for e in 0..ne as usize { start[esrc[e] as usize + 1] += 1; }
    for v in 0..nv as usize { start[v + 1] += start[v]; }
    let mut fill = start.clone();
    let mut out: Vec<i32> = vec![0; ne as usize];
    for e in 0..ne as usize {
        let s = esrc[e] as usize;
        out[fill[s] as usize] = edst[e];
        fill[s] += 1;
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
            for k in start[u]..start[u + 1] {
                let w = out[k as usize] as usize;
                if dist[w] < 0 { dist[w] = d + 1; q.push(w as i32); }
            }
        }
    }
    emit(nv + ne);
    emit(total);
}
