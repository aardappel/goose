// error: root struct references must be plain references
root struct S { n: i64 }
struct Link { s: S&<u32> }
fn main() {}
