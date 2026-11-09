// error: an own value with no empty state
// An own link is read with take(), which leaves it null: it is declared optional.
struct Node { v: i64, next: own Node&<u32> }
fn main() {}
