// error: an element cannot be a zero-length array: i64[0]
// Nor is a zero-length array an array's element (§3.4).
fn main() {
    let a: i64[0][2] = [[], []];
    print(a.len);
}
