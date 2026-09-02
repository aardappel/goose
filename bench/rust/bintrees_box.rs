// The Benchmarks Game's binary-trees: build a great many short-lived complete
// binary trees, check each, throw it away, with one long-lived tree kept across
// the run. See bench/goose/bintrees.goose. It is famously an allocator
// benchmark, which is exactly why it is here: this row pays a malloc per node
// and a recursive `Drop` per tree, and both are inside the measurement.
//
// `Option<Box<TNode>>` is the direct Rust spelling of a nullable owning child,
// and it costs nothing over a bare `Box`: the null is the pointer's niche, so a
// node is two words with no tag. What it costs is the pointer chase, twice --
// once building and once dropping.
mod bench;
use bench::*;

const MAXDEPTH: i64 = 15;                // BENCH_N
const MINDEPTH: i64 = 4;

struct TNode { l: Option<Box<TNode>>, r: Option<Box<TNode>> }

fn make(depth: i64) -> Box<TNode> {
    if depth == 0 { return Box::new(TNode { l: None, r: None }); }
    let l = make(depth - 1);
    let r = make(depth - 1);
    Box::new(TNode { l: Some(l), r: Some(r) })
}

// The node count: a leaf counts 1.
fn check(n: &TNode) -> i64 {
    match (&n.l, &n.r) {
        (Some(l), Some(r)) => 1 + check(l) + check(r),
        _ => 1,
    }
}

// Build, check, discard: the tree is a local, so the discard is a recursive drop.
fn tree_check(depth: i64) -> i64 {
    let root = make(depth);
    check(&root)
}

fn main() {
    emit(tree_check(MAXDEPTH + 1));      // The stretch tree.
    let long = make(MAXDEPTH);
    let mut d = MINDEPTH;
    while d <= MAXDEPTH {
        let iters: i64 = 1 << (MAXDEPTH - d + MINDEPTH);
        let mut sum: i64 = 0;
        for _ in 0..iters { sum += tree_check(d); }
        emit(iters);
        emit(d);
        emit(sum);
        d += 2;
    }
    emit(check(&long));
}
