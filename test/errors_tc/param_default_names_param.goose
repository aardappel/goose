// error: the default of parameter b of f names parameter a
// A default is evaluated at the call, where the parameters do not exist yet
// (§7.1). The parameter is what `a` means in the declaration, so the global
// of that name is not taken instead; `::a` would name the global.
let a = 5;
fn f(a: i64, b: i64 = a) -> i64 { a + b }
fn main() { print(f(1)); }
