// runtime-debug: a call's result of one array or slice kind, checked as
// another where it lands, constructs the slot's kind (§4.2): fixed,
// static-capacity limited and runtime-capacity limited arrays, slices,
// variable, grow-only and grow-shrink arrays, of bytes, integers, structs and
// variable-size elements, building variable arrays of other length storages,
// resizable arrays and runtime-capacity limited arrays -- as arguments,
// bindings, an assignment, fields, elements, results, a long-distance
// return's value, globals and an `if`'s value. The calls are to functions,
// builtins and a tag dispatch. At -O2 most functions that only return a
// value unwrap to it, those with several returns stay inlined blocks, and
// the two-result ones and the dispatch stay calls.
struct P { x: i64, y: u8 }
struct R { tag: u8[], v: u8[], w: u8[u16], z: u8[varint], c: u8[..], after: i64 }
struct B { n: i64, data: u8[>..] }
enum Sh { Sq { a: i64 }, Ci { r: i64 } }

var h: u8[>..] = [];

// One return each.
fn fix() -> u8[3] { return [7, 8, 9]; }
fn fixi() -> i64[3] { return [300, 400, 500]; }
fn fixp() -> P[2] { return [P { x: 1, y: 2 }, P { x: 3, y: 4 }]; }
fn lim() -> u8[..8] { return "lim"; }
fn limi() -> i64[..4] { return [11, 22]; }
fn sli() -> u8[:] { return h[..]; }
fn capr() -> u8[..] { return "caps"; }
fn var1() -> u8[] { return "varia"; }
fn vlit() -> u8[] { return [1, 2, 3, 4, 5, 6]; }
fn grow() -> u8[>..] { return "growing"; }
fn glit() -> u8[>..] { return [5, 6]; }
fn clip(s: u8[:]) -> u8[..8] { return s; }
fn wrap() -> u8[] { return fix(); }
fn wrapl() -> u8[] { return clip(h[1..]); }

// Several returns each.
fn fixb(k: i64) -> u8[3] {
    if k > 1 { return [1, 2, 3]; }
    let a: u8[3] = [4, 5, 6];
    return a;
}
fn limb(k: i64) -> u8[..8] {
    if k > 1 { return "long!"; }
    return h[..2];
}
fn slib(k: i64) -> u8[:] {
    if k > 1 { return h[1..]; }
    return h[..];
}
fn capb(k: i64) -> u8[..] {
    var l: u8[..] = [..6];
    l.push(97);
    if k > 1 { l.push(98); }
    return l;
}
fn capl(k: i64) -> u8[..] {
    if k > 1 { return [..3]; }
    return "cc";
}
fn varb(k: i64) -> u8[] {
    if k > 1 { return [9, 8, 7]; }
    return "xy";
}
fn growb(k: i64) -> u8[>..] {
    var r: u8[>..] = [];
    r.push(10);
    if k > 1 { r.push(20); }
    return r;
}
fn gsh() -> u8[>..<] {
    var r: u8[>..<] = [];
    r.push(4);
    r.push(5);
    return r;
}
fn words() -> u8[][] { return ["ab", "cde"]; }
fn wordsb(k: i64) -> u8[][] {
    if k > 1 { return ["x"]; }
    var a: u8[][>..] = [];
    a.push("pq");
    a.push("rst");
    return a;
}

// A value returned to a function further up.
fn inner(k: i64) -> i64 {
    if k > 0 { return fix() from outer; }
    return 5;
}
fn outer(k: i64) -> u8[] {
    let n = inner(k);
    return [n as u8];
}

// Two results each: an ordinary value context takes the first.
fn fix2() -> u8[3], i64 { return [21, 22, 23], 1; }
fn lim2() -> u8[..8], i64 { return "l2", 2; }
fn sli2() -> u8[:], i64 { return h[2..], 3; }
fn cap2() -> u8[..], i64 { return "cap2", 4; }
fn var2() -> u8[], i64 { return "v2", 5; }

fn name(s: Sh.Sq) -> u8[3] { return [83, 81, s.a as u8]; }
fn name(s: Sh.Ci) -> u8[3] { return [67, 73, s.r as u8]; }

// Results of another kind than the calls in their returns.
fn asvar() -> u8[] { return lim(); }
fn asgrow() -> u8[>..] { return fix(); }
fn ascap() -> u8[..] { return sli(); }
fn aswide() -> u8[u16] { return capr(); }

// Each slot's length and element sum, read as the slot's kind; a variable's
// or a field's through a slice of it.
fn tv(x: u8[]) -> i64 {
    var s = 0;
    for e in x { s += e; }
    return x.len * 10000 + s;
}
fn tw(x: u8[u16]) -> i64 {
    var s = 0;
    for e in x { s += e; }
    return x.len * 10000 + s;
}
fn tr(x: u8[>..]) -> i64 {
    var s = 0;
    for e in x { s += e; }
    return x.len * 10000 + s;
}
fn tl(x: u8[..]) -> i64 {
    var s = 0;
    for e in x { s += e; }
    return x.len * 10000 + s;
}
fn ti(x: i64[]) -> i64 {
    var s = 0;
    for e in x { s += e; }
    return x.len * 10000 + s;
}
fn sv(x: u8[:]) -> i64 {
    var s = 0;
    for e in x { s += e; }
    return x.len * 10000 + s;
}
fn tn(x: u8[][u16]) -> i64 {
    var t = 0;
    for w in x { t = t * 10 + w.len; }
    return x.len * 10000 + t;
}
fn tnr(x: u8[][>..]) -> i64 {
    var t = 0;
    for w in x { t = t * 10 + w.len; }
    return x.len * 10000 + t;
}
fn tp(x: P[]) -> i64 {
    var s = 0;
    for e in x { s += e.x * 10 + e.y; }
    return x.len * 10000 + s;
}

var gv: u8[] = fix();
var gr: u8[>..] = lim();
var gl: u8[..] = var1();

fn main() {
    h.append("sliced");
    print(tv(fix()), " ", tv(lim()), " ", tv(sli()), " ", tv(capr()), " ", tv(grow()));
    print(tw(fix()), " ", tw(lim()), " ", tw(sli()), " ", tw(capr()), " ", tw(var1()));
    print(tr(fix()), " ", tr(lim()), " ", tr(sli()), " ", tr(capr()), " ", tr(vlit()));
    print(tl(fix()), " ", tl(lim()), " ", tl(sli()), " ", tl(var1()), " ", tl(vlit()), " ",
          tl(grow()), " ", tl(glit()));
    print(ti(fixi()), " ", ti(limi()), " ", tp(fixp()));
    print(tv(clip(h[..])), " ", tv(wrap()), " ", tv(wrapl()), " ", tv(glit()));
    for k in 3 {
        print(tv(fixb(k)), " ", tv(limb(k)), " ", tv(slib(k)), " ", tv(capb(k)), " ",
              tv(capl(k)), " ", tw(varb(k)), " ", tl(varb(k)), " ", tl(growb(k)));
        print(tr(fixb(k)), " ", tr(limb(k)), " ", tl(slib(k)), " ", tr(capb(k)), " ",
              tl(capl(k)), " ", tw(growb(k)));
    }
    print(tv(fix2()), " ", tr(lim2()), " ", tl(sli2()), " ", tw(cap2()), " ", tl(var2()));

    let a: u8[] = fix();
    let b: u8[>..] = lim();
    var c: u8[>..] = sli();
    let d: u8[..] = var1();
    let e: u8[u16] = capr();
    print(sv(a), " ", sv(b), " ", sv(c), " ", sv(d), " ", sv(e));
    c = fixb(2);
    print(sv(c));
    c = capb(2);
    print(sv(c));

    let r = R { tag: "t", v: fix(), w: lim(), z: sli(), c: var1(), after: 77 };
    print(sv(r.tag), " ", sv(r.v), " ", sv(r.w), " ", sv(r.z), " ", sv(r.c), " ", r.after);
    let q = R { tag: "tt", v: capb(1), w: fixb(1), z: capr(), c: fix(), after: 78 };
    print(sv(q.tag), " ", sv(q.v), " ", sv(q.w), " ", sv(q.z), " ", sv(q.c), " ", q.after);
    let bb = B { n: 3, data: lim() };
    print(bb.n, " ", sv(bb.data));

    var els: u8[][>..] = [];
    els.push(fix());
    els.push(lim());
    els.push(capr());
    els.push(slib(1));
    for x in els { print(sv(x)); }
    let lit: u8[][] = [capr(), fix(), sli()];
    for x in lit { print(sv(x)); }

    print(tv(asvar()), " ", tr(asgrow()), " ", tl(ascap()), " ", tw(aswide()));
    print(sv(gv), " ", sv(gr), " ", sv(gl));

    for k in 3 {
        let y: u8[] = if k == 0 { fix() } else if k == 1 { lim() } else { capr() };
        let z: u8[>..] = if k == 0 { sli() } else if k == 1 { capb(k) } else { fixb(k) };
        print(sv(y), " ", sv(z));
    }

    var popped: u8[3][>..] = [];
    popped.push([1, 2, 3]);
    popped.push([4, 5, 6]);
    let dv: u8[] = default<u8[3]>();
    print(sv(dv), " ", tl(popped.pop()), " ", tv(popped.pop()));
    print(tl(str(42)), " ", tl(to_bytes(h)), " ", tw(str(7)));
    let img = to_bytes(h);
    print(tl(from_bytes<u8[]>(img[..])), " ", tw(from_bytes<u8[]>(img[..])));
    print(tl(gsh()), " ", tr(gsh()), " ", tv(gsh()), " ", tv(outer(1)), " ", tv(outer(0)));
    print(tn(words()), " ", tnr(words()));
    for k in 3 { print(tn(wordsb(k)), " ", tnr(wordsb(k))); }
    var shapes: Sh[>..] = [];
    shapes.push(Sh.Sq { a: 1 });
    shapes.push(Sh.Ci { r: 2 });
    for s in shapes { print(tv(name(s)), " ", tl(name(s))); }
}
