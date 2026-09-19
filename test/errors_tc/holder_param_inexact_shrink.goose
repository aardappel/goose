// error: cannot pop xs while w is still used
// What g's by-value parameter holds is one array only where the call site's
// value held references into one. The first call's w views inner's ys alone,
// so its body may pop xs while w is in use; the second's views xs as well,
// and gets a body of its own, where the pop would leave w.a viewing the slot
// the push reuses (§9.2, §5.1).
struct W2 { a: i64[:], b: i64[:] }
fn g(xs: i64[>..]&, w: W2) { xs.pop(); xs.push(77); print(w.a[w.a.len - 1]); }
fn inner(xs: i64[>..]&) {
    var ys: i64[>..] = [4, 5, 6];
    g(xs, W2 { a: ys[..], b: ys[..] });
    g(xs, W2 { a: xs[..], b: ys[..] });
}
fn main() { var xs: i64[>..] = [1, 2, 3]; inner(xs); }
