// A check that completes proves its condition from then on: the index was
// in range, a slice's bounds were ordered and inside the length. Only on
// the paths the check ran on.
import std;

fn after_index(xs: i64[:], k: i64) -> i64 {
    let a = xs[k];             // bce:keep
    return a + xs[k];          // bce:elide (the check above passed)
}

fn after_slice(xs: i64[:], lo: i64, hi: i64) -> i64 {
    let s = xs[lo..hi];        // bce:keep
    var t = s.len;
    for i in lo..hi { t += xs[i]; } // bce:elide
    return t;
}

// The checked value is the sum as computed, so after `xs[k + 1]` the sum is
// in range, but k itself may be -1.
fn after_offset(xs: i64[:], k: i64) -> i64 {
    let a = xs[k + 1];         // bce:keep
    let b = xs[k + 1];         // bce:elide
    let c = xs[k];             // bce:keep
    return a + b + c;
}

fn branch_only(xs: i64[:], k: i64, c: bool) -> i64 {
    var t = 0;
    if c { t = xs[k]; }        // bce:keep
    return t + xs[k];          // bce:keep (the check above may not have run)
}

fn main() {
    var ks: i64[>..] = [8, 2, 5, 1];
    var bs: bool[>..] = [false];
    var nums: i64[>..] = [1, 2, 3, 4, 5, 6, 7, 8];
    let xs = nums[..ks[0]];
    print(after_index(xs, ks[1]));          // 6
    print(after_slice(xs, ks[1], ks[2]));   // 15
    print(after_offset(xs, ks[3]));         // 8
    print(branch_only(xs, ks[1], bs[0]));   // 3
}
