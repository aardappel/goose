// error: array size K names a parameter, not a constant
// In a block's own parameter types its parameters' names mean the
// parameters, as in a function's (§3.3).
let K = 3;

fn both<F>(n: i64) -> i64 { F(n, [1, 2, 3]) }

fn main() {
    print(both(5) { K: i64, v: i64[K] => v.len + K });
}
