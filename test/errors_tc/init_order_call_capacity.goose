// error: reordering may cross a side effect, runtime check, or nonlocal exit
// A pure callee still checks the adaptation of its argument.
struct Pair { a: i64, b: i64 }
fn length(bytes: u8[..2]) -> i64 { bytes.len }
fn make(bytes: const u8[:]) -> Pair { Pair { b: length(bytes), a: 0 } }
fn main() { print(make("too long")); }
