// error: xs is not fixed-size and is not copied implicitly
// error: pass copy(xs) for a copy, or bind it by reference
// Whole assignment from a non-fixed lvalue needs copy() (§4.1).
fn main() {
    var xs: i64[>..] = [1, 2];
    var ys: i64[>..] = [];
    ys = xs;
}
