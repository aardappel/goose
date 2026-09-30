// A view read out of the storage a parameter's class stands for -- the
// elements a slice parameter views, the holder a reference parameter names --
// is one of the views that storage holds: a shrink in the callee judges it
// by what the callee stored there, and leaves the rest to each call, which
// judges it by its record of the argument's storage (§5.1). Here none of
// them views the array shrunk.
struct H { s: const u8[:] }
struct N { v: i64, next: N&? }
var TABLE: (const u8[:])[>..] = [];
var GOUT: u8[>..] = [];

fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    for w in words { out.clear(); out.append(w); }
}
fn first_word(out: u8[>..]&, words: (const u8[:])[:]) {
    let w0 = words[0];
    out.clear();
    out.append(w0);
}
fn field(out: u8[>..]&, h: H&) {
    let s = h.s;
    out.clear();
    out.append(s);
}
fn clr(out: u8[>..]&) -> i64 {
    out.clear();
    out.append("c");
    return 0;
}
fn show(w: const u8[:], n: i64) { print(w, " ", n); }
fn held(out: u8[>..]&, words: (const u8[:])[:]) {
    show(words[0], clr(out));
}
fn holders(out: u8[>..]&, wss: (H[:])[:]) {
    let hs = wss[0];
    out.clear();
    out.append(hs[0].s);
}
// A slice of such a view, and a reference into what it views.
fn parts(out: u8[>..]&, words: (const u8[:])[:]) {
    let tail = words[1][1..];
    let c = &words[1][0];
    out.clear();
    out.append(tail);
    print(c);
}
// Out of either parameter's storage.
fn either(out: u8[>..]&, a: (const u8[:])[:], b: (const u8[:])[:], c: bool) {
    let ws = if c { a } else { b };
    let w = ws[0];
    out.clear();
    out.append(w);
}
// Through a reference to a slice with a class of its own for the slice.
fn through_slot(out: u8[>..]&, ws: ((const u8[:])[:])&) {
    let w = ws[1];
    out.clear();
    out.append(w);
}
// first_word's pair passes through mid, and a view mid reads out of its
// own parameter's storage is what first_word's parameter views.
fn mid(out: u8[>..]&, words: (const u8[:])[:]) {
    first_word(out, words);
    print(out);
}
fn mid_inner(out: u8[>..]&, nested: ((const u8[:])[:])[:]) {
    first_word(out, nested[0]);
    print(out);
}
fn put(out: u8[>..]&, w: const u8[:]) {
    out.clear();
    out.append(w);
}
fn each_put(out: u8[>..]&, words: (const u8[:])[:]) {
    for w in words { put(out, w); }
}
fn pick(ws: (const u8[:])[:]) -> const u8[:] { return ws[1]; }
fn picked(out: u8[>..]&, words: (const u8[:])[:]) {
    let w = pick(words);
    out.clear();
    out.append(w);
}
fn nested_fn(out: u8[>..]&, words: (const u8[:])[:]) {
    fn g() {
        let w = words[0];
        out.clear();
        out.append(w);
    }
    g();
}
recursive fn walk(out: u8[>..]&, words: (const u8[:])[:], i: i64) {
    if i < words.len {
        let w = words[i];
        out.clear();
        out.append(w);
        walk(out, words, i + 1);
    }
}
fn run<F>() { F(); }
fn in_block(out: u8[>..]&, words: (const u8[:])[:]) {
    run() {
        let w = words[1];
        out.clear();
        out.append(w);
    };
}
fn each<F>(words: (const u8[:])[:]) {
    for w in words { F(); print(w); }
}
// A view of another array stored there: the call gives the two apart.
fn store_other(a: u8[>..]&, b: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = b[..];
    let w = words[0];
    a.clear();
    a.append("def");
    print(w, " ", a);
}
fn bytes(arr: i32[>..]&, words: (const u8[:])[:]) {
    let w = words[0];
    arr.clear();
    arr.push(7);
    print(w.len);
}
// The node the caller linked lies outside the array shrunk.
fn follow(ns: N[>..]&) {
    let n = ns[1].next;
    if n {
        ns.clear();
        print(n.v);
    }
}

fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    names.push("yz");
    emit(buf, names[..]);
    print(buf);
    emit(buf, ["p", "q"]);
    print(buf);
    first_word(buf, names[..]);
    print(buf);
    var h = H { s: "hh" };
    field(buf, h);
    print(buf);
    held(buf, names[..]);
    var hs: H[>..] = [];
    hs.push(H { s: "h0" });
    var outer: (H[:])[>..] = [];
    outer.push(hs[..]);
    holders(buf, outer[..]);
    print(buf);
    parts(buf, names[..]);
    print(buf);
    var more: (const u8[:])[>..] = [];
    more.push("m");
    either(buf, names[..], more[..], false);
    print(buf);
    var s = names[..];
    through_slot(buf, s);
    print(buf);
    mid(buf, names[..]);
    var nested: ((const u8[:])[:])[>..] = [];
    nested.push(more[..]);
    mid_inner(buf, nested[..]);
    each_put(buf, names[..]);
    print(buf);
    picked(buf, names[..]);
    print(buf);
    nested_fn(buf, names[..]);
    print(buf);
    walk(buf, names[..], 0);
    print(buf);
    in_block(buf, names[..]);
    print(buf);
    each(names[..]) { buf.clear(); buf.append("e"); };
    var other: u8[>..] = [];
    other.append("oo");
    store_other(buf, other, names[..]);
    var arr: i32[>..] = [1, 2];
    var raw: (const u8[:])[>..] = [];
    raw.push("rrr");
    bytes(arr, raw[..]);
    var linked = N { v: 5, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &linked;
    follow(nodes);
    TABLE.push("t");
    GOUT.append("g");
    first_word(GOUT, TABLE[..]);
    print(GOUT);
}
