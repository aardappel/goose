// goose-threads
// A par for that is a match's arm is a fork as one in a block is, which
// checks chunk_at's bounds before any iteration runs
// (docs/design/child_threads.md §4.4).
fn run(which: i64, a: i64[:], bounds: const i64[:]) {
    match which {
        0 => par for k in bounds.len - 1 {
            if k < 2 {
                let c = a.chunk_at(bounds, k);
                for &x in c { x = 0; }
            }
        },
        _ => {},
    }
}

fn main() {
    var a: i64[4] = [1, 2, 3, 4];
    let bounds = [0, 1, 2, 1, 4];
    print("before");
    run(0, a, bounds);
    print("after ", a);
}
