// The one-pass linked build: one edge pool, each vertex's edges chained
// together. This is the direct analogue of bench/goose/graph.goose, and the
// difference is the whole point of the row -- Goose chains real `Edge&`
// references that stay valid because the pool never moves, while Rust must
// chain u32 indices into a `Vec` it re-indexes on every hop. There is no safe
// Rust spelling of the pointer version at any level of effort.
mod bench;
use bench::*;

const V: i64 = 100000;                   // BENCH_N
const DEG: i64 = 8;
const SOURCES: i64 = 4;

struct Edge { to: i32, next: u32 }

fn main() {
    let nv = V;
    let ne = nv * DEG;
    let mut r: u64 = 12345;
    // pool[0..nv) are the list heads, pool[nv..) the edges themselves.
    let mut pool: Vec<Edge> = Vec::with_capacity((nv + ne) as usize);
    for _ in 0..nv { pool.push(Edge { to: -1, next: 0 }); }
    for _ in 0..ne {
        r = xs_next(r);
        let u = xs_mod(r, nv) as usize;
        let to = xs_mod(r >> 24, nv) as i32;
        let next = pool[u].next;
        pool.push(Edge { to, next });
        pool[u].next = pool.len() as u32 - 1;
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
            let mut c = pool[u].next;
            while c != 0 {
                let w = pool[c as usize].to as usize;
                if dist[w] < 0 { dist[w] = d + 1; q.push(w as i32); }
                c = pool[c as usize].next;
            }
        }
    }
    emit(nv + ne);
    emit(total);
}
