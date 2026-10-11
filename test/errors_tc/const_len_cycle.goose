// error: cycle in constant initializer: b
let a: i64[b.len] = [1];
let b: i64[a.len] = [1];

fn main() { print(a, b); }
