// `const` is shallow (§9.5): a reference or slice read out of a field or an
// element is as writable as the slot's type says, whatever the path to the
// slot, so a path going on through it writes what a load of it could -- out
// of a const local or global, a const array, a call's result, through a
// `const H&` parameter and a read-only instantiation, by a `for` binding and
// in a rendered field -- with =, +=, ++, & and by-reference and slice
// arguments, into fields, elements and a slice's elements.
struct S { n: i64, a: i64[3] }
struct H { r: S& }
struct HA { r: i64[3]& }
struct HS { s: i64[:] }
struct T { n: i64 }
struct HT { r: T& }

var gx = S { n: 0, a: [0, 0, 0] };
const gh = H { r: gx };

fn inc(p: i64&) { p = p + 1; }
fn poke(s: i64[:]) { s[0] = 9; }
fn mk(x: S&) -> H { return H { r: x }; }

// Rendering a T counts it up.
fn format(out: u8[>..]&, v: T&) {
    v.n += 1;
    format(out, v.n);
}

fn through_const(h: const H&) {
    h.r.n = 6;
    let p = &h.r.a[0];
    inc(p);
}

fn through_readonly(h: H&) {
    h.r.n += 2;
    h.r.a[1]++;
}

fn fill(hs: const HS&) { hs.s[2] = 4; }

// A const local's reference field, a reference to an array and a slice.
fn fields() {
    var x = S { n: 0, a: [0, 0, 0] };
    const h = H { r: x };
    h.r.n = 7;
    h.r.n += 1;
    h.r.n++;
    let m = &h.r.n;
    inc(m);
    inc(h.r.n);
    h.r.a[0] = 5;
    h.r.a[1] += 2;
    inc(h.r.a[2]);
    let q = &h.r.a;
    q[2] += 5;
    poke(h.r.a[1..]);
    print(x.n, " ", x.a);
    const ha = HA { r: x.a };
    ha.r[0] = 1;
    inc(ha.r[1]);
    let e = &ha.r[2];
    e += 1;
    poke(ha.r[1..]);
    print(x.a);
    const hs = HS { s: x.a[..] };
    hs.s[0] = 2;
    inc(hs.s[1]);
    let f = &hs.s[2];
    inc(f);
    poke(hs.s[2..]);
    fill(hs);
    print(x.a);
}

// Elements of const arrays, by index and by `for`, and a call's result.
fn elements() {
    var x = S { n: 0, a: [0, 0, 0] };
    const hh: H[2] = [H { r: x }, H { r: x }];
    hh[1].r.n = 20;
    for b in hh {
        let m = &b.r.n;
        inc(m);
    }
    const rs: (S&)[2] = [x, x];
    rs[0].n += 1;
    inc(rs[1].n);
    for r in rs { inc(r.n); }
    for &r in rs { r.a[0]++; }
    const ss: (i64[:])[2] = [x.a[1..], x.a[2..]];
    for s in ss { poke(s); }
    mk(x).r.a[0] += 3;
    print(x.n, " ", x.a);
}

// A const global, the parameters, and a rendered field.
fn others() {
    gh.r.n = 3;
    inc(gh.r.a[1]);
    print(gx.n, " ", gx.a);
    var x = S { n: 0, a: [0, 0, 0] };
    const h = H { r: x };
    through_const(h);
    through_readonly(h);
    print(x.n, " ", x.a);
    var t = T { n: 1 };
    const ht = HT { r: t };
    print(ht);
    print(t.n);
}

fn main() {
    fields();
    elements();
    others();
}
