// Generate an s-expression text, parse it into a tree, walk it: see
// bench/goose/sexp.goose. This is the idiomatic Rust shape -- an `enum` whose
// variants own their children through `Option<Box<Node>>`, with `String`
// symbols -- and it maps onto the Goose enum almost line for line. What it
// costs is one allocation per node plus one per symbol, a node sized for the
// largest variant, and a full recursive `Drop` walk at exit.
//
// Children are linked first-child / previous-sibling exactly as the Goose and
// C++ versions do, so the walk order (and the checksum) agree.
mod bench;
use bench::*;

const N: i64 = 20000;                    // BENCH_N
const DEPTH: i64 = 4;
const PASSES: i64 = 4;

// --- generation --------------------------------------------------------------

fn emit_sym(text: &mut String, seed: u64) {
    let mut r = seed;
    let n = 3 + xs_mod(r, 12);
    for _ in 0..n { r = xs_next(r); text.push((b'a' + xs_mod(r, 26) as u8) as char); }
}

fn emit_num(text: &mut String, v: i64) {
    if v == 0 { text.push('0'); return; }
    let mut tmp = [0u8; 24];
    let (mut k, mut d) = (0usize, v);
    while d > 0 { tmp[k] = b'0' + (d % 10) as u8; k += 1; d /= 10; }
    while k > 0 { k -= 1; text.push(tmp[k] as char); }
}

fn gen(text: &mut String, depth: i64, seed: u64) -> u64 {
    let mut r = xs_next(seed);
    if depth == 0 || xs_mod(r, 4) == 0 {
        if xs_mod(r >> 8, 2) == 0 { emit_sym(text, r); }
        else { emit_num(text, xs_mod(r >> 16, 100000)); }
        text.push(' ');
        return r;
    }
    text.push('(');
    let kids = 2 + xs_mod(r >> 24, 4);
    for _ in 0..kids { r = gen(text, depth - 1, r); }
    text.push(')');
    text.push(' ');
    r
}

// --- parse and walk ----------------------------------------------------------

enum Node {
    Sym { name: String, next: Option<Box<Node>> },
    Num { v: i32, next: Option<Box<Node>> },
    List { first: Option<Box<Node>>, next: Option<Box<Node>> },
}

fn skip_space(b: &[u8], pos: &mut usize) { while *pos < b.len() && b[*pos] == b' ' { *pos += 1; } }

fn parse_node(text: &str, pos: &mut usize, count: &mut i64, prev: Option<Box<Node>>) -> Box<Node> {
    let b = text.as_bytes();
    skip_space(b, pos);
    *count += 1;
    let c = b[*pos];
    if c == b'(' {
        *pos += 1;
        let mut chain: Option<Box<Node>> = None;
        loop {
            skip_space(b, pos);
            if b[*pos] == b')' { *pos += 1; break; }
            chain = Some(parse_node(text, pos, count, chain));
        }
        return Box::new(Node::List { first: chain, next: prev });
    }
    let start = *pos;
    if c.is_ascii_digit() {
        let mut v: i64 = 0;
        while *pos < b.len() && b[*pos].is_ascii_digit() {
            v = v * 10 + (b[*pos] - b'0') as i64;
            *pos += 1;
        }
        return Box::new(Node::Num { v: v as i32, next: prev });
    }
    while *pos < b.len() && b[*pos] != b' ' && b[*pos] != b'(' && b[*pos] != b')' { *pos += 1; }
    Box::new(Node::Sym { name: text[start..*pos].to_string(), next: prev })
}

// The sibling link, reached through the match: `next` lives on the variants.
fn nextof(n: &Node) -> Option<&Node> {
    match n {
        Node::Sym { next, .. } | Node::Num { next, .. } | Node::List { next, .. } => next.as_deref(),
    }
}

// The pass index rides along so the four walks are four different computations.
fn walk(n: &Node, p: i64) -> i64 {
    match n {
        Node::Sym { name, .. } => name.len() as i64 + name.as_bytes()[0] as i64 + p,
        Node::Num { v, .. } => (*v as i64) ^ p,
        Node::List { first, .. } => 1 + walk_chain(first.as_deref(), p),
    }
}

fn walk_chain(c: Option<&Node>, p: i64) -> i64 {
    match c { None => 0, Some(n) => walk(n, p) + walk_chain(nextof(n), p) }
}

fn main() {
    let mut text = String::new();
    let mut seed: u64 = 12345;
    for _ in 0..N { seed = gen(&mut text, DEPTH, seed); }
    let mut pos = 0usize;
    let mut count: i64 = 0;
    let mut roots: Vec<Box<Node>> = Vec::new();
    loop {
        skip_space(text.as_bytes(), &mut pos);
        if pos >= text.len() { break; }
        let r = parse_node(&text, &mut pos, &mut count, None);
        roots.push(r);
    }
    emit(text.len() as i64);
    emit(count);
    emit(roots.len() as i64);
    emit(0);
    let mut total: i64 = 0;
    for p in 0..PASSES {
        for r in &roots { total += walk(r, p); }
    }
    emit(total);
}
