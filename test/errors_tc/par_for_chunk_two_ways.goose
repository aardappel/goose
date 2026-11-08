// error: which are cut differently and so overlap between iterations
// R2 (§4.4): one array is partitioned one way: chunk(k, 4) and the element
// k are different parts of it for the same k.
fn main() {
    var xs: i64[8] = [0, 0, 0, 0, 0, 0, 0, 0];
    par for k in 4 {
        let c = xs.chunk(k, 4);
        c[0] = k;
        xs[k] = k;
    }
    print(xs);
}
