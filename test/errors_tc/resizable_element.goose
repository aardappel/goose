// Array elements may not be resizable (§3.3).
fn main() {
    var a: i64[>..][] = [];
    print(a.len);
}
