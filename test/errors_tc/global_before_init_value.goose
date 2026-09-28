// error: variable B may be used before it is assigned: globals initialize in declaration order
// A global's value read before its initializer has run (§11.1).
var A: i64 = B + 1;
var B: i64 = 3;
fn main() { print(A); }
