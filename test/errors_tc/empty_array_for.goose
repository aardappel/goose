// error: cannot infer the element type of []: a for loop gives it none
// A loop over a [] gives it no element type for its binder (§4.2).
fn main() {
    for x in [] { }
    print(1);
}
