// error: export fn cannot be inline: its C wrapper calls it
export inline fn f(x: i64) -> i64 { x }
fn main() { }
