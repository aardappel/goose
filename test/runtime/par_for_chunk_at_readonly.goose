// goose-threads
// chunk_at of the loop's bounds checks them whole at the fork, before any
// iteration runs, whether the iterations own the runs or only read them, as
// they read a const global's here (docs/design/child_threads.md §4.4): the
// offsets fall, though the iterations that read take only good runs.
const A: i64[4] = [1, 2, 3, 4];

fn main() {
    let bounds = [0, 1, 2, 1, 4];
    var out: i64[4] = [0, 0, 0, 0];
    print("before");
    par for k in bounds.len - 1 {
        if k < 2 {
            let c = A.chunk_at(bounds, k);
            out[k] = c[0];
        }
    }
    print("after ", out);
}
