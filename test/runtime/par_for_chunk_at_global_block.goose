// goose-threads
// A par for written in a global initializer itself, which no function holds,
// runs as a plain loop, after the checks its fork would make: chunk_at's
// bounds, before any iteration runs (docs/design/child_threads.md §4.4).
let INIT = {
    var a: i64[4] = [1, 2, 3, 4];
    let bounds = [0, 1, 2, 1, 4];
    par for k in bounds.len - 1 {
        if k < 2 {
            let c = a.chunk_at(bounds, k);
            for &x in c { x = 0; }
        }
    }
    a[0]
};

fn main() { print("init ", INIT); }
