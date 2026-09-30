// error: the default of field t of S leads to this construction of S, which takes it again
// S's default builds a T, whose default builds an S taking the first again
// (§3.2), here from a global's initializer.
struct S { a: i64 = 1, t: i64 = T { .. }.b }
struct T { b: i64 = 2, s: i64 = S { .. }.a }
let G = S { .. };
fn main() { print(G.t); }
