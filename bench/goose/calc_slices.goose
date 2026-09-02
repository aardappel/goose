// calc.goose with the input text in a grow-only local instead of a global
// grow-shrink array: the buffer is cleared per input (spec 5.1) and the
// parser reads it through a `u8[:]` slice rather than indexing a global by
// position. Same work, same output; what it demonstrates is that "scratch I
// refill every iteration" and "structure I can point into" are now the same
// type. The truncation corruption becomes a shorter slice, since a grow-only
// array has no `resize`.
//
// Everything else is calc.goose: N arithmetic expressions generated, parsed
// by recursive descent into a per-input AST pool, evaluated, with a quarter
// of them malformed so `return ... from` runs from deep inside the recursion.

import rng;

let N = 250000;                          // BENCH_N

let E_EOF = 1;
let E_PAREN = 2;
let E_TOKEN = 3;
let E_DIV0 = 4;

enum Expr {
    Num { v: varint },
    Neg { of: Expr..&<u16> },
    Paren { of: Expr..& },
    Bin { op: u8, l: Expr..&<u16>, r: Expr..&<u16> },
}

var pos = 0;
var nodes = 0;                           // Every node ever built, bad inputs included.

// --- generation --------------------------------------------------------------

fn emit_num(t: u8[>..]&, v: i64) {
    if v >= 10 { t.push(('0' + v / 10) as! u8); }
    t.push(('0' + v % 10) as! u8);
}

recursive fn gen(t: u8[>..]&, depth: i64, seed: u64) -> u64 {
    var r = xs_next(seed);
    let k = xs_mod(r, 8);
    if depth == 0 || k == 0 { emit_num(t, 1 + xs_mod(r >> 8, 99)); return r; }
    if k == 1 { t.push('-'); return gen(t, depth - 1, r); }
    if k == 2 {
        t.push('(');
        r = gen(t, depth - 1, r);
        t.push(')');
        return r;
    }
    r = gen(t, depth - 1, r);
    let op = xs_mod(r >> 16, 4);
    t.push(' ');
    if op == 0 { t.push('+'); } else if op == 1 { t.push('-'); }
    else if op == 2 { t.push('*'); } else { t.push('/'); }
    t.push(' ');
    return gen(t, depth - 1, xs_next(r));
}

// One in four inputs is damaged after generation; the returned length is what
// the parser sees, which is how the truncation is spelled without a `resize`.
fn corrupt(t: u8[>..]&, seed: u64) -> i64 {
    let kind = xs_mod(seed, 16);
    if kind == 0 {
        var i = t.len;
        while i > 0 {
            i--;
            if t[i] == ')' { t[i] = ' '; return t.len; }
        }
        t[0] = '#';
        return t.len;
    }
    if kind == 1 { t[xs_mod(seed >> 8, t.len)] = '#'; return t.len; }
    if kind == 2 { return t.len / 2; }
    if kind == 3 { t.append(" / 0"); }
    return t.len;
}

// --- parsing -----------------------------------------------------------------

fn skip(src: u8[:]) { while pos < src.len && src[pos] == ' ' { pos++; } }

recursive fn parse_expr(pool: Expr..[>..]&, src: u8[:]) -> Expr..& {
    var lhs: Expr..& = parse_term(pool, src);
    loop {
        skip(src);
        guard pos < src.len && (src[pos] == '+' || src[pos] == '-');
        let op = src[pos];
        pos++;
        let rhs: Expr..& = parse_term(pool, src);
        nodes++;
        lhs .= pool.push(Expr.Bin { op: op, l: lhs, r: rhs });
    }
    return lhs;
}

recursive fn parse_term(pool: Expr..[>..]&, src: u8[:]) -> Expr..& {
    var lhs: Expr..& = parse_factor(pool, src);
    loop {
        skip(src);
        guard pos < src.len && (src[pos] == '*' || src[pos] == '/');
        let op = src[pos];
        pos++;
        let rhs: Expr..& = parse_factor(pool, src);
        nodes++;
        lhs .= pool.push(Expr.Bin { op: op, l: lhs, r: rhs });
    }
    return lhs;
}

recursive fn parse_factor(pool: Expr..[>..]&, src: u8[:]) -> Expr..& {
    skip(src);
    guard pos < src.len else { return 0, E_EOF from run_one; }
    let c = src[pos];
    if c >= '0' && c <= '9' {
        var v = 0;
        while pos < src.len && src[pos] >= '0' && src[pos] <= '9' {
            v = v * 10 + (src[pos] - '0');
            pos++;
        }
        nodes++;
        return pool.push(Expr.Num { v: v });
    }
    if c == '-' {
        pos++;
        let f: Expr..& = parse_factor(pool, src);
        nodes++;
        return pool.push(Expr.Neg { of: f });
    }
    guard c == '(' else { return 0, E_TOKEN from run_one; }
    pos++;
    let e: Expr..& = parse_expr(pool, src);
    skip(src);
    guard pos < src.len && src[pos] == ')' else { return 0, E_PAREN from run_one; }
    pos++;
    nodes++;
    return pool.push(Expr.Paren { of: e });
}

// --- evaluation --------------------------------------------------------------
// Unsigned, so overflow wraps identically in every language.

recursive fn eval(e: Expr.Num&) -> u64 { e.v as u64 }
recursive fn eval(e: Expr.Neg&) -> u64 { 0 - eval(e.of) }
recursive fn eval(e: Expr.Paren&) -> u64 { eval(e.of) }
recursive fn eval(e: Expr.Bin&) -> u64 {
    let a = eval(e.l);
    let b = eval(e.r);
    if e.op == '+' { return a + b; }
    if e.op == '-' { return a - b; }
    if e.op == '*' { return a * b; }
    guard b != 0 else { return 0, E_DIV0 from run_one; }
    return a / b;
}

// One input: its own AST pool, released on return however the input ends.
fn run_one(src: u8[:]) -> u64, i64 {
    var pool: Expr..[>..] = [];
    pos = 0;
    let root: Expr..& = parse_expr(&pool, src);
    skip(src);
    guard pos == src.len else { return 0, E_TOKEN; }
    return eval(root), 0;
}

fn main() {
    var text: u8[>..] = [];
    var seed: u64 = 12345;
    var ok = 0;
    var errs: i64[5] = [0; 5];
    var sum: u64 = 0;
    var chars = 0;
    for i in N {
        seed = xs_next(seed);
        text.clear();                    // Last input's slices died with the iteration.
        let depth = 3 + xs_mod(seed, 4);
        let s = gen(&text, depth, seed >> 8);
        let n = corrupt(&text, s);
        chars += n;
        let v, err = run_one(text[0..n]);
        if err == 0 { ok++; sum = sum * 31 + v; } else { errs[err] += 1; }
    }
    print(chars);
    print(nodes);
    print(ok);
    print(errs[E_EOF]);
    print(errs[E_PAREN]);
    print(errs[E_TOKEN]);
    print(errs[E_DIV0]);
    print(sum);
}
