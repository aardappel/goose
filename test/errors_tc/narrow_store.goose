// Storing a non-literal i64 into smaller storage is never implicit (§3.1).
struct S { x: u8 }
fn main() {
    var s = S { x: 0 };
    let y = 1;
    s.x = y;
}
