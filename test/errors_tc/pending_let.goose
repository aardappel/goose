// A let bound to [] can never learn its element type (§4.2).
fn main() {
    let xs = [];
    print(xs.len);
}
