// error: return outside of a function
// A block written in a global initializer has no named function around it
// for a plain return to exit (§7.6).
fn each<F>(n: i64) { for i in n { F(i); } }
let x = block { each(3) { return 1 }; 0 };
fn main() { print(x); }
