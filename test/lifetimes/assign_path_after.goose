// A whole assignment of a grow-only array leaves a reference to the whole
// array valid, as a clear does: a struct holding one, or the reference field
// the assignment writes through, may be used after the assignment, and its
// flat fields read by the right-hand side (§4.4, §5.1).
import std;

struct S { n: u32, r: u8[>..]& }

fn reset(s: S&) {
    s.r = ['a', 'b'];
    s.n += 1;
}

fn mk(n: u32) -> u8[>..] {
    var r: u8[>..] = [];
    r.push(n as u8 + 'a');
    return r;
}

fn regen(s: S&) {
    s.r = mk(s.n);
    s.n += 1;
}

struct T { n: u32, r: u8[>..]&, keys: u8[>..]& }

fn rekey(t: T&) {
    t.r = ['x'];
    t.keys.push('k');
    t.n += 1;
}

struct D { n: i64, tail: u8[>..] }
struct H { n: i64, d: D& }

fn main() {
    var a: u8[>..] = [];
    var s = S { n: 0, r: a };
    for i in 2 { reset(s); }
    print(a, " ", s.n);
    for i in 2 { regen(s); }
    print(a, " ", s.n);

    var b: u8[>..] = [];
    var k: u8[>..] = [];
    var t = T { n: 0, r: b, keys: k };
    for i in 2 { rekey(t); }
    print(b, " ", k, " ", t.n);

    var d = D { n: 1, tail: ['p'] };
    var h = H { n: 'q', d: d };
    d.tail = mk(h.n as u32 - 'a');
    print(d.tail, " ", h.d.n);
}
