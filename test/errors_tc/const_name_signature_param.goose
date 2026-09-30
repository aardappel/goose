// error: array size K names a parameter, not a constant
// In a function's own parameter types its parameters' names mean the
// parameters, which hold arguments, and hide the global K (§3.3).
let K = 3;

fn f(K: i64, a: i64[K]) -> i64 { a.len + K }

fn main() { print(f(5, [1, 2, 3])); }
