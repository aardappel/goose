// error: defer outside of a function: a global initializer has no scope exit (§6.6)
let g: i64 = { defer print("never"); 1 };
fn main() { print(g); }
