// error: variable B may be used before it is assigned: globals initialize in declaration order
// error: global_before_init_index.goose:5 has run
// An element of a global read before its initializer has run (§11.1).
var A: i64 = B[1];
var B: i64[3] = [1, 2, 3];
fn main() { print(A); }
