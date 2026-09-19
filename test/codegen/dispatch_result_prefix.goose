// runtime-debug: a dispatch's variable-array result takes its slot's prefix.
// A case-function dispatch (§8.2) whose arms return a variable array has
// every arm write the value in the arms' own layout, and where it lands in
// a slot of another length storage -- a struct or variant field, an element
// of an array literal, nested or of a grow-only array, a pushed element, a
// declared local, an argument, a dispatched call's argument, a branch's
// value, a returned value, one returned from further in, a global -- the
// slot's prefix replaces the arms' after the switch (§4.2, §7.3). The arms
// return u32, varint and u8 prefixes and the slots take varint (a second
// byte past 127 elements), u8, u16, u32 and u64 ones, so the prefix both
// shrinks and grows; adjacent values differ in length. An append of a
// function returning such a dispatch asks for its element run: the arms'
// own prefix is slid out whatever the function's storage, and a dispatch of
// resizable results hands its run on as it is.
enum Shape { Circle { r: i64 }, Sq { s: i64 } }
struct P { name: u8[varint], k: i64 }
struct Q { a: u8[u8], b: u8[], c: u8[u64], k: i64 }
enum E { A { s: u8[u16], k: i64 }, B }

fn mk(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in n { out.push(('a' + i % 26) as u8); }
    return out;
}
fn lbl(c: Shape.Circle) -> u8[] { return mk(c.r); }
fn lbl(q: Shape.Sq) -> u8[] { return "sq"; }
fn tag(c: Shape.Circle) -> u8[varint] { return mk(c.r); }
fn tag(q: Shape.Sq) -> u8[varint] { return "SQR"; }
fn short(c: Shape.Circle) -> u8[u8] { return mk(c.r); }
fn short(q: Shape.Sq) -> u8[u8] { return "s"; }
fn label(c: Shape.Circle) -> u8[>..] { return mk(c.r); }
fn label(q: Shape.Sq) -> u8[>..] {
    var out: u8[>..] = [];
    for i in q.s { out.push('#'); }
    return out;
}

fn take(s: u8[varint], k: i64) -> i64 {
    print(s, " ", k);
    return s.len;
}
fn describe(c: Shape.Circle, label: u8[u16]) -> i64 {
    print(label, " circle ", c.r);
    return label.len;
}
fn describe(q: Shape.Sq, label: u8[u16]) -> i64 {
    print(label, " sq ", q.s);
    return label.len;
}
// Big enough, and called often enough, to stay calls at -O2, where an
// append asks for their element-run twins as at -O0.
fn wide(s: Shape) -> u8[varint] {
    if spin(s) < 0 { return "never"; }
    return lbl(s);
}
fn plain(s: Shape) -> u8[] {
    if spin(s) < 0 { return "never"; }
    return tag(s);
}
fn grown(s: Shape) -> u8[varint] {
    if spin(s) < 0 { return "never"; }
    return label(s);
}

// An arm that unwinds to the function holding the dispatch.
fn bail(c: Shape.Circle) -> u8[] {
    if c.r > 4 { return "far" from evaluate; }
    return mk(c.r);
}
fn bail(q: Shape.Sq) -> u8[] { return "sq"; }
fn evaluate(s: Shape) -> u8[varint] { return bail(s); }
// A dispatch returned from further in, into the target's destination.
fn deep(s: Shape) { return lbl(s) from outer; }
fn outer(s: Shape) -> u8[u8] {
    deep(s);
    return "none";
}

fn spin(s: Shape) -> i64 {
    var t = match s { Circle c => c.r, Sq q => q.s };
    for i in 10 { t += i; t *= 2; t -= i / 2; t += i % 3; }
    return t;
}
fn sum(xs: u8[:]) -> i64 {
    var n = 0;
    for x in xs { n += x as i64; }
    return n;
}

let GA: Shape = Shape.Circle { r: 5 };
let GV: u8[varint] = lbl(GA);
let GW: u8[u16] = tag(GA);
let GK = 11;

fn main() {
    let a: Shape = Shape.Circle { r: 3 };
    let b: Shape = Shape.Sq { s: 2 };
    let c6: Shape = Shape.Circle { r: 6 };
    let c127: Shape = Shape.Circle { r: 127 };
    let c128: Shape = Shape.Circle { r: 128 };
    let big: Shape = Shape.Circle { r: 200 };

    let p = P { name: lbl(a), k: 7 };
    print(p.name, " ", p.k);
    let q = Q { a: lbl(big), b: tag(c128), c: short(a), k: 8 };
    print(q.a.len, " ", sum(q.a), " ", q.b.len, " ", sum(q.b), " ", q.c, " ", q.k);
    let es: E..[] = [E.A { s: lbl(c6), k: 1 }, E.B, E.A { s: tag(b), k: 2 }];
    for e in es { match e { A v => print(v.s, " ", v.k), B => print("B") } }

    // Past 127 elements a varint prefix needs a second byte.
    let xs: u8[varint][] = [lbl(a), lbl(b), lbl(c127), lbl(c128), short(big), lbl(c6)];
    for x in xs { print(x.len, " ", sum(x)); }
    let us: u8[u8][] = [lbl(b), lbl(big), tag(c128)];
    for u in us { print(u.len, " ", sum(u)); }
    let ws: u8[u16][] = [lbl(c128), tag(b), short(big)];
    for w in ws { print(w.len, " ", sum(w)); }
    let ls: u8[][] = [tag(a), tag(big), short(c6), short(b)];
    for l in ls { print(l.len, " ", sum(l)); }
    let nest: u8[varint][][] = [[lbl(a)], [lbl(b), lbl(big)]];
    for row in nest { for r in row { print(r.len, " ", sum(r)); } }

    var vg: u8[varint][>..] = [lbl(big), lbl(a)];
    vg.push(lbl(b));
    vg.push(lbl(c128));
    for v in vg { print(v.len, " ", sum(v)); }
    var ug: u8[u16][>..] = [];
    ug.push(short(big));
    ug.push(tag(c6));
    for u in ug { print(u.len, " ", sum(u)); }

    let x: u8[varint] = lbl(a);
    let x2: u8[u8] = lbl(big);
    print(x, " ", x2.len, " ", sum(x2));
    print(take(lbl(c6), 9));
    print(describe(b, lbl(a)));
    print(describe(a, tag(c6)));
    for k in 2 {
        let y: u8[u8] = if k > 0 { lbl(c6) } else { tag(b) };
        let z: u8[varint] = match k { 0 => lbl(b), _ => tag(a) };
        print(y, " ", z);
    }
    print(wide(a), " ", wide(b), " ", wide(big).len);
    print(evaluate(a), " ", evaluate(b), " ", evaluate(c6));
    print(outer(a), " ", outer(b));
    print(GV, " ", GW, " ", GK);

    var out: u8[>..] = [];
    out.append(lbl(a));
    out.push('|');
    out.append(tag(c6));
    out.push('|');
    out.append(wide(b));
    out.push('|');
    out.append(plain(a));
    out.push('|');
    out.append(grown(b));
    out.push('|');
    out.append(grown(c6));
    print(out);
    var runs: u8[>..] = [];
    runs.append(wide(big));
    runs.append(plain(c128));
    runs.append(grown(big));
    runs.append(plain(b));
    print(runs.len, " ", sum(runs));
}
