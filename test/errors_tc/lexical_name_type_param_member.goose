// error: type parameter F is bound to a function value, which is called as F(...), not as a member
// x.F() names the function value F, which hides the global function F
// (§7.6, §11.1); it is called as F(x).
fn F(x: i64) -> i64 { x * 100 }
fn g(x: i64) -> i64 { x + 1 }

fn apply<F>(x: i64) -> i64 { x.F() }

fn main() { print(apply(2, g)); }
