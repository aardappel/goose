// error: reordering may cross a side effect, runtime check, or nonlocal exit
// The read is harmless, but construction checks the receiving capacity.
struct Packet { tag: i64, bytes: u8[..2] }
fn make(bytes: const u8[:]) -> Packet { Packet { bytes: bytes, tag: 0 } }
fn main() { print(make("too long")); }
