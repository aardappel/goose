// The error-free twin of calc.goose: the same generator, parser, AST pool and
// evaluator, but nothing damages the generated text, so every input parses and
// evaluates and no `return ... from` (spec 7.9) is ever taken. The propagation
// machinery is still emitted -- a discriminant check after every call on the
// path out of parse_factor and eval -- so this row prices those checks against
// --unsafe-no-rf-check, which omits them and is therefore only correct on a
// program that never takes a long-distance return.

import rng;

let N = 250000;                          // BENCH_N

let E_EOF = 1;
let E_PAREN = 2;
let E_TOKEN = 3;
let E_DIV0 = 4;

enum Expr {
    Num { v: varint },
    Neg { of: Expr..&<u16> },
    Paren { of: Expr..&<u16> },
    Bin { op: u8, l: Expr..&<u16>, r: Expr..&<u16> },
}

var text: u8[>..<] = [];
var pos = 0;
var nodes = 0;                           // Every node ever built.

// --- generation --------------------------------------------------------------

fn emit_num(v: i64) {
    if v >= 10 { text.push(('0' + v / 10) as! u8); }
    text.push(('0' + v % 10) as! u8);
}

recursive fn gen(depth: i64, seed: u64) -> u64 {
    var r = xs_next(seed);
    let k = xs_mod(r, 8);
    if depth == 0 || k == 0 { emit_num(1 + xs_mod(r >> 8, 99)); return r; }
    if k == 1 { text.push('-'); return gen(depth - 1, r); }
    if k == 2 {
        text.push('(');
        r = gen(depth - 1, r);
        text.push(')');
        return r;
    }
    r = gen(depth - 1, r);
    let op = xs_mod(r >> 16, 3);   // Never '/', so eval's E_DIV0 guard never fires.
    text.push(' ');
    if op == 0 { text.push('+'); } else if op == 1 { text.push('-'); }
    else if op == 2 { text.push('*'); } else { text.push('/'); }
    text.push(' ');
    return gen(depth - 1, xs_next(r));
}

fn corrupt(seed: u64) {
    // The same call and the same seed consumption as calc.goose's, minus the
    // damage: xs_mod lands in 0..15, so nothing is ever written.
    let kind = xs_mod(seed, 16);
    if kind == 16 { text[0] = '#'; }
}

// --- parsing -----------------------------------------------------------------

fn skip() { while pos < text.len && text[pos] == ' ' { pos++; } }

recursive fn parse_expr(pool: Expr..[>..]&) -> Expr..& {
    var lhs: Expr..& = parse_term(pool);
    loop {
        skip();
        guard pos < text.len && (text[pos] == '+' || text[pos] == '-');
        let op = text[pos];
        pos++;
        let rhs: Expr..& = parse_term(pool);
        nodes++;
        lhs .= pool.push(Expr.Bin { op: op, l: lhs, r: rhs });
    }
    return lhs;
}

recursive fn parse_term(pool: Expr..[>..]&) -> Expr..& {
    var lhs: Expr..& = parse_factor(pool);
    loop {
        skip();
        guard pos < text.len && (text[pos] == '*' || text[pos] == '/');
        let op = text[pos];
        pos++;
        let rhs: Expr..& = parse_factor(pool);
        nodes++;
        lhs .= pool.push(Expr.Bin { op: op, l: lhs, r: rhs });
    }
    return lhs;
}

recursive fn parse_factor(pool: Expr..[>..]&) -> Expr..& {
    skip();
    guard pos < text.len else { return 0, E_EOF from run_one; }
    let c = text[pos];
    if c >= '0' && c <= '9' {
        var v = 0;
        while pos < text.len && text[pos] >= '0' && text[pos] <= '9' {
            v = v * 10 + (text[pos] - '0');
            pos++;
        }
        nodes++;
        return pool.push(Expr.Num { v: v });
    }
    if c == '-' {
        pos++;
        let f: Expr..& = parse_factor(pool);
        nodes++;
        return pool.push(Expr.Neg { of: f });
    }
    guard c == '(' else { return 0, E_TOKEN from run_one; }
    pos++;
    let e: Expr..& = parse_expr(pool);
    skip();
    guard pos < text.len && text[pos] == ')' else { return 0, E_PAREN from run_one; }
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
fn run_one() -> u64, i64 {
    var pool: Expr..[>..] = [];
    pos = 0;
    let root: Expr..& = parse_expr(pool);
    skip();
    guard pos == text.len else { return 0, E_TOKEN; }
    return eval(root), 0;
}

fn main() {
    var seed: u64 = 12345;
    var ok = 0;
    var errs: i64[5] = [0; 5];
    var sum: u64 = 0;
    var chars = 0;
    for i in N {
        seed = xs_next(seed);
        text.clear();
        let depth = 3 + xs_mod(seed, 4);
        let s = gen(depth, seed >> 8);
        corrupt(s);
        chars += text.len;
        let v, err = run_one();
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
