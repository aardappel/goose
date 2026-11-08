// goose-threads
// An abort in a par for's iteration ends the program as an abort outside it
// does (docs/design/child_threads.md §4.7), on whichever thread runs the
// iteration's chunk.
fn main() {
    let xs = [1, 2, 3];
    var out: i64[64] = [0; 64];
    print("before");
    par for k in 64 { out[k] = xs[k / 21]; }
    print("after ", out[0]);
}
