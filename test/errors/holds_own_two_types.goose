// error: holds_own takes exactly one type
fn f<T>() { const assert !holds_own<T, T>, "plain"; }
fn main() { f<i64>(); }
