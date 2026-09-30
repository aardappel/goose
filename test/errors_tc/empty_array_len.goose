// error: cannot infer the element type of []: .len gives it none
// Reading a member of a [] gives it no element type (§4.2).
fn main() {
    print([].len);
}
