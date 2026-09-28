// error: the default of parameter a of f leads to this call, which takes it again
// Evaluating the default would take it again without end (§7.1).
fn f(a: i64 = f()) -> i64 { a }
fn main() { print(f()); }
