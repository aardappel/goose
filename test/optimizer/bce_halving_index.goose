// Halving indices: a quotient or right shift by a constant bounds a
// nonnegative dividend from below and above, and a variable whose every
// assignment provably lowers it stays below its starting value across a
// loop, which is how a heap's sift-up index is proven.
import std;

fn quotients(xs: i64[:]) -> i64 {
    var s = 0;
    for i in xs.len {
        s += xs[i / 2];                            // bce:elide
        s += xs[i >> 1];                           // bce:elide
        s += xs[i >> 65];                          // bce:elide (the count is masked to 1)
        if i > 0 {
            s += xs[(i - 1) / 2];                  // bce:elide
            s += xs[(i + 1) / 2];                  // bce:elide (below i + 1 once i + 1 >= 1)
        }
        s += xs[i / 2 + xs.len / 2];               // bce:keep (in range, but not as a difference)
        if i >= 3 {
            s += xs[(i - 5) / 3];                  // bce:keep (the dividend may be negative)
        }
    }
    return s;
}

fn narrow(b: u8, t: i64[128]) -> i64 {
    return t[b / 2] + t[b >> 1];                   // bce:elide (u8 / 2 is at most 127)
}

// The sift-up of std's heap_push: i only ever moves to its parent.
fn sift_up(xs: i64[>..<]&, v: i64) {
    xs.push(v);
    var i = xs.len - 1;
    let x = xs[i];                                 // bce:elide
    while i > 0 {
        let p = (i - 1) / 2;
        if !(x < xs[p]) { break; }                 // bce:elide
        xs[i] = xs[p];                             // bce:elide
        i = p;
    }
    xs[i] = x;                                     // bce:elide
}

// A plain assignment that may raise the variable makes it non-monotone.
fn not_monotone(xs: i64[:], k: i64) -> i64 {
    var s = 0;
    var i = xs.len - 1;
    while i > 0 {
        let p = (i - 1) / 2;
        s += xs[p];                                // bce:keep (i may have been raised)
        if k > 3 { i = p + k; } else { i = p; }
        if s > 1000 { break; }
    }
    return s;
}

fn main() {
    var a: i64[>..] = [];
    for i in 9 { a.push(i * 10); }
    // Values read from elements, so the call sites state nothing about them.
    let k = a[2] / 10;
    print(quotients(a[k..]));
    var t: i64[128] = [0; 128];
    t[100] = 7;
    print(narrow(201, t));
    var h: i64[>..<] = [];
    for v in [5, 3, 8, 1, 9, 2, 7] { sift_up(h, v); }
    print(h);
    print(not_monotone(a, a[0]));
}
