// error: variable B may be used before it is assigned: globals initialize in declaration order
// A function that A's initializer calls reads B, whose initializer runs
// after A's (§11.1).
var A: i64 = f();
var B: i64 = 3;
fn f() -> i64 { return B * 2; }
fn main() { print(A); }
