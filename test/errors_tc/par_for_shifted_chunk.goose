// error: which may be views of one array from different starts, and so overlap between iterations
// R2 (§4.4): the runs of two views of one array that start at different
// elements overlap from one iteration to the next.
fn main() {
    var xs: i64[9] = [0, 0, 0, 0, 0, 0, 0, 0, 0];
    let a = xs[0..8];
    let b = xs[1..9];
    par for k in 4 {
        let c = a.chunk(k, 4);
        let d = b.chunk(k, 4);
        c[0] = 1;
        d[1] = 2;
    }
    print(xs);
}
