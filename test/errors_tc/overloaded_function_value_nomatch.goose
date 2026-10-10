// error: no matching overload for call to hash
// An overload set passed as a function value resolves at each call of it,
// where none of std's hash overloads takes an i64[:].
import std;
fn apply<F>(xs: i64[:]) -> u64 { F(xs) }
fn main() {
    let xs = [1, 2];
    print(apply(xs, hash));
}
