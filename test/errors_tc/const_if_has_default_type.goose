// error: has_default<U> requires U to be a fields<T> accessor binding in scope
fn main() { const for <U> in types<i64> { const if has_default<U> { print(1); } } }
