// error: a is not fixed-size and is not copied implicitly
// A view of a bare block's value views a copy of the array its tail names
// (§4.1).
fn total(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    print(total(({ a })[..]));
}
