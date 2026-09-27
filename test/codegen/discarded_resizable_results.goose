// runtime-define: GS_STACK_RESERVE=4194304
// Resizable values evaluated only for their effects: call statements whose
// result is a `T[>..]`, a `T[>..<]`, a struct with a resizable tail or a
// variable-mode ADT with a resizable payload (§7.1) -- method-style,
// through a function value and in loops too -- and blocks, branches, loop
// breaks, variables and variant constants whose value is one. -O2 splices
// the callees in, so what is left to discard is the body's named result,
// its literal tail, a folded variant constant or an early return's local.
// Each value's storage goes with the statement or block that built it
// (§7.3): the loops discard more than a stack of the 4 MB reservation
// above could hold at once.

struct Fo { n: i64, items: u8[>..] }     // a frame object
struct Pt { x: i64, y: i64 }
enum Log { Empty, Lines { items: i64[>..] } }

var calls = 0;

fn word(c: u8, n: i64) -> u8[>..] {
    calls++;
    var w: u8[>..] = [];
    for i in n { w.push(c); }
    w
}
fn early(stop: bool) -> i64[>..<] {
    calls++;
    var x: i64[>..<] = [1, 2, 3];
    if stop { return x; }
    x.pop();
    x
}
fn frame(k: i64) -> Fo {
    calls++;
    var f = Fo { n: k, items: [] };
    f.items.push(k as u8);
    f
}
fn pts() -> Pt[>..] { [Pt { x: 1, y: 2 }, Pt { x: 3, y: 4 }] }
fn text() -> u8[>..] { "abc" }
fn log(k: i64) -> Log.. {
    calls++;
    if k > 0 { Log.Lines { items: [k, k] } } else { Log.Empty }
}

// A library function's shape: the result sized and filled through a slice.
fn fill(first: i64, out: u32[:]) -> i64 {
    calls++;
    for i in out.len { out[i] = (first + i) as u32; }
    return first;
}
fn ranges(first: i64) -> u32[>..] {
    var out: u32[>..] = [];
    let n = fill(first, out);
    out.resize(n, 0);
    fill(first, out);
    return out;
}

// Called method-style.
fn scaled(p: Pt, k: i64) -> i64[>..] {
    calls++;
    var v: i64[>..] = [p.x * k];
    v.push(p.y * k);
    v
}
fn spaced(s: const u8[:], gap: u8) -> u8[>..] {
    calls++;
    var r: u8[>..] = [];
    for c in s { r.push(c); r.push(gap); }
    r
}

// 80 KB each: a hundred of either at once would overflow the reservation.
fn big(n: i64) -> i64[>..] {
    var x: i64[>..] = [];
    x.resize(n, 7);
    x
}
fn filled() -> i64[>..] { [7; 10000] }

fn twice<F>() {
    F();
    F();
}

fn values(c: bool) {
    var a: u8[>..] = ['a'];
    var b: u8[>..] = ['b', 'b'];
    a;
    if c { a } else { b };
    match b.len {
        2 => b,
        _ => a,
    };
    {
        var t: u8[>..] = ['t'];
        t.push('u');
        t
    };
    var f = Fo { n: 1, items: ['f'] };
    f.items;
    f;
    let g: Log.. = Log.Lines { items: [1] };
    g;
    Log.Empty;
    a.push('c');
    print(a, " ", b, " ", f.items);
}

fn main() {
    var keep: u8[>..] = ['<'];
    word('a', 3);
    keep.push('1');
    early(true);
    early(false);
    keep.push('2');
    frame(5);
    frame(6).items;
    pts();
    text();
    ranges(99);
    log(0);
    log(1);
    keep.push('3');
    let p = Pt { x: 2, y: 5 };
    p.scaled(3);
    "xyz".spaced('-');
    keep.spaced('.');
    keep.push('>');
    twice() { word('f', 2) }
    twice(text);
    loop { break word('l', 1); }
    block { var t: u8[>..] = ['b']; t.push('c'); break copy(t); }
    print(keep, " ", calls);                    // <123> 15
    values(true);                               // ac bb f
    values(false);                              // ac bb f

    for i in 200 { big(10000); }
    var k = 0;
    while k < 200 { filled(); k++; }
    let n = 10000;
    for i in 200 { n.big(); }
    for i in 50 {
        word('w', i);
        let q = Pt { x: i, y: 1 };
        q.scaled(i);
        keep.spaced(' ');
    }
    keep.push('!');
    print(keep, " ", calls);                    // <123>! 165
}
