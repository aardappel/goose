// error: a is not fixed-size and is not copied implicitly
// A block's value is a copy of its tail's, which a non-fixed variable reaches
// only through copy() (§4.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    let y = block { a };
    a[0] = 5;
    print(y[0]);
}
