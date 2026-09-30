// The elements a slice parameter views, and the holder a reference
// parameter names, hold what the caller stored there: a shrink in the
// callee leaves them to each call, which judges them by its record of the
// argument's storage (§5.1). Here none of them views the array shrunk.
struct H { s: const u8[:] }
var TABLE: (const u8[:])[>..] = [];

fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    out.clear();
    for w in words { out.append(w); }
}
fn emit_ref(out: u8[>..]&, words: (const u8[:])[>..]&) {
    out.clear();
    for w in words { out.append(w); }
}
// emit's pair passes through mid to its callers.
fn mid(out: u8[>..]&, words: (const u8[:])[:]) {
    emit(out, words);
    print(words[0]);
}
// A view of b stored into words' elements: the call gives a and b apart.
fn store_other(a: u8[>..]&, b: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = b[..];
    a.clear();
    a.append("def");
    print(words[0], " ", a);
}
fn holder(a: u8[>..]&, h: H&) {
    a.clear();
    a.append("ghi");
    print(h.s, " ", a);
}
fn mk(a: u8[>..]&) -> (const u8[:])[] { [a[..], "q"] }
// The slice in ws's slot views names.
fn through_slot(out: u8[>..]&, ws: ((const u8[:])[:])&) {
    out.clear();
    for w in ws { out.append(w); }
}
fn emit_holders(out: u8[>..]&, hs: H[:]) {
    out.clear();
    for x in hs { out.append(x.s); }
}
// words' elements view arrays whose elements hold text.
fn emit_nested(out: u8[>..]&, words: ((const u8[:])[:])[:]) {
    out.clear();
    for ws in words { for w in ws { out.append(w); } }
}
fn clr(a: u8[>..]&) -> i64 {
    a.clear();
    a.append("mno");
    return 0;
}
fn show(v: (const u8[:])[:], n: i64) { print(v[0], " ", n); }
fn held(out: u8[>..]&, words: (const u8[:])[:]) {
    show(words, clr(out));
}
fn nested(out: u8[>..]&, words: (const u8[:])[:]) {
    fn g() {
        out.clear();
        out.append(words[1]);
    }
    g();
}
recursive fn walk(a: u8[>..]&, v: (const u8[:])[:], w: (const u8[:])[:], n: i64) {
    if n == 0 {
        a.clear();
        a.append("jkl");
        return;
    }
    walk(a, w, v, n - 1);
    print(v[0]);
}

fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    names.push("y");
    emit(buf, names[..]);
    print(buf);
    emit(buf, ["p", "q"]);
    print(buf);
    emit_ref(buf, names);
    print(buf);
    mid(buf, names[..]);
    print(buf);
    var other: u8[>..] = [];
    other.append("zz");
    emit(buf, mk(other));
    print(buf);
    store_other(buf, other, names[..]);
    var h = H { s: other[..] };
    holder(buf, h);
    var s = names[..];
    through_slot(buf, s);
    print(buf);
    held(buf, names[..]);
    nested(buf, names[..]);
    print(buf);
    var hl: H[>..] = [];
    hl.push(H { s: other[..] });
    emit_holders(buf, hl[..]);
    print(buf);
    var outer: ((const u8[:])[:])[>..] = [];
    outer.push(names[..]);
    emit_nested(buf, outer[..]);
    print(buf);
    TABLE.push("t");
    emit(buf, TABLE[..]);
    print(buf);
    var tt: (const u8[:])[>..] = [];
    tt.push("r");
    walk(buf, names[..], tt[..], 2);
    print(buf);
}
