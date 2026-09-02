// The same parse-and-evaluate over one arena: a single `Vec<Expr>` that is
// `clear()`ed at the start of every input and keeps its capacity, with children
// named by `u32` index. See bench/goose/calc.goose.
//
// This is the shape the Rust community recommends for a tree, and here it also
// removes the whole teardown cost: clearing a `Vec` of index-linked nodes drops
// nothing, so an input's tree really is discarded by resetting a length, which
// is what the Goose row's local pool does. That works only because `Expr`
// contains no owning field; the moment a node held a `String` the clear would be
// a per-node drop walk again.
//
// Errors are still `Result<_, u8>` and `?`. The links being `u32` rather than
// references is not a safety difference -- an out-of-range index panics -- but
// the type no longer says what it points at, and nothing ties an index to the
// arena it came from.
mod bench;
use bench::*;

const N: i64 = 100000;                   // BENCH_N

const E_EOF: u8 = 1;
const E_PAREN: u8 = 2;
const E_TOKEN: u8 = 3;
const E_DIV0: u8 = 4;

enum Expr {
    Num(i64),
    Neg(u32),
    Paren(u32),
    Bin(u8, u32, u32),
}

// --- generation --------------------------------------------------------------

fn emit_num(text: &mut Vec<u8>, v: i64) {
    if v >= 10 { text.push(b'0' + (v / 10) as u8); }
    text.push(b'0' + (v % 10) as u8);
}

fn gen(text: &mut Vec<u8>, depth: i64, seed: u64) -> u64 {
    let mut r = xs_next(seed);
    let k = xs_mod(r, 8);
    if depth == 0 || k == 0 { emit_num(text, 1 + xs_mod(r >> 8, 99)); return r; }
    if k == 1 { text.push(b'-'); return gen(text, depth - 1, r); }
    if k == 2 {
        text.push(b'(');
        r = gen(text, depth - 1, r);
        text.push(b')');
        return r;
    }
    r = gen(text, depth - 1, r);
    let op = xs_mod(r >> 16, 4);
    text.push(b' ');
    text.push(match op { 0 => b'+', 1 => b'-', 2 => b'*', _ => b'/' });
    text.push(b' ');
    gen(text, depth - 1, xs_next(r))
}

// One in four inputs is damaged after generation.
fn corrupt(text: &mut Vec<u8>, seed: u64) {
    let kind = xs_mod(seed, 16);
    if kind == 0 {
        let mut i = text.len();
        while i > 0 {
            i -= 1;
            if text[i] == b')' { text[i] = b' '; return; }
        }
        text[0] = b'#';
        return;
    }
    if kind == 1 { let i = xs_mod(seed >> 8, text.len() as i64) as usize; text[i] = b'#'; return; }
    if kind == 2 { let half = text.len() / 2; text.truncate(half); return; }
    if kind == 3 { text.extend_from_slice(b" / 0"); }
}

// --- parsing -----------------------------------------------------------------

struct Parser<'a> { t: &'a [u8], pool: &'a mut Vec<Expr>, pos: usize, nodes: i64 }

impl<'a> Parser<'a> {
    fn skip(&mut self) { while self.pos < self.t.len() && self.t[self.pos] == b' ' { self.pos += 1; } }

    fn push(&mut self, e: Expr) -> u32 {
        self.nodes += 1;
        self.pool.push(e);
        self.pool.len() as u32 - 1
    }

    fn expr(&mut self) -> Result<u32, u8> {
        let mut lhs = self.term()?;
        loop {
            self.skip();
            if self.pos >= self.t.len() { break; }
            let op = self.t[self.pos];
            if op != b'+' && op != b'-' { break; }
            self.pos += 1;
            let rhs = self.term()?;
            lhs = self.push(Expr::Bin(op, lhs, rhs));
        }
        Ok(lhs)
    }

    fn term(&mut self) -> Result<u32, u8> {
        let mut lhs = self.factor()?;
        loop {
            self.skip();
            if self.pos >= self.t.len() { break; }
            let op = self.t[self.pos];
            if op != b'*' && op != b'/' { break; }
            self.pos += 1;
            let rhs = self.factor()?;
            lhs = self.push(Expr::Bin(op, lhs, rhs));
        }
        Ok(lhs)
    }

    fn factor(&mut self) -> Result<u32, u8> {
        self.skip();
        if self.pos >= self.t.len() { return Err(E_EOF); }
        let c = self.t[self.pos];
        if c.is_ascii_digit() {
            let mut v: i64 = 0;
            while self.pos < self.t.len() && self.t[self.pos].is_ascii_digit() {
                v = v * 10 + (self.t[self.pos] - b'0') as i64;
                self.pos += 1;
            }
            return Ok(self.push(Expr::Num(v)));
        }
        if c == b'-' {
            self.pos += 1;
            let f = self.factor()?;
            return Ok(self.push(Expr::Neg(f)));
        }
        if c != b'(' { return Err(E_TOKEN); }
        self.pos += 1;
        let e = self.expr()?;
        self.skip();
        if self.pos >= self.t.len() || self.t[self.pos] != b')' { return Err(E_PAREN); }
        self.pos += 1;
        Ok(self.push(Expr::Paren(e)))
    }

    // One input: parse it, insist the whole text was consumed, evaluate it.
    fn run(&mut self) -> Result<u64, u8> {
        let root = self.expr()?;
        self.skip();
        if self.pos != self.t.len() { return Err(E_TOKEN); }
        eval(self.pool, root)
    }
}

// --- evaluation --------------------------------------------------------------
// Unsigned, so overflow wraps identically in every language.

fn eval(pool: &[Expr], i: u32) -> Result<u64, u8> {
    match &pool[i as usize] {
        Expr::Num(v) => Ok(*v as u64),
        Expr::Neg(o) => Ok(0u64.wrapping_sub(eval(pool, *o)?)),
        Expr::Paren(o) => eval(pool, *o),
        Expr::Bin(op, l, r) => {
            let a = eval(pool, *l)?;
            let b = eval(pool, *r)?;
            match op {
                b'+' => Ok(a.wrapping_add(b)),
                b'-' => Ok(a.wrapping_sub(b)),
                b'*' => Ok(a.wrapping_mul(b)),
                _ => if b == 0 { Err(E_DIV0) } else { Ok(a / b) },
            }
        }
    }
}

fn main() {
    let mut text: Vec<u8> = Vec::new();
    let mut pool: Vec<Expr> = Vec::new();
    let mut seed: u64 = 12345;
    let mut ok: i64 = 0;
    let mut errs: [i64; 5] = [0; 5];
    let mut sum: u64 = 0;
    let mut chars: i64 = 0;
    let mut nodes: i64 = 0;               // Every node ever built, bad inputs included.
    for _ in 0..N {
        seed = xs_next(seed);
        text.clear();
        let depth = 3 + xs_mod(seed, 4);
        let s = gen(&mut text, depth, seed >> 8);
        corrupt(&mut text, s);
        chars += text.len() as i64;
        pool.clear();
        let mut p = Parser { t: &text, pool: &mut pool, pos: 0, nodes: 0 };
        let res = p.run();
        nodes += p.nodes;
        match res {
            Ok(v) => { ok += 1; sum = sum.wrapping_mul(31).wrapping_add(v); }
            Err(e) => errs[e as usize] += 1,
        }
    }
    emit(chars);
    emit(nodes);
    emit(ok);
    emit(errs[E_EOF as usize]);
    emit(errs[E_PAREN as usize]);
    emit(errs[E_TOKEN as usize]);
    emit(errs[E_DIV0 as usize]);
    println!("{}", sum);
}
