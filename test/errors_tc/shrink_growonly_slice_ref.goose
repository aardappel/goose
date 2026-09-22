// error: cannot clear xs while p is still used
// error: the slice it refers to may point into it
// A reference to a slice variable keeps the slice into a grow-only array
// live, as the slice variable itself would (§5.1).
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    var s = xs[..];
    let p: i64[:]& = s;
    xs.clear();
    print(p[0]);
}
