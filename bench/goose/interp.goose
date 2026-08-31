// Expression-tree evaluation: the dispatch benchmark. Goose has no vtables,
// so a node carries no vtable pointer and `eval` is a jump table over the
// tag with inlinable arms (spec 8.2). The nodes are variable-mode, so a leaf
// costs 5 bytes and a binary node 9 -- C++ pays max-payload-plus-alignment
// for std::variant, or a vtable pointer plus a separate allocation per node
// for the class hierarchy.

import rng;

let DEPTH = 20;                          // BENCH_N
let PASSES = 8;
let M = 1000003;                         // Keeps every value in range: no overflow.

enum Expr {
    Num { v: i32 },
    Add { l: Expr..&<u32>, r: Expr..&<u32> },
    Mul { l: Expr..&<u32>, r: Expr..&<u32> },
    Neg { of: Expr..&<u32> },
}

var pool: Expr..[>..] = [];

recursive fn build(depth: i64, seed: u64) -> Expr..& {
    let s = xs_next(seed);
    if depth == 0 { return pool.push(Expr.Num { v: xs_mod(s, 1000) as i32 }); }
    let k = xs_mod(s, 8);
    if k < 1 {
        let a: Expr..& = build(depth - 1, s);
        return pool.push(Expr.Neg { of: a });
    }
    let a: Expr..& = build(depth - 1, s);
    let b: Expr..& = build(depth - 1, xs_next(s));
    if k < 5 { return pool.push(Expr.Add { l: a, r: b }); }
    return pool.push(Expr.Mul { l: a, r: b });
}

// Case functions: the language's virtual-call idiom, dispatched on the tag.
recursive fn eval(e: Expr.Num&) -> i64 { e.v }
recursive fn eval(e: Expr.Add&) -> i64 { (eval(e.l) + eval(e.r)) % M }
recursive fn eval(e: Expr.Mul&) -> i64 { (eval(e.l) * eval(e.r)) % M }
recursive fn eval(e: Expr.Neg&) -> i64 { (M - eval(e.of)) % M }

fn main() {
    let root: Expr..& = build(DEPTH, 12345);
    var total = 0;
    for p in PASSES { total = (total + eval(root)) % M; }
    print(pool.len);
    print(total);
}
