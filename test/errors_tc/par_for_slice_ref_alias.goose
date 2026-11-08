// error: r is or may point into storage partitioned by this par for
// A reference to a slice variable leads where the slice points, into the
// array the iterations own (docs/design/child_threads.md §4.4).
fn main() {
    var xs: i64[4] = [1, 1, 1, 1];
    let v = xs[0..4];
    let r = &v;
    par for k in 4 { xs[k] = r[0] + 1; }
    print(xs);
}
