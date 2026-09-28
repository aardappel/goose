// error: variable B may be used before it is assigned: globals initialize in declaration order
// A function that A's initializer calls reads an element of B, whose
// initializer runs after A's (§11.1).
var A: i64 = f(1);
var B: i64[3] = [1, 2, 3];
fn f(i: i64) -> i64 { return B[i]; }
fn main() { print(A); }
