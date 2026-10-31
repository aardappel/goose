// Contract clauses are asserts (docs/design/proofs.md §17): checked in every
// build, facts for bounds-check elimination past their check, and dropped
// where the facts already settle them. run_tests.py reads the C: clamp's
// ensures is proved at each of its returns, and the requires that indexes
// stays.

import std;

// The requires puts i below a.len, so the loop needs no bounds check.
checkfree fn sum_first(a: const i64[:], n: i64) -> i64
    requires 0 <= n && n <= a.len
{
    var s: i64 = 0;
    for i in n { s += a[i]; }
    s
}

fn clamp(a: const i64[:], i: i64) -> i64
    requires a.len > 0
    requires a[0] != 99
    ensures 0 <= result && result < a.len
{
    if i < 0 { return 0; }
    if i >= a.len { return a.len - 1; }
    i
}

fn main() {
    let xs = [3, 1, 4, 1, 5];
    print(sum_first(xs, xs[1] + 2), " ", sum_first(xs, xs.len));
    print(clamp(xs, -2), " ", clamp(xs, 2), " ", clamp(xs, 9));
}
