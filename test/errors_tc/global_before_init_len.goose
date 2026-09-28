// error: variable B may be used before it is assigned: globals initialize in declaration order
// The length of a global array read before its initializer has built it
// (§11.1).
var A: i64 = B.len;
var B: i64[>..] = [1, 2, 3];
fn main() { print(A); }
