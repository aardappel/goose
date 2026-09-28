// error: variable Q may be used before it is assigned: globals initialize in declaration order
// A path through a field of a global, read before the global's initializer
// has run (§11.1).
struct P { xs: i64[3] }
var A: i64 = Q.xs[1];
var Q: P = P { xs: [1, 2, 3] };
fn main() { print(A); }
