// runtime-define: GS_DEBUG=1
// A checked `as` of an f32 names the value in the f32's own text form
// (§3.7), not the digits of the f64 it widens to.
fn main() {
    var xs: f32[>..] = [2.0, 0.1];
    print(xs[0] as i64);
    print(xs[1] as i64);
}
