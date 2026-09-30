// error: cannot assign through this path (const, or a read-only instantiation
// An array behind a reference joins as a slice where the reference points,
// read-only where the reference is (§6.4, §9.5): a write through the slice
// would reach the array a `const i64[3]&` names.
fn zero_first(r: const i64[3]&, rest: i64[>..]&, c: bool) {
    let s = if c { r } else { rest[..] };
    s[0] = 0;
}
fn main() {
    var a: i64[3] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    zero_first(a, b, true);
    print(a);
}
