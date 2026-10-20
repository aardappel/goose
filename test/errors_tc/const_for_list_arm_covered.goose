// error: earlier arms match all of its values
fn f(n: i64) -> i64 { match n { const for <C> in [1, 2] { C => 1, }, 2 => 3, _ => 0 } }
fn main() { print(f(2)); }
