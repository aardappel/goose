// A faster complete-tree layout than tree_arena.rs puts children over
// parents. In a pre-order layout the left child of node s is s+1 and the
// right child is s + 2^(D-depth), and the DFS that `sum_tree` performs
// therefore visits slots 0,1,2,...,N-1: the tree walk is a sequential
// scan, the pattern both arena rows miss. The links hold slot deltas
// instead of indices, which also turns node addressing from
// index*12 into an add. Same 12-byte nodes, same seeds taken in the
// same recursive order (so identical checksums), same safe-Rust bounds
// checks -- only the layout changed. This is what the SoA rows are to
// their benchmarks: a representation choice inside one language.
mod bench;
use bench::*;

const DEPTH: i64 = 16;                   // BENCH_N
const PASSES: i64 = 8;

#[derive(Clone)]
struct TNode { v: i32, l: u32, r: u32 }  // l/r hold child slots - this slot; 0 = no child

fn build(pool: &mut [TNode], level: i64, slot: u32, seed: u64) {
    let v = xs_mod(seed, 1000) as i32;
    if level == DEPTH {
        pool[slot as usize] = TNode { v, l: 0, r: 0 };
        return;
    }
    let rslot = slot + (1u32 << (DEPTH - level));
    pool[slot as usize] = TNode { v, l: 1, r: rslot - slot };
    build(pool, level + 1, slot + 1, xs_next(seed));
    build(pool, level + 1, rslot, xs_next(seed + 1));
}

fn sum_tree(pool: &[TNode], slot: u32, p: i64) -> i64 {
    let t = &pool[slot as usize];
    let mut s = (t.v as i64) ^ p;
    if t.l != 0 { s += sum_tree(pool, slot + t.l, p); }
    if t.r != 0 { s += sum_tree(pool, slot + t.r, p); }
    s
}

fn main() {
    let n = (1usize << (DEPTH + 1)) - 1;
    let mut pool: Vec<TNode> = vec![TNode { v: 0, l: 0, r: 0 }; n];
    build(&mut pool, 0, 0, 12345);
    let count = pool.len() as i64;
    let mut total: i64 = 0;
    for p in 0..PASSES { total += sum_tree(&pool, 0, p); }
    emit(count);
    emit(total);
}
