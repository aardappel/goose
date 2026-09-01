// The same parse into one arena with u32 links and `&str` symbols borrowed from
// the source text: the performance-conscious safe Rust, and the closest
// analogue of what Goose does by default. The borrow works only because `text`
// is finished before parsing starts and never grows again -- a parser that
// interned or rewrote text while building nodes could not be written this way,
// and would be back to owned `String`s or offsets.
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

enum Node<'a> {
    Sym { name: &'a str, next: u32 },
    Num { v: i32, next: u32 },
    List { first: u32, next: u32 },
}

fn skip_space(b: &[u8], pos: &mut usize) { while *pos < b.len() && b[*pos] == b' ' { *pos += 1; } }

fn parse_node<'a>(text: &'a str, pos: &mut usize, pool: &mut Vec<Node<'a>>, prev: u32) -> u32 {
    let b = text.as_bytes();
    skip_space(b, pos);
    let c = b[*pos];
    if c == b'(' {
        *pos += 1;
        let mut chain: u32 = 0;
        loop {
            skip_space(b, pos);
            if b[*pos] == b')' { *pos += 1; break; }
            chain = parse_node(text, pos, pool, chain);
        }
        pool.push(Node::List { first: chain, next: prev });
        return pool.len() as u32 - 1;
    }
    let start = *pos;
    if c.is_ascii_digit() {
        let mut v: i64 = 0;
        while *pos < b.len() && b[*pos].is_ascii_digit() {
            v = v * 10 + (b[*pos] - b'0') as i64;
            *pos += 1;
        }
        pool.push(Node::Num { v: v as i32, next: prev });
        return pool.len() as u32 - 1;
    }
    while *pos < b.len() && b[*pos] != b' ' && b[*pos] != b'(' && b[*pos] != b')' { *pos += 1; }
    pool.push(Node::Sym { name: &text[start..*pos], next: prev });
    pool.len() as u32 - 1
}

fn nextof(n: &Node) -> u32 {
    match n {
        Node::Sym { next, .. } | Node::Num { next, .. } | Node::List { next, .. } => *next,
    }
}

fn walk(pool: &[Node], i: u32, p: i64) -> i64 {
    match &pool[i as usize] {
        Node::Sym { name, .. } => name.len() as i64 + name.as_bytes()[0] as i64 + p,
        Node::Num { v, .. } => (*v as i64) ^ p,
        Node::List { first, .. } => 1 + walk_chain(pool, *first, p),
    }
}

fn walk_chain(pool: &[Node], c: u32, p: i64) -> i64 {
    if c == 0 { 0 } else { walk(pool, c, p) + walk_chain(pool, nextof(&pool[c as usize]), p) }
}

fn main() {
    let mut text = String::new();
    let mut seed: u64 = 12345;
    for _ in 0..N { seed = gen(&mut text, DEPTH, seed); }
    let mut pos = 0usize;
    let mut pool: Vec<Node> = Vec::new();
    pool.push(Node::Num { v: 0, next: 0 });        // index 0 is the null sentinel
    let mut roots: Vec<u32> = Vec::new();
    loop {
        skip_space(text.as_bytes(), &mut pos);
        if pos >= text.len() { break; }
        let r = parse_node(&text, &mut pos, &mut pool, 0);
        roots.push(r);
    }
    emit(text.len() as i64);
    emit(pool.len() as i64 - 1);
    emit(roots.len() as i64);
    emit(0);
    let mut total: i64 = 0;
    for p in 0..PASSES {
        for &r in &roots { total += walk(&pool, r, p); }
    }
    emit(total);
}
