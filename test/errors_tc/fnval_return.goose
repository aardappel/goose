// error: function values cannot be returned
fn inc(x: i64) -> i64 { x + 1 }
fn make() { return inc; }
fn apply<F>(x: i64) -> i64 { F(x) }
fn main() { print(apply(5, make())); }
