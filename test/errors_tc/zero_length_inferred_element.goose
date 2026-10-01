// error: an element cannot be a zero-length array: i64[0]
// A fill whose value is a zero-length array would make that array the
// elements of the literal's own type (§3.4).
fn main() {
    let none: i64[0] = [];
    let rows = [none; 2];
    print(rows.len);
}
