// error: xs is not fixed-size and is not copied implicitly
// A block's untyped parameter takes the if's value as it is, a copy of the
// branch taken, as an un-annotated let would (§4.1, §7.6).
fn each<F>(xs: i64[>..]&, ys: i64[>..]&, c: bool) { F(if c { xs } else { ys }); }
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    each(a, b, a.len > b.len) { v => print(v.len) };
}
