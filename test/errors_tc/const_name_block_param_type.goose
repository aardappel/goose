// error: array size K names a local variable, not a constant
// A block's parameter types are written where the block is, in main's
// scope (§3.3, §7.6).
let K = 3;

fn apply<F>(x: i64[3]) -> i64 { F(x) }

fn main() {
    let K = 5;
    print(apply([1, 2, 3]) { v: i64[K] => v.len });
}
