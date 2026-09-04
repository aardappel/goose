// The flagship: generate an s-expression text, parse it into a tree, walk it.
// Nodes live in one grow-only pool with no allocator, each node is exactly as
// big as its variant needs, symbol text is stored inline behind a varint
// length, and the tree is never freed because there is nothing to free.
//
// Children are linked first-child / previous-sibling, so every node is
// complete when it is pushed and no link is ever patched afterwards. That
// means the walk sees each list's children in reverse source order, which the
// C++ rows reproduce exactly so the checksums line up.
//
// Every link is a 4-byte self-relative offset into the pool, including the
// sentinel end of a sibling chain: a null relative reference is the zero
// offset, which needs no root, so a chain that ends in null is still
// relative throughout (spec 3.9).
//
// The parser carries a `return from` error path (spec 7.9) it never takes on
// this input, which is the honest thing to measure: real parsers have one,
// and it costs a discriminant check on every return in the cycle.

import rng;
import std;

let N = 200000;                          // BENCH_N
let DEPTH = 4;
let PASSES = 4;

enum Sexp {
    Sym  { name: u8[varint], next: Sexp..&<u32>? },
    Num  { v: varint, next: Sexp..&<u32>? },
    List { first: Sexp..&<u32>?, next: Sexp..&<u32>? },
}

var text: u8[>..] = [];
var pos = 0;
var pool: Sexp..[>..] = [];
var roots: Sexp..&[>..] = [];

// --- generation --------------------------------------------------------------

fn emit_sym(seed: u64) {
    var r = seed;
    let n = 3 + xs_mod(r, 12);
    for i in n {
        r = xs_next(r);
        text.push(('a' + xs_mod(r, 26)) as! u8);
    }
}

recursive fn gen(depth: i64, seed: u64) -> u64 {
    var r = xs_next(seed);
    if depth == 0 || xs_mod(r, 4) == 0 {
        if xs_mod(r >> 8, 2) == 0 { emit_sym(r); }
        else { format_int(text, xs_mod(r >> 16, 100000), 10, 0, ' '); }
        text.push(' ');
        return r;
    }
    text.push('(');
    let kids = 2 + xs_mod(r >> 24, 4);
    for k in kids { r = gen(depth - 1, r); }
    text.push(')');
    text.push(' ');
    r
}

// --- parsing -----------------------------------------------------------------

fn skip_space() { while pos < text.len && text[pos] == ' ' { pos++; } }

// A symbol's bytes, copied straight into the node under construction.
fn sym_text(start: i64, end: i64) -> u8[varint] { text[start..end] }

recursive fn parse_node(prev: Sexp..&?) -> Sexp..& {
    skip_space();
    guard pos < text.len else { return 0, "eof" from parse_all; }
    let c = text[pos];
    if c == '(' {
        pos++;
        var chain: Sexp..&? = null;
        loop {
            skip_space();
            guard pos < text.len else { return 0, "unclosed" from parse_all; }
            if text[pos] == ')' { pos++; break; }
            chain .= parse_node(chain);
        }
        return pool.push(Sexp.List { first: chain, next: prev });
    }
    let start = pos;
    if c >= '0' && c <= '9' {
        var v = 0;
        while pos < text.len && text[pos] >= '0' && text[pos] <= '9' {
            v = v * 10 + (text[pos] - '0');
            pos++;
        }
        return pool.push(Sexp.Num { v: v, next: prev });
    }
    while pos < text.len && text[pos] != ' ' && text[pos] != '(' && text[pos] != ')' { pos++; }
    return pool.push(Sexp.Sym { name: sym_text(start, pos), next: prev })
}

fn parse_all() -> i64, u8[] {
    loop {
        skip_space();
        guard pos < text.len;
        let r: Sexp..& = parse_node(null);
        roots.push(r);
    }
    return roots.len, "";
}

// --- walking -----------------------------------------------------------------

// The sibling link, reached through tag dispatch: `next` lives on the variant
// types, not on the enum.
fn nextof(n: Sexp.Sym&)  -> Sexp..? { n.next }
fn nextof(n: Sexp.Num&)  -> Sexp..? { n.next }
fn nextof(n: Sexp.List&) -> Sexp..? { n.next }

// The sibling chain is walked by passing the next link down: inside a
// recursive cycle a reference may be passed as an argument but not stored
// (spec 7.8), so the loop form with `.=` is not available here.
// The pass index rides along so the four walks are four different
// computations, which stops a backend that can see the walk is pure from
// hoisting it out of the loop (see bench/notes.md).
recursive fn walk(n: Sexp.Sym&, p: i64) -> i64 { n.name.len + n.name[0] + p }
recursive fn walk(n: Sexp.Num&, p: i64) -> i64 { n.v ^ p }
recursive fn walk(n: Sexp.List&, p: i64) -> i64 { 1 + walk_chain(n.first, p) }

recursive fn walk_chain(c: Sexp..?, p: i64) -> i64 {
    guard c else { return 0; }
    return walk(c, p) + walk_chain(nextof(c), p);
}

fn main() {
    var seed: u64 = 12345;
    for i in N { seed = gen(DEPTH, seed); }
    let nroots, err = parse_all();
    print(text.len);
    print(pool.len);
    print(nroots);
    print(err.len);
    var total = 0;
    for p in PASSES {
        for r in roots { total += walk(r, p); }
    }
    print(total);
}
