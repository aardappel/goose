// error: the element type of xs was never determined: nothing was pushed or appended into it, and no array was assigned to it
// A `var x = []` nothing is ever pushed into has no element type (§4.2).
fn main() {
    var xs = [];
    print(xs.len);
}
