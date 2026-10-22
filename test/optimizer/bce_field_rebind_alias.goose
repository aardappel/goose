// Rebinding a reference a field holds redirects every place read through
// that field, under whichever name it is read: `p.r` is `h.r` where
// `p = &h`.
import std;

struct H { n: i64, r: i64[>..]& }

fn through_ref(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var h = H { n: 0, r: big };
    let p = &h;
    var t = 0;
    if h.r.len > 3 {
        t = h.r[3];                             // bce:elide
        p.r .= to;
        t += h.r[3];                            // bce:keep (h.r is to now)
    }
    return t;
}

fn through_var(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var h = H { n: 0, r: big };
    let p = &h;
    var t = 0;
    if p.r.len > 3 {
        t = p.r[3];                             // bce:elide
        h.r .= to;
        t += p.r[3];                            // bce:keep (p.r is to now)
    }
    return t;
}

// The field a reference to a field holds.
struct O { m: i64, h: H }

fn nested(to: i64[>..]&) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var o = O { m: 0, h: H { n: 0, r: big } };
    let q = &o.h;
    var t = 0;
    if q.r.len > 3 {
        o.h.r .= to;
        t = q.r[3];                             // bce:keep (q.r is to now)
    }
    o.h.r .= big;
    if o.h.r.len > 3 {
        q.r .= to;
        t += o.h.r[3];                          // bce:keep (o.h.r is to now)
    }
    return t;
}

fn main() {
    var long: i64[>..] = [10, 20, 30, 40];
    var short: i64[>..] = [9];
    print(through_ref(long), " ", through_var(long), " ", nested(long));
    print(through_ref(short));
}
