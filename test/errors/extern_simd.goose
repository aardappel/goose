// error: extern fn cannot be simd
extern simd fn strlen(s: u8&) -> u64;
fn main() {}
