// error: recursive fn cannot be inline
inline recursive fn f(n: i64) -> i64 { if n < 1 { return 0; } return f(n - 1); }
fn main() { print(f(3)); }
