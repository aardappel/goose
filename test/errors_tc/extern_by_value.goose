// A variable-size value cannot cross to C by value (§7.10).
extern fn take(s: u8[]);
fn main() { take("x"); }
