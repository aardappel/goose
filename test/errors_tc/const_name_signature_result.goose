// error: array size N names a parameter, not a constant
// So in its result types (§3.3): no size follows an argument.
let N = 2;

fn fill(N: i64) -> i64[N] { [N; 2] }

fn main() { print(fill(4)); }
