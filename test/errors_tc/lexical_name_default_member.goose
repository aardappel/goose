// error: the default of parameter y of apply names function value F
// A member call in a default looks for a function, which the function value
// F is, hiding the global function F (§7.1).
fn F(x: i64) -> i64 { x * 100 }
fn g(x: i64) -> i64 { x + 1 }
let two = 2;

fn apply<F>(x: i64, y: i64 = two.F()) -> i64 { F(x) + y }

fn main() { print(apply(1, g)); }
