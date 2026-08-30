// Array elements may not be resizable (§3.3).
fn main() {
    var a: int[>..][] = [];
    print(a.len);
}
