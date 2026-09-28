// error: return outside of a function
// A default is no function: a plain return in a block written there has no
// named function around it to exit (§7.6), as one directly in it has none.
fn each<F>(n: i64) { for i in n { F(i); } }
fn first(a: i64 = block { each(3) { return 1 }; 0 }) -> i64 { a }
fn main() { print(first()); }
