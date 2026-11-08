// goose-threads
// As par_for_chunk_at_arm.goose, with the arm a block of the one par for,
// which the optimizer folds into the arm itself.
fn run(which: i64, a: i64[:], bounds: const i64[:]) {
    match which {
        0 => {},
        _ => {
            par for k in bounds.len - 1 {
                if k < 2 {
                    let c = a.chunk_at(bounds, k);
                    for &x in c { x = 0; }
                }
            }
        },
    }
}

fn main() {
    var a: i64[4] = [1, 2, 3, 4];
    let bounds = [0, 1, 2, 1, 4];
    print("before");
    run(1, a, bounds);
    print("after ", a);
}
