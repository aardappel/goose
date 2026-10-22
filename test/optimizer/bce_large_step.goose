// `v += L` with a literal too large to shift facts by is `v = v + L`, which
// gives v no direction: two steps of 2^62 from 0 wrap to i64's bottom in a
// release build, where a bound from v's start would let it past the check.
import std;

fn probe(xs: i64[:], k: i64) -> i64 {
    var t = 0;
    var v = 0;
    var n = k;
    while n > 0 {
        v += 4611686018427387904;
        if v < xs.len {
            t += xs[v];          // bce:keep
        }
        n -= 1;
    }
    return t;
}

fn main() {
    var a: i64[>..] = [1, 2, 3];
    print(probe(a[..], 1));
    print(probe(a[..], 2));
}
