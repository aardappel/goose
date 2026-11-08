// error: r is or may point into storage partitioned by this par for
// As par_for_slice_ref_alias.goose, read in a function taking the reference
// to the slice (docs/design/child_threads.md §4.4).
fn first(s: i64[:]&) -> i64 { s[0] }
fn main() {
    var xs: i64[4] = [1, 1, 1, 1];
    let v = xs[0..4];
    let r = &v;
    par for k in 4 { xs[k] = first(r) + 1; }
    print(xs);
}
