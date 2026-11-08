// error: r is or may point into storage partitioned by this par for
// As par_for_slice_ref_alias.goose, read in a nested function the body
// calls (docs/design/child_threads.md §4.4).
fn main() {
    var xs: i64[4] = [1, 1, 1, 1];
    let v = xs[0..4];
    let r = &v;
    fn peek() -> i64 { r[0] }
    par for k in 4 { xs[k] = peek() + 1; }
    print(xs);
}
