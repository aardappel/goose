// The same tree in one arena with u32 child indices, exactly reserved: no
// per-node allocation and no teardown. This is the shape the Rust community
// recommends for anything pointer-heavy, because the borrow checker will not
// let a node hold a reference into the vector that owns it. It reaches the same
// place Goose does, but the links are indices rather than typed references:
// nothing stops index 7 of this pool being used against a different pool, and
// the type no longer says what it points at.
mod bench;
use bench::*;

const DEPTH: i64 = 16;                   // BENCH_N
const PASSES: i64 = 8;

struct TNode { v: i32, l: u32, r: u32 }  // 0 = null: node 0 is a leaf sentinel slot

fn build(pool: &mut Vec<TNode>, depth: i64, seed: u64) -> u32 {
    if depth == 0 {
        pool.push(TNode { v: xs_mod(seed, 1000) as i32, l: 0, r: 0 });
        return pool.len() as u32 - 1;
    }
    let l = build(pool, depth - 1, xs_next(seed));
    let r = build(pool, depth - 1, xs_next(seed + 1));
    pool.push(TNode { v: xs_mod(seed, 1000) as i32, l, r });
    pool.len() as u32 - 1
}

fn sum_tree(pool: &[TNode], n: u32, p: i64) -> i64 {
    let t = &pool[n as usize];
    let mut s = (t.v as i64) ^ p;
    if t.l != 0 { s += sum_tree(pool, t.l, p); }
    if t.r != 0 { s += sum_tree(pool, t.r, p); }
    s
}

fn main() {
    let mut pool: Vec<TNode> = Vec::with_capacity(1usize << (DEPTH + 1));
    pool.push(TNode { v: 0, l: 0, r: 0 });        // Index 0 is the null sentinel.
    let root = build(&mut pool, DEPTH, 12345);
    let count = pool.len() as i64 - 1;
    let mut total: i64 = 0;
    for p in 0..PASSES { total += sum_tree(&pool, root, p); }
    emit(count);
    emit(total);
}
