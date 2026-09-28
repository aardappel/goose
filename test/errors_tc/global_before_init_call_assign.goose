// error: variable B may be used before it is assigned: globals initialize in declaration order
// Only a global's own initializer constructs it (§11.1): a function that
// A's initializer calls cannot assign B before that has run.
var A: i64 = f();
var B: i64 = 3;
fn f() -> i64 { B = 4; return 1; }
fn main() { print(A, B); }
