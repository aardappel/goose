// A slice whose elements hold references, and a reference to a holder, lead
// on to what those references point into (§5.1): for a holder the function
// names exactly, where the holder's store record says, in the scans and in
// the pairs the callers judge; for a `var`, anything that outlives the
// holder; for a global holder, what the judgement of the globals says. A
// shrink of anything else leaves them in use.
struct B { s: u8[:] }
struct C { s: const u8[:] }
var NAMES: (const u8[:])[>..] = [];
var NC: C = C { s: "q" };
var OUT: u8[>..] = [];

// xs and bs view b, not a: a and b are told apart at the call.
fn other(a: u8[>..]&, b: u8[>..]&) {
    var xs: (u8[:])[>..] = [];
    xs.push(b[..]);
    let v = xs[..];
    var bs: B[>..] = [];
    bs.push(B { s: b[..] });
    let r = &bs[0];
    a.clear();
    a.append("def");
    print(v[0], " ", r.s, " ", a);
}

// NAMES holds static text only, which every global shrink judges.
fn emit(out: u8[>..]&) {
    let ns = NAMES[..];
    out.clear();
    for n in ns { out.append(n); }
}

fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    other(a, b);
    // ss outlives c, so nothing it views can be in c.
    var ss: (u8[:])[>..] = [];
    ss.push(b[0..2]);
    var v = ss[..];
    {
        var c: u8[>..] = [];
        c.append("pq");
        c.clear();
        c.append("rs");
        print(v[0], " ", c);
    }
    NAMES.push("x");
    NAMES.push("y");
    emit(OUT);
    print(OUT);
    let ns = NAMES[..];
    let rc = &NC;
    OUT.clear();
    OUT.append(ns[1]);
    OUT.append(rc.s);
    print(OUT);
}
