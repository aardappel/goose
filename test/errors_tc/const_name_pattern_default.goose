// error: the default of parameter b of f names parameter LO
// A match pattern in a default names what a top-level declaration would
// (§7.1, §8.1): the parameter LO, not the global of the name.
let LO = 3;

fn f(LO: i64, b: i64 = match 3 { LO => 1, _ => 2 }) -> i64 { LO + b }

fn main() { print(f(5)); }
