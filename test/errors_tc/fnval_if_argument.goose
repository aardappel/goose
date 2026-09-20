// error: a function value must be a function name or block literal
fn inc(x: i64) -> i64 { x + 1 }
fn apply<F>(x: i64) -> i64 { F(x) }
fn main() { print(apply(5, if true { print("effect"); inc } else { inc })); }
