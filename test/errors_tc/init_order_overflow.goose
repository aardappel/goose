// error: reordering may cross a side effect, runtime check, or nonlocal exit
struct Pair { a: i64, b: i64 }
fn make(x: i64) -> Pair { Pair { b: x + 1, a: x } }
fn main() { print(make(1)); }
