// error: return outside of a function
// A field default is no function: a plain return in a block written there
// has no named function around it to exit (§7.6).
fn each<F>(n: i64) { for i in n { F(i); } }
struct S { a: i64 = block { each(3) { return 1 }; 0 } }
fn main() { let s = S {}; print(s.a); }
