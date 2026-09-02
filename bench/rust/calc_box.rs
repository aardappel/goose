// Many small parses with real error paths: N arithmetic expressions are
// generated, parsed by recursive descent into an AST and evaluated, and a
// quarter of them are malformed, so the error path is taken from deep inside the
// recursion on every fourth input. See bench/goose/calc.goose.
//
// The owning shape: each node is a `Box<Expr>`, so the tree costs one allocation
// per node and one recursive `Drop` per input -- on the error path too, where
// the partially built tree is destroyed as the `?` unwinds through the parser.
// Errors are `Result<_, u8>` with `?`, which is Rust's answer to both of the C++
// rows at once: it reads like the exception version and costs like the
// error-code version, a discriminant test per frame with no unwinder involved.
//
// The text buffer is reused across inputs -- it is the input, not the thing
// being measured -- and evaluation is unsigned 64-bit so overflow wraps
// identically in every language.
mod bench;
use bench::*;

const N: i64 = 100000;                   // BENCH_N

const E_EOF: u8 = 1;
const E_PAREN: u8 = 2;
const E_TOKEN: u8 = 3;
const E_DIV0: u8 = 4;

enum Expr {
    Num(i64),
    Neg(Box<Expr>),
    Paren(Box<Expr>),
    Bin(u8, Box<Expr>, Box<Expr>),
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

struct Parser<'a> { t: &'a [u8], pos: usize, nodes: i64 }

impl<'a> Parser<'a> {
    fn skip(&mut self) { while self.pos < self.t.len() && self.t[self.pos] == b' ' { self.pos += 1; } }

    fn expr(&mut self) -> Result<Box<Expr>, u8> {
        let mut lhs = self.term()?;
        loop {
            self.skip();
            if self.pos >= self.t.len() { break; }
            let op = self.t[self.pos];
            if op != b'+' && op != b'-' { break; }
            self.pos += 1;
            let rhs = self.term()?;
            self.nodes += 1;
            lhs = Box::new(Expr::Bin(op, lhs, rhs));
        }
        Ok(lhs)
    }

    fn term(&mut self) -> Result<Box<Expr>, u8> {
        let mut lhs = self.factor()?;
        loop {
            self.skip();
            if self.pos >= self.t.len() { break; }
            let op = self.t[self.pos];
            if op != b'*' && op != b'/' { break; }
            self.pos += 1;
            let rhs = self.factor()?;
            self.nodes += 1;
            lhs = Box::new(Expr::Bin(op, lhs, rhs));
        }
        Ok(lhs)
    }

    fn factor(&mut self) -> Result<Box<Expr>, u8> {
        self.skip();
        if self.pos >= self.t.len() { return Err(E_EOF); }
        let c = self.t[self.pos];
        if c.is_ascii_digit() {
            let mut v: i64 = 0;
            while self.pos < self.t.len() && self.t[self.pos].is_ascii_digit() {
                v = v * 10 + (self.t[self.pos] - b'0') as i64;
                self.pos += 1;
            }
            self.nodes += 1;
            return Ok(Box::new(Expr::Num(v)));
        }
        if c == b'-' {
            self.pos += 1;
            let f = self.factor()?;
            self.nodes += 1;
            return Ok(Box::new(Expr::Neg(f)));
        }
        if c != b'(' { return Err(E_TOKEN); }
        self.pos += 1;
        let e = self.expr()?;
        self.skip();
        if self.pos >= self.t.len() || self.t[self.pos] != b')' { return Err(E_PAREN); }
        self.pos += 1;
        self.nodes += 1;
        Ok(Box::new(Expr::Paren(e)))
    }

    // One input: parse it, insist the whole text was consumed, evaluate it. The
    // tree is a local, so it is dropped however the input ends.
    fn run(&mut self) -> Result<u64, u8> {
        let root = self.expr()?;
        self.skip();
        if self.pos != self.t.len() { return Err(E_TOKEN); }
        eval(&root)
    }
}

// --- evaluation --------------------------------------------------------------
// Unsigned, so overflow wraps identically in every language.

fn eval(e: &Expr) -> Result<u64, u8> {
    match e {
        Expr::Num(v) => Ok(*v as u64),
        Expr::Neg(o) => Ok(0u64.wrapping_sub(eval(o)?)),
        Expr::Paren(o) => eval(o),
        Expr::Bin(op, l, r) => {
            let a = eval(l)?;
            let b = eval(r)?;
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
        let mut p = Parser { t: &text, pos: 0, nodes: 0 };
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
