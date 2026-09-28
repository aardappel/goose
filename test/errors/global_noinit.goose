// error: let and const global declarations require an initializer
// Only a var global may leave its value to its type (§11.1): a let one
// could never be given another.
let g: i64;
fn main() { print(g); }
