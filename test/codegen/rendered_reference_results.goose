// A call's reference result that print, str or format renders is read where
// the reference points, as a reference variable is (§3.7, §3.8): a user
// format overload taking the value, or a part of it, by reference is handed
// the storage the reference names, and the parts after an overload's are
// read from there too, from the value the rendering began with. Each
// overload below changes that storage, through its parameter, or before it
// reads its parameter or the next part is read, and every line prints the
// same at every optimization level, the calls inlined or not: results
// returned on one path or several, passed through or forwarded from another
// call, a case function's, a function value's naming a function and push's
// element, rendered by print, as a receiver, by str and by format into a
// resizable and a limited array; a struct's, a const struct's, one holding
// the next part, a resizable array's, and a slice an overload for its
// elements re-points.
struct S { x: i64 }
struct E { k: i64 }
struct P { a: E, b: E }
struct T { x: i64 }
enum K { A { n: i64 }, B { m: i64 } }

var g: u8[>..] = [];
var h: u8[>..] = [];
var gs: u8[:] = h[..];
var gs2: u8[:] = h[..];
var ga = S { x: 1 };
let gc = S { x: 4 };
var gp = P { a: E { k: 1 }, b: E { k: 2 } };
var nums: i32[>..] = [];
var views: u8[:][>..] = [];
var tbig: T[>..] = [];
var tsmall: T[>..] = [];
var ts: T[:] = tbig[..];

// Renders the length, then re-points the slice it is handed.
fn format(out: u8[>..]&, p: u8[:]&) {
    format(out, p.len);
    p = g[1..];
}
// Writes the struct, then reads it through its parameter.
fn format(out: u8[>..]&, v: S&) {
    ga.x = 5;
    format(out, v.x);
}
// Writes the struct holding the next part.
fn format(out: u8[>..]&, e: E) {
    gp.b.k = 9;
    format(out, e.k);
}
// Grows the array, then reads its length through its parameter.
fn format(out: u8[>..]&, v: i32[>..]&) {
    nums.push(3);
    format(out, v.len);
}
// Re-points the slice holding the element it renders.
fn format(out: u8[>..]&, t: T) {
    ts = tsmall[..];
    format(out, t.x);
}

fn get() -> u8[:]& { gs }
fn id(r: u8[:]&) -> u8[:]& { r }
fn pick(c: bool) -> u8[:]& {
    if c { return gs; }
    return gs2;
}
fn later() -> u8[:]& { pick(false) }
fn which(a: K.A) -> u8[:]& { gs }
fn which(b: K.B) -> u8[:]& { gs2 }
fn get_s() -> S& { ga }
fn get_c() -> const S& { gc }
fn get_p() -> P& { gp }
fn get_nums() -> i32[>..]& { nums }
fn get_ts() -> T[:]& { ts }
fn apply<F>() { print(F()); }

fn reset() {
    gs = h[..];
    gs2 = h[..];
    ga.x = 1;
    gp.b.k = 2;
}

fn main() {
    g.append("xyz");
    h.append("abcd");
    reset();
    print(get());
    print(gs.len);
    reset();
    print(id(gs));
    print(gs.len);
    reset();
    print(pick(true), " ", pick(false));
    print(gs.len, " ", gs2.len);
    reset();
    print(later());
    print(gs.len, " ", gs2.len);
    reset();
    let k: K = K.B { m: 1 };
    print(which(k));
    print(gs.len, " ", gs2.len);
    reset();
    apply(get);
    print(gs.len);
    reset();
    get().print();
    print(gs.len);
    reset();
    print(str(get()), " ", gs.len);
    reset();
    var out: u8[>..] = [];
    format(out, id(gs), " ");
    print(out, gs.len);
    reset();
    var lim: u8[..8] = [];
    format(lim, get(), " ");
    print(lim, gs.len);
    reset();
    views.push(h[..]);
    print(views.push(h[..]));
    print(views[0].len, " ", views[1].len);
    reset();
    print(get_s());
    print(get_c());
    print(get_p());
    nums.push(1);
    print(get_nums());
    tbig.push(T { x: 1 });
    tbig.push(T { x: 2 });
    tbig.push(T { x: 3 });
    tsmall.push(T { x: 9 });
    ts = tbig[..];
    print(get_ts());
    print(ts.len);
}
