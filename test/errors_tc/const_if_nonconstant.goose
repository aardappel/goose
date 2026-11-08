// error: const if condition n names a local variable, not a constant
fn main() { let n = 1; const if n == 1 { print(n); } }
