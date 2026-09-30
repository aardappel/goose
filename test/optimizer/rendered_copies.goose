// warning: the construct's value is a copy of gs either way
// A call's by-value result, and a bare block's, if's or match's value, is a
// temporary copy where print, str or format renders it around user format
// overloads (§3.7, §9.2): an overload taking it, or a part of it, by
// reference is handed that part of the copy, and the parts after an
// overload's are read from the copy too. An inlined call reduced to the
// storage its body names, or a construct folded to its branch, must still be
// a copy there: each overload below writes that storage before it reads its
// parameter, and every line prints the same at every optimization level. A
// reference parameter binds the branch itself instead (§4.1).
struct S { x: i64 }
struct W { s: u8[:] }
struct E { k: i64 }
struct P { a: E, b: E }

var g: u8[>..] = [];
var h: u8[>..] = [];
var gs: u8[:] = h[..];
var gw = W { s: h[..] };
var ga = S { x: 1 };
var gi: i32 = 1;
var gp = P { a: E { k: 1 }, b: E { k: 2 } };

// Each overload overwrites the storage what it renders may be folded to.
fn format(out: u8[>..]&, p: u8[:]&) {
    gs = g[1..];
    gw.s = g[1..];
    format(out, p.len);
}
fn format(out: u8[>..]&, v: S&) {
    ga.x = 5;
    format(out, v.x);
}
fn format(out: u8[>..]&, v: i32&) {
    gi = 5;
    format(out, v as i64);
}
fn format(out: u8[>..]&, e: E) {
    gp.b.k = 9;
    format(out, e.k);
}

fn reset() {
    gs = h[..];
    gw.s = h[..];
    ga.x = 1;
    gi = 1;
    gp.b.k = 2;
}

fn pick(n: i64) -> bool { return n > 2; }
fn next(n: i64) -> i64 { return n + 1; }
fn get() -> u8[:] { gs }
fn get_s() -> S { ga }
fn get_i() -> i32 { gi }
fn get_p() -> P { gp }
fn set(p: u8[:]&) { p = g[1..]; }

fn main() {
    g.append("xyz");
    h.append("abcd");
    reset();
    print(if pick(3) { gs } else { return; });
    reset();
    print(match next(3) { 4 => gs, _ => return });
    reset();
    print({ gs });
    reset();
    print(if true { gs } else { h[..] });
    reset();
    print(if pick(3) { &gs } else { return; });
    reset();
    print(get());
    reset();
    get().print();
    reset();
    print(if pick(3) { gw } else { return; });
    reset();
    print((if pick(3) { gw } else { return; }).s);
    reset();
    print(if pick(3) { ga } else { return; });
    reset();
    print(get_s());
    reset();
    print(get_i());
    reset();
    print(if pick(3) { gp } else { return; });
    reset();
    print(get_p());
    reset();
    print(str(if pick(3) { gs } else { return; }));
    reset();
    var out: u8[>..] = [];
    out.format(if pick(3) { gs } else { return; }, " ");
    reset();
    format(out, get());
    print(out);
    reset();
    var s = h[..];
    set(if pick(3) { s } else { return; });
    print(s.len);
}
