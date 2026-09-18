// error: a block takes no type arguments
// A function value bound to a block has no type parameters for an explicit
// list to bind (§7.6, §7.7); one bound to a generic named function does.
fn call_it<F>(x: i64) -> i64 { F<u8>(x) }
fn main() { print(call_it(3) { it * 2 }); }
