// error: the default of parameter y of run names function value F
// A default calls no function value the function takes (§7.1).
fn run<F>(x: i64, y: i64 = F(1)) -> i64 { x + y }
fn main() { print(run(1) { it * 2 }); }
