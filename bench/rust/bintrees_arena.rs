// The same binary-trees over one `Vec<TNode>` that is `clear()`ed between trees
// and keeps its capacity, with children as `u32` indices. See
// bench/goose/bintrees.goose.
//
// This is the arena by hand, and it is what the Benchmarks Game's own fast Rust
// and C++ entries do (with a crate, which a std-only suite cannot use). Because
// `TNode` owns nothing, `clear()` drops nothing: a whole tree is discarded by
// resetting a length, which is the same O(1) release the Goose row gets from a
// local pool. The allocator is out of the loop entirely after the first tree,
// since the stretch tree sizes the buffer for everything that follows.
//
// Index 0 is the null sentinel, so it is a wasted slot in every pool -- the
// price of spelling `Option<&TNode>` as a `u32`.
mod bench;
use bench::*;

const MAXDEPTH: i64 = 15;                // BENCH_N
const MINDEPTH: i64 = 4;

struct TNode { l: u32, r: u32 }          // 0 = null.

fn make(pool: &mut Vec<TNode>, depth: i64) -> u32 {
    if depth == 0 {
        pool.push(TNode { l: 0, r: 0 });
        return pool.len() as u32 - 1;
    }
    let l = make(pool, depth - 1);
    let r = make(pool, depth - 1);
    pool.push(TNode { l, r });
    pool.len() as u32 - 1
}

// The node count: a leaf counts 1.
fn check(pool: &[TNode], i: u32) -> i64 {
    let n = &pool[i as usize];
    if n.l == 0 || n.r == 0 { return 1; }
    1 + check(pool, n.l) + check(pool, n.r)
}

// Build, check, discard: the discard is the `clear()` the next tree starts with.
fn tree_check(pool: &mut Vec<TNode>, depth: i64) -> i64 {
    pool.clear();
    pool.push(TNode { l: 0, r: 0 });     // Index 0 is the null sentinel.
    let root = make(pool, depth);
    check(pool, root)
}

fn main() {
    let mut pool: Vec<TNode> = Vec::new();
    emit(tree_check(&mut pool, MAXDEPTH + 1));   // The stretch tree.
    let mut long: Vec<TNode> = Vec::new();
    long.push(TNode { l: 0, r: 0 });
    let lroot = make(&mut long, MAXDEPTH);
    let mut d = MINDEPTH;
    while d <= MAXDEPTH {
        let iters: i64 = 1 << (MAXDEPTH - d + MINDEPTH);
        let mut sum: i64 = 0;
        for _ in 0..iters { sum += tree_check(&mut pool, d); }
        emit(iters);
        emit(d);
        emit(sum);
        d += 2;
    }
    emit(check(&long, lroot));
}
