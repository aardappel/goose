// error: the default of parameter b cannot be passed as u8: constant 300 does not fit u8
// A default is checked against its parameter's type at each call leaving
// it out (§7.1).
fn f(a: i64, b: u8 = 300) -> i64 { a + b }
fn main() { print(f(1, 2)); print(f(1)); }
