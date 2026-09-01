// The same interpreter over one arena with u32 links. The enum stays -- Rust's
// tagged unions are safe and free -- but the children become indices, which is
// the only way to put the nodes in one contiguous block in safe Rust. A node is
// 12 bytes here against Goose's 5 for a leaf and 9 for a binary node, because a
// fixed enum pays max-payload-plus-alignment for every node.
mod bench;
use bench::*;

const DEPTH: i64 = 16;                   // BENCH_N
const PASSES: i64 = 8;
const M: i64 = 1000003;

enum Node { Num(i32), Add(u32, u32), Mul(u32, u32), Neg(u32) }

fn build(pool: &mut Vec<Node>, depth: i64, seed: u64) -> u32 {
    let s = xs_next(seed);
    if depth == 0 {
        pool.push(Node::Num(xs_mod(s, 1000) as i32));
        return pool.len() as u32 - 1;
    }
    let k = xs_mod(s, 8);
    if k < 1 {
        let a = build(pool, depth - 1, s);
        pool.push(Node::Neg(a));
        return pool.len() as u32 - 1;
    }
    let a = build(pool, depth - 1, s);
    let b = build(pool, depth - 1, xs_next(s));
    pool.push(if k < 5 { Node::Add(a, b) } else { Node::Mul(a, b) });
    pool.len() as u32 - 1
}

fn eval(pool: &[Node], i: u32, p: i64) -> i64 {
    match &pool[i as usize] {
        Node::Num(v) => (*v as i64 + p) % M,
        Node::Add(l, r) => (eval(pool, *l, p) + eval(pool, *r, p)) % M,
        Node::Mul(l, r) => (eval(pool, *l, p) * eval(pool, *r, p)) % M,
        Node::Neg(o) => (M - eval(pool, *o, p)) % M,
    }
}

fn main() {
    let mut pool: Vec<Node> = Vec::new();
    let root = build(&mut pool, DEPTH, 12345);
    let mut total: i64 = 0;
    for p in 0..PASSES { total = (total + eval(&pool, root, p)) % M; }
    emit(pool.len() as i64);
    emit(total);
}
