// error: array length 256 exceeds storage range
struct Packet { bytes: u8[u8], tail: i64 }
fn main() { let p = Packet { bytes: [1; 256], tail: 42 }; }
