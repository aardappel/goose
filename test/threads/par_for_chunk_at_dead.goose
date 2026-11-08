// goose-threads
// chunk_at's bounds are checked at the fork whatever the iterations take,
// though no iteration can take one (`if false`): the optimizer may drop every
// other name of the arrays, which the check still reads -- from a nested
// function, whose free variables they stay, and in the function they are
// declared in (docs/design/child_threads.md §4.4). The bounds here are good.
fn main() {
    var a: i64[4] = [1, 2, 3, 4];
    let bounds = [0, 1, 2, 3, 4];
    fn run() {
        par for k in bounds.len - 1 {
            if false {
                let c = a.chunk_at(bounds, k);
                for &x in c { x = 0; }
            }
        }
    }
    run();
    print(a);
    var b: i64[4] = [5, 6, 7, 8];
    let bounds2 = [0, 2, 4];
    par for k in bounds2.len - 1 {
        if false {
            let c = b.chunk_at(bounds2, k);
            for &x in c { x = 0; }
        }
    }
    print("done");
}
