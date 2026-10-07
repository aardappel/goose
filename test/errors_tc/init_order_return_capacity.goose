// error: reordering may cross a side effect, runtime check, or nonlocal exit
// Returning a read can check the result's capacity, before the caller reads len.
struct Pair { a: i64, b: i64 }
fn limited(bytes: const u8[:]) -> u8[..2] { return bytes; }
fn make(bytes: const u8[:]) -> Pair { Pair { b: limited(bytes).len, a: 0 } }
fn main() { print(make("too long")); }
