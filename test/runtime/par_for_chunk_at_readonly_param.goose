// goose-threads
// As par_for_chunk_at_readonly.goose, the runs read out of a const slice
// parameter.
fn firsts(a: const i64[:], bounds: const i64[:], out: i64[:]) {
    par for k in bounds.len - 1 {
        if k < 2 {
            let c = a.chunk_at(bounds, k);
            out[k] = c[0];
        }
    }
}

fn main() {
    let a: i64[4] = [1, 2, 3, 4];
    let bounds = [0, 1, 2, 1, 4];
    var out: i64[4] = [0, 0, 0, 0];
    print("before");
    firsts(a, bounds, out);
    print("after ", out);
}
