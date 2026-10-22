// A reference to an array element reads through the references the element
// holds, which rebinding one, storing a value that holds one, or a callee
// handed the elements doing either, redirects.
import std;

struct H { n: i64, r: i64[>..]& }

recursive fn store_first(xs: H[:], to: i64[>..]&) { xs[0] = H { n: 2, r: to }; }
recursive fn rebind_first(xs: H[:], to: i64[>..]&) { xs[0].r .= to; }

fn rebind(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var hs = [H { n: 0, r: big }, H { n: 1, r: big }];
    let e = &hs[0];
    var t = 0;
    if e.r.len > 3 {
        t = e.r[3];                             // bce:elide
        hs[0].r .= to;
        t += e.r[3];                            // bce:keep (e.r is to now)
    }
    return t;
}

fn store(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var hs = [H { n: 0, r: big }, H { n: 1, r: big }];
    let e = &hs[0];
    var t = 0;
    if e.r.len > 3 {
        t = e.r[3];                             // bce:elide
        hs[0] = H { n: 2, r: to };
        t += e.r[3];                            // bce:keep (e.r is to now)
    }
    return t;
}

fn through_binder(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var hs = [H { n: 0, r: big }, H { n: 1, r: big }];
    let e = &hs[0];
    var t = 0;
    if e.r.len > 3 {
        for &x in hs { x.r .= to; }
        t = e.r[3];                             // bce:keep (e.r is to now)
    }
    return t;
}

fn in_callees(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var hs = [H { n: 0, r: big }, H { n: 1, r: big }];
    let e = &hs[0];
    let s = hs[..];
    var t = 0;
    if e.r.len > 3 {
        store_first(s, to);
        t = e.r[3];                             // bce:keep (store_first made e.r to)
    }
    hs[0].r .= big;
    if e.r.len > 3 {
        rebind_first(s, to);
        t += e.r[3];                            // bce:keep (rebind_first made e.r to)
    }
    return t;
}

// A loop over what an element's reference names reads it again after the
// body rebinds the reference: 1, then b[1].
fn loop_view() -> i64 {
    var a: i64[>..] = [1, 2, 3, 4, 5];
    var b: i64[>..] = [10, 20];
    var hs = [H { n: 0, r: a }, H { n: 1, r: a }];
    let e = &hs[0];
    var t = 0;
    for x in e.r {
        t += x;
        hs[0].r .= b;
    }
    return t;
}

fn main() {
    var long: i64[>..] = [10, 20, 30, 40];
    var short: i64[>..] = [9];
    print(rebind(long), " ", store(long), " ", through_binder(long), " ", in_callees(long),
          " ", loop_view());
    print(rebind(short));
}
