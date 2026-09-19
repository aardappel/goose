// An operation computes and wraps at its operands' type (§6.2), whatever
// wider slot its value lands in: an i8 `k + 1` bounds nothing about k = 127,
// where a release build computes -128.
import std;

// A sum stored into an i64.
fn next(a: i64[:], k: i8) -> i64 {
    if k >= 0 {
        let j: i64 = k + 1;
        if j < a.len { return a[j]; } // bce:keep
    }
    -1
}

// Intervals at i8: 100 + 100 and 100 * 2 leave its range.
fn table(k: i8, m: i8) -> i64 {
    var a: i64[256] = [7; 256];
    var r: i64 = 0;
    if k >= 0 && k <= 100 && m >= 0 && m <= 100 {
        let s: i64 = k + m;
        r += a[s]; // bce:keep
        let p: i64 = k * 2;
        r += a[p]; // bce:keep
        let q: i64 = (k as i64) * 2;
        r += a[q]; // bce:elide
    }
    r
}

fn main() {
    var a: i64[>..] = [1, 2, 3];
    var ks: i8[>..] = [10, 20, 127];
    print(table(ks[0], ks[1]));
    print(next(a, ks[2]));
}
