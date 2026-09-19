// runtime-debug: an exit's value lands where its destination starts.
// A return, a break with a value and a `return ... from` build their value
// at the top of the stack they deliver it to, while the receiver expects it
// where that top was when the function, inlined body or loop was entered.
// When the exit abandons something already built there -- an intermediate's
// named result in the destination the target handed it, part of the
// target's own literal, str() or struct, a length prefix claimed for a
// callee's elements, part of a literal around an early return or a break --
// the value is moved down over it. Every shape runs with and without the
// exit; at -O2 the helpers are inlined, so exits leave inlined bodies, into
// an inlined target or into one that stays a function (evaluate2, called
// from several places). After each delivery something is built right
// behind it on the same stack.

// Named results of intermediates that hand on the target's destination: a
// resizable, one returned as a u8[] behind a claimed prefix, a frame object.
fn bail(n: i64) -> i64 {
    if n > 2 { return str("bad", n) from evaluate; }
    return n;
}
fn describe(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    out.push('x');
    out.push(('0' + bail(n)) as u8);
    return out;
}
fn evaluate(n: i64) -> u8[>..] { return describe(n); }

fn bail2(n: i64) -> i64 {
    if n > 2 { return "bad" from evaluate2; }
    return n;
}
fn describe2(n: i64) -> u8[] {
    var out: u8[>..] = [];
    out.push('y');
    out.push(('0' + bail2(n)) as u8);
    return out;
}
fn evaluate2(n: i64) -> u8[] { return describe2(n); }

struct Bag { tag: i64, items: i64[>..] }
fn bail_bag(n: i64) -> i64 {
    if n > 2 { return Bag { tag: 99, items: [7, 8, 9] } from take_bag; }
    return n;
}
fn mid_bag(n: i64) -> Bag {
    var b = Bag { tag: 1, items: [] };
    b.items.push(10);
    b.items.push(bail_bag(n));
    return b;
}
fn take_bag(n: i64) -> Bag { return mid_bag(n); }

// Two results, each on its own destination.
fn bail_pair(n: i64) -> i64 {
    if n > 2 { return "p", "qq" from take_pair; }
    return n;
}
fn mid_pair(n: i64) -> u8[], u8[] {
    var a: u8[>..] = [];
    var b: u8[>..] = [];
    a.push('a');
    b.push('b');
    b.push(('0' + bail_pair(n)) as u8);
    return a, b;
}
fn take_pair(n: i64) -> u8[], u8[] { return mid_pair(n); }

// The target's own value under construction.
fn bail_s(n: i64) -> i64 {
    if n > 2 { return "bad" from ev_str; }
    return n;
}
fn ev_str(n: i64) -> u8[] { return str("s", bail_s(n)); }

fn part(n: i64) -> u8[] {
    if n > 2 { return [str("bad")] from ev_lit; }
    return str("p", n);
}
fn ev_lit(n: i64) -> u8[][] { return [str("a"), part(n)]; }

struct Two { a: u8[], b: u8[] }
fn part_two(n: i64) -> u8[] {
    if n > 2 { return Two { a: "bad", b: "!" } from ev_two; }
    return str("f", n);
}
fn ev_two(n: i64) -> Two { return Two { a: str("a"), b: part_two(n) }; }

// An intermediate's literal in the destination it was handed.
fn part_mid(n: i64) -> u8[] {
    if n > 2 { return [str("bad")] from ev_mid; }
    return str("q", n);
}
fn mid(n: i64) -> u8[][] { return [str("m"), part_mid(n)]; }
fn ev_mid(n: i64) -> u8[][] { return mid(n); }

// A u8[] result in a u8[varint] destination: its element-run twin writes
// behind a prefix claimed before the call.
fn bail_rp(n: i64) -> i64 {
    if n > 2 { return "bad" from ev_rp; }
    return n;
}
fn d_rp(n: i64) -> u8[] {
    let k = bail_rp(n);
    return str("r", k);
}
fn ev_rp(n: i64) -> u8[varint] { return d_rp(n); }

// A callee with no element-run twin, being a target itself: its value is
// re-prefixed for a u8[varint], or slid in behind appended elements, and
// neither may happen to a value in flight to an outer target.
fn hop_rp(n: i64) {
    if n == 1 { return str("hop") from twin_rp; }
    if n > 2 { return str("bad") from ev_tw; }
}
fn twin_rp(n: i64) -> u8[] {
    hop_rp(n);
    return str("t", n);
}
fn ev_tw(n: i64) -> u8[varint] { return twin_rp(n); }

fn hop_sl(n: i64) {
    if n == 1 { return str("hop") from twin_sl; }
    if n > 2 { return str("bad") from ev_sl; }
}
fn twin_sl(n: i64) -> u8[] {
    hop_sl(n);
    return str("t", n);
}
fn mid_sl(n: i64) -> u8[>..] {
    var v: u8[>..] = [];
    v.push('v');
    v.append(twin_sl(n));
    return v;
}
fn ev_sl(n: i64) -> u8[>..] { return mid_sl(n); }

// A second `return from` while the first one's value is being built.
fn inner(n: i64) -> u8[] {
    if n > 3 { return [str("inner")] from outer; }
    return str("i", n);
}
fn half(n: i64) -> u8[] {
    if n > 2 { return [str("h"), inner(n)] from outer; }
    return str("g", n);
}
fn outer(n: i64) -> u8[][] { return [str("o"), half(n)]; }

// Recursion: the innermost call catches.
fn rbail(n: i64) -> i64 {
    if n == 2 { return str("stop", n) from rec; }
    return n;
}
recursive fn rec(n: i64) -> u8[] {
    if n == 0 { return str("end"); }
    return str("<", rbail(n), rec(n - 1), ">");
}

// A plain return, and breaks, inside a value under construction.
fn early(n: i64) -> u8[][] {
    return [str("a"), if n > 2 { return [str("bad")]; } else { str("b", n) }];
}
fn early_str(n: i64) -> u8[] {
    return str("x", if n > 2 { return "bad"; } else { n });
}
fn brk(n: i64) -> u8[][] {
    var i = 0;
    let r: u8[][] = loop {
        i += 1;
        if i > 1 { break [str("a"), if n > 2 { break [str("bad")]; } else { str("b", n) }]; }
    };
    return r;
}
fn blk(n: i64) -> u8[][] {
    let r: u8[][] = block {
        break [str("a"), if n > 2 { break [str("bad")]; } else { str("b", n) }];
    };
    return r;
}
fn early_g(n: i64) -> u8[][] {
    return [str("a"), if n > 2 { return [str("bad")]; } else { str("b") }];
}
let gl: u8[][] = early_g(3);

fn main() {
    for n in 1..5 {
        var e = evaluate(n);
        e.push('!');
        print(n, " ", e, " ", [evaluate2(n), str("next"), evaluate2(1)]);
        var b = take_bag(n);
        b.items.push(100 + n);
        let more = take_bag(1);
        print(n, " ", b.tag, " ", b.items, " ", more.items);
        let p, q = take_pair(n);
        print(n, " ", p, " ", q);
        let two = ev_two(n);
        print(n, " ", ev_str(n), " ", [ev_lit(n), [str("next")]], " ", two.a, two.b);
        print(n, " ", [ev_mid(n), [str("next")]], " ", ev_rp(n), " ", ev_tw(n));
        var v = ev_sl(n);
        v.push('!');
        print(n, " ", v, " ", [outer(n), [str("next")]], " ", evaluate2(n));
        print(n, " ", [early(n), [str("next")]], " ", early_str(n), " ", brk(n), " ", blk(n));
    }
    print(gl, " ", rec(3), " ", rec(1));
}
