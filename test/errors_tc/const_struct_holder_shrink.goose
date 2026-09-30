// error: cannot pop xs while h is still used
// A holder copied into a const slot keeps what its references point into
// (§5.1): popping xs would free the element h.r points at.
struct H { r: i64& }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let held = H { r: xs[2] };
    const h: H = held;
    xs.pop();
    print(h.r);
}
