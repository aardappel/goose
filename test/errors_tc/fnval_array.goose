// error: function values cannot be stored in arrays
fn inc(x: i64) -> i64 { x + 1 }
fn main() { let fs = [inc]; }
