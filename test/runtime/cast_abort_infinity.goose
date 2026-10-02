// runtime-define: GS_DEBUG=1
// A checked `as` of an infinity names it as print does (§3.7), on every
// backend and C library.
fn main() {
    var xs: f64[>..] = [1.0, 0.0];
    let inf = xs[0] / xs[1];
    print(xs[0] as i64);
    print((-inf) as i32);
}
