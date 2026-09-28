// error: cannot call drop, which shrinks a: the elements of a, which the statement reads after it
// error: may be in a
// The array is evaluated before the slice's bound, which clears it (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn main() {
    var a: i64[>..] = [1, 2];
    print(a[drop(a)..]);
}
