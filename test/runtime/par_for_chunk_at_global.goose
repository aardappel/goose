// goose-threads
// A par for in a function a global initializer calls checks chunk_at's
// bounds before any iteration runs, at every optimization level (§4.4 of
// docs/design/child_threads.md): offsets that fall abort there, though the
// iterations that would take the bad chunks never take them. The optimizer
// keeps a body holding a par for a call, which runs it from a function of
// its own.
fn init() -> i64 {
    var a: i64[4] = [1, 2, 3, 4];
    let bounds = [0, 1, 2, 1, 4];
    par for k in bounds.len - 1 {
        if k < 2 {
            let c = a.chunk_at(bounds, k);
            for &x in c { x = 0; }
        }
    }
    return a[0];
}

let INIT = init();

fn main() { print("init ", INIT); }
