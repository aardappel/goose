// error: reordering may cross a side effect, runtime check, or nonlocal exit
// Checking a callee includes capacity checks on its local bindings.
struct Pair { a: i64, b: i64 }
fn length(bytes: const u8[:]) -> i64 {
    let local: u8[..2] = bytes;
    local.len
}
fn make(bytes: const u8[:]) -> Pair { Pair { b: length(bytes), a: 0 } }
fn main() { print(make("too long")); }
