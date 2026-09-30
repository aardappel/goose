// What a holder's references lead to through a parameter's class -- the
// elements of the caller's array a holder parameter's slice views, the
// caller's holder it refers to, and the same kept in a local holder --
// holds what the activation stored there and what its callers put there:
// a shrink in the callee leaves the second to each call, which judges it
// by its record of the argument's storage (§5.1). Here none of them views
// the array shrunk.
struct P { words: (const u8[:])[:] }
struct S { r: const u8[:] }
struct Q { s: S& }
struct H { s: const u8[:] }
struct PS { q: S& }
struct PH { hs: H[:] }
struct QP { p: P }
var TABLE: (const u8[:])[>..] = [];

// The holder parameter's words view the caller's names.
fn emit(out: u8[>..]&, p: P) {
    out.clear();
    for w in p.words { out.append(w); }
}
// A local holder keeps the slice parameter.
fn keep(out: u8[>..]&, words: (const u8[:])[:]) {
    let h = P { words: words };
    out.clear();
    for w in h.words { out.append(w); }
}
// A copy of the holder parameter.
fn copied(out: u8[>..]&, p: P) {
    let h = p;
    out.clear();
    out.append(h.words[0]);
}
// emit's pair passes through mid to its callers.
fn mid(out: u8[>..]&, p: P) {
    emit(out, p);
    print(out, " ", p.words[0]);
}
// g clears f's out and reads f's holder parameter.
fn nested(out: u8[>..]&, p: P) {
    fn g() {
        out.clear();
        out.append(p.words[1]);
    }
    g();
}
// A local holder refers to the holder q names.
fn keep_ref(out: u8[>..]&, q: S&) {
    let h = Q { s: q };
    out.clear();
    out.append(h.s.r);
}
// The holder parameter refers to a holder of the caller's.
fn emit_ref(out: u8[>..]&, p: PS) {
    out.clear();
    out.append(p.q.r);
}
// The holder parameter views holders of the caller's.
fn emit_holders(out: u8[>..]&, p: PH) {
    out.clear();
    for x in p.hs { out.append(x.s); }
}
// A local holder keeps the slice in ws's slot, which views names.
fn keep_view(out: u8[>..]&, ws: ((const u8[:])[:])&) {
    let h = P { words: ws };
    out.clear();
    out.append(h.words[1]);
}
// A view of b stored into the elements a local holder's words view: the
// call gives a and b apart.
fn store_other(a: u8[>..]&, b: u8[>..]&, words: (const u8[:])[:]) {
    let h = P { words: words };
    words[0] = b[..];
    a.clear();
    a.append("def");
    print(h.words[0], " ", a);
}
// Every level keeps v in a holder of its own.
recursive fn walk(a: u8[>..]&, v: (const u8[:])[:], n: i64) {
    let h = P { words: v };
    if n == 0 {
        a.clear();
        a.append("jkl");
        return;
    }
    walk(a, v, n - 1);
    print(h.words[0], " ", n);
}
// emit_q leaves what q's holder holds to through_ref, where hq keeps
// through_ref's words.
fn emit_q(out: u8[>..]&, q: QP&) {
    out.clear();
    out.append(q.p.words[1]);
}
fn through_ref(out: u8[>..]&, words: (const u8[:])[:]) {
    var hq = QP { p: P { words: words } };
    emit_q(out, hq);
}

fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    names.push("y");
    emit(buf, P { words: names[..] });
    print(buf);
    keep(buf, names[..]);
    print(buf);
    copied(buf, P { words: names[..] });
    print(buf);
    mid(buf, P { words: names[..] });
    nested(buf, P { words: names[..] });
    print(buf);
    var ss = S { r: "s" };
    keep_ref(buf, ss);
    print(buf);
    emit_ref(buf, PS { q: ss });
    print(buf);
    var other: u8[>..] = [];
    other.append("zz");
    var hl: H[>..] = [];
    hl.push(H { s: other[..] });
    emit_holders(buf, PH { hs: hl[..] });
    print(buf);
    var s = names[..];
    keep_view(buf, s);
    print(buf);
    store_other(buf, other, names[..]);
    walk(buf, names[..], 2);
    print(buf);
    through_ref(buf, names[..]);
    print(buf);
    let pp = P { words: names[..] };
    emit(buf, pp);
    print(buf);
    TABLE.push("t");
    emit(buf, P { words: TABLE[..] });
    print(buf);
}
