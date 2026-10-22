// A mask bounds `x & m` to [0, m] only where m's term is the value the
// machine computed: `n + 1` for n at i64's top wraps to i64's bottom in a
// release build, and that negative mask keeps x's sign bit. So the division
// of the masked value stays signed, and an index by it stays checked.
import std;

fn quot(x: i64, n: i64) -> i64 {
    if n < 0 { return 0; }
    return (x & (n + 1)) / 3;
}

fn pick(a: i64[:], x: i64, n: i64) -> i64 {
    if n < 0 { return 0; }
    let m = x & (n + 1);
    if m < a.len { return a[m]; }   // bce:keep
    return -1;
}

fn main() {
    var ks: i64[>..] = [-1, 9223372036854775807, 6, 1];
    let a: i64[] = [10, 20, 30];
    print(quot(ks[0], ks[1]), " ", quot(ks[2], ks[3]));
    print(pick(a[..], ks[2], ks[3]));
}
