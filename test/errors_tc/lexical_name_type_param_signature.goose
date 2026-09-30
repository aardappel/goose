// error: array size N names a type parameter, not a constant
// In a function's own signature its type parameter N hides the global N and
// stands for a type, which no size can be (§3.3).
let N = 4;

fn total<N>(a: i64[N], x: N) -> i64 { a.len }

fn main() { print(total([1, 2, 3, 4], 1)); }
