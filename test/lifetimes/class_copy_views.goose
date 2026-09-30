// A holder copied out of the storage a parameter's class stands for -- an
// element of the caller's array a slice parameter views, the holder a
// reference parameter names -- and a view read out of that storage kept in
// a holder of the activation's hold what that storage holds: a shrink in
// the callee judges them by what the callee stored there, and leaves the
// rest to each call, which judges it by its record of the argument's
// storage (§5.1). Here none of them views the array shrunk.
struct H { s: const u8[:] }
struct N { v: i64, next: N&? }
enum E { Txt { s: const u8[:] }, Nothing { k: i64 } }
var TABLE: H[>..] = [];
var GOUT: u8[>..] = [];

fn emit(out: u8[>..]&, hs: H[:]) {
    for h in hs { out.clear(); out.append(h.s); }
}
fn kept(out: u8[>..]&, words: (const u8[:])[:]) {
    let h = H { s: words[0] };
    out.clear();
    out.append(h.s);
}
fn assigned(out: u8[>..]&, words: (const u8[:])[:]) {
    var h = H { s: "" };
    h.s = words[1];
    out.clear();
    out.append(h.s);
}
fn copied(out: u8[>..]&, hs: H[:]) {
    let h = hs[1];
    out.clear();
    out.append(h.s);
}
fn matched(out: u8[>..]&, es: E[:]) {
    match es[0] {
        Txt t => {
            out.clear();
            out.append(t.s);
        },
        Nothing n => print(n.k),
    }
}
fn deref(out: u8[>..]&, r: H&) {
    let h: H = r;
    out.clear();
    out.append(h.s);
}
fn field(out: u8[>..]&, r: H&) {
    let h = H { s: r.s };
    out.clear();
    out.append(h.s);
}
fn popped(out: u8[>..]&, hs: H[>..]&) {
    let h = hs.pop();
    out.clear();
    out.append(h.s);
}
fn appended(out: u8[>..]&, hs: H[:]) {
    var keep: H[>..] = [];
    keep.append(hs);
    out.clear();
    out.append(keep[1].s);
}
fn pushed(out: u8[>..]&, hs: H[:]) {
    var keep: H[>..] = [];
    for h in hs { keep.push(h); }
    out.clear();
    out.append(keep[0].s);
}
fn dup(out: u8[>..]&, hs: H[:]) {
    let h = copy(hs[0]);
    out.clear();
    out.append(h.s);
}
// A callee copies a holder out of one parameter's storage into another's.
fn copy_into(dst: H[:], src: H[:]) { dst[0] = src[0]; }
fn copied_in(out: u8[>..]&, dst: H[:], src: H[:]) {
    copy_into(dst, src);
    let h = dst[0];
    out.clear();
    out.append(h.s);
}
fn pick(ws: (const u8[:])[:]) -> const u8[:] { return ws[1]; }
fn picked(out: u8[>..]&, words: (const u8[:])[:]) {
    let h = H { s: pick(words) };
    out.clear();
    out.append(h.s);
}
// emit's pair passes through mid.
fn mid(out: u8[>..]&, hs: H[:]) {
    emit(out, hs);
    print(out);
}
fn nested_fn(out: u8[>..]&, hs: H[:]) {
    fn g() {
        for h in hs { out.clear(); out.append(h.s); }
    }
    g();
}
fn run<F>() { F(); }
fn in_block(out: u8[>..]&, hs: H[:]) {
    run() {
        let h = hs[0];
        out.clear();
        out.append(h.s);
    };
}
recursive fn walk(out: u8[>..]&, hs: H[:], i: i64) {
    if i < hs.len {
        let h = hs[i];
        out.clear();
        out.append(h.s);
        walk(out, hs, i + 1);
        print(h.s);
    }
}
// Through a reference to a slice with a class of its own for the slice.
fn through_slot(out: u8[>..]&, ws: (H[:])&) {
    for h in ws { out.clear(); out.append(h.s); }
}
// Static text stored there before the copy.
fn own_static(out: u8[>..]&, hs: H[:]) {
    hs[0] = H { s: "k" };
    let h = hs[0];
    out.clear();
    out.append(h.s);
}
// A view of another array stored there: the call gives a and b apart.
fn store_other(a: u8[>..]&, b: u8[>..]&, hs: H[:]) {
    hs[0] = H { s: b[..] };
    let h = hs[0];
    a.clear();
    a.append("def");
    print(h.s, " ", a);
}
fn bytes(arr: i32[>..]&, hs: H[:]) {
    let h = hs[0];
    arr.clear();
    arr.push(7);
    print(h.s.len);
}
// The node the caller linked lies outside the array shrunk.
fn follow(ns: N[>..]&) {
    let n = ns[1];
    ns.clear();
    let nx = n.next;
    if nx { print(nx.v); }
}

fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "h0" });
    hs.push(H { s: "h1" });
    emit(buf, hs[..]);
    print(buf);
    var names: (const u8[:])[>..] = [];
    names.push("x");
    names.push("yz");
    kept(buf, names[..]);
    print(buf);
    assigned(buf, names[..]);
    print(buf);
    copied(buf, hs[..]);
    print(buf);
    var es: E[>..] = [];
    es.push(E.Txt { s: "t" });
    matched(buf, es[..]);
    print(buf);
    var h = H { s: "hh" };
    deref(buf, h);
    print(buf);
    field(buf, h);
    print(buf);
    var more: H[>..] = [];
    more.push(H { s: "m0" });
    more.push(H { s: "m1" });
    popped(buf, more);
    print(buf, " ", more.len);
    appended(buf, hs[..]);
    print(buf);
    pushed(buf, hs[..]);
    print(buf);
    dup(buf, hs[..]);
    print(buf);
    var dsts: H[>..] = [];
    dsts.push(H { s: "d" });
    copied_in(buf, dsts[..], hs[..]);
    print(buf);
    picked(buf, names[..]);
    print(buf);
    mid(buf, hs[..]);
    nested_fn(buf, hs[..]);
    print(buf);
    in_block(buf, hs[..]);
    print(buf);
    walk(buf, hs[..], 0);
    print(buf);
    var s = hs[..];
    through_slot(buf, s);
    print(buf);
    own_static(buf, hs[..]);
    print(buf);
    var other: u8[>..] = [];
    other.append("oo");
    store_other(buf, other, hs[..]);
    var arr: i32[>..] = [1, 2];
    var ints: i32[>..] = [5, 6];
    var raw: H[>..] = [];
    raw.push(H { s: bytes_of(ints[..]) });
    bytes(arr, raw[..]);
    var linked = N { v: 5, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &linked;
    follow(nodes);
    TABLE.push(H { s: "t" });
    GOUT.append("g");
    emit(GOUT, TABLE[..]);
    print(GOUT);
}
