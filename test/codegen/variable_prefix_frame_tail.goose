// runtime-debug: a frame object's type (C.2) as the tail of a struct with a
// variable-size prefix, or of an ADT payload, is bytes of that value like
// its other fields: its fixed fields follow the prefix and its own tail's
// count is the owner's. Built from every kind of value and from defaults,
// read, grown in place, reassigned whole and copied out.
struct Stack { n: i64, items: i64[>..<] }
struct Outer { n: i64, tag: u8[], tail: Stack }
// A bare pad aligns nothing in a variable-size layout, where an explicit
// one still takes its bytes; the frame object's own C layout aligns.
struct Padded { a: u8, pad, b: i32, pad 2, c: i16, items: i64[>..<] }
struct PadOuter { tag: u8[], p: Padded }
struct Inner { m: i32, items: i64[>..<] }
struct Mid { k: i64, inner: Inner }
struct Deep { tag: u8[], mid: Mid }
struct Bare { items: i64[>..<] }
struct BareOuter { tag: u8[], tail: Bare }
enum Shape { Plain { n: i64 }, Stacked { k: i64, s: Stack } }

fn made(k: i64) -> Stack { return Stack { n: k, items: [k, k + 1] }; }
// A named result: at -O2 its elements are built where the caller's value
// lands.
fn named(k: i64) -> Stack {
    var s = Stack { n: k, items: [] };
    for i in 0..k { s.items.push(i * 10); }
    return s;
}
fn mid(k: i64) -> Mid { return Mid { k: k, inner: Inner { m: 70, items: [k] } }; }
fn padded(k: i32) -> Padded { return Padded { a: 1, b: k, c: 3, items: [4] }; }
fn bare() -> Bare { return Bare { items: [1, 2] }; }
// The value under construction is abandoned for another one on the way.
fn early(c: bool) -> Outer {
    return Outer { n: 1, tag: "partial",
                   tail: if c { return Outer { n: 2, tag: "early", tail: made(3) }; }
                         else { made(4) } };
}
fn refill(o: Outer&, k: i64) {
    o.tail = made(k);
    o.tail.items.push(o.tail.n);
}
// A frame object held as one still has its tail's header to reference.
fn deepen(x: Mid&) {
    let r = x.inner.items;
    r.push(x.k);
}
fn total(var s: Stack) -> i64 {
    s.items.push(100);
    var t = s.n;
    for x in s.items { t += x; }
    return t;
}

var global = Outer { n: 1, tag: "global", tail: made(2) };
var blank: Outer;

fn main() {
    var s = Stack { n: 5, items: [6] };
    var lit = Outer { n: 1, tag: "lit", tail: Stack { n: 2, items: [3] } };
    var call = Outer { n: 1, tag: "call", tail: made(4) };
    var cp = Outer { n: 1, tag: "copy", tail: copy(s) };
    var br = Outer { n: 1, tag: "branch", tail: if s.n > 3 { copy(s) } else { made(0) } };
    var blk = Outer { n: 1, tag: "block", tail: { let k = s.n * 2; Stack { n: k, items: [k] } } };
    var nrv = Outer { n: 1, tag: "named", tail: named(3) };
    s.items.push(7);
    print(lit, " ", call, " ", cp);
    print(br, " ", blk, " ", nrv, " ", s);

    // Reads, and the tail grown and shrunk where it lies.
    call.tail.items.push(20);
    call.tail.items.append([30, 40]);
    call.tail.n = call.tail.items.pop();
    call.tail.items[0] = call.tail.items[1] * 2;
    var sum = 0;
    for x in call.tail.items { sum += x; }
    print(call.tail.n, " ", call.tail.items.len, " ", sum, " ", call.tag, " ", call.n);
    call.tail.items.resize(6, -1);
    print(call);
    call.tail.items.clear();
    global.tail.items.push(global.tail.n);
    print(call, " ", global);

    // Whole assignments: of the owner, and of the tail alone.
    lit = Outer { n: 9, tag: "whole", tail: made(8) };
    print(lit);
    lit.tail = Stack { n: 7, items: [] };
    lit.tail.items.push(lit.n);
    cp.tail = named(2);
    refill(br, 6);
    print(lit, " ", cp, " ", br);

    // Copied out whole.
    var out = copy(nrv.tail);
    out.items.push(99);
    print(out, " ", nrv.tail, " ", total(copy(blk.tail)), " ", blk.tail.items.len);

    print(early(true), " ", early(false));

    // Nested frame objects, a layout with pads, and one with no fixed fields.
    var d = Deep { tag: "deep", mid: Mid { k: 1, inner: Inner { m: 2, items: [3] } } };
    var e = Deep { tag: "deeper", mid: mid(4) };
    d.mid.inner.items.push(30);
    e.mid.k = e.mid.inner.items.pop();
    var m = copy(d.mid);
    m.inner.items.push(5);
    deepen(m);
    e.mid = copy(m);
    e.mid.inner.m = 71;
    print(d, " ", e, " ", m);
    var p = PadOuter { tag: "p", p: Padded { a: 5, b: 6, c: 7, items: [8] } };
    var q = PadOuter { tag: "q", p: padded(9) };
    print(p, " ", q);
    var r = copy(q.p);
    r.items.push(10);
    p.p = copy(r);
    print(p, " ", r);
    var b = BareOuter { tag: "b", tail: bare() };
    var c = copy(b.tail);
    c.items.push(3);
    b.tail = copy(c);
    b.tail.items.push(4);
    print(b, " ", c);

    // Filled in from defaults: by a literal's `..`, and as a var global's
    // starting value.
    var dflt = Outer { n: 2, .. };
    dflt.tail.items.push(dflt.n);
    var dd = Deep { tag: "dd", .. };
    dd.mid.inner.items.push(3);
    blank.tail.items.push(1);
    print(dflt, " ", dd, " ", blank);

    // A payload's resizable last field.
    var sh: Shape.. = Shape.Stacked { k: 1, s: made(2) };
    var sl: Shape.. = Shape.Stacked { k: 3, s: Stack { n: 4, items: [] } };
    print(sh, " ", sl);
}
