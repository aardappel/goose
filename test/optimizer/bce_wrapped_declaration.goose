// A declaration sets a variable's `v <= len(P) + c` invariant only from a
// value that cannot have wrapped: `x - 5` for an x just above i64's bottom
// wraps to near its top in a release build, which the length's bound on x
// says nothing about.
import std;

fn probe(xs: i64[:], x: i64) -> i64 {
    if x >= xs.len { return 0; }
    var v = x - 5;
    var t = 0;
    if v >= 0 {
        t = xs[v];              // bce:keep
    }
    return t;
}

fn main() {
    var a: i64[>..] = [1, 2, 3];
    var ks: i64[>..] = [2, -9223372036854775807];
    print(probe(a[..], ks[0]));
    print(probe(a[..], ks[1]));
}
