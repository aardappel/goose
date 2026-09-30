// A user format overload taking a value by reference (§3.7) is handed what a
// reference argument names, as writable as the reference is (§9.5), as a
// reference parameter is (§4.1): through a reference parameter, a local
// reference, one rebound, a call's reference result, a reference read out of
// a field -- of a `const` struct too, `const` being shallow -- or out of an
// element, a narrowed optional and `for` binders, by print, str and format;
// the elements and fields of an array or a struct behind a reference, a
// slice field it re-points, a self-relative link it follows, and a scalar.
// Each write lands in the storage the reference names at every optimization
// level, the functions inlined or not.
struct S { x: i64 }
struct H { r: S& }
struct O { s: S, n: i64 }
struct Q { f: u8[:] }
struct Cell { v: i64 }
struct Inner { link: Cell&<u16>? }
struct Nest { cells: Cell[..4], inner: Inner }

var gs = S { x: 0 };
var buf: u8[>..] = [];

// Counts the value up and renders the new count.
fn format(out: u8[>..]&, v: S&) {
    v.x += 1;
    out.append("S");
    format(out, v.x);
}
// Renders the first byte and consumes it.
fn format(out: u8[>..]&, p: u8[:]&) {
    if p.len > 0 {
        out.push(p[0]);
        p = p[1..];
    }
}
fn format(out: u8[>..]&, v: Cell&) {
    v.v *= 10;
    format(out, v.v);
}
fn format(out: u8[>..]&, v: u16&) {
    v += 2;
    format(out, "u", v as i64);
}

fn viaparam(r: S&) { print(r); }
fn viastr(r: S&) -> u8[>..] { return str("<", r, ">"); }
fn viaformat(r: S&) {
    var o: u8[>..] = [];
    format(o, "[", r, "]");
    print(o);
}
fn get_s() -> S& { gs }
fn arrays(ra: S[2]&) { print(ra); }
fn outer(ro: O&) { print(ro); }
fn slices(rq: Q&) { print(rq, " ", rq); }
fn links(rn: Nest&) { print(rn); }
fn scalar(ru: u16&) { print(ru); }

fn main() {
    var a = S { x: 0 };
    viaparam(a);
    viaparam(a);
    print(a.x);
    print(viastr(a), " ", a.x);
    viaformat(a);
    print(a.x);

    let lr .= a;
    print(lr);
    var b = S { x: 50 };
    var vr .= a;
    print(vr);
    vr .= b;
    print(vr);
    print(a.x, " ", b.x);
    print(get_s(), " ", get_s());
    print(gs.x);

    var h = H { r: a };
    print(h.r);
    const ch = H { r: a };
    print(ch.r);
    print(a.x);
    var rs: (S&)[2] = [a, gs];
    print(rs[1]);
    print(gs.x);
    var o: S? = a;
    if o { print(o); }
    print(a.x);

    var sa: S[2] = [S { x: 10 }, S { x: 20 }];
    for &e in sa { print(e); }
    print(sa[0].x, " ", sa[1].x);
    for r in rs { print(r); }
    print(a.x, " ", gs.x);

    arrays(sa);
    print(sa[0].x, " ", sa[1].x);
    var ob = O { s: S { x: 100 }, n: 1 };
    outer(ob);
    print(ob.s.x);

    buf.append("abc");
    var q = Q { f: buf[..] };
    slices(q);
    print(q.f.len);
    var t = Nest { cells: [], inner: Inner { link: null } };
    t.cells.push(Cell { v: 3 });
    t.inner.link .= t.cells[0];
    links(t);
    print(t.cells[0].v);
    var n: u16 = 7;
    scalar(n);
    print(n as i64);
}
