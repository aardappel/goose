// error: cannot call drop, which shrinks a: a[..], evaluated earlier in the statement
// error: may still refer into a
// An array literal's elements are built in order: the first slice is in
// place when the second's bound clears the array it views (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn main() {
    var a: i64[>..] = [1, 2];
    var b: i64[>..] = [3, 4];
    let ss: const i64[:][2] = [a[..], b[..drop(a)]];
    print(ss[0]);
}
