// A multi-value binding of a call's reference results (§4.1): a reference
// to a fixed-size value binds a copy of the value, as a single binding's
// does, whichever result carries it -- the one C returns or one an
// out-parameter takes -- and whatever makes the call: a function, case
// functions dispatched on a tag (§8.2), a long-distance return forwarding
// its callee's results (§7.9); in a body inlined at -O2 too. The pointees
// are an integer, a slice, a struct and varint elements. `.=` binds the
// references themselves, as does a reference to a non-fixed value, one
// generic function doing either by its type argument: writes through them
// reach the originals, while the copies keep the values they were bound to.
struct P { a: i64, b: i64 }
struct Named { id: i64, tags: i64[] }
enum Pick { First { k: i64 }, Second { k: i64 } }

var g: u8[>..] = [];
var gx = 1;
var gy = 2;

fn both(p: i64&, q: i64&) -> i64&, i64& { return q, p; }
fn one(p: u8[:]&, q: u8[:]&) -> u8[:]&, i64 { return q, 5; }
fn points(p: P&, q: P&) -> P&, P& { return q, p; }
fn ends(xs: varint[:], ys: varint[:]) -> varint&, i64, varint& {
    for x in xs { for y in ys { return x, xs.len, y; } }
    abort("empty");
}
// The first result is bytes, so the second is the one C returns.
fn after(s: u8[], p: i64&, q: i64&) -> u8[], i64&, i64& { return s, p, q; }
fn pick(c: Pick.First, p: i64&, q: i64&) -> i64&, i64& { return p, q; }
fn pick(c: Pick.Second, p: i64&, q: i64&) -> i64&, i64& { return q, p; }
fn arrays(a: i64[>..]&, b: i64[>..]&) -> i64[>..]&, i64[>..]& { return b, a; }
fn records(a: Named&, b: Named&) -> Named&, Named& { return b, a; }
fn swapped<T>(a: T&, b: T&) -> T&, T& { return b, a; }
fn weigh(p: i64&, q: i64&) -> i64 {
    let e, k = both(p, q);
    return e * 10 + k;
}

// Never inlined into outer, so the return stays long-distance at -O2 too.
recursive fn leave(k: i64) {
    if k > 0 { leave(k - 1); }
    return both(gx, gy) from outer;
}
fn outer() -> i64, i64 {
    leave(1);
    return 0, 0;
}

fn main() {
    var x = 1;
    var y = 2;
    let e, k = both(x, y);
    print(e, " ", k);                                   // 2 1

    g.append("abcd");
    var sx = g[0..2];
    var sy = g[2..4];
    let s, n = one(sx, sy);
    print(s, " ", n);                                   // cd 5

    var p = P { 1, 2 };
    var q = P { 3, 4 };
    var pc, qc = points(p, q);
    p.a = 10;
    pc.b = 40;
    x = 5;
    sy = g[0..1];
    print(pc, " ", qc, " ", p, " ", q);                 // P { 3, 40 } P { 1, 2 } P { 10, 2 } P { 3, 4 }
    print(e, " ", k, " ", s);                           // 2 1 cd

    let xv: varint[] = [300, 5];
    let yv: varint[] = [-7000];
    let n1, w, n2 = ends(xv, yv);
    print(n1, " ", w, " ", n2);                         // 300 2 -7000

    let t, a, b = after("hey", x, y);
    x = 6;
    print(t, " ", a, " ", b);                           // hey 5 2

    let c1: Pick = Pick.Second { k: 1 };
    let c2: Pick = Pick.First { k: 2 };
    let d1, d2 = pick(c1, x, y);
    let d3, d4 = pick(c2, x, y);
    print(d1, " ", d2, " ", d3, " ", d4);               // 2 6 6 2

    let o1, o2 = outer();
    print(o1, " ", o2);                                 // 2 1

    let re, rk .= both(x, y);
    re = 20;
    rk = 60;
    print(x, " ", y, " ", re, " ", rk);                 // 60 20 20 60
    let rs, rn .= one(sx, sy);
    sy = g[1..3];
    print(rs, " ", rn);                                 // bc 5
    let g1, g2 = swapped(x, y);
    x = 3;
    print(g1, " ", g2, " ", weigh(x, y));               // 20 60 203

    var xs: i64[>..] = [1];
    var ys: i64[>..] = [2];
    let ra, rb = arrays(xs, ys);
    ra.push(20);
    let ra2, rb2 .= arrays(xs, ys);
    rb2.push(10);
    print(xs, " ", ys, " ", rb.len, " ", ra2.len);      // [1, 10] [2, 20] 2 2
    let h1, h2 = swapped(xs, ys);
    h1.push(30);
    print(ys, " ", h2.len);                             // [2, 20, 30] 2
    var m1 = Named { 1, [1] };
    var m2 = Named { 2, [2, 3] };
    let r1, r2 = records(m1, m2);
    r1.id = 7;
    print(m2.id, " ", r2.tags);                         // 7 [1]
}
