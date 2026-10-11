// Assigning through a reference field writes the array the field refers to
// (§3.8, §4.4). The right-hand side may read the struct's fields that
// cannot be or hold that array: flat ones, and arrays of another type; and
// on a local whose record shows the field refers elsewhere, an array of the
// same type too.
import std;

fn twice(xs: const u8[:]) -> u8[>..<] {
    var r: u8[>..<] = [];
    r.append(xs);
    r.append(xs);
    return r;
}

fn encode(words: const i64[:]) -> u8[>..<] {
    var r: u8[>..<] = [];
    for w in words { r.push(w as u8 + 'a'); }
    return r;
}

struct D { n: i64, r: u8[>..<]&, own: u8[>..<] }
struct W { n: i64, r: u8[>..<]&, words: i64[>..<] }

fn bump(s: D&) { s.r = twice([s.n as u8 + 'a']); }
fn spell(s: W&) { s.r = encode(s.words); }

fn main() {
    var x: u8[>..<] = [];
    var d = D { n: 2, r: x, own: ['x', 'y'] };
    d.r = twice(d.own);
    print(x);
    bump(d);
    print(x);

    var out: u8[>..<] = [];
    var w = W { n: 0, r: out, words: [0, 1, 2] };
    spell(w);
    print(out);
}
