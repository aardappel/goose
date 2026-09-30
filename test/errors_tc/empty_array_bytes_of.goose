// error: cannot infer the element type of []: bytes_of gives it none
// A member builtin gives its receiver no element type (§4.2).
fn main() {
    print(bytes_of([]).len);
}
