// error: variable R may be used before it is assigned: globals initialize in declaration order
// A function that A's initializer calls rebinds the reference global R,
// whose initializer runs after A's (§11.1).
var X: i64 = 1;
var A: i64 = f();
var R: i64& = &X;
fn f() -> i64 { R .= &X; return 1; }
fn main() { print(A, R); }
