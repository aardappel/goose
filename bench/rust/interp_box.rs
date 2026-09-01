// Expression-tree evaluation: see bench/goose/interp.goose. `enum` plus `Box`
// plus `match` is the canonical Rust AST, and it is genuinely better than the
// C++ virtual/unique_ptr shape it replaces: the tag is a byte, there is no
// vtable pointer, and the match arms inline. What it still pays is one
// allocation per node and a node sized for the largest variant -- two `Box`es --
// even for a leaf that holds one i32.
mod bench;
use bench::*;

const DEPTH: i64 = 16;                   // BENCH_N
const PASSES: i64 = 8;
const M: i64 = 1000003;                  // Keeps every value in range: no overflow.

enum Expr {
    Num(i32),
    Add(Box<Expr>, Box<Expr>),
    Mul(Box<Expr>, Box<Expr>),
    Neg(Box<Expr>),
}

fn build(count: &mut i64, depth: i64, seed: u64) -> Box<Expr> {
    let s = xs_next(seed);
    *count += 1;
    if depth == 0 { return Box::new(Expr::Num(xs_mod(s, 1000) as i32)); }
    let k = xs_mod(s, 8);
    if k < 1 { return Box::new(Expr::Neg(build(count, depth - 1, s))); }
    let a = build(count, depth - 1, s);
    let b = build(count, depth - 1, xs_next(s));
    if k < 5 { Box::new(Expr::Add(a, b)) } else { Box::new(Expr::Mul(a, b)) }
}

// The pass index enters at the leaves so the eight evaluations differ.
fn eval(e: &Expr, p: i64) -> i64 {
    match e {
        Expr::Num(v) => (*v as i64 + p) % M,
        Expr::Add(l, r) => (eval(l, p) + eval(r, p)) % M,
        Expr::Mul(l, r) => (eval(l, p) * eval(r, p)) % M,
        Expr::Neg(o) => (M - eval(o, p)) % M,
    }
}

fn main() {
    let mut count: i64 = 0;
    let root = build(&mut count, DEPTH, 12345);
    let mut total: i64 = 0;
    for p in 0..PASSES { total = (total + eval(&root, p)) % M; }
    emit(count);
    emit(total);
}
