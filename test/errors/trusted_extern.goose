// error: extern fn cannot be trusted
// A trusted fn is where an own value's obligation begins or ends unchecked
// (docs/design/ownership.md); an extern's own parameters and results are
// C's to account for already.
trusted extern fn f() -> i64;
fn main() {}
