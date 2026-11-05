// error: const if condition requires a scalar or string constant, or supported integer constant expression
fn f() -> i64 { 1 }
fn main() { const if f() == 1 { print(1); } }
