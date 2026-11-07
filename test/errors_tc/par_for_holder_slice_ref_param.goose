// error: cannot assign through this path
// As par_for_holder_slice_ref.goose, with the callee taking the reference to
// the slice itself (docs/design/child_threads.md §4.3 R1).
struct H { s: i64[:] }
fn bump(s: i64[:]&) { s[0] += 1; }
fn main() {
    var xs: i64[4] = [0, 0, 0, 0];
    let h = H { s: xs };
    par for k in 100000 { var copy = h; bump(&copy.s); }
    print(xs[0]);
}
