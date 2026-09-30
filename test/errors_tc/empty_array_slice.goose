// error: cannot infer the element type of []: an index, slice or field of it gives it none
// Slicing a [] gives it no element type (§4.2).
fn main() {
    print([][0..0].len);
}
