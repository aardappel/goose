// error: variable B may be used before it is assigned: globals initialize in declaration order
// A field default runs at each construction (§3.2), here in A's
// initializer, before B's has run (§11.1).
struct P { x: i64 = B[0] }
var A: P = P {};
var B: i64[3] = [1, 2, 3];
fn main() { print(A.x); }
