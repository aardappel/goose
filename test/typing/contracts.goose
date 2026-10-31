// runtime-debug
// Contracts (docs/design/proofs.md, phase 1): requires, ensures with result
// and old(e), loop invariants, implies and bounded quantifiers. Every clause
// here holds, so the debug run, which checks them, prints what the others do.

fn lower_bound(a: const i64[:], key: i64) -> i64
    requires all i in 1..a.len: a[i - 1] <= a[i]
    ensures 0 <= result && result <= a.len
    ensures all k in 0..result: a[k] < key
    ensures all k in result..a.len: a[k] >= key
{
    var lo: i64 = 0;
    var hi = a.len;
    while lo < hi
        invariant 0 <= lo && lo <= hi && hi <= a.len
        invariant all k in 0..lo: a[k] < key
        invariant all k in hi..a.len: a[k] >= key
    {
        let mid = lo + (hi - lo) / 2;
        if a[mid] < key { lo = mid + 1; } else { hi = mid; }
    }
    return lo;
}

// A bare return and the end of the body both reach the ensures.
fn bump(x: i64&)
    ensures x == old(x) + 1
{
    if x < 0 {
        x = x + 1;
        return;
    }
    x += 1;
}

// The body's tail is the result.
fn sign(x: i64) -> i64
    ensures x > 0 implies result == 1
    ensures x < 0 implies result == -1
    ensures any v in -1..2: v == result
{
    if x > 0 { 1 } else if x < 0 { -1 } else { 0 }
}

// A nonfixed result, returned by name and checked in place.
fn squares(n: i64) -> i64[>..]
    requires n >= 0
    ensures result.len == n
    ensures all i in 0..n: result[i] == i * i
{
    var out: i64[>..] = [];
    for i in n invariant out.len == i {
        out.push(i * i);
    }
    return out;
}

fn fill(c: u8, n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for _ in n { out.push(c); }
    out
}

// A nonfixed result built where it is returned.
fn repeated(c: u8, n: i64) -> u8[>..]
    ensures result.len == n
    ensures all i in 0..n: result[i] == c
{
    return fill(c, n);
}

// Checked in each specialization.
fn largest<T>(xs: const T[:]) -> T
    requires xs.len > 0
    ensures all i in 0..xs.len: xs[i] <= result
    ensures any i in 0..xs.len: xs[i] == result
{
    var best = xs[0];
    for x in xs { if x > best { best = x; } }
    best
}

// An early return from inside a loop, and the fallthrough.
fn first_neg(xs: const i64[:]) -> i64
    ensures result == -1 || xs[result] < 0
    ensures all i in 0..(if result < 0 { xs.len } else { result }): xs[i] >= 0
{
    for i in xs.len { if xs[i] < 0 { return i; } }
    -1
}

// Nested quantifiers.
fn has_pair_sum(xs: const i64[:], t: i64) -> bool
    ensures result == any i in 0..xs.len: any j in i + 1..xs.len: xs[i] + xs[j] == t
{
    for i in xs.len {
        for j in i + 1..xs.len { if xs[i] + xs[j] == t { return true; } }
    }
    false
}

// The contract words are names everywhere else.
fn words() -> i64 {
    let all = 1;
    let any = 2;
    let implies = 3;
    let invariant = 4;
    let requires = 5;
    let ensures = 6;
    let old = 7;
    all + any + implies + invariant + requires + ensures + old
}

fn main() {
    let a = [1, 3, 3, 7, 9];
    print(lower_bound(a, 3), " ", lower_bound(a, 8), " ", lower_bound(a, 100));
    var y: i64 = 4;
    bump(y);
    var z: i64 = -3;
    bump(z);
    print(y, " ", z);
    print(sign(5), " ", sign(-5), " ", sign(0));
    let s = squares(5);
    print(s.len, " ", s[4]);
    print(repeated('x', 3));
    print(largest([3, 9, 2]), " ", largest([2.5, 1.0]));
    print(first_neg([1, 2, -3, 4]), " ", first_neg([1, 2]));
    print(has_pair_sum([1, 4, 6], 10), " ", has_pair_sum([1, 4, 6], 3));
    print(words());
}
