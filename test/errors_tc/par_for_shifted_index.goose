// error: which may be views of one array from different starts, and so overlap between iterations
// R2 (§4.4): a[k] and b[k] are the iteration's own only where a and b are
// one view; here b starts one element after a, so b[k] is a[k + 1].
fn main() {
    var xs: i64[9] = [0, 0, 0, 0, 0, 0, 0, 0, 0];
    let a = xs[0..8];
    let b = xs[1..9];
    par for k in 8 { a[k] = 1; b[k] = 2; }
    print(xs);
}
