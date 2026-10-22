// error: returning a reference rooted in local, which dies with the call of apply that this return leaves
// A block's return leaves the function it is handed to, whose locals die
// with it: the result would point into apply's frame.
var G: i64 = 7;
fn apply<F>() -> i64 { var local: i64 = 5; F(&local); return 0; }
fn outer() -> i64& {
    apply() { x: i64& => return x; };
    return G;
}
fn main() {
    print(outer() + 1);
}
