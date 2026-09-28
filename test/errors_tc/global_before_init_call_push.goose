// error: variable B may be used before it is assigned: globals initialize in declaration order
// A function that A's initializer calls grows the global array B, which
// B's initializer builds only after A's has run (§11.1).
var A: i64 = f();
var B: i64[>..] = [];
fn f() -> i64 { B.push(4); return 1; }
fn main() { print(A, B); }
