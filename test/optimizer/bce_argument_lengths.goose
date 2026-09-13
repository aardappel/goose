// runtime-debug: a length passed to a callee is the one its argument had.
import std;
recursive fn first(s: i64[:], k: i64) -> i64 {
    return s[1] + k; // bce:keep
}
fn main() {
    var a: i64[4] = [10, 20, 30, 40];
    var n = 1;
    // a[0..1] has length 1 when it is passed, before n changes.
    print(first(a[0..n], { n += 1; 0 }));
}
