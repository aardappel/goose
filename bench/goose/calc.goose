// Many small parses with real error paths: N arithmetic expressions are
// generated, parsed by recursive descent into an AST, and evaluated, and a
// quarter of them are malformed -- a paren dropped, a stray character, a
// truncated text, a division by zero -- so the error path is taken from deep
// inside the recursion on every fourth input. It is the shape of a config
// loader, a query parser, a message validator: lots of small inputs, each
// with its own short-lived tree, some of them bad.
//
// Two things are on trial. The AST pool is a local of the per-input function
// (spec 7.8 lets a recursive cycle build into a pool it is handed by
// reference), so each input's tree is torn down by a pointer reset with no
// per-node free, where C++ and Rust destroy every node they built -- on the
// error path too, in the middle of unwinding. And the error itself is a
// `return ... from` (spec 7.9): a discriminant check per frame on the way
// out, against C++ exceptions (idiomatic, with a full unwind) or error codes
// threaded through every return (expert), and Rust's Result plus `?`.
//
// The tree keeps a Paren node for a parenthesised subexpression, because at
// that back edge the checker does not yet know the root of parse_expr's
// result; every implementation builds the same node so the node counts agree.

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

var text: u8[>..<] = [];
var pos = 0;
var nodes = 0;                           // Every node ever built, bad inputs included.

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
    let op = xs_mod(r >> 16, 4);
    text.push(' ');
    if op == 0 { text.push('+'); } else if op == 1 { text.push('-'); }
    else if op == 2 { text.push('*'); } else { text.push('/'); }
    text.push(' ');
    return gen(depth - 1, xs_next(r));
}

// One in four inputs is damaged after generation.
fn corrupt(seed: u64) {
    let kind = xs_mod(seed, 16);
    if kind == 0 {
        var i = text.len;
        while i > 0 {
            i--;
            if text[i] == ')' { text[i] = ' '; return; }
        }
        text[0] = '#';
        return;
    }
    if kind == 1 { text[xs_mod(seed >> 8, text.len)] = '#'; return; }
    if kind == 2 { text.resize(text.len / 2); return; }
    if kind == 3 { text.append(" / 0"); }
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
    let root: Expr..& = parse_expr(&pool);
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
