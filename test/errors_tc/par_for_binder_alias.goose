// error: a reference or slice read out of storage here may point into storage partitioned
// The loop's own binder is read out of the sequence, and may point at an
// element another iteration owns (docs/design/child_threads.md §4.4).
fn main() {
    var xs: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    let rs: i64&[2] = [xs[1], xs[0]];
    par for r, k in rs { xs[k] = r + 1; }
    print(xs);
}
