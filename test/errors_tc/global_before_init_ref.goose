// error: variable B may be used before it is assigned: globals initialize in declaration order
// A reference to a global taken before its initializer has run (§11.1).
var R: i64& = &B;
var B: i64 = 3;
fn main() { print(R); }
