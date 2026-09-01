// A pointer-linked binary tree: see bench/goose/tree.goose. `Option<Box<Node>>`
// children are the textbook Rust tree, and this is what the language points at
// first: an allocation per node, 24 bytes each, and a full recursive `Drop`
// walk before the process can exit. `Box::new` also builds the node in a stack
// temporary and memcpy-moves it to the heap, which Goose's construct-in-place
// push does not do.
mod bench;
use bench::*;

const DEPTH: i64 = 16;                   // BENCH_N
const PASSES: i64 = 8;

struct TNode { v: i32, l: Option<Box<TNode>>, r: Option<Box<TNode>> }

fn build(depth: i64, seed: u64) -> Box<TNode> {
    let mut n = Box::new(TNode { v: xs_mod(seed, 1000) as i32, l: None, r: None });
    if depth > 0 {
        n.l = Some(build(depth - 1, xs_next(seed)));
        n.r = Some(build(depth - 1, xs_next(seed + 1)));
    }
    n
}

// The pass index is folded into every node's contribution, so the eight passes
// are eight different computations; otherwise a backend that can see the walk
// is pure hoists it out of the loop (see bench/notes.md).
fn sum_tree(n: &TNode, p: i64) -> i64 {
    let mut s = (n.v as i64) ^ p;
    if let Some(l) = &n.l { s += sum_tree(l, p); }
    if let Some(r) = &n.r { s += sum_tree(r, p); }
    s
}

fn main() {
    let root = build(DEPTH, 12345);
    let count = (1i64 << (DEPTH + 1)) - 1;
    let mut total: i64 = 0;
    for p in 0..PASSES { total += sum_tree(&root, p); }
    emit(count);
    emit(total);
}
