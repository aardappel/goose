// error: simd fn cannot be inline: its callers would run the baseline only
simd inline fn f(x: i64) -> i64 { x }
fn main() { }
