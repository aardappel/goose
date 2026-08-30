// The same graph and the same BFS, built as CSR with the two-pass
// count-then-fill instead of one-pass linking. This is the shape a
// performance-conscious C++ programmer writes, and it exists here to separate
// two things the one-pass row conflates: what the language costs, and what
// the data structure costs. Traversal here is a contiguous run per vertex
// rather than a pointer chase, and it needs two passes over the edges plus
// two more arrays to get there.

import rng;

let V = 1000000;                         // BENCH_N
let DEG = 8;
let SOURCES = 4;

fn main() {
    let ne = V * DEG;
    var esrc: i32[>..] = [];
    var edst: i32[>..] = [];
    var start: i32[>..] = [];
    for v in V + 1 { start.push(0); }
    var r = 12345;
    for e in ne {
        r = xs_next(r);
        let u = xs_mod(r, V);
        esrc.push(u as i32);
        edst.push(xs_mod(unsigned_shr(r, 24), V) as i32);
        start[u + 1] = (start[u + 1] + 1) as i32;
    }
    for v in V { start[v + 1] = (start[v + 1] + start[v]) as i32; }
    var fill: i32[>..] = [];
    for v in V + 1 { fill.push(start[v] as i32); }
    var out: i32[>..] = [];
    for e in ne { out.push(0); }
    for e in ne {
        let s = esrc[e];
        out[fill[s]] = edst[e] as i32;
        fill[s] = (fill[s] + 1) as i32;
    }
    // BFS, identical to graph.goose apart from how the edges are reached.
    var dist: i32[>..] = [];
    for v in V { dist.push(-1); }
    var q: i32[>..<] = [];
    var total = 0;
    var seed = 999;
    for s in SOURCES {
        seed = xs_next(seed);
        let src = xs_mod(seed, V);
        for v in V { dist[v] = -1; }
        q.clear();
        dist[src] = 0;
        q.push(src as i32);
        var qh = 0;
        while qh < q.len {
            let u = q[qh];
            qh++;
            let d = dist[u];
            total += d;
            for k in start[u]..start[u + 1] {
                let w = out[k];
                if dist[w] < 0 {
                    dist[w] = (d + 1) as i32;
                    q.push(w as i32);
                }
            }
        }
    }
    print(V + ne);
    print(total);
}
