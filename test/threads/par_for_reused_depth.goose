// goose-threads
// A function with a par for of its own, over an array of its own, called
// from two par fors of another function, the second inside more blocks: the
// second call reuses the specialization the first made, whose own array no
// loop of the caller sees from outside, however deep the caller's loop is
// (TypeCheck::ParRecord).
fn scratch() -> i64 {
    var a: i64[4] = [0, 0, 0, 0];
    par for i in 4 { a[i] = i; }
    return a[3];
}

fn fill(dst: i64[:]) {
    par for k in dst.len { dst[k] = scratch(); }
    {
        {
            par for k in dst.len { dst[k] += scratch() + k; }
        }
    }
}

fn main() {
    var xs: i64[8] = [0, 0, 0, 0, 0, 0, 0, 0];
    fill(xs);
    print(xs);
}
