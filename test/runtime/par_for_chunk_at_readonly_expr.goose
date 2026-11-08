// goose-threads
// As par_for_chunk_at_readonly.goose, the runs read out of a slice the body
// takes, whose length the fork cannot know: the bounds are checked
// non-decreasing from 0 there.
fn main() {
    var xs: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    let bounds = [0, 1, 2, 1, 4];
    var out: i64[4] = [0, 0, 0, 0];
    print("before");
    par for k in bounds.len - 1 {
        if k < 2 {
            let c = xs[0..4].chunk_at(bounds, k);
            out[k] = c[0];
        }
    }
    print("after ", out);
}
