// A pointer-linked binary tree: the shape of every AST, scene graph and
// index structure. Nodes live in one grow-only pool and link to each other
// with self-relative u32 offsets, so a node is 12 bytes with no allocator
// header, no padding and no separate allocation. The C++ rows pay a malloc
// per node (and its header, and its size-class rounding), 24-byte nodes, and
// -- for the owning-pointer rows -- a full recursive destructor walk before
// the process can exit.
//
// Children are pushed before their parent, so every link is a small backward
// offset (spec A.2).

import rng;

let DEPTH = 20;                          // BENCH_N
let PASSES = 8;

struct TNode { v: i32, l: TNode&<u32>?, r: TNode&<u32>? }

// A global: the whole program's node arena, owning the bottom of one stack.
var pool: TNode[>..] = [];

recursive fn build(depth: i64, seed: u64) -> TNode& {
    if depth == 0 {
        return pool.push(TNode { v: xs_mod(seed, 1000) as i32 });
    }
    let l: TNode& = build(depth - 1, xs_next(seed));
    let r: TNode& = build(depth - 1, xs_next(seed + 1));
    return pool.push(TNode { v: xs_mod(seed, 1000) as i32, l: l, r: r });
}

// The pass index is folded into every node's contribution, so the eight
// passes are eight different computations. Without that a backend that can
// see `sum_tree` is pure hoists the call out of the loop and the benchmark
// measures one pass instead of eight -- clang does exactly that.
recursive fn sum_tree(n: TNode&, p: i64) -> i64 {
    var t: i64 = n.v ^ p;
    let l = n.l;                         // Narrowing tracks locals, so bind first.
    if l { t += sum_tree(l, p); }
    let r = n.r;
    if r { t += sum_tree(r, p); }
    t
}

fn main() {
    let root: TNode& = build(DEPTH, 12345);
    var total = 0;
    for p in PASSES { total += sum_tree(root, p); }
    print(pool.len);
    print(total);
}
