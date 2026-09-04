// Whole assignment from a non-fixed lvalue needs copy() (§4.1).
fn main() {
    var xs: i64[>..] = [1, 2];
    var ys: i64[>..] = [];
    ys = xs;
}
