// error: cannot write through this reference
// As par_for_key_ref_index.goose, for the chunk an iteration owns by k
// (docs/design/child_threads.md §4.3 R2).
fn set0(x: i64&) { x = 0; }
fn main() {
    var xs: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    par for k in 4 {
        set0(k);
        let c = xs.chunk(k, 4);
        c[0] += 1;
    }
    print(xs);
}
