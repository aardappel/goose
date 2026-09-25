// error: cannot call drop, which may shrink a: an earlier expression value
// An if passed to a reference parameter binds the branch taken by reference
// (§4.1), so the callee pops `a` or `b` while the slice of `a` taken before
// the call is still held (§5.2).
fn total(s: const i64[:], n: i64) -> i64 { s.len + n }
fn drop(xs: i64[>..<]&) -> i64 { xs.pop() }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let c = a.len > b.len;
    print(total(a[..], drop(if c { a } else { b })));
}
