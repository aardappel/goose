// error: ambiguous call to f: multiple overloads match equally well
// A default is no argument: both overloads match f(1) by its one argument
// alone, and neither is preferred for leaving b out (§7.1).
fn f(a: i64) -> i64 { a }
fn f(a: i64, b: i64 = 0) -> i64 { a + b }
fn main() { print(f(1)); }
