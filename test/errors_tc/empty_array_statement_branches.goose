// error: cannot infer the element type of []: a statement gives it none
// A construct in statement position discards its branches' values: each is
// a statement, which gives a [] no element type (§4.2).
fn main() {
    let c = true;
    if c { [] } else { [] };
    print(1);
}
