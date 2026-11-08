// error: const if condition n names a local variable, not a constant
fn f(n) { const if n == 1 { print(1); } }
fn main() { f(1); }
