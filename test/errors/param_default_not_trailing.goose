// error: parameter b needs a default: only trailing parameters have defaults
// A default may be left out only at the end of the arguments (§7.1).
fn f(a: i64 = 1, b: i64) -> i64 { a + b }
fn main() { print(f(1, 2)); }
