// A `var x = []` nothing is ever pushed into has no element type (§4.2).
fn main() {
    var xs = [];
    print(xs.len);
}
