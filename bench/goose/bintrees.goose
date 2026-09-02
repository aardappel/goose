// The Benchmarks Game's binary-trees, included for recognisability: build
// a great many short-lived complete binary trees, check each, throw it
// away, with one long-lived tree kept across the run. It is famously an
// allocator benchmark -- the winning C++ and Rust entries use an arena
// library for exactly that reason -- so it is not a row this suite leans on,
// but it is the purest measurement of what Goose's memory model deletes:
// every tree here is a local pool of 8-byte nodes linked by 4-byte relative
// references, built by a recursive function into the pool it is handed, and
// freed by resetting one pointer when the function returns. The std-only
// idiomatic C++ and Rust rows pay a malloc and a free per node; their
// arena rows are a vector cleared per tree, the same thing by hand.

let MAXDEPTH = 17;                       // BENCH_N
let MINDEPTH = 4;

struct TNode { l: TNode&<u32>?, r: TNode&<u32>? }

recursive fn make(pool: TNode[>..]&, depth: i64) -> TNode& {
    if depth == 0 { return pool.push(TNode {}); }
    let l: TNode& = make(pool, depth - 1);
    let r: TNode& = make(pool, depth - 1);
    return pool.push(TNode { l: l, r: r });
}

recursive fn check(n: TNode&) -> i64 {
    let l = n.l;
    guard l else { return 1; }
    let r = n.r;
    guard r else { return 1; }
    return 1 + check(l) + check(r);
}

// Build, check, discard: the pool is a local, so the discard is free.
fn tree_check(depth: i64) -> i64 {
    var pool: TNode[>..] = [];
    let root: TNode& = make(&pool, depth);
    return check(root);
}

fn main() {
    print(tree_check(MAXDEPTH + 1));     // The stretch tree.
    var long: TNode[>..] = [];
    let lroot: TNode& = make(&long, MAXDEPTH);
    var d = MINDEPTH;
    while d <= MAXDEPTH {
        let iters = 1 << (MAXDEPTH - d + MINDEPTH);
        var sum = 0;
        for i in iters { sum += tree_check(d); }
        print(iters);
        print(d);
        print(sum);
        d += 2;
    }
    print(check(lroot));
}
