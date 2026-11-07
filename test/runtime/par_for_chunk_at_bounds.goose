// goose-threads
// A par for whose iterations own chunk_at runs checks the bounds once, before
// any iteration runs (docs/design/child_threads.md §4.4): offsets that fall
// abort at the loop, which has written nothing.
fn main() {
    var a: i64[6] = [1, 2, 3, 4, 5, 6];
    let bounds = [0, 2, 4, 3, 6];
    print("before ", a);
    par for k in bounds.len - 1 {
        let c = a.chunk_at(bounds, k);
        for &x in c { x = 0; }
    }
    print("after ", a);
}
