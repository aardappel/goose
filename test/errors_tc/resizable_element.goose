// error: array elements may not be resizable: i64[>..]
// Array elements may not be resizable (§3.3).
fn main() {
    var a: i64[>..][] = [];
    print(a.len);
}
