// error: copy takes a value or a reference, not a slice
// A reference to a slice cannot bypass the restriction on copying slices.
fn main() {
    let a: i64[2] = [1, 2];
    let s = a[..];
    print(copy(&s));
}
