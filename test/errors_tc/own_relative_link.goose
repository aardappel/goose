// error: own qualifies a reference, a slice, a struct or an integer handle, not Node&<u32>?: an owned relative link is not supported
struct Node { v: i64 }
struct Tree { root: own Node&<u32>? }
fn main() {}
