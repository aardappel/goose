// error: array size xs names a parameter, not a constant
// A size in a signature names no parameter (§3.3, §11.1), `.len` of one
// included: there are no size parameters.
let xs = [1, 2, 3];

fn f(xs: i64[3], ys: i64[xs.len]) { print(xs, ys); }

fn main() { f([1, 2, 3], [4, 5, 6]); }
