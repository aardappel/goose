// error: storing a read-only reference
// A reference to the iteration's index, written in the body
// (docs/design/child_threads.md §4.3 R2).
fn main() {
    var out: i64[8] = [0, 0, 0, 0, 0, 0, 0, 0];
    par for k in 8 {
        let r: i64& = k;
        r = 0;
        out[k] = 1;
    }
    print(out);
}
